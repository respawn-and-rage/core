#include "ERProceduralMap.h"
#include "ERLayoutSolids.h"

bool ERProcedural::BuildUrbanTown(const FERProceduralConfig& C,const FERBlockRoomConfig& B,FERProceduralLayout& Out,const FERCellSettings& Cells)
{
    const auto* T=C.TemplateAsset.Get();if(!T)return false;
    const auto& R=T->UrbanTown;
    if(T->TemplateId.IsEmpty() || T->Revision<1 || T->Symmetry!=EERTemplateSymmetry::HalfTurn || C.Floors!=1 ||
       B.BlockSize!=FVector(50,20,25) || !FMath::IsFinite(B.ChunkSize) || B.ChunkSize<100 ||
       !FMath::IsFinite(R.StreetWidth) || R.StreetWidth<600 || R.StreetWidth>800 ||
       !FMath::IsFinite(R.AlleyWidth) || R.AlleyWidth<350 || R.AlleyWidth>500 ||
       !FMath::IsFinite(R.DoorWidth) || R.DoorWidth<180 || R.DoorWidth>240 ||
       !FMath::IsFinite(R.StoreyHeight) || R.StoreyHeight<320 || R.StoreyHeight>380 || R.MaxStoreys<1 || R.MaxStoreys>3 ||
       !T->Pieces.IsEmpty() || T->Town.Enabled || T->Variation.Enabled || T->ShellWallMesh || T->RoofMesh)return false;
    FERProceduralLayout L;L.LayoutSymmetry=EERTemplateSymmetry::HalfTurn;
    const FVector2D Arena=T->SizingTeamSize>0 ? T->SizingArena:FVector2D(T->ArenaSize);
    if(Arena.ContainsNaN() || Arena.X<3400 || Arena.Y<4000 || Arena.X>24000 || Arena.Y>24000)return false;
    L.ArenaSize={Arena.X,Arena.Y,1400};
    L.Corridors.Add({-Arena*.5,Arena*.5});
    const float HX=Arena.X*.5,HY=Arena.Y*.5,Street=R.StreetWidth,Gap=R.AlleyWidth;
    const FTransform Turn(FRotator(0,180,0));FRandomStream Random(C.Seed);int32 Id=0;
    FTransform Placement=FTransform::Identity;
    auto Fixed=[&](FVector Center,FVector Size,EERBlockMaterial Material,bool Pair,bool Roof=false)
    {
        FERLayoutFixedPiece P;P.Transform=FTransform(FQuat::Identity,Center,Size/100)*Placement;P.Material=Material;P.Roof=Roof;
        L.FixedPieces.Add(P);if(Pair){P.Transform=P.Transform*Turn;L.FixedPieces.Add(P);}
    };
    // Glazing: a storefront span along the wall, split into panes of at most 180cm between 20cm piers.
    auto Wall=[&](FVector Start,float Length,float Yaw,float Height,EERBlockMaterial Material,bool Door,float DoorCenter,FVector2D Glazing=FVector2D::ZeroVector)
    {
        const int32 First=L.Panels.Num();
        FERCellWorld::AddConstruction(0,Material,FTransform(FRotator(0,Yaw,0),Start)*Placement,FVector(Length,5,Height),Cells,L.Panels);
        const int32 Panes=Glazing.Y-Glazing.X>=120 ? FMath::CeilToInt32((Glazing.Y-Glazing.X+20)/200):0;
        const float Pane=Panes ? (Glazing.Y-Glazing.X-(Panes-1)*20)/Panes:0;
        for(int32 I=First;I<L.Panels.Num();++I)
        {
            auto& P=L.Panels[I];P.Id=Id++;P.bAnchorBottom=true;
            if(Door)P.Holes.Add(FBox2D(FVector2D(DoorCenter-R.DoorWidth*.5,0),FVector2D(DoorCenter+R.DoorWidth*.5,260)));
            for(int32 J=0;J<Panes;++J)P.Holes.Add(FBox2D(FVector2D(Glazing.X+J*(Pane+20),95),FVector2D(Glazing.X+J*(Pane+20)+Pane,255)));
        }
        for(int32 J=0;J<Panes;++J)
        {
            const int32 Glass=L.Panels.Num();
            FERCellWorld::AddConstruction(0,EERBlockMaterial::Glass,FTransform(FRotator(0,Yaw,0),Start+FRotator(0,Yaw,0).RotateVector(FVector(Glazing.X+J*(Pane+20),0,0))+FVector(0,0,95))*Placement,FVector(Pane,5,160),Cells,L.Panels);
            for(int32 I=Glass;I<L.Panels.Num();++I){L.Panels[I].Id=Id++;L.Panels[I].bAnchorBottom=true;}
        }
        if(Panes)L.FeatureNames.AddUnique(TEXT("Storefront"));
        const int32 End=L.Panels.Num();
        for(int32 I=First;I<End;++I){auto P=L.Panels[I];P.Id=Id++;P.Transform=P.Transform*Turn;L.Panels.Add(MoveTemp(P));}
    };
    auto Route=[&](const TArray<FVector>& Points)
    {
        TArray<FVector> World;for(const auto& P:Points)World.Add(Placement.TransformPosition(P));
        L.RoomAccessRoutes.Add(World);TArray<FVector> Mate;for(const auto& P:World)Mate.Add(Turn.TransformPosition(P));L.RoomAccessRoutes.Add(MoveTemp(Mate));
    };
    auto ClosedBox=[&](FVector Center,FVector Size,EERBlockMaterial Material)
    {
        Fixed(Center,Size,Material,true);
        const FBox Box=FBox(Center-Size*.5,Center+Size*.5).TransformBy(Placement);
        L.InaccessibleGround.Add(Box);L.InaccessibleGround.Add(Box.TransformBy(Turn));
    };
    auto Polygon=[&](const TArray<FVector>& Local,TArray<TArray<FVector>>& Set)
    {
        TArray<FVector> World;for(const auto& P:Local)World.Add(Placement.TransformPosition(P));
        Set.Add(World);for(auto& P:World)P=Turn.TransformPosition(P);Set.Add(MoveTemp(World));
    };
    // Lot-local rectangle to a furnishable space and its half-turn mate.
    TArray<FVector> LotDoors;
    auto Interior=[&](FVector2D Min,FVector2D Max,EERInteriorKind Kind,uint8 Walls,float Height)
    {
        if(Max.X-Min.X<280 || Max.Y-Min.Y<280)return;
        FERLayoutInterior Space;Space.Frame=FTransform(FVector((Min+Max)*.5,0))*Placement;Space.Half=(Max-Min)*.5;
        Space.Kind=Kind;Space.Walls=Walls;Space.Height=Height;
        for(const auto& Door:LotDoors)Space.Doors.Add(Placement.TransformPosition(Door));
        AddInteriorPair(L,MoveTemp(Space));
    };
    // The envelope contains closed urban fabric. Its playable outline has
    // diagonal corners, projecting lots and paired deep perimeter recesses.
    Fixed({-HX-20,0,700},{40,Arena.Y+80,1400},EERBlockMaterial::Brick,true);
    Fixed({0,-HY-20,700},{Arena.X,40,1400},EERBlockMaterial::Concrete,true);
    const float CutX=FMath::Min(HX*.62f,1400.f),CutY=FMath::Min(HY*.4f,1600.f);
    Polygon({{HX-CutX,-HY,0},{HX,-HY,0},{HX,-HY+CutY,0}},L.InaccessibleGroundPolygons);
    const FVector DiagonalA(HX-CutX,-HY,700),DiagonalB(HX,-HY+CutY,700);
    FERLayoutFixedPiece Diagonal;Diagonal.Material=EERBlockMaterial::Brick;
    Diagonal.Transform=FTransform((DiagonalB-DiagonalA).Rotation(),(DiagonalA+DiagonalB)*.5,FVector((DiagonalB-DiagonalA).Size(),40,1400)/100);
    L.FixedPieces.Add(Diagonal);Diagonal.Transform=Diagonal.Transform*Turn;L.FixedPieces.Add(Diagonal);
    const float IndentMin=-HY*.56f,IndentMax=-HY*.18f,Indent=Street*.55f;
    const float Shoulder=FMath::Min(340.f,(IndentMax-IndentMin)*.25f);
    const TArray<FVector> StreetShoulder{{-HX,IndentMin,0},{-HX+Indent,IndentMin+Shoulder,0},
        {-HX+Indent,IndentMax-Shoulder,0},{-HX,IndentMax,0}};
    Polygon(StreetShoulder,L.InaccessibleGroundPolygons);
    for(int32 Edge=0;Edge<3;++Edge)
    {
        const FVector A=StreetShoulder[Edge],End=StreetShoulder[Edge+1];
        FERLayoutFixedPiece FacadePiece;
        FacadePiece.Material=EERBlockMaterial::Concrete;
        FacadePiece.Transform=FTransform((End-A).Rotation(),(A+End)*.5+FVector(0,0,700),
            FVector((End-A).Size()+40,40,1400)/100);
        L.FixedPieces.Add(FacadePiece);
        FacadePiece.Transform=FacadePiece.Transform*Turn;
        L.FixedPieces.Add(FacadePiece);
    }
    const float ProjectionX=Random.FRandRange(-.12f,.08f)*Arena.X;
    const float ProjectionWidth=FMath::Min(Arena.X*.28f,1600.f),ProjectionDepth=Random.RandRange(400,580);
    ClosedBox({ProjectionX,-HY+ProjectionDepth*.5,700},{ProjectionWidth,ProjectionDepth,1400},EERBlockMaterial::Brick);
    const bool Compact=T->SizingTeamSize<=3;
    const float LeftMargin=Compact ? 200.f:0.f,RightMargin=Compact ? 850.f:0.f;
    const float CoreWidth=Arena.X-2*Street-LeftMargin-RightMargin,CoreHalfDepth=HY-1050;
    // Compact plans draw one broad block, two blocks split by an alley, or a block beside an open market.
    const uint32 Variant=HashCombineFast(uint32(C.Seed),0x544f5756u);
    const bool Split=Compact && CoreWidth>=3400 && Variant%3u!=0;
    const bool Market=Split && (Variant>>3)%2u==0;
    const int32 Columns=Compact ? (Split ? 2:1):FMath::Max(2,2*FMath::FloorToInt32((CoreWidth+Gap)/3200));
    const int32 Rows=FMath::Max(1,FMath::FloorToInt32((CoreHalfDepth+Gap*.5)/2400));
    const float BandDepth=(CoreHalfDepth-(Rows-.5f)*Gap)/Rows;
    if(BandDepth<650 || CoreWidth-(Columns-1)*Gap<Columns*700)return false;
    auto Facade=[&](float Width,float Depth,int32 Storeys,float NotchX)
    {
        const int32 Windows=FMath::Max(1,FMath::FloorToInt32(Width/350));
        for(int32 FloorIndex=1;FloorIndex<Storeys;++FloorIndex)for(int32 W=0;W<Windows;++W)
        {
            const float WX=Width*(W+.5f)/Windows,Z=FloorIndex*R.StoreyHeight+160;
            for(float FaceY:{-3.f,Depth+3})
            {
                if(FaceY>0 && WX<NotchX+90)continue;
                Fixed({WX,FaceY,Z},{160,8,180},EERBlockMaterial::Metal,true);
                Fixed({WX,FaceY+(FaceY<0 ? -5:5),Z},{140,2,160},EERBlockMaterial::Glass,true);
            }
        }
    };
    auto LeftX=[&](float Y){return -HX+Street*((Y>IndentMin-150 && Y<IndentMax+150) ? .8f:.5f);};
    for(int32 Row=0;Row<Rows;++Row)
    {
        TArray<float> Weights;float Sum=0;
        for(int32 Col=0;Col<Columns;++Col){const float Weight=Random.FRandRange(.6f,1.6f);Weights.Add(Weight);Sum+=Weight;}
        const float Remainder=CoreWidth-(Columns-1)*Gap-Columns*700;float X=-HX+Street+LeftMargin;
        for(int32 Col=0;Col<Columns;++Col)
        {
            const float LotWidth=700+Remainder*Weights[Col]/Sum;
            float Front=Random.RandRange(0,3)*50.f,Back=Random.RandRange(0,2)*40.f;
            if(Row==0 && Col==Columns-1 && !Compact)Front=FMath::Max(Front,CutY-500);
            const float Width=LotWidth-Random.RandRange(0,2)*40.f,Depth=BandDepth-Front-Back;
            const float Y=-CoreHalfDepth+Row*(BandDepth+Gap)+Front;
            if(Width<650 || Depth<550)return false;
            const bool Reverse=Random.RandRange(0,1)!=0;
            Placement=FTransform(FRotator(0,Reverse ? 180:0,0),Reverse ? FVector(X+Width,Y+Depth,0):FVector(X,Y,0));
            const int32 Storeys=Random.RandRange(1,R.MaxStoreys);
            const float Height=Storeys*R.StoreyHeight,NotchX=Width*Random.FRandRange(.33f,.46f),NotchY=Depth*Random.FRandRange(.48f,.62f);
            const auto Material=Random.RandRange(0,1)==0 ? EERBlockMaterial::Brick:EERBlockMaterial::Concrete;
            const float ShoulderX=Width*.59f,ShoulderY=Depth*.20f;
            const TArray<FVector> Outline{{0,0,0},{ShoulderX,0,0},{ShoulderX,ShoulderY,0},{Width,ShoulderY,0},
                {Width,Depth,0},{NotchX,Depth,0},{NotchX,NotchY,0},{0,NotchY,0}};
            const bool Closed=Columns>2 && Col%3==2;
            if(Market && Col==1 && Row==0)
            {
                // An open square of stalls: the route crosses it between the two streets.
                LotDoors.Reset();
                Route({{Width*.5f,-160,112},{Width*.5f,Depth+160,112}});
                Interior({0,0},{Width,Depth},EERInteriorKind::Court,0,400);
                L.FeatureNames.AddUnique(TEXT("MarketSquare"));
            }
            else if(Closed)
            {
                const float H=FMath::Max(680.f,Height);
                ClosedBox({Width*.5,NotchY*.5,H*.5},{Width,NotchY,H},Material);
                ClosedBox({(Width+NotchX)*.5,(Depth+NotchY)*.5,H*.5},{Width-NotchX,Depth-NotchY,H},Material);
                Facade(Width,Depth,FMath::Max(2,Storeys),NotchX);
            }
            else
            {
                // An L-shaped two-room shop, with the entrance and exit in
                // different wings. The missing corner is an outdoor courtyard.
                const float DoorX=Width*.25f,ExitX=Width*.76f;
                for(int32 Edge=0;Edge<Outline.Num();++Edge)
                {
                    const FVector A=Outline[Edge],D=Outline[(Edge+1)%Outline.Num()]-A;
                    const bool Portal=Edge==0 || Edge==4;
                    const float DoorAlong=Edge==0 ? DoorX:Width-ExitX,Run=float(D.Size2D());
                    // Street fronts are glazed beside their door, or across a doorless recess.
                    const FVector2D Glazing=Random.RandRange(0,2)==0 ? FVector2D::ZeroVector:
                        Portal ? FVector2D(DoorAlong+R.DoorWidth*.5f+60,Run-50):Edge==2 ? FVector2D(60,Run-60):FVector2D::ZeroVector;
                    Wall(A,Run,float(D.Rotation().Yaw),R.StoreyHeight,Material,Portal,DoorAlong,Glazing);
                    if(Storeys>1)
                    {
                        const float UH=Height-R.StoreyHeight-20;
                        FERLayoutFixedPiece Upper;Upper.Material=Material;
                        Upper.Transform=FTransform(D.Rotation(),A+D*.5+FVector(0,0,R.StoreyHeight+20+UH*.5),FVector(D.Size2D(),20,UH)/100)*Placement;
                        L.FixedPieces.Add(Upper);Upper.Transform=Upper.Transform*Turn;L.FixedPieces.Add(Upper);
                    }
                }
                const float PartitionY=NotchY*.75f;
                Wall({5,PartitionY,0},Width-10,0,R.StoreyHeight,EERBlockMaterial::Drywall,true,ExitX-5);
                TArray<float> RoofLevels{R.StoreyHeight+10};if(Storeys>1)RoofLevels.Add(Height+10);
                for(float Z:RoofLevels)
                {
                    Fixed({ShoulderX*.5,ShoulderY*.5,Z},{ShoulderX,ShoulderY,20},EERBlockMaterial::Concrete,true,true);
                    Fixed({Width*.5,(ShoulderY+NotchY)*.5,Z},{Width,NotchY-ShoulderY,20},EERBlockMaterial::Concrete,true,true);
                    Fixed({(Width+NotchX)*.5,(Depth+NotchY)*.5,Z},{Width-NotchX,Depth-NotchY,20},EERBlockMaterial::Concrete,true,true);
                }
                Facade(Width,Depth,Storeys,NotchX);
                Fixed({DoorX,-50,285},{280,100,10},EERBlockMaterial::Metal,true);
                Fixed({ExitX,Depth+50,285},{280,100,10},EERBlockMaterial::Metal,true);
                Fixed({ShoulderX*.5,-60,6},{ShoulderX,120,12},EERBlockMaterial::Concrete,true);
                Fixed({(Width+NotchX)*.5,Depth+60,6},{Width-NotchX,120,12},EERBlockMaterial::Concrete,true);
                const float TurnY=(ShoulderY+PartitionY)*.5f;
                TArray<FVector> Through{{DoorX,-160,112},{DoorX,TurnY,112},{ExitX,TurnY,112}};
                if(Depth>=1300 && Width>=1000)
                {
                    const float SecondY=(NotchY+Depth)*.5f;
                    const float OffsetDoor=Width*.63f;
                    Wall({NotchX+5,SecondY,0},Width-NotchX-10,0,R.StoreyHeight,
                        EERBlockMaterial::Drywall,true,OffsetDoor-NotchX-5);
                    const float Approach=(NotchY+SecondY)*.5f;
                    const float Departure=(SecondY+Depth)*.5f;
                    Through.Append({{ExitX,Approach,112},{OffsetDoor,Approach,112},
                        {OffsetDoor,Departure,112},{ExitX,Departure,112}});
                }
                Through.Add({ExitX,Depth+160,112});
                Route(Through);
                // Sales floor behind the front, back rooms past each partition, and the open court.
                const float Headroom=R.StoreyHeight-10,BackEnd=Depth>=1300 && Width>=1000 ? (NotchY+Depth)*.5f:Depth;
                LotDoors={{DoorX,0,0},{ExitX,PartitionY,0},{ExitX,Depth,0}};
                if(BackEnd<Depth)LotDoors.Add({Width*.63f,BackEnd,0});
                Interior({8,8},{ShoulderX-8,ShoulderY},EERInteriorKind::Shop,1|2|8,Headroom);
                Interior({8,ShoulderY+8},{Width-8,PartitionY-4},EERInteriorKind::Shop,2|4|8,Headroom);
                Interior({8,PartitionY+8},{Width-8,NotchY-8},EERInteriorKind::BackRoom,1|2|8,Headroom);
                Interior({NotchX+8,NotchY+8},{Width-8,BackEnd-8},EERInteriorKind::BackRoom,2|4|8,Headroom);
                if(BackEnd<Depth)Interior({NotchX+8,BackEnd+8},{Width-8,Depth-8},EERInteriorKind::BackRoom,15,Headroom);
                Interior({10,NotchY+10},{NotchX-10,Depth},EERInteriorKind::Court,1|2,400);
                const FBox Bounds=FBox(FVector::ZeroVector,FVector(Width,Depth,Height)).TransformBy(Placement);
                L.TownBuildings.Add(Bounds);L.TownBuildings.Add(Bounds.TransformBy(Turn));
                Polygon(Outline,L.TownFootprints);L.FeatureNames.Add(TEXT("ThroughShop"));
            }
            Placement=FTransform::Identity;X+=LotWidth+Gap;
        }
        if(Row>0)
        {
            const float Y=-CoreHalfDepth+Row*(BandDepth+Gap)-Gap*.5;
            const TArray<FVector> Lane{{LeftX(Y),Y,112},{HX-Street*.5,Y,112}};
            L.CrossingRoutes.Add(Lane);TArray<FVector> Mate;for(const auto& P:Lane)Mate.Add(Turn.TransformPosition(P));L.CrossingRoutes.Add(MoveTemp(Mate));
        }
    }
    // A shared central gallery joins the nearest room wings into one complex.
    // Its branches follow the actual portals, leaving irregular open courts.
    TArray<FVector> Mouths;
    for(const auto& Path:L.RoomAccessRoutes)
    {
        const FVector P=FMath::Abs(Path[0].Y)<FMath::Abs(Path.Last().Y) ? Path[0]:Path.Last();
        if(FMath::Abs(P.Y)<400)Mouths.Add(P);
    }
    const float Bend=Random.RandRange(280,500);
    const float Drift=T->ReflectionBuild ? 0.f:float(Random.RandRange(60,95));
    auto SpineY=[&](double X){return Drift*FMath::Clamp(X/Bend,-1.,1.);};
    auto Gallery=[&](FVector A,FVector B)
    {
        const FVector D=B-A;if(D.Size2D()<1)return;
        const FVector Across=FVector::CrossProduct(D.GetSafeNormal2D(),FVector::UpVector)*160;
        L.TownFootprints.Add({A+Across,B+Across,B-Across,A-Across});
        FERLayoutFixedPiece Roof;Roof.Material=EERBlockMaterial::Concrete;Roof.Roof=true;
        Roof.Transform=FTransform(D.Rotation(),(A+B)*.5+FVector(0,0,R.StoreyHeight-102),FVector(D.Size2D(),320,20)/100);
        L.FixedPieces.Add(Roof);L.RoomAccessRoutes.Add({A,B});
    };
    double GalleryReach=Bend;
    for(const auto& Mouth:Mouths)
    {
        const FVector Join(Mouth.X,SpineY(Mouth.X),112);
        Gallery(Mouth+FVector(0,FMath::Sign(Mouth.Y)*170,0),Join);
        GalleryReach=FMath::Max(GalleryReach,FMath::Abs(Mouth.X));
    }
    const TArray<FVector> Spine{{-GalleryReach,-Drift,112},{-Bend,-Drift,112},
        {Bend,Drift,112},{GalleryReach,Drift,112}};
    for(int32 I=1;I<Spine.Num();++I)Gallery(Spine[I-1],Spine[I]);
    // The ring follows the jagged lots and the diagonal corner rather than a
    // rectangle. Both halves share the same sampled physical construction.
    TArray<FVector> Left{{-HX+Street*.5,-HY+800,100},{-HX+Street*.5,IndentMin-220,100},
        {-HX+Street*.8,IndentMin-100,100},{-HX+Street*.8,IndentMax+100,100},
        {-HX+Street*.5,IndentMax+220,100}};
    if(Compact)
    {
        // Closed service buildings divide the wide corner court into staggered
        // lanes. The block footprints and the bypass have the same half-turn mate.
        const float CornerY=-HY+CutY+270;
        const float LaneY=-HY+CutY+800;
        ClosedBox({HX-1130,CornerY,340},{440,300,680},EERBlockMaterial::Brick);
        ClosedBox({HX-620,LaneY,340},{480,340,680},EERBlockMaterial::Concrete);
        const float InnerTurn=-LaneY-285;
        const float OuterTurn=-CornerY-300;
        Left.Append({{-HX+Street*.5,InnerTurn,100},{-HX+1100,InnerTurn,100},
            {-HX+1100,OuterTurn,100},{-HX+650,OuterTurn,100},
            {-HX+650,HY-CutY-330,100}});
    }
    Left.Add({-HX+Street*.5,HY-CutY-250,100});
    Route(Left);L.GroundRoutes.Add(Left);
    const TArray<FVector> Bottom{Left[0],{HX-CutX-250,-HY+800,100},{HX-Street*.5,-HY+CutY+250,100}};
    Route(Bottom);
    // A second transverse street reaches the courtyard side of the shops.
    TArray<FVector> Cross{{LeftX(-Drift),-Drift,112}};
    Cross.Append(Spine);
    Cross.Add({HX-Street*.5,Drift,112});
    L.CrossingRoutes.Add(MoveTemp(Cross));
    // Screened bases occupy opposite ends of the ring, away from the diagonal
    // closed corners. The outer street and interiors remain independently open.
    // A 2.4m perimeter bypass supports opposing player traffic. The other end
    // has its own reserved dogleg, clear of the projecting service building.
    Wall({-HX+240,-HY+720,0},800,0,340,EERBlockMaterial::Brick,false,0);
    for(int32 I=0;I<10;++I)
    {
        const FVector P(-HX+250+(I%5)*100,-HY+250+(I/5)*120,100);
        L.AlphaSpawns.Emplace(FRotator(0,0,0),P);L.OmegaSpawns.Emplace(FRotator(0,180,0),Turn.TransformPosition(P));
    }
    L.Spawn=L.AlphaSpawns[0].GetLocation();
    const TArray<FVector> Exit{L.Spawn,{-HX+120,-HY+450,100},{-HX+120,-HY+800,100},Left[0]};
    Route(Exit);const auto MateExit=L.RoomAccessRoutes.Last();
    const TArray<FVector> OtherExit{L.Spawn,{-HX+650,-HY+620,100},
        {-HX+1160,-HY+620,100},{-HX+1160,-HY+800,100},Bottom[1]};
    Route(OtherExit);
    L.SpawnExitRoutes={Exit,MateExit,OtherExit,L.RoomAccessRoutes.Last()};
    TArray<FVector> Patrol=Left;
    for(int32 I=Bottom.Num()-2;I>=0;--I)Patrol.Add(Turn.TransformPosition(Bottom[I]));
    for(int32 I=1;I<Left.Num();++I)Patrol.Add(Turn.TransformPosition(Left[I]));
    for(int32 I=Bottom.Num()-2;I>=0;--I)Patrol.Add(Bottom[I]);
    L.Routes={Exit,Patrol,MateExit};
    // Kerb furniture backs onto the outer walls of the ring, clear of both spawn corners.
    LotDoors.Reset();
    Interior({-HX+4,-HY+900},{-HX+Street*.5f,IndentMin-200},EERInteriorKind::Street,8,400);
    Interior({-HX+4,IndentMax+200},{-HX+Street*.5f,HY-CutY-300},EERInteriorKind::Street,8,400);
    Interior({-HX+900,-HY+4},{HX-CutX-250,-HY+Street*.5f},EERInteriorKind::Street,1,400);
    ERLayoutQueries::FLayoutSolids Solids(L);
    auto Clear=[&](const TArray<TArray<FVector>>& Paths)
    {
        for(const auto& Path:Paths)for(int32 I=1;I<Path.Num();++I)if(!Solids.PawnClear(Path[I-1],Path[I]))
        {UE_LOG(LogTemp,Warning,TEXT("ER_TOWN_ROUTE_REJECT seed=%d from=%s to=%s"),C.Seed,*Path[I-1].ToString(),*Path[I].ToString());return false;}
        return true;
    };
    if(!Clear(L.RoomAccessRoutes) || !Clear(L.CrossingRoutes) || !Clear({Patrol}))return false;
    Out=MoveTemp(L);return true;
}
