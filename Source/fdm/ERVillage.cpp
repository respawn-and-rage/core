#include "ERProceduralMap.h"
#include "Engine/StaticMesh.h"
#include "ERDestructibleMesh.h"

namespace ERProcedural
{
bool BuildVillage(const FERProceduralConfig& C,const FERBlockRoomConfig& B,FERProceduralLayout& Out,const FERCellSettings& Cells)
{
    const auto* T=C.TemplateAsset.Get();if(!T)return false;
    const auto& R=T->Village;
    auto GridRange=[](double A,double B){return FMath::IsFinite(A) && FMath::IsFinite(B) && FMath::CeilToInt(A/100)<=FMath::FloorToInt(B/100);};
    if(!GridRange(R.ArenaMin.X,R.ArenaMax.X) || !GridRange(R.ArenaMin.Y,R.ArenaMax.Y) ||
       !GridRange(R.BlockWidth.X,R.BlockWidth.Y) || !GridRange(R.BlockDepth.X,R.BlockDepth.Y) || T->TemplateId.IsEmpty() || T->Revision<1 || T->Symmetry!=EERTemplateSymmetry::HalfTurn || C.Floors!=1 ||
       B.BlockSize!=FVector(50,20,25) || !FMath::IsFinite(B.ChunkSize) || B.ChunkSize<100 ||
       R.ArenaMin.ContainsNaN() || R.ArenaMax.ContainsNaN() || R.ArenaMin.X<(T->SizingTeamSize>0 ? 2800:3400) || R.ArenaMin.Y<(T->SizingTeamSize>0 ? 3600:4400) ||
       R.ArenaMax.X>(T->SizingTeamSize>0 ? 20000:5600) || R.ArenaMax.Y>(T->SizingTeamSize>0 ? 24000:13200) || R.ArenaMin.X>R.ArenaMax.X || R.ArenaMin.Y>R.ArenaMax.Y ||
       R.BlockWidth.ContainsNaN() || R.BlockDepth.ContainsNaN() || R.BlockWidth.X<2000 || R.BlockWidth.Y>3000 || R.BlockWidth.X>R.BlockWidth.Y ||
       R.BlockDepth.X<600 || R.BlockDepth.Y>800 || R.BlockDepth.X>R.BlockDepth.Y ||
       R.MinCentralBlocks<1 || R.MaxCentralBlocks>5 || R.MinCentralBlocks>R.MaxCentralBlocks ||
       R.MinStoreys<1 || R.MaxStoreys>3 || R.MinStoreys>R.MaxStoreys || R.StoreyBudget<R.MaxCentralBlocks*R.MinStoreys || R.StoreyBudget>10 ||
       !FMath::IsFinite(R.DeckHeight) || R.DeckHeight<240 || R.DeckHeight>280 ||
       !FMath::IsFinite(R.PerimeterHeight) || R.PerimeterHeight<1100 || R.PerimeterHeight>1600 ||
       !FMath::IsNearlyEqual(Cells.CellSize,5) || !FMath::IsNearlyEqual(Cells.WoodDepth,2) ||
       T->Town.Enabled || T->Variation.Enabled || !T->Pieces.IsEmpty() || T->RoofMesh || T->ShellWallMesh)return false;
    FRandomStream Geometry(C.Seed),Cosmetic(int32(HashCombine(uint32(C.Seed),0x56494c4cu)));
    FERProceduralLayout L;
    auto Sample=[&](double A,double D){return float(Geometry.RandRange(FMath::CeilToInt(A/100),FMath::FloorToInt(D/100))*100);};
    const FTransform HalfTurn(FRotator(0,180,0));
    const int32 Count=Geometry.RandRange(R.MinCentralBlocks,R.MaxCentralBlocks);
    TArray<FERVillageBlock> Plans;
    const bool Odd=Count%2!=0;
    float Envelope=0,RequiredWidth=0;const float CenterGap=Sample(300,500);
    // Keep the compact layouts' approach lengths and widen their blocks by one metre.
    const bool Compact=T->SizingTeamSize==1 || (T->SizingTeamSize==2 && Count==2);
    auto SampleBlockSize=[&]()
    {
        const float Width=Sample(R.BlockWidth.X,R.BlockWidth.Y);
        const float Depth=Sample(R.BlockDepth.X,R.BlockDepth.Y);
        return FVector2D(Width+(Compact ? 100 : 200),Depth+(Compact ? 0 : 100));
    };
    if(Odd)
    {
        FERVillageBlock P;P.Frame=FTransform::Identity;P.Size=SampleBlockSize();
        P.Storeys=Geometry.RandRange(R.MinStoreys,R.MaxStoreys);P.SelfSymmetric=true;Plans.Add(P);Envelope=P.Size.Y*.5f;
    }
    for(int32 I=0;I<Count/2;++I)
    {
        FERVillageBlock P;P.Size=SampleBlockSize();
        const float Gap=Envelope>0 ? Sample(600,900):CenterGap*.5f;
        P.Frame=FTransform(FVector(Sample(-300,300),-Envelope-Gap-P.Size.Y*.5f,0));
        P.Storeys=Geometry.RandRange(R.MinStoreys,R.MaxStoreys);Envelope+=Gap+P.Size.Y;Plans.Add(P);
    }
    auto FloorsUsed=[&](){int32 N=0;for(const auto& P:Plans)N+=P.Storeys*(P.SelfSymmetric ? 1:2);return N;};
    while(FloorsUsed()>R.StoreyBudget)
        for(auto& P:Plans)if(P.Storeys>R.MinStoreys && FloorsUsed()>R.StoreyBudget)--P.Storeys;
    for(const auto& P:Plans)RequiredWidth=FMath::Max(RequiredWidth,float(FMath::Abs(P.Frame.GetLocation().X)+P.Size.X*.5+200));
    // Sparse layouts reserve a further3m at each approach for the wider
    // transverse cover and its bypass, clear of spawn structures and stairs.
    const float RequiredLength=Envelope*2+(T->SizingTeamSize>=2 ? 1000:0)+Sample(T->SizingTeamSize==1 ? 2600:Count<=2 ? 4400:3800,T->SizingTeamSize==1 ? 2800:Count<=2 ? 5000:4400);
    if(RequiredWidth*2>R.ArenaMax.X || RequiredLength>R.ArenaMax.Y)return false;
    const double MinWidth=FMath::Max(double(RequiredWidth*2),R.ArenaMin.X);
    const double MaxWidth=FMath::Min(R.ArenaMax.X,FMath::Max(R.ArenaMin.X,double(RequiredWidth*2+1200)));
    const double MinLength=FMath::Max(double(RequiredLength),R.ArenaMin.Y);
    const double MaxLength=FMath::Min(R.ArenaMax.Y,FMath::Max(R.ArenaMin.Y,double(RequiredLength+1000)));
    if(T->SizingTeamSize>1)
    {
        TArray<FVector> Envelopes;
        const float Area=TargetAreaM2(T->SizingTeamSize);
        for(float W=FMath::CeilToFloat(MinWidth/100)*100;W<=FMath::Min(R.ArenaMax.X,FMath::Sqrt(Area*10000)*1.25f);W+=100)
        {
            const float D=FMath::Max(FMath::CeilToFloat(MinLength/100)*100,FMath::GridSnap(Area*10000/W,100.f));
            if(D<MinLength || D>R.ArenaMax.Y || D/W>3.5f || !ValidTeamArea({W,D,R.PerimeterHeight},T->SizingTeamSize))continue;
            Envelopes.Add({W,D,R.PerimeterHeight});
        }
        if(Envelopes.IsEmpty())return false;
        L.ArenaSize=Envelopes[Geometry.RandRange(0,Envelopes.Num()-1)];
    }
    else L.ArenaSize={Sample(MinWidth,MaxWidth),Sample(MinLength,MaxLength),R.PerimeterHeight};
    int32 Tallest=1;for(const auto& P:Plans)Tallest=FMath::Max(Tallest,P.Storeys);
    L.ArenaSize.Z=FMath::Max(R.PerimeterHeight,R.DeckHeight+(Tallest-1)*300+600);
    const float HX=L.ArenaSize.X*.5f,HY=L.ArenaSize.Y*.5f,Z=R.DeckHeight;
    bool MirrorConstruction=true;
    int32 Id=0;
    // A timber member is a filled volume of calibrated 2cm-deep wood cells.
    // Thick beams contain several depth cells; global HP/thickness presets stay
    // untouched. Thin cladding and metal sheets use one calibrated depth cell.
    auto Box=[&](FVector Center,FVector Size,EERBlockMaterial Material,bool Solid=true,FQuat Rotation=FQuat::Identity,bool Pair=true)
    {
        FERCellPanelDesc P;P.Id=Id++;P.Material=Material;
        const int32 Thin=Size.Z<=Size.X && Size.Z<=Size.Y ? 2:Size.X<Size.Y ? 0:1;
        FQuat Axes=FQuat::Identity;
        if(Thin==2){Axes=FRotator(0,0,90).Quaternion();P.Size={Size.X,Size.Z,Size.Y};}
        else if(Thin==0){Axes=FRotator(0,90,0).Quaternion();P.Size={Size.Y,Size.X,Size.Z};}
        else P.Size=Size;
        // Cut beam ends can be fractional centimetres at a ladder well.
        // Trim the member's depth to whole calibrated wood cells.
        if(Material==EERBlockMaterial::Wood)P.Size.Y=FMath::Max(Cells.WoodDepth,FMath::FloorToFloat(P.Size.Y/Cells.WoodDepth+1.e-4f)*Cells.WoodDepth);
        const FQuat Q=Rotation*Axes;
        P.Transform=FTransform(Q,Center-Q.RotateVector(P.Size*.5));
        const float Depth=Material==EERBlockMaterial::Metal ? Cells.SheetThickness:Cells.WoodDepth;
        if(!Solid){P.Size.Y=Depth;P.Transform=FTransform(Q,Center-Q.RotateVector(P.Size*.5));}
        P.Cell={FERCellWorld::FitCell(P.Size.X,Cells.CellSize),FERCellWorld::FitCell(P.Size.Y,Depth),FERCellWorld::FitCell(P.Size.Z,Cells.CellSize)};
        P.bAnchorBottom=FBox(FVector::ZeroVector,P.Size).TransformBy(P.Transform).Min.Z<=.1;
        const int32 First=L.Panels.Add(P);
        if(Pair && MirrorConstruction){P.Id=Id++;P.Transform=P.Transform*HalfTurn;L.Panels.Add(P);}
        return First;
    };
    auto Wood=[&](FVector P,FVector S){return Box(P,S,EERBlockMaterial::Wood);};
    auto ImportedWood=[&](const UERDestructibleMesh* Source,FVector Center,FVector Size)
    {
        if(!Source || !Source->Source || Source->Corners.IsEmpty() || Source->Corners.Num()>30000)return false;
        FERCellPanelDesc P;P.Id=Id++;P.Material=EERBlockMaterial::Wood;P.Size=Size;
        P.Transform=FTransform(Center-Size*.5);P.bAnchorBottom=true;P.AnchorHeight=30;
        P.Cell={FERCellWorld::FitCell(Size.X,Cells.CellSize),FERCellWorld::FitCell(Size.Y,Cells.WoodDepth),FERCellWorld::FitCell(Size.Z,Cells.CellSize)};
        P.Surface=Source->Source->GetMaterial(0);P.Skin=Source->Corners;
        P.SkinCRC=Source->GeometryCRC;P.SkinName=Source->Source->GetFName();
        for(auto& V:P.Skin){V.Position*=Size;V.Normal=(V.Normal*Source->Source->GetBoundingBox().GetSize()/Size).GetSafeNormal();}
        L.Panels.Add(P);P.Id=Id++;P.Transform=P.Transform*HalfTurn;L.Panels.Add(P);return true;
    };
    auto Fixed=[&](FVector P,FVector S,EERBlockMaterial M,bool Pair=true)
    {
        FERLayoutFixedPiece V;V.Transform=FTransform(FQuat::Identity,P,S/100);V.Material=M;
        L.FixedPieces.Add(V);if(Pair && MirrorConstruction){V.Transform=V.Transform*HalfTurn;L.FixedPieces.Add(V);}
    };
    // A slightly leaning log palisade. The four hidden continuations affect
    // locomotion only, so visible cracks remain actual shot apertures.
    for(int32 Axis=0;Axis<2;++Axis)
    {
        const float Half=Axis ? HX:HY;
        const int32 Poles=FMath::CeilToInt(2*Half/32.f);const float Pitch=2*Half/Poles;
        for(int32 I=0;I<Poles;++I)
        {
            const float Along=-Half+(I+.5f)*Pitch;
            const float Height=Cosmetic.FRandRange(380,440),Diameter=Cosmetic.FRandRange(24,28);
            const FQuat Lean(Axis ? FVector::YAxisVector:FVector::XAxisVector,FMath::DegreesToRadians(Cosmetic.FRandRange(-2,2)));
            const FVector Base=Axis ? FVector(Along,-HY-18,0):FVector(-HX-18,Along,0);
            FERLayoutFixedPiece Pole;Pole.Material=EERBlockMaterial::Wood;Pole.Mesh=R.PalisadePoleMesh;
            Pole.Transform=FTransform(Lean,Base+Lean.RotateVector({0,0,Height*.5f}),FVector(Diameter,Diameter,Height)/100);
            L.FixedPieces.Add(Pole);Pole.Transform=Pole.Transform*HalfTurn;L.FixedPieces.Add(Pole);
        }
        FERLayoutFixedPiece Barrier;Barrier.Material=EERBlockMaterial::Wood;Barrier.CollisionOnly=true;Barrier.PawnBarrier=true;
        const FVector Position=Axis ? FVector(0,-HY-10,L.ArenaSize.Z*.5f):FVector(-HX-10,0,L.ArenaSize.Z*.5f);
        const FVector Size=Axis ? FVector(L.ArenaSize.X+40,20,L.ArenaSize.Z):FVector(20,L.ArenaSize.Y+40,L.ArenaSize.Z);
        Barrier.Transform=FTransform(FQuat::Identity,Position,Size/100);L.FixedPieces.Add(Barrier);Barrier.Transform=Barrier.Transform*HalfTurn;L.FixedPieces.Add(Barrier);
    }
    for(float Height:{100.f,300.f})
    {
        Fixed({-HX-36,0,Height},{18,L.ArenaSize.Y,20},EERBlockMaterial::Wood);
        Fixed({0,-HY-36,Height},{L.ArenaSize.X,18,20},EERBlockMaterial::Wood);
    }
    auto GraySheet=[&](FVector P,FVector S)
    {
        const int32 Index=Box(P,S,EERBlockMaterial::Metal,false);
        L.Panels[Index].Surface=R.CoverSurface;if(MirrorConstruction)L.Panels[Index+1].Surface=R.CoverSurface;
    };
    // Split planks and beams around the real ladder wells. Upper posts start
    // on the preceding floor so they never close the ground doglegs.
    auto Deck=[&](float X,float Y,float Width,float Depth,float Height,float SupportZ=0.f,const TArray<FBox>& Holes=TArray<FBox>())
    {
        // A plank stub past a well that reaches no beam, or a beam end under no plank, would stand on nothing.
        const float BeamY[]={Y-(Depth*.5f-25),Y,Y+(Depth*.5f-25)};
        auto Holed=[&](float X0,float X1,float Y0,float Y1){return Holes.ContainsByPredicate([&](const FBox& H){return X1>H.Min.X && X0<H.Max.X && Y1>H.Min.Y && Y0<H.Max.Y;});};
        auto OnBeam=[&](float X0,float X1,float Y0,float Y1)
        {
            for(float CY:BeamY)if(Y1>CY-10 && Y0<CY+10 && !Holes.ContainsByPredicate([&](const FBox& H){return X0>=H.Min.X && X1<=H.Max.X && CY+10>H.Min.Y && CY-10<H.Max.Y;}))return true;
            return false;
        };
        auto Strip=[&](float X0,float X1,float Y0,float Y1,float CZ,float H)
        {
            TArray<FVector2D> Segments{{Y0,Y1}};
            for(const auto& Hole:Holes)if(X1>Hole.Min.X && X0<Hole.Max.X)
            {
                TArray<FVector2D> Next;
                for(const auto& S:Segments)
                {if(Hole.Min.Y>S.X)Next.Add({S.X,FMath::Min(S.Y,Hole.Min.Y)});if(Hole.Max.Y<S.Y)Next.Add({FMath::Max(S.X,Hole.Max.Y),S.Y});}
                Segments=MoveTemp(Next);
            }
            for(const auto& S:Segments)if(S.Y>S.X+.1 && OnBeam(X0,X1,S.X,S.Y))Wood({(X0+X1)*.5f,(S.X+S.Y)*.5f,CZ},{X1-X0,S.Y-S.X,H});
        };
        const int32 N=FMath::RoundToInt(Width/28);const float Pitch=Width/N;
        auto UnderPlank=[&](float X0,float X1,float CY)
        {
            for(int32 I=0;I<N;++I){const float A=X-Width*.5f+I*Pitch;if(X1>A && X0<A+Pitch && !Holed(A,A+Pitch,CY-10,CY+10))return true;}
            return false;
        };
        for(int32 I=0;I<N;++I){const float A=X-Width*.5f+I*Pitch;Strip(A+.15f,A+Pitch-.15f,Y-Depth*.5f,Y+Depth*.5f,Height-3,6);}
        // Transverse beams stop at wells rather than passing through a climber.
        for(float CY:BeamY)
        {
            TArray<FVector2D> Segments{{X-Width*.5f,X+Width*.5f}};
            for(const auto& Hole:Holes)if(CY+10>Hole.Min.Y && CY-10<Hole.Max.Y)
            {
                TArray<FVector2D> Next;for(const auto& S:Segments){if(Hole.Min.X>S.X)Next.Add({S.X,FMath::Min(S.Y,Hole.Min.X)});if(Hole.Max.X<S.Y)Next.Add({FMath::Max(S.X,Hole.Max.X),S.Y});}Segments=MoveTemp(Next);
            }
            for(const auto& S:Segments)if(S.Y>S.X+.1 && UnderPlank(S.X,S.Y,CY))Wood({(S.X+S.Y)*.5f,CY,Height-11},{S.Y-S.X,20,10});
        }
        for(float DX:{-.45f,-.15f,.15f,.45f})for(float DY:{-1.f,1.f})
        {
            const FVector Position(X+DX*Width,Y+DY*(Depth*.5f-25),Height-10);
            if(!Holes.ContainsByPredicate([&](const FBox& H){return Position.X>H.Min.X-44 && Position.X<H.Max.X+44;}))
                Wood({Position.X,Position.Y,SupportZ+(Height-SupportZ-16)*.5f},{20,20,Height-SupportZ-16});
        }
        const FBox D(FVector(X-Width*.5f,Y-Depth*.5f,Height-6),FVector(X+Width*.5f,Y+Depth*.5f,Height));
        L.ElevatedDecks.Add(D);if(MirrorConstruction)L.ElevatedDecks.Add(D.TransformBy(HalfTurn));
        for(const auto& H: Holes){L.VillageFloorHoles.Add(H);if(MirrorConstruction)L.VillageFloorHoles.Add(H.TransformBy(HalfTurn));}
    };
    auto Stairs=[&](FVector Mouth,float Height,float Width,float Yaw)
    {
        const FTransform Frame(FRotator(0,Yaw,0),Mouth);
        const int32 Count=FMath::CeilToInt(Height/20);const float Step=Height/Count,Length=Count*30;
        TArray<FVector> Path{Frame.TransformPosition({0,-Length-80,100})};
        for(int32 I=0;I<Count;++I)
        {
            const float Y=-Length+(I+.5f)*30,H=(I+1)*Step;
            FERLayoutFixedPiece V;V.Material=EERBlockMaterial::Wood;
            V.Transform=FTransform(FQuat::Identity,FVector(0,Y,H*.5f),FVector(Width,30,H)/100)*Frame;
            L.FixedPieces.Add(V);if(MirrorConstruction){V.Transform=V.Transform*HalfTurn;L.FixedPieces.Add(V);}
            Path.Add(Frame.TransformPosition({0,Y,H+100}));
        }
        Path.Add(Frame.TransformPosition({0,35,Height+100}));L.StairRoutes.Add(Path);
        if(MirrorConstruction){for(auto& P:Path)P=HalfTurn.TransformPosition(P);L.StairRoutes.Add(Path);}
    };
    auto Hut=[&](float X,float Y,float Floor,float Width,float Depth,float Height,bool Roof=true)
    {
        const float Wall=8,Door=170;
        for(float Side:{-1.f,1.f})
        {
            int32 I=Wood({X,Y+Side*(Depth*.5f-Wall*.5f),Floor+Height*.5f},{Width,Wall,Height});
            L.Panels[I].Holes.Add(FBox2D({(Width-Door)*.5f,0},{(Width+Door)*.5f,215}));if(MirrorConstruction)L.Panels[I+1].Holes=L.Panels[I].Holes;
            I=Wood({X+Side*(Width*.5f-Wall*.5f),Y,Floor+Height*.5f},{Wall,Depth-2*Wall,Height});
            L.Panels[I].Holes.Add(FBox2D({Depth*.5f-78,85},{Depth*.5f+62,175}));if(MirrorConstruction)L.Panels[I+1].Holes=L.Panels[I].Holes;
        }
        for(float DX:{-1.f,1.f})for(float DY:{-1.f,1.f})Wood({X+DX*(Width*.5f-18),Y+DY*(Depth*.5f-18),Floor+Height*.5f},{20,20,Height});
        for(float DY:{-1.f,1.f})Wood({X,Y+DY*(Depth*.5f-18),Floor+Height-10},{Width-56,20,20});
        if(Roof)
            Box({X,Y,Floor+Height+Cells.SheetThickness*.5f},
                {Width+70,Depth+70,Cells.SheetThickness},EERBlockMaterial::Metal,false);
        FERLayoutSecondary House;House.Frame=FTransform(FVector(X,Y,Floor));House.Size={Width,Depth,Height};House.Material=EERBlockMaterial::Wood;
        L.SecondaryStructures.Add(House);if(MirrorConstruction){House.Frame=House.Frame*HalfTurn;L.SecondaryStructures.Add(House);}
    };
    for(const auto& Plan:Plans)
    {
        const float BX=Plan.Frame.GetLocation().X,BY=Plan.Frame.GetLocation().Y,W=Plan.Size.X,D=Plan.Size.Y,SX=W/2800,SY=D/700;
        L.VillageBlocks.Add(Plan);if(!Plan.SelfSymmetric){auto Copy=Plan;Copy.Frame=Copy.Frame*HalfTurn;L.VillageBlocks.Add(Copy);}
        MirrorConstruction=!Plan.SelfSymmetric;
        auto ClosedBay=[&](TArray<FVector2D> Polygon)
        {
            const float Height=Z-6,Thickness=12;
            if(T->ReflectionBuild && Plan.SelfSymmetric)
            {
                // Keep an orthogonal 80cm seam through each dogleg. The
                // diagonal approaches remain outside the reflection plane.
                TArray<FVector2D> Seamed;
                for(int32 I=0;I<Polygon.Num();++I)
                {
                    const auto A=Polygon[I],Bend=Polygon[(I+1)%Polygon.Num()];Seamed.Add(A);
                    if(A.Y*Bend.Y<0 && A.X!=Bend.X)
                    {
                        const double X=FMath::Lerp(A.X,Bend.X,-A.Y/(Bend.Y-A.Y));
                        const double Sign=A.Y<0 ? -1:1;Seamed.Add({X,Sign*40});Seamed.Add({X,-Sign*40});
                    }
                }
                Polygon=MoveTemp(Seamed);
            }
            for(auto& P:Polygon)P={BX+P.X*SX,BY+P.Y*SY};
            for(int32 I=0;I<Polygon.Num();++I)
            {
                const FVector2D A=Polygon[I],Bend=Polygon[(I+1)%Polygon.Num()],Edge=Bend-A;
                const FVector2D Center=(A+Bend)*.5+FVector2D(-Edge.Y,Edge.X).GetSafeNormal()*Thickness*.5;
                Box({Center.X,Center.Y,Height*.5f},{Edge.Size(),Thickness,Height},EERBlockMaterial::Wood,true,FRotator(0,FMath::RadiansToDegrees(FMath::Atan2(Edge.Y,Edge.X)),0).Quaternion());
                Wood({FMath::Clamp(A.X,BX-W*.5+8,BX+W*.5-8),FMath::Clamp(A.Y,BY-D*.5+8,BY+D*.5-8),Height*.5f},{16,16,Height});
            }
            L.VillageClosedGround.Add(Polygon);if(MirrorConstruction){for(auto& P:Polygon)P=-P;L.VillageClosedGround.Add(Polygon);}
        };
        if(Plan.SelfSymmetric)MirrorConstruction=true;
        ClosedBay({{-1400,-350},{-660,-350},{-660,-120},{-400,120},{-400,350},{-1400,350}});
        if(Plan.SelfSymmetric)
        {
            MirrorConstruction=false;
            ClosedBay({{-420,-350},{160,-350},{160,-120},{420,120},{420,350},{-160,350},{-160,120},{-420,-120}});
        }
        else
        {
            ClosedBay({{1400,-350},{1400,350},{400,350},{400,120},{660,-120},{660,-350}});
            ClosedBay({{-420,-350},{420,-350},{420,-120},{160,120},{160,350},{-160,350},{-160,120},{-420,-120}});
        }
        MirrorConstruction=true;
        for(float Side:{-1.f,1.f})
        {
            if(Plan.SelfSymmetric && Side>0)continue;
            TArray<FVector> Passage;
            for(const FVector2D P:{FVector2D(540,-450),{540,-200},{540,-120},{410,0},{280,120},{280,200},{280,450}})
                Passage.Add({BX+Side*P.X*SX,BY+P.Y*SY,100});
            L.VillagePassages.Add(Passage);for(auto& P:Passage)P=HalfTurn.TransformPosition(P);L.VillagePassages.Add(Passage);
            for(float End:{-1.f,1.f})
            {
                const float X=BX+Side*(End<0 ? 540:280)*SX,Y=BY+End*(D*.5f-8);
                for(float DX:{-1.f,1.f})Wood({X+DX*(120*SX+8),Y,(Z-6)*.5f},{16,16,Z-6});
                Wood({X,Y,Z-16},{240*SX+32,20,20});
            }
        }
        for(int32 Level=0;Level<Plan.Storeys;++Level)
        {
            const float Floor=Z+Level*300,RoomW=FMath::Min(480.f,W*.18f),RoomD=D-260,RoomX=W*.34f;
            TArray<FBox> Holes;
            if(Level>0)for(float Side:{-1.f,1.f})
            {
                const float Sign=(Level%2 ? 1.f:-1.f)*(Plan.SelfSymmetric && Side>0 ? -1.f:1.f);
                const float X=BX+Side*(RoomX+RoomW*.5f+80),Y=BY-Sign*D*.5f;
                Holes.Add(FBox(FVector(X-60,FMath::Min(Y+Sign*10,Y+Sign*205),Floor-20),FVector(X+60,FMath::Max(Y+Sign*10,Y+Sign*205),Floor+5)));
            }
            MirrorConstruction=!Plan.SelfSymmetric;Deck(BX,BY,W,D,Floor,Level>0 ? Floor-300:0,Holes);
            for(float Side:{-1.f,1.f})
            {
                if(Plan.SelfSymmetric && Side>0)continue;
                MirrorConstruction=true;
                Hut(BX+Side*RoomX,BY,Floor,RoomW,RoomD,Level+1==Plan.Storeys ? 240:294,Level+1==Plan.Storeys);
            }
            MirrorConstruction=!Plan.SelfSymmetric;
            Wood({BX,BY,Floor+68},{400*SX,320*SY,136});GraySheet({BX,BY,Floor+136.1f},{420*SX,340*SY,.2f});
            FERLayoutContainer Store;Store.Frame=FTransform(FVector(BX,BY,Floor));Store.Size={400*SX,320*SY,136};L.Containers.Add(Store);
            if(MirrorConstruction){Store.Frame=Store.Frame*HalfTurn;L.Containers.Add(Store);}
            MirrorConstruction=true;
            Wood({0,BY+D*.25f,Floor+90},{260,24,180});GraySheet({0,BY+D*.25f+12.1f,Floor+90},{260,.2f,180});
            for(float RoomSide:{-1.f,1.f})for(float Side:{-1.f,1.f})
            {
                if(Plan.SelfSymmetric && RoomSide>0)continue;
                const float RW=(RoomW-190)*.5f,CX=BX+RoomSide*RoomX+Side*(95+RW*.5f),CY=BY+D*.5f-35;
                Wood({CX,CY,Floor+62.5f},{RW,24,125});GraySheet({CX,CY+12.1f,Floor+62.5f},{RW,.2f,125});GraySheet({CX,CY,Floor+125.1f},{RW,26,.2f});
                // The return meets the wall: over a ladder well no plank would carry it.
                Wood({BX+RoomSide*RoomX+Side*(RoomW*.5f+12),CY-40,Floor+62.5f},{24,80,125});
                GraySheet({BX+RoomSide*RoomX+Side*(RoomW*.5f+24.1f),CY-40,Floor+62.5f},{.2f,80,125});
                FERLayoutCover Cover;Cover.Frame=FTransform(FVector(CX,CY,Floor));Cover.Size={RW,24,125};Cover.Material=EERBlockMaterial::Wood;Cover.HalfHeight=60;
                Cover.Hide={CX,CY-50,Floor+60};Cover.Entry={BX+RoomSide*RoomX,CY-50,Floor+60};Cover.SightFrom={CX,CY+400,Floor+110};Cover.SightTo={CX,CY-50,Floor+110};
                L.CoverSites.Add(Cover);Cover.Frame=Cover.Frame*HalfTurn;for(FVector* P:{&Cover.Hide,&Cover.Entry,&Cover.SightFrom,&Cover.SightTo})*P=HalfTurn.TransformPosition(*P);L.CoverSites.Add(Cover);
            }
            if(Level>0)for(float Side:{-1.f,1.f})
            {
                if(Plan.SelfSymmetric && Side>0)continue;
                const float Direction=Level%2 ? 1.f:-1.f;
                const FTransform Frame(FRotator(0,-90*Direction,0),{BX+Side*(RoomX+RoomW*.5f+80),BY-Direction*(D*.5f-110),Floor-300});
                for(float DY:{-39.f,39.f})Wood(Frame.TransformPosition({0,DY,152.5f}),{8,8,305});
                for(float H=25;H<300;H+=25)Wood(Frame.TransformPosition({0,0,H}),{76,6,6});
                L.Ladders.Add({Frame,305,300});L.Ladders.Add({Frame*HalfTurn,305,300});
                L.VillageUpperLadders.Add({Frame,305,300});L.VillageUpperLadders.Add({Frame*HalfTurn,305,300});
            }
        }
        MirrorConstruction=true;
        if(L.VillageAccessFlights.IsEmpty())L.VillageAccessFlights.Add(L.StairRoutes.Num());
        for(float X:{-1.f,0.f,1.f})Stairs({BX+X*(W*.34f+100),BY-D*.5f,0},Z,170,0);
    }
    MirrorConstruction=true;
    // Each spawn has two genuine elevated structures: a rear firing gallery
    // and an opposite corner lookout. The side shack stays inside the arena.
    L.VillageBaseVariant=Geometry.RandRange(0,2);
    const float GalleryHeight=160+40*Geometry.RandRange(0,2),GalleryWidth=FMath::Clamp(L.ArenaSize.X-1760,1600.f,2200.f),GalleryDepth=400+100*Geometry.RandRange(0,1);
    const float GalleryY=-HY+GalleryDepth*.5f+40,GalleryFront=GalleryY+GalleryDepth*.5f,GalleryStairX=GalleryWidth*.5f-100;
    Deck(0,GalleryY,GalleryWidth,GalleryDepth,GalleryHeight);
    L.VillageAccessFlights.Add(L.StairRoutes.Num());
    for(float X:{-GalleryStairX,GalleryStairX})Stairs({X,GalleryFront,0},GalleryHeight,150,180);
    if(L.VillageBaseVariant==1)
    {
        for(float DX:{-1.f,1.f})for(float DY:{-1.f,1.f})Wood({DX*(GalleryWidth*.5f-20),GalleryY+DY*(GalleryDepth*.5f-20),GalleryHeight+130},{20,20,260});
        Wood({0,GalleryY,GalleryHeight+250},{GalleryWidth,20,20});GraySheet({0,GalleryY,GalleryHeight+260},{GalleryWidth+20,GalleryDepth+20,.2f});
    }
    if(L.VillageBaseVariant==2)Hut(0,GalleryY,GalleryHeight,GalleryWidth-180,GalleryDepth-180,240);
    for(float X:{-GalleryWidth*.24f,GalleryWidth*.24f})
    {
        Wood({X,GalleryFront-5,GalleryHeight+55},{GalleryWidth*.28f,20,110});GraySheet({X,GalleryFront+5.1f,GalleryHeight+55},{GalleryWidth*.28f,.2f,110});
    }
    Wood({0,GalleryFront-5,GalleryHeight+150},{1300,20,300});
    Wood({0,-HY+40,GalleryHeight+55},{GalleryWidth-20,20,110});
    FERTemplateBase Gallery;Gallery.Frame=FTransform(FVector(0,GalleryY,0));Gallery.Size={GalleryWidth,GalleryDepth,GalleryHeight};Gallery.RoofZ=GalleryHeight;
    L.Bases.Add(Gallery);Gallery.Frame=Gallery.Frame*HalfTurn;L.Bases.Add(Gallery);
    if(T->SizingTeamSize!=1)
    {
    const FVector Tower(-HX+300,-HY+320,0);const float TowerHeight=260+40*Geometry.RandRange(0,2);
    if(R.BridgeStructure)
    {
        if(!ImportedWood(R.BridgeStructure,Tower+FVector(0,0,TowerHeight*.5f),{440,440,TowerHeight}))return false;
        const FBox D(Tower+FVector(-220,-220,TowerHeight-15),Tower+FVector(220,220,TowerHeight));L.ElevatedDecks.Add(D);L.ElevatedDecks.Add(D.TransformBy(HalfTurn));
    }
    else Deck(Tower.X,Tower.Y,440,440,TowerHeight);
    L.VillageAccessFlights.Add(L.StairRoutes.Num());
    Stairs(Tower+FVector(0,220,0),TowerHeight,150,180);
    for(float DX:{-1.f,1.f})for(float DY:{-1.f,1.f})Wood(Tower+FVector(DX*195,DY*195,TowerHeight+115),{18,18,230});
    for(float DX:{-1.f,1.f})Wood(Tower+FVector(DX*205,0,TowerHeight+95),{18,440,20});
    Wood(Tower+FVector(0,-205,TowerHeight+95),{440,18,20});
    for(float DX:{-1.f,1.f})Wood(Tower+FVector(DX*155,205,TowerHeight+95),{110,18,20});
    Wood(Tower+FVector(0,0,TowerHeight+220),{410,20,20});
    Box(Tower+FVector(0,0,TowerHeight+230),{500,500,.2f},EERBlockMaterial::Metal,false);
    L.DistrictTowers.Add(FTransform(Tower));L.DistrictTowers.Add(FTransform(Tower)*HalfTurn);L.DistrictTowerHeights={TowerHeight,TowerHeight};
    // One long, enterable shack on the opposing side of each spawn.
    const float StoreW=Sample(500,600),StoreD=Sample(600,800),StoreH=260+40*Geometry.RandRange(0,2);
    const FVector Store(HX-StoreW*.5f-130,-HY+850,StoreH*.5f);
    if(R.StorageStructure && !ImportedWood(R.StorageStructure,Store,{StoreW,StoreD,StoreH}))return false;
    if(!R.StorageStructure)Hut(Store.X,Store.Y,0,StoreW,StoreD,StoreH,false);
    Wood(Store+FVector(0,0,StoreH*.5f-3),{StoreW,StoreD,6});GraySheet(Store+FVector(0,0,StoreH*.5f+.1f),{StoreW+40,StoreD+40,.2f});
    const FBox Shack(Store-FVector(StoreW,StoreD,StoreH)*.5f,Store+FVector(StoreW,StoreD,StoreH)*.5f);L.TownBuildings.Add(Shack);L.TownBuildings.Add(Shack.TransformBy(HalfTurn));
    // Timber ladder on the inward side of the shack roof, away from the flank.
    const FTransform Ladder(FRotator(0,180,0),{Store.X-StoreW*.5f-10,Store.Y,0});
    for(float Y:{-39.f,39.f})Wood(Ladder.TransformPosition({0,Y,(StoreH+110)*.5f}),{8,8,StoreH+110});
    for(float H=25;H<StoreH;H+=25)Wood(Ladder.TransformPosition({0,0,H}),{6,76,6});
    L.Ladders.Add({Ladder,StoreH+110,StoreH});L.Ladders.Add({Ladder*HalfTurn,StoreH+110,StoreH});
    Wood({0,-HY+620,90},{600,24,180});GraySheet({0,-HY+632.1f,90},{600,.2f,180});
    }
    for(int32 SpawnRow=0;SpawnRow<2;++SpawnRow)for(int32 I=0;I<5;++I)
    {
        const FVector P((I-2)*280,GalleryY-60+SpawnRow*120,GalleryHeight+100);
        L.AlphaSpawns.Add(FTransform(FRotator(0,90,0),P));L.OmegaSpawns.Add(FTransform(FRotator(0,-90,0),HalfTurn.TransformPosition(P)));
    }
    // Reserve a cross lane between blocks; an odd origin block occupies Y=0.
    float LaneY=0;
    if(Odd)
    {
        const float CenterFront=-Plans[0].Size.Y*.5f;
        LaneY=Count==1 ? CenterFront-520:Plans[1].Frame.GetLocation().Y+Plans[1].Size.Y*.5f+160;
    }
    const float SideWidth=T->SizingTeamSize==1 ? 300.f:Count==1 ? 700.f:Count==2 ? 580.f:260.f,SideHeight=240,Flank=HX-60;
    TArray<float> WallYs;
    if(Count==1)
    {
        const float Run=FMath::CeilToInt(Z/20)*30;
        const float ApproachEnd=-Plans[0].Size.Y*.5f-Run-140;
        const float Y=(-HY+1300+ApproachEnd)*.5f;
        WallYs={Y,-Y};LaneY=Y+20;
    }
    else
    {
        WallYs.Add(LaneY+20);
        if(Count==2)
        {
            const auto& Alpha=Plans[0];const float Run=FMath::CeilToInt(Z/20)*30;
            const float ApproachEnd=Alpha.Frame.GetLocation().Y-Alpha.Size.Y*.5f-Run-140;
            WallYs.Add((-HY+1300+ApproachEnd)*.5f);
        }
    }
    TArray<TArray<FVector>> LeftBypasses;
    for(float Y:WallYs)
    {
        const float X=-HX+SideWidth*.5f,Inner=-HX+SideWidth+100;
        Wood({X,Y,SideHeight*.5f},{SideWidth,24,SideHeight});
        GraySheet({X,Y+12.1f,SideHeight*.5f},{SideWidth,.2f,SideHeight});
        GraySheet({X,Y,SideHeight+.1f},{SideWidth,26,.2f});
        const FBox Wall(FVector(-HX,Y-12,0),FVector(-HX+SideWidth,Y+12,SideHeight));
        L.VillageSideWalls.Add(Wall);L.VillageSideWalls.Add(Wall.TransformBy(HalfTurn));
        TArray<FVector> Bypass{{-Flank,Y-80,100},{Inner,Y-80,100},{Inner,Y+55,100},{-Flank,Y+55,100}};
        LeftBypasses.Add(Bypass);L.VillageSideBypasses.Add(Bypass);
        for(auto& P:Bypass)P=HalfTurn.TransformPosition(P);L.VillageSideBypasses.Add(Bypass);
    }
    // Follow each widened wall around its inner end; never route a pawn
    // through the cover or cut a diagonal across one of its corners.
    LeftBypasses.Sort([](const auto& A,const auto& B){return A[0].Y<B[0].Y;});
    L.CrossingRoutes.SetNum(2);L.CrossingRoutes[0].Add({-Flank,FMath::Min(-HY+1300,float(LeftBypasses[0][0].Y)-100),100});
    for(const auto& Bypass:LeftBypasses)L.CrossingRoutes[0].Append(Bypass);
    L.CrossingRoutes[0].Add({-Flank,FMath::Max(HY-1300,float(LeftBypasses.Last().Last().Y)+100),100});
    L.CrossingRoutes[1]=L.CrossingRoutes[0];for(auto& P:L.CrossingRoutes[1])P=HalfTurn.TransformPosition(P);
    const float LaneStart=WallYs[0]-80,Inner=-HX+SideWidth+100;
    L.LowerRoute={{-Flank,LaneStart,100},{Inner,LaneStart,100},{0,LaneStart,100},{-Inner,Odd ? LaneStart:-LaneStart,100},{Flank,Odd ? LaneStart:-LaneStart,100}};
    const auto& First=Plans[0];const FVector FP=First.Frame.GetLocation();
    L.UpperRoute={{FP.X-First.Size.X*.45,FP.Y-First.Size.Y*.5+65,Z+100},{FP.X,FP.Y-First.Size.Y*.5+65,Z+100},{FP.X+First.Size.X*.45,FP.Y-First.Size.Y*.5+65,Z+100}};
    TArray<FVector> Exit{L.AlphaSpawns[0].GetLocation(),{0,GalleryY,GalleryHeight+100},{0,GalleryFront-50,GalleryHeight+100},{-GalleryStairX,GalleryFront-50,GalleryHeight+100}};
    const auto& GalleryFlight=L.StairRoutes[L.VillageAccessFlights[1]];
    for(int32 I=GalleryFlight.Num()-1;I>=0;--I)Exit.Add(GalleryFlight[I]);
    L.Routes.Add(Exit);for(auto& P:Exit)P=HalfTurn.TransformPosition(P);L.Routes.Add(Exit);L.Routes.Add(L.CrossingRoutes[0]);L.Routes.Add(L.CrossingRoutes[1]);L.Routes.Add(L.LowerRoute);
    for(const auto& Passage:L.VillagePassages)L.Routes.Add(Passage);
    L.Spawn=L.AlphaSpawns[0].GetLocation();
    PlanVillageTerrain(L,C.Seed,Envelope,WallYs);
    if(T->SizingTeamSize>=2)
    {
        // Distribute native ground-level timber/metal L shelters across the
        // available approaches and flanks. Tall storeys do not fill a plan.
        TArray<FBox> Occupied;
        for(const auto& P:L.Panels)
        {
            const auto Bounds=FBox(FVector::ZeroVector,P.Size).TransformBy(P.Transform);
            if(Bounds.Min.Z<260 && Bounds.Max.Z>0)Occupied.Add(Bounds);
        }
        for(const auto& P:L.FixedPieces)if(!P.VisualOnly)
        {
            const auto Bounds=FBox(FVector(-50),FVector(50)).TransformBy(P.Transform);
            if(Bounds.Min.Z<260 && Bounds.Max.Z>0)Occupied.Add(Bounds);
        }
        TArray<TArray<FVector>> Reserved=L.Routes;Reserved.Append(L.StairRoutes);Reserved.Append(L.VillageSideBypasses);
        auto Clear=[&](const FBox& Bounds)
        {
            if(Bounds.Min.X<-HX+90 || Bounds.Max.X>HX-90 || Bounds.Min.Y<-HY+1000 || Bounds.Max.Y>HY-1000)return false;
            if(!L.VillageTerrainProfile.IsEmpty() && !FMath::IsNearlyEqual(VillageGroundHeight(L,{0,Bounds.Min.Y-300}),VillageGroundHeight(L,{0,Bounds.Max.Y+300}),.01f))return false;
            const auto Margin=Bounds.ExpandBy(FVector(80,80,5));
            for(const auto& B:Occupied)if(Margin.Intersect(B))return false;
            const auto Standing=Bounds.ExpandBy(FVector(50,50,93));
            for(const auto& Path:Reserved)for(int32 I=1;I<Path.Num();++I)
                if(FMath::LineBoxIntersection(Standing,Path[I-1],Path[I],Path[I]-Path[I-1]))return false;
            for(const auto* Starts:{&L.AlphaSpawns,&L.OmegaSpawns})for(const auto& Start:*Starts)
                if(Standing.IsInsideOrOn(Start.GetLocation()))return false;
            return true;
        };
        const float Area=L.ArenaSize.X*L.ArenaSize.Y/10000;
        const int32 Target=FMath::Min(24,FMath::CeilToInt(Area/450)),Minimum=FMath::CeilToInt(Area/1500);
        const int32 Columns=FMath::CeilToInt(L.ArenaSize.X/600),Rows=FMath::Max(1,FMath::CeilToInt((HY-1300)/600));
        TArray<TArray<int32>> Order;Order.SetNum(Rows);TArray<int32> Next;Next.Init(0,Rows);
        for(auto& Row:Order)
        {
            for(int32 Col=0;Col<Columns;++Col)Row.Add(Col);
            for(int32 I=Row.Num()-1;I>0;--I)Swap(Row[I],Row[Geometry.RandRange(0,I)]);
        }
        int32 Pairs=0;bool Added=true;
        while(Added && Pairs<Target)
        {
            Added=false;
            // One shelter per depth band on each pass prevents clustering.
            for(int32 Row=0;Row<Rows && Pairs<Target;++Row)
            {
                bool Placed=false;
                while(Next[Row]<Columns && !Placed)
                {
                    const int32 Col=Order[Row][Next[Row]++],FirstYaw=Geometry.RandRange(0,3);
                    const FVector Center(-HX+(Col+.5f)*L.ArenaSize.X/Columns,-(Row+.5f)*(HY-1300)/Rows,0);
                    for(int32 Turn=0;Turn<4 && !Placed;++Turn)
                    {
                        const FTransform Frame(FRotator(0,90*((FirstYaw+Turn)%4),0),Center);
                        const auto Bounds=FBox(FVector(-240,-120,0),FVector(240,120,240)).TransformBy(Frame),Other=Bounds.TransformBy(HalfTurn);
                        if(Bounds.ExpandBy(FVector(80,80,0)).Intersect(Other) || !Clear(Bounds) || !Clear(Other))continue;
                        auto EntryClear=[&](const FTransform& At,const FBox& Partner)
                        {
                            const FVector A=At.TransformPosition({0,260,100}),D=At.TransformPosition({0,0,100});
                            const FVector CapsuleMargin(35,35,93);
                            if(FMath::Abs(A.X)>HX-60 || FMath::Abs(A.Y)>HY-60)return false;
                            if(FMath::LineBoxIntersection(Partner.ExpandBy(CapsuleMargin),A,D,D-A))return false;
                            for(const auto& B:Occupied)if(FMath::LineBoxIntersection(B.ExpandBy(CapsuleMargin),A,D,D-A))return false;
                            return true;
                        };
                        if(!EntryClear(Frame,Other) || !EntryClear(Frame*HalfTurn,Bounds))continue;
                        auto Piece=[&](FVector P,FVector Size,bool Metal=false)
                        {
                            const int32 Index=Box(Frame.TransformPosition(P),Size,Metal ? EERBlockMaterial::Metal:EERBlockMaterial::Wood,!Metal,Frame.GetRotation());
                            if(Metal){L.Panels[Index].Surface=R.CoverSurface;L.Panels[Index+1].Surface=R.CoverSurface;}
                        };
                        Piece({0,-108,120},{480,24,240});Piece({0,-95.9f,120},{480,.2f,240},true);Piece({0,-108,240.1f},{480,26,.2f},true);
                        Piece({-228,12,120},{24,216,240});Piece({-215.9f,12,120},{.2f,216,240},true);Piece({-228,12,240.1f},{26,216,.2f},true);
                        L.LowCovers.Add(Bounds);L.LowCovers.Add(Other);Occupied.Add(Bounds);Occupied.Add(Other);
                        FERLayoutCover Site;Site.Frame=Frame;Site.Size={480,240,240};Site.Material=EERBlockMaterial::Wood;Site.HalfHeight=92.5;
                        Site.Entry=Frame.TransformPosition({0,260,100});Site.Hide=Frame.TransformPosition({0,0,100});
                        Site.SightFrom=Frame.TransformPosition({0,-400,164});Site.SightTo=Frame.TransformPosition({0,0,164});
                        L.CoverSites.Add(Site);Reserved.Add({Site.Entry,Site.Hide});
                        Site.Frame=Site.Frame*HalfTurn;for(FVector* P:{&Site.Entry,&Site.Hide,&Site.SightFrom,&Site.SightTo})*P=HalfTurn.TransformPosition(*P);
                        L.CoverSites.Add(Site);Reserved.Add({Site.Entry,Site.Hide});
                        ++Pairs;Placed=true;Added=true;
                    }
                }
            }
        }
        // A sparse candidate cannot pass just because its area is correct.
        if(Pairs<Minimum)return false;
    }
    FERLayoutFixedPiece Backdrop;Backdrop.Transform=FTransform(FQuat::Identity,FVector(0,0,-22),FVector(L.ArenaSize.X+200000,L.ArenaSize.Y+200000,4)/100);Backdrop.Material=EERBlockMaterial::Wood;Backdrop.VisualOnly=true;L.FixedPieces.Add(Backdrop);
    if(!R.ExteriorTrees.IsEmpty())for(int32 I=0;I<64;++I)
    {
        auto* Mesh=R.ExteriorTrees[I%R.ExteriorTrees.Num()].Get();if(!Mesh)return false;
        const float Height=Cosmetic.RandRange(900,1450),Diameter=Cosmetic.RandRange(400,600);
        const int32 Edge=I%4;const float Along=Cosmetic.FRandRange(-1,1),Margin=Diameter*.5f+180;
        const FVector P=Edge<2 ? FVector((Edge ? -1:1)*(HX+Margin),Along*(HY+650),Height*.5f):FVector(Along*(HX+650),(Edge==2 ? -1:1)*(HY+Margin),Height*.5f);
        FERLayoutFixedPiece V;V.Transform=FTransform(FQuat::Identity,P,FVector(Diameter,Diameter,Height)/100);V.Mesh=Mesh;V.VisualOnly=true;V.Material=EERBlockMaterial::Wood;V.MeshRotation=FRotator(0,Cosmetic.FRandRange(0,360),0);L.FixedPieces.Add(V);
    }
    if(R.SkyDomeMesh)
    {
        FERLayoutFixedPiece Sky;Sky.Mesh=R.SkyDomeMesh;Sky.VisualOnly=true;Sky.Material=EERBlockMaterial::Wood;
        Sky.Transform=FTransform(FQuat::Identity,FVector::ZeroVector,FVector(200000,200000,200000)/100);L.FixedPieces.Add(Sky);
    }
    L.FeatureNames={TEXT("Jungle"),TEXT("VariableSymmetricBlocks"),TEXT("OneToThreeStoreys"),TEXT("VariableSpawnBases"),TEXT("RearSpawnGalleries"),TEXT("CornerLookouts"),TEXT("SideShacks"),TEXT("GrayCoverPockets"),TEXT("AdaptiveStandingSideWalls"),TEXT("LogPalisade"),TEXT("SealedPerimeter"),TEXT("OnlyHatchedGroundPassages")};
    ApplyVillageTerrain(L);
    Out=MoveTemp(L);return true;
}
}
