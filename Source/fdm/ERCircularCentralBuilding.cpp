#include "ERProceduralMap.h"
#include "ERIndustrialFence.h"
#include "Algo/Reverse.h"

namespace ERProcedural
{
bool BuildCircularCentralBuilding(const UERMapTemplate& T,int32 Seed,TArray<FERMapTemplatePiece>& Pieces,FERProceduralLayout& L,TArray<FVector>& Patrol,int32 CrossingOverride)
{
    const auto& V=T.CentralBuilding;
    if(V.MinFloors<1 || V.MaxFloors>3 || V.MinFloors>V.MaxFloors || T.RoofMesh || T.ShellWallMesh || CrossingOverride<0 || CrossingOverride>3)return false;
    for(const auto* Palette:{&V.ConcreteSurfaces,&V.BrickSurfaces,&V.WoodSurfaces,&V.GroundSurfaces,&V.ContainerSurfaces})
        if(Palette->Num()>8 || Palette->Contains(nullptr))return false;
    FRandomStream Geometry(Seed);
    const int32 Choice=T.SizingTeamSize>0 ? T.CompositionSeed:Seed;
    auto Pick=[&](float A,float B){return Geometry.RandRange(FMath::CeilToInt32(A/100),FMath::FloorToInt32(B/100))*100.f;};
    L.CircularArena=true;L.GradedTerrain=true;L.LayoutSymmetry=EERTemplateSymmetry::Mirror;
    const float Diameter=T.SizingTeamSize>0 ? T.SizingArena.X:7600;
    if(!FMath::IsFinite(Diameter) || Diameter<3500 || Diameter>18000)return false;
    const float Radius=Diameter*.5f,Stride=320;
    L.ArenaSize={Diameter,Diameter,2000};
    L.CrossingRule=CrossingOverride ? EERCrossingRule(CrossingOverride):EERCrossingRule(1+HashCombineFast(uint32(Choice),0x43524f53u)%3);
    L.CentralShape=HashCombineFast(uint32(Choice),0x53484150u)%4;
    L.CentralHill=(HashCombineFast(uint32(Choice),0x48494c4cu)&1)!=0;
    const float W=Pick(V.BuildingWidth.X,V.BuildingWidth.Y),D=Pick(V.BuildingDepth.X,V.BuildingDepth.Y);
    const float Neck=FMath::GridSnap(W*.60f,100.f),Band=FMath::GridSnap(D*.24f,100.f);
    const float InnerMin=-W*.5f,InnerMax=L.CentralShape==1 ? -W*.5f+Neck:W*.5f;
    const float OuterMin=L.CentralShape==2 ? -Neck*.5f:L.CentralShape==3 ? -W*.5f+400:-W*.5f;
    const float OuterMax=L.CentralShape==2 ? Neck*.5f:W*.5f;
    // H profile: retain a centered neck, rather than moving one team's wing.
    const float MinI=L.CentralShape==1 ? -Neck*.5f:InnerMin,MaxI=L.CentralShape==1 ? Neck*.5f:InnerMax;
    const float CommonMin=FMath::Max(MinI,OuterMin),CommonMax=FMath::Min(MaxI,OuterMax),HallX=(CommonMin+CommonMax)*.5f;
    const float SideRoom=FMath::Max(0.f,float(FMath::Sqrt(FMath::Max(0.f,FMath::Square(Radius-140)-FMath::Square(D*.5f+260)))-W*.5f-260));
    const float BuildingX=L.CentralHill ? 0:-FMath::Min3(Radius*.32f,1500.f,SideRoom);
    L.BuildingFloors=Geometry.RandRange(V.MinFloors,V.MaxFloors);
    L.CentralBuildingSize={W,D,L.BuildingFloors*Stride};
    L.HillHeight=FMath::Min(320.f,Radius*.065f);L.HillFalloff=FMath::Max(650.f,Radius*.38f);L.HillPlateau=Radius*.10f;
    const float HillX=FMath::Min(Radius-250,FMath::Max(Radius*.48f,BuildingX+W*.5f+180+L.HillPlateau+200));
    L.HillCenter={L.CentralHill ? 0:HillX,0,0};
    const float Base=L.CentralHill ? L.HillHeight:0;
    L.CentralBuildingCenter={BuildingX,0,Base};
    L.TerrainPads.Add(FBox(FVector(BuildingX-W*.5f,-D*.5f,Base),FVector(BuildingX+W*.5f,D*.5f,Base)));
    auto Surface=[&](const TArray<TObjectPtr<UMaterialInterface>>& Palette,uint32 Salt)->UMaterialInterface*
    {return Palette.IsEmpty() ? nullptr:Palette[HashCombineFast(uint32(Seed),Salt)%Palette.Num()].Get();};
    L.GroundSurface=Surface(V.GroundSurfaces,0x47524f55u);
    const auto MainMaterial=Geometry.RandRange(0,1)!=0 ? EERBlockMaterial::Brick:EERBlockMaterial::Concrete;
    auto Box=[&](FVector Center,FVector Size,EERBlockMaterial Material,bool Fixed=false,bool Pair=true,bool Deck=false)->FERMapTemplatePiece&
    {
        auto& P=Pieces.AddDefaulted_GetRef();P.Center=Center;P.Size=Size;P.Material=Material;P.Destructible=!Fixed;P.Mirrored=Pair;P.WalkableSurface=Deck;
        auto* Finish=Material==EERBlockMaterial::Metal ? (Deck && V.DeckSurface ? V.DeckSurface.Get():V.SteelSurface.Get()):
            Material==EERBlockMaterial::Brick ? Surface(V.BrickSurfaces,0x42524943u):Material==EERBlockMaterial::Wood ? Surface(V.WoodSurfaces,0x574f4f44u):Surface(V.ConcreteSurfaces,0x434f4e43u);
        if(Fixed)P.FixedSurface=Finish;else P.CellSurface=Finish;return P;
    };
    auto BuildingBox=[&](FVector P,FVector Size,EERBlockMaterial Material,bool Fixed=false,bool Pair=true,bool Deck=false)->FERMapTemplatePiece&
    {
        auto& Piece=Box(P+L.CentralBuildingCenter,Size,Material,Fixed,Pair,Deck);
        const float Bottom=P.Z-Size.Z*.5f;Piece.Anchored=Bottom>0 && FMath::IsNearlyZero(FMath::Fmod(Bottom,Stride),.01f);return Piece;
    };
    auto Mate=[](FVector P){return FVector(P.X,-P.Y,P.Z);};
    auto GroundPath=[&](FVector2D A,FVector2D B)
    {
        TArray<FVector> Path;const int32 Steps=FMath::Max(1,FMath::CeilToInt32(FVector2D::Distance(A,B)/60));
        for(int32 I=0;I<=Steps;++I){const auto P=FMath::Lerp(A,B,float(I)/Steps);Path.Add(FVector(P,CentralGroundHeight(L,P)+100));}return Path;
    };
    auto Append=[](TArray<FVector>& Path,const TArray<FVector>& Leg)
    {for(const auto& Point:Leg)if(Path.IsEmpty() || !Path.Last().Equals(Point,.01))Path.Add(Point);};
    const float DoorY=-D*.5f+180,StairX=CommonMin+150,StairStart=-D*.5f+400,StairEnd=StairStart+480;
    const FBox2D Hole({StairX-110,StairStart-20},{StairX+110,StairEnd});
    const FBox2D WallWell({HallX-110,-250},{HallX+110,0});
    const TArray<FBox2D> Footprints{FBox2D({OuterMin,-D*.5f},{OuterMax,-Band}),FBox2D({MinI,-Band},{MaxI,0})};
    for(const auto& Rect:Footprints)for(int32 Copy=0;Copy<2;++Copy)
    {
        const FBox Bounds(FVector(Rect.Min,0)+L.CentralBuildingCenter,FVector(Rect.Max,L.BuildingFloors*Stride)+L.CentralBuildingCenter);
        L.CentralFootprints.Add(Copy ? FBox(Mate(Bounds.Max).ComponentMin(Mate(Bounds.Min)),Mate(Bounds.Max).ComponentMax(Mate(Bounds.Min))):Bounds);
    }
    // Exact rectangular union. Emit boundary walls inside occupied cells;
    // the Y=0 seam stays open until the selected separator is added.
    TArray<float> X{MinI,MaxI};X.AddUnique(OuterMin);X.AddUnique(OuterMax);X.Sort();
    const TArray<float> Y{-D*.5f,-Band,0};const int32 NX=X.Num()-1;
    TArray<uint8> Occupied;Occupied.Init(0,NX*2);
    for(int32 J=0;J<2;++J)for(int32 I=0;I<NX;++I)
        Occupied[J*NX+I]=(X[I]+X[I+1])*.5f>Footprints[J].Min.X-.01 && (X[I]+X[I+1])*.5f<Footprints[J].Max.X+.01 ? 1:0;
    auto Wall=[&](bool AlongX,float At,float Lo,float Hi,float FloorZ,bool Door)
    {
        if(Hi-Lo<1)return;
        auto Part=[&](float A,float B,float Bottom,float Top)
        {
            if(B-A<1 || Top-Bottom<1)return;
            BuildingBox(AlongX ? FVector((A+B)*.5f,At,FloorZ+(Bottom+Top)*.5f):FVector(At,(A+B)*.5f,FloorZ+(Bottom+Top)*.5f),AlongX ? FVector(B-A,20,Top-Bottom):FVector(20,B-A,Top-Bottom),MainMaterial);
        };
        const bool Window=!Door && Hi-Lo>=500;
        if(Door || Window)
        {
            const float Center=Door ? DoorY:(Lo+Hi)*.5f,Half=Door ? 120:90,Sill=Door ? 0:90;
            Part(Lo,Center-Half,0,300);Part(Center+Half,Hi,0,300);Part(Center-Half,Center+Half,0,Sill);Part(Center-Half,Center+Half,240,300);
        }
        else Part(Lo,Hi,0,300);
        if(FMath::IsNearlyEqual(FloorZ,(L.BuildingFloors-1)*Stride))
            BuildingBox(AlongX ? FVector((Lo+Hi)*.5f,At,L.BuildingFloors*Stride+50):FVector(At,(Lo+Hi)*.5f,L.BuildingFloors*Stride+50),AlongX ? FVector(Hi-Lo,20,100):FVector(20,Hi-Lo,100),MainMaterial);
    };
    TArray<FVector> NorthStair;
    for(int32 Floor=0;Floor<L.BuildingFloors;++Floor)
    {
        const float Z=Floor*Stride;
        for(int32 J=0;J<2;++J)for(int32 I=0;I<NX;++I)if(Occupied[J*NX+I])
        {
            const bool Bottom=J==0 || !Occupied[(J-1)*NX+I],Top=J==0 && !Occupied[(J+1)*NX+I];
            if(Bottom)Wall(true,Y[J]+10,X[I],X[I+1],Z,false);
            if(Top)Wall(true,Y[J+1]-10,X[I],X[I+1],Z,false);
            const float Lo=Y[J]+(Bottom ? 20:0),Hi=Y[J+1]-(Top ? 20:0);
            if(I==0 || !Occupied[J*NX+I-1])Wall(false,X[I]+10,Lo,Hi,Z,J==0 && FMath::IsNearlyEqual(X[I],OuterMin));
            if(I==NX-1 || !Occupied[J*NX+I+1])Wall(false,X[I+1]-10,Lo,Hi,Z,false);
        }
        // A room wing beside the circulation spine, with a real side doorway.
        if(CommonMax-HallX>=650)
        {
            const float At=HallX+320,Gap=DoorY+300,Lo=-D*.5f+20,Hi=-Band;
            if(Gap+110<Hi && Gap-110>Lo)
            {
                BuildingBox({At,(Lo+Gap-110)*.5f,Z+150},{15,Gap-110-Lo,300},MainMaterial);
                BuildingBox({At,(Gap+110+Hi)*.5f,Z+150},{15,Hi-Gap-110,300},MainMaterial);
                BuildingBox({At,Gap,Z+270},{15,220,60},MainMaterial);
                TArray<FVector> Room{{BuildingX+At-120,Gap,Base+Z+100},{BuildingX+At+120,Gap,Base+Z+100}};L.RoomAccessRoutes.Add(Room);for(auto& P:Room)P=Mate(P);L.RoomAccessRoutes.Add(Room);
            }
        }
        // Deck rectangles partition the union and subtract the actual stairwell.
        for(const auto& Rect:Footprints)
        {
            TArray<double> CX{Rect.Min.X,Rect.Max.X},CY{Rect.Min.Y,Rect.Max.Y};
            for(double Cut:{Hole.Min.X,Hole.Max.X})if(Cut>Rect.Min.X && Cut<Rect.Max.X)CX.AddUnique(Cut);
            for(double Cut:{Hole.Min.Y,Hole.Max.Y})if(Cut>Rect.Min.Y && Cut<Rect.Max.Y)CY.AddUnique(Cut);
            const bool WallHeadroom=Floor==0 && L.CrossingRule==EERCrossingRule::ClimbableWall;
            if(WallHeadroom)
            {
                for(double Cut:{WallWell.Min.X,WallWell.Max.X})if(Cut>Rect.Min.X && Cut<Rect.Max.X)CX.AddUnique(Cut);
                for(double Cut:{WallWell.Min.Y,WallWell.Max.Y})if(Cut>Rect.Min.Y && Cut<Rect.Max.Y)CY.AddUnique(Cut);
            }
            CX.Sort();CY.Sort();
            for(int32 IX=1;IX<CX.Num();++IX)for(int32 IY=1;IY<CY.Num();++IY)
            {
                const FVector2D Mid((CX[IX-1]+CX[IX])*.5,(CY[IY-1]+CY[IY])*.5);
                if(Hole.IsInside(Mid) || (WallHeadroom && WallWell.IsInside(Mid)))continue;
                BuildingBox({Mid.X,Mid.Y,Z+Stride-10},{CX[IX]-CX[IX-1],CY[IY]-CY[IY-1],20},EERBlockMaterial::Metal,true,true,true);
            }
        }
        TArray<FVector> Stair{{BuildingX+StairX,StairStart-100,Base+Z+100}};
        for(int32 Step=0;Step<16;++Step)
        {
            const float At=StairStart+(Step+.5f)*30,Top=(Step+1)*20;
            BuildingBox({StairX,At,Z+Top-10},{180,30,20},EERBlockMaterial::Metal,true,true,true);
            Stair.Add({BuildingX+StairX,At,Base+Z+Top+120});
        }
        Stair.Add({BuildingX+StairX,StairEnd+100,Base+Z+Stride+100});
        if(Floor==0)NorthStair=Stair;
        L.StairRoutes.Add(Stair);for(auto& P:Stair)P=Mate(P);L.StairRoutes.Add(Stair);
        TArray<FVector> Entry{{BuildingX+OuterMin-100,DoorY,Base+Z+100},{BuildingX+OuterMin+160,DoorY,Base+Z+100}};
        L.RoomAccessRoutes.Add(Entry);for(auto& P:Entry)P=Mate(P);L.RoomAccessRoutes.Add(Entry);
        if(Floor>0)
        {
            BuildingBox({OuterMin-90,DoorY,Z-10},{180,400,20},EERBlockMaterial::Metal,true,true,true);
            BuildingBox({OuterMin-170,DoorY,Z+50},{20,400,100},MainMaterial);
            for(float Sign:{-1.f,1.f})BuildingBox({OuterMin-80,DoorY+Sign*190,Z+50},{160,20,100},MainMaterial);
        }
    }
    L.CentralEntrances.Emplace(FRotator(0,180,0),FVector(BuildingX+OuterMin,DoorY,Base));
    L.CentralEntrances.Emplace(FRotator(0,180,0),FVector(BuildingX+OuterMin,-DoorY,Base));
    // Enterable secondary buildings flank the lateral hill, with level pads.
    float FlankX=L.CentralHill ? BuildingX+W*.5f+350:Radius*.48f;
    if(!L.CentralHill)
    {
        const int32 Pairs=T.SizingTeamSize<3 ? 1:1+int32((uint32(Choice)>>2)&1);
        const float HouseX=Radius*(T.SizingTeamSize==1 ? .60f:.55f);
        for(int32 I=0;I<Pairs;++I)
        {
            const float HW=T.SizingTeamSize==1 ? 500:Pick(600,1000),HD=T.SizingTeamSize==1 ? 500:Pick(600,800);
            const float HouseY=-Radius*(I ? .17f:T.SizingTeamSize==1 ? .35f:.48f);
            const FVector2D Center(HouseX,HouseY);const float H=CentralGroundHeight(L,Center);
            if(FVector2D(HouseX+HW*.5f,HouseY-HD*.5f).Size()>Radius-100)return false;
            for(int32 Copy=0;Copy<2;++Copy)
            {
                const FVector C=Copy ? Mate(FVector(Center,H)):FVector(Center,H);
                L.TerrainPads.Add(FBox(C-FVector(HW*.5f+180,HD*.5f+180,0),C+FVector(HW*.5f+180,HD*.5f+180,0)));
                L.SecondaryStructures.Add({FTransform(C),{HW,HD,320},1,EERBlockMaterial::Brick,false,false});
                L.TownBuildings.Add(FBox(C-FVector(HW*.5f,HD*.5f,0),C+FVector(HW*.5f,HD*.5f,320)));
            }
            Box({HouseX+HW*.5f-10,HouseY,H+150},{20,HD-40,300},EERBlockMaterial::Brick);
            for(float Sign:{-1.f,1.f})Box({HouseX,HouseY+Sign*(HD*.5f-10),H+150},{HW,20,300},EERBlockMaterial::Brick);
            for(float Sign:{-1.f,1.f})Box({HouseX-HW*.5f+10,HouseY+Sign*(HD+140)*.25f,H+150},{20,(HD-220)*.5f,300},EERBlockMaterial::Brick);
            Box({HouseX-HW*.5f+10,HouseY,H+270},{20,180,60},EERBlockMaterial::Brick);
            Box({HouseX,HouseY,H+310},{HW,HD,20},EERBlockMaterial::Brick,false,true,true);
            TArray<FVector> Entry{{HouseX-HW*.5f-100,HouseY,H+100},{HouseX,HouseY,H+100}};L.RoomAccessRoutes.Add(Entry);for(auto& P:Entry)P=Mate(P);L.RoomAccessRoutes.Add(Entry);
            FlankX=FMath::Min(FlankX,(BuildingX+FMath::Max(MaxI,OuterMax)+HouseX-HW*.5f)*.5f);
        }
    }
    // Small, varied cover groups; sample the north half and mirror every member.
    // Rear entries stay outside all return walls, even in the pocket profile.
    for(int32 BandIndex=0;BandIndex<2;++BandIndex)for(float Sign:{-1.f,1.f})
    {
        const FVector2D P(Sign*Radius*(.55f+BandIndex*.20f)+Geometry.RandRange(-4,4)*10.f,-Radius*(.55f-BandIndex*.35f)+Geometry.RandRange(-4,4)*10.f);
        const float OutsideX=FMath::Abs(P.X-BuildingX)-W*.5f,OutsideY=FMath::Abs(P.Y)-D*.5f;
        if((OutsideX<450 && OutsideY<450) || P.Size()>Radius-500 || FMath::Abs(P.X-FlankX)<600)continue;
        bool Clear=true;for(const auto& S:L.SecondaryStructures)Clear&=FMath::Abs(P.X-S.Frame.GetLocation().X)>S.Size.X*.5+650 || FMath::Abs(P.Y-S.Frame.GetLocation().Y)>S.Size.Y*.5+650;
        if(!Clear)continue;
        const float H=CentralGroundHeight(L,P);
        const float PadHalf=450+Radius*.025f;
        for(float PairSign:{-1.f,1.f})L.TerrainPads.Add(FBox(FVector(P.X,PairSign*P.Y,H)-FVector(PadHalf,PadHalf,0),FVector(P.X,PairSign*P.Y,H)+FVector(PadHalf,PadHalf,0)));
        const int32 Kind=int32(HashCombineFast(uint32(Choice),uint32(BandIndex*2+(Sign>0 ? 1:0))+0x434f5645u)%5);
        const float Yaw=Geometry.RandRange(0,1)!=0 ? 90.f:0.f;
        const FTransform Group(FRotator(0,Yaw,0),FVector(P,H));
        const float Height=Geometry.RandRange(0,1)!=0 ? 150.f:180.f;
        const EERBlockMaterial Material=Kind==4 ? EERBlockMaterial::Wood:Geometry.RandRange(0,1)!=0 ? EERBlockMaterial::Brick:EERBlockMaterial::Concrete;
        auto Member=[&](FVector Center,FVector Size)
        {auto& Piece=Box(Group.TransformPosition(Center),Size,Material);Piece.RotationYaw=Yaw;};
        const float Width=Geometry.RandRange(0,3)*60.f+240;
        FVector Bounds(Width,40,Height);
        switch(Kind)
        {
        case 0:Member({0,0,Height*.5f},Bounds);break;
        case 1:
        case 2:
            Member({0,0,Height*.5f},Bounds);
            Member({-Width*.5f+20,-100,Height*.5f},{40,160,Height});
            if(Kind==2)Member({Width*.5f-20,-100,Height*.5f},{40,160,Height});
            Bounds.Y=200;break;
        case 3:
            Member({0,0,Height*.5f},{200,100,Height});
            Member({170,-110,Height*.5f},{120,100,Height});Bounds={460,210,Height};break;
        default:
            Member({0,30,Height*.5f},{240,180,Height});Bounds={240,180,Height};break;
        }
        static const FName Names[]{TEXT("Cover.Straight"),TEXT("Cover.Corner"),TEXT("Cover.Pocket"),TEXT("Cover.Staggered"),TEXT("Cover.Block")};
        L.FeatureNames.AddUnique(Names[Kind]);
        const float HalfHeight=Height<160 ? 62.5f:92.5f,Eye=Height<160 ? 115.f:172.5f;
        FERLayoutCover Cover;Cover.Frame=Group;Cover.Size=Bounds;Cover.Material=Material;Cover.HalfHeight=HalfHeight;
        Cover.Hide=Group.TransformPosition({0,-220,HalfHeight+2.5f});Cover.Entry=Group.TransformPosition({0,-280,HalfHeight+2.5f});
        Cover.SightFrom=Group.TransformPosition({0,200,172.5f});Cover.SightTo=Group.TransformPosition({0,-220,Eye});
        const bool FiringSteps=Kind<=2 && (HashCombineFast(uint32(Choice),uint32(BandIndex*2+(Sign>0 ? 1:0))+0x53544550u)&1)!=0;
        if(FiringSteps)
        {
            Member({0,-155,10},{100,30,20});
            Member({0,-125,20},{100,30,40});
            Member({0,-65,30},{100,90,60});
            TArray<FVector> Flight{Group.TransformPosition({0,-280,100}),Group.TransformPosition({0,-155,120}),Group.TransformPosition({0,-125,140}),Group.TransformPosition({0,-65,160})};
            Cover.FiringStair=L.StairRoutes.Num();Cover.FiringPosition=Flight.Last();
            L.StairRoutes.Add(Flight);for(auto& Point:Flight)Point=Mate(Point);L.StairRoutes.Add(Flight);
            L.FeatureNames.AddUnique(TEXT("Cover.FiringSteps"));
        }
        L.CoverSites.Add(Cover);Cover.Frame=FTransform(FRotator(0,180-Yaw,0),FVector(P.X,-P.Y,H));
        if(FiringSteps){++Cover.FiringStair;Cover.FiringPosition=Mate(Cover.FiringPosition);}
        for(auto* Point:{&Cover.Hide,&Cover.Entry,&Cover.SightFrom,&Cover.SightTo})Point->Y=-Point->Y;L.CoverSites.Add(Cover);
    }
    // Distinct separators span the full diameter, except for the solid outer
    // walls already occupying the building seam. Hard walls are never mantleable;
    // their crossing is through the first upper landing inside the building.
    if(L.CrossingRule!=EERCrossingRule::OpenYard)
    {
        TArray<float> Cuts{-Radius+30,BuildingX+MinI,BuildingX+MinI+20,BuildingX+MaxI-20,BuildingX+MaxI,Radius-30};Cuts.Sort();
        for(int32 I=1;I<Cuts.Num();++I)
        {
            const float A=Cuts[I-1],B=Cuts[I];if(B-A<1)continue;
            const float Mid=(A+B)*.5f;
            if((Mid>BuildingX+MinI && Mid<BuildingX+MinI+20) || (Mid>BuildingX+MaxI-20 && Mid<BuildingX+MaxI))continue;
            const int32 Count=FMath::Max(1,FMath::CeilToInt32((B-A)/200));
            for(int32 J=0;J<Count;++J)
            {
                const float Lo=FMath::Lerp(A,B,float(J)/Count),Hi=FMath::Lerp(A,B,float(J+1)/Count),CX=(Lo+Hi)*.5f;
                const float H=FMath::Max3(CentralGroundHeight(L,{Lo,0}),CentralGroundHeight(L,{Hi,0}),CentralGroundHeight(L,{CX,0}));
                const bool Hard=L.CrossingRule==EERCrossingRule::FencedCrossings;const float Top=H+(Hard ? 300:120);
                Box({CX,0,Top*.5f},{Hi-Lo,40,Top},Hard ? EERBlockMaterial::Concrete:EERBlockMaterial::Brick,Hard,false);
            }
        }
    }
    // One permanent perimeter follows the terrain and ends at its visible top.
    constexpr int32 BoundarySides=128;const float BoundaryRadius=Radius-20;
    for(int32 I=0;I<BoundarySides/2;++I)
    {
        const double A=-PI+(I+.5)*2*PI/BoundarySides;const float Length=2*BoundaryRadius*FMath::Tan(PI/BoundarySides)-.5f;
        const FVector2D P(FMath::Cos(A)*BoundaryRadius,FMath::Sin(A)*BoundaryRadius);const float H=CentralGroundHeight(L,P);
        auto& Plinth=Box(FVector(P,H+50),{Length,12,100},EERBlockMaterial::Concrete,true);Plinth.RotationYaw=FMath::RadiansToDegrees(A)+90;
        auto& Fence=Box(FVector(P,H+100+ERIndustrialFence::ModuleHeightCm*.5f),{Length,12,ERIndustrialFence::ModuleHeightCm},EERBlockMaterial::Metal,true);Fence.RotationYaw=FMath::RadiansToDegrees(A)+90;Fence.PawnBarrier=true;
    }
    const float ScreenY=FMath::Min(-Radius+700,-D*.5f-180),ScreenHalf=500,ScreenGround=CentralGroundHeight(L,{0,ScreenY});
    Box({0,ScreenY,ScreenGround+150},{1000,20,300},MainMaterial);L.FeatureNames.Add(TEXT("ScreenedSpawns"));
    const float SpawnY=-Radius+250;
    for(int32 I=0;I<10;++I)
    {
        const FVector2D P((I%5-2)*150,SpawnY+(I/5)*160);L.AlphaSpawns.Emplace(FRotator(0,90,0),FVector(P,CentralGroundHeight(L,P)+100));
    }
    // Approach the west-facing side door around the front corner, rather than
    // tracing a straight north/south line through opposed facade openings.
    const float DoorOutside=BuildingX+OuterMin-120;
    const TArray<FVector2D> Approach{{0,ScreenY-100},{-ScreenHalf-150,ScreenY-100},{-ScreenHalf-150,ScreenY+100},
        {DoorOutside,-D*.5f-180},{DoorOutside,DoorY},{BuildingX+OuterMin+160,DoorY},{BuildingX+HallX,DoorY}};
    TArray<FVector> North;
    for(int32 I=1;I<Approach.Num();++I)Append(North,GroundPath(Approach[I-1],Approach[I]));
    const float JoinY=L.CrossingRule==EERCrossingRule::FencedCrossings ? StairStart-100:-300;
    Append(North,GroundPath(Approach.Last(),{BuildingX+HallX,JoinY}));
    if(L.CrossingRule==EERCrossingRule::FencedCrossings)
    {
        Append(North,GroundPath({BuildingX+HallX,JoinY},{BuildingX+StairX,JoinY}));
        L.CrossingRoutes.Add(North);auto South=North;for(auto& P:South)P=Mate(P);L.CrossingRoutes.Add(South);
        TArray<FVector> Upper{NorthStair.Last(),Mate(NorthStair.Last())};L.RooftopRoutes.Add(Upper);
        Patrol=North;
    }
    else if(L.CrossingRule==EERCrossingRule::ClimbableWall)
    {
        TArray<FVector> Over{{BuildingX+HallX,-300,Base+100}};
        for(int32 I=0;I<6;++I)
        {
            const float YStep=-200+(I+.5f)*30,Top=Base+(I+1)*20;
            Box({BuildingX+HallX,YStep,Top*.5f},{180,30,Top},EERBlockMaterial::Concrete,true,true,true);
            Over.Add({BuildingX+HallX,YStep,Top+120});
        }
        Over.Add({BuildingX+HallX,-10,Base+240});Over.Add({BuildingX+HallX,10,Base+240});
        const auto Up=Over;for(int32 I=Up.Num()-3;I>=0;--I)Over.Add(Mate(Up[I]));
        L.StairRoutes.Add(Over);L.ClimbPoints={{BuildingX+HallX,-200,Base+100},{BuildingX+HallX,200,Base+100}};
        L.CrossingRoutes.Add(North);auto South=North;for(auto& P:South)P=Mate(P);L.CrossingRoutes.Add(South);
        Patrol=North;
    }
    else
    {
        TArray<FVector> Through=North;Append(Through,GroundPath({BuildingX+HallX,-300},{BuildingX+HallX,300}));
        auto South=North;for(auto& P:South)P=Mate(P);Algo::Reverse(South);Append(Through,South);L.CrossingRoutes.Add(Through);Patrol=Through;
    }
    // Each flank approach joins the screened base; separated variants stop
    // before the wall instead of publishing an obstructed ground route.
    TArray<FVector> Flank;
    Append(Flank,GroundPath({-ScreenHalf-150,ScreenY+100},{FlankX,-D*.5f-180}));
    const float FlankEnd=L.CrossingRule==EERCrossingRule::OpenYard ? Radius*.40f:-250;
    Append(Flank,GroundPath({FlankX,-D*.5f-180},{FlankX,FlankEnd}));L.CrossingRoutes.Add(Flank);
    if(L.CrossingRule!=EERCrossingRule::OpenYard){for(auto& P:Flank)P=Mate(P);L.CrossingRoutes.Add(Flank);}
    for(int32 I=Patrol.Num()-2;I>0;--I){const FVector Point=Patrol[I];Patrol.Add(Point);}
    L.FeatureNames.Add(L.CentralHill ? TEXT("CentralHill"):TEXT("OpposedHill"));return true;
}
}
