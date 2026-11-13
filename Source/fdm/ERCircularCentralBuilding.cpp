#include "ERProceduralMap.h"
#include "ERIndustrialFence.h"
#include "Algo/Reverse.h"

namespace ERProcedural
{
bool BuildCircularCentralBuilding(const UERMapTemplate& T,int32 Seed,TArray<FERMapTemplatePiece>& Pieces,FERProceduralLayout& L,TArray<FVector>& Patrol,int32 CrossingOverride)
{
    const auto& V=T.CentralBuilding;
    if(V.MinFloors<1 || V.MaxFloors>4 || V.MinFloors>V.MaxFloors || T.RoofMesh || T.ShellWallMesh || CrossingOverride<0 || CrossingOverride>3)return false;
    for(const auto* Palette:{&V.ConcreteSurfaces,&V.BrickSurfaces,&V.WoodSurfaces,&V.GroundSurfaces,&V.ContainerSurfaces})
        if(Palette->Num()>8 || Palette->Contains(nullptr))return false;
    FRandomStream Geometry(Seed);
    const int32 Choice=T.SizingTeamSize>0 ? T.CompositionSeed:Seed;
    auto Pick=[&](float A,float B){return Geometry.RandRange(FMath::CeilToInt32(A/100),FMath::FloorToInt32(B/100))*100.f;};
    L.CircularArena=true;L.GradedTerrain=true;L.LayoutSymmetry=EERTemplateSymmetry::Mirror;
    const float Diameter=T.SizingTeamSize>0 ? T.SizingArena.X:7600;
    if(!FMath::IsFinite(Diameter) || Diameter<3500 || Diameter>18000)return false;
    L.CrossingRule=CrossingOverride ? EERCrossingRule(CrossingOverride):EERCrossingRule(1+HashCombineFast(uint32(Choice),0x43524f53u)%3);
    const float Radius=Diameter*.5f,Stride=320;
    const bool Oval=L.CrossingRule==EERCrossingRule::ClimbableWall;
    const float RadiusX=Radius*(Oval ? .9f:1.f),RadiusY=Radius*(Oval ? (1.f/.9f):1.f);
    L.ArenaSize={2*RadiusX,2*RadiusY,2000};
    L.CentralShape=HashCombineFast(uint32(Choice),0x53484150u)%4;
    L.CentralHill=(HashCombineFast(uint32(Choice),0x48494c4cu)&1)!=0;
    const bool Waterfront=!L.CentralHill && L.CrossingRule==EERCrossingRule::OpenYard &&
        (HashCombineFast(uint32(Choice),0x57415452u)&2u)!=0;
    const bool WaterfrontCover=Waterfront && T.SizingTeamSize!=1;
    const float W=Pick(V.BuildingWidth.X,V.BuildingWidth.Y),D=Pick(V.BuildingDepth.X,V.BuildingDepth.Y);
    const float Neck=FMath::GridSnap(W*.60f,100.f),Band=FMath::GridSnap(D*.24f,100.f);
    const float InnerMin=-W*.5f,InnerMax=L.CentralShape==1 ? -W*.5f+Neck:W*.5f;
    const float OuterMin=L.CentralShape==2 ? -Neck*.5f:L.CentralShape==3 ? -W*.5f+400:-W*.5f;
    const float OuterMax=L.CentralShape==2 ? Neck*.5f:W*.5f;
    // H profile: retain a centered neck, rather than moving one team's wing.
    const float MinI=L.CentralShape==1 ? -Neck*.5f:InnerMin,MaxI=L.CentralShape==1 ? Neck*.5f:InnerMax;
    const float CommonMin=FMath::Max(MinI,OuterMin),CommonMax=FMath::Min(MaxI,OuterMax),HallX=(CommonMin+CommonMax)*.5f;
    const float SideY=(D*.5f+260)/(RadiusY-140);
    const float SideRoom=FMath::Max(0.f,(RadiusX-140)*FMath::Sqrt(FMath::Max(0.f,1-FMath::Square(SideY)))-W*.5f-260);
    const float WaterfrontOuterAtEnd=-RadiusX*FMath::Sqrt(1.f-FMath::Square(.5f))+60.f;
    const float BuildingX=L.CentralHill ? 0:Waterfront ?
        FMath::Max3(-RadiusX*.5f,-SideRoom,WaterfrontOuterAtEnd+900.f+W*.5f):
        -FMath::Min3(RadiusX*.32f,1500.f,SideRoom);
    L.BuildingFloors=Geometry.RandRange(V.MinFloors,V.MaxFloors);
    L.CentralBuildingSize={W,D,L.BuildingFloors*Stride};
    L.HillHeight=FMath::Min(320.f,Radius*.065f);L.HillFalloff=FMath::Max(650.f,Radius*.38f);L.HillPlateau=Radius*.10f;
    const float HillX=FMath::Min(RadiusX-250,FMath::Max(RadiusX*.48f,BuildingX+W*.5f+180+L.HillPlateau+200));
    L.HillCenter={L.CentralHill ? 0:HillX,0,0};
    constexpr float FloorThickness=5;
    const float Grade=L.CentralHill ? L.HillHeight:0;
    const float Base=Grade+FloorThickness;
    const bool RoofPool=L.CrossingRule==EERCrossingRule::OpenYard && L.CentralHill;
    const float RoofPoolHalfX=L.BuildingFloors==2 ? 120.f:150.f;
    const float RoofPoolHalfY=L.BuildingFloors==2 ? 180.f:250.f;
    const float RoofPoolLocalX=HallX+130.f;
    const FBox2D RoofPoolHole({RoofPoolLocalX-RoofPoolHalfX-20,-RoofPoolHalfY-20},
                              {RoofPoolLocalX+RoofPoolHalfX+20,RoofPoolHalfY+20});
    L.CentralBuildingCenter={BuildingX,0,Base};
    if(Waterfront)L.FeatureNames.Add(TEXT("VillaWaterfront"));
    L.TerrainPads.Add(FBox(FVector(BuildingX-W*.5f-250,-D*.5f-150,Grade),
                           FVector(BuildingX+W*.5f+150,D*.5f+150,Grade)));
    auto Surface=[&](const TArray<TObjectPtr<UMaterialInterface>>& Palette,uint32 Salt)->UMaterialInterface*
    {return Palette.IsEmpty() ? nullptr:Palette[HashCombineFast(uint32(Seed),Salt)%Palette.Num()].Get();};
    L.GroundSurface=Surface(V.GroundSurfaces,0x47524f55u);
    constexpr auto MainMaterial=EERBlockMaterial::Concrete;
    const auto ScreenMaterial=Geometry.RandRange(0,1)!=0 ? EERBlockMaterial::Brick : EERBlockMaterial::Concrete;
    auto Box=[&](FVector Center,FVector Size,EERBlockMaterial Material,bool Fixed=false,bool Pair=true,bool Deck=false)->FERMapTemplatePiece&
    {
        auto& P=Pieces.AddDefaulted_GetRef();P.Center=Center;P.Size=Size;P.Material=Material;P.Destructible=!Fixed;P.Mirrored=Pair;P.WalkableSurface=Deck;
        auto* Finish=Material==EERBlockMaterial::Metal ? (Deck && V.DeckSurface ? V.DeckSurface.Get():V.SteelSurface.Get()):
            Material==EERBlockMaterial::Brick ? Surface(V.BrickSurfaces,0x42524943u):Material==EERBlockMaterial::Wood ? Surface(V.WoodSurfaces,0x574f4f44u):Surface(V.ConcreteSurfaces,0x434f4e43u);
        if(Fixed && Material!=EERBlockMaterial::Glass && Material!=EERBlockMaterial::Drywall)P.FixedSurface=Finish;
        else if(!Fixed && Material!=EERBlockMaterial::Glass && Material!=EERBlockMaterial::Drywall)P.CellSurface=Finish;
        return P;
    };
    auto BuildingBox=[&](FVector P,FVector Size,EERBlockMaterial Material,bool Fixed=false,bool Pair=true,bool Deck=false)->FERMapTemplatePiece&
    {
        auto& Piece=Box(P+L.CentralBuildingCenter,Size,Material,Fixed,Pair,Deck);
        const float Bottom=P.Z-Size.Z*.5f;Piece.Anchored=Bottom>=0 && FMath::IsNearlyZero(FMath::Fmod(Bottom,Stride),.01f);return Piece;
    };
    auto Mate=[](FVector P){return FVector(P.X,-P.Y,P.Z);};
    // Villa-local rectangle on one storey, furnished with its mirrored mate.
    auto Interior=[&](FVector2D Min,FVector2D Max,float FloorZ,EERInteriorKind Kind,uint8 Walls,TArray<FVector> Doors={})
    {
        if(Max.X-Min.X<260 || Max.Y-Min.Y<260)return;
        FERLayoutInterior Space;Space.Frame=FTransform(L.CentralBuildingCenter+FVector((Min+Max)*.5,FloorZ));
        Space.Half=(Max-Min)*.5;Space.Kind=Kind;Space.Walls=Walls;Space.Height=300;
        for(const auto& Door:Doors)Space.Doors.Add(L.CentralBuildingCenter+Door);
        AddInteriorPair(L,MoveTemp(Space),true);
    };
    auto GroundPath=[&](FVector2D A,FVector2D B)
    {
        TArray<FVector> Path;const int32 Steps=FMath::Max(1,FMath::CeilToInt32(FVector2D::Distance(A,B)/60));
        for(int32 I=0;I<=Steps;++I){const auto P=FMath::Lerp(A,B,float(I)/Steps);Path.Add(FVector(P,CentralGroundHeight(L,P)+(Waterfront ? 120.f:100.f)));}return Path;
    };
    auto AddPairedStair=[&](TArray<FVector> Path)
    {
        L.StairRoutes.Add(Path);
        for(auto& Point:Path)Point=Mate(Point);
        L.StairRoutes.Add(MoveTemp(Path));
    };
    auto Append=[](TArray<FVector>& Path,const TArray<FVector>& Leg)
    {for(const auto& Point:Leg)if(Path.IsEmpty() || !Path.Last().Equals(Point,.01))Path.Add(Point);};
    const float DoorY=-D*.5f+180,StairX=HallX-200,StairStart=-200.f,StairEnd=200.f;
    // Alpha spawns at negative Y. Alternate which team approaches the low end
    // without consuming the geometry stream or changing the shared stairwell.
    const float StairDirection=(HashCombineFast(uint32(Seed),0x53544149u)&1u)!=0 ? 1.f:-1.f;
    const float StairBottomY=StairDirection>0 ? StairStart:StairEnd;
    const float StairTopY=-StairBottomY;
    constexpr float RoofRise=100;
    if(RoofPool)L.CentralBuildingSize.Z+=RoofRise;
    const float RoofStairExtension=RoofPool ? 125.f:0.f;
    const float WaterfrontShoreX=BuildingX+FMath::Min(OuterMin+240.f,HallX-150.f);
    const FBox2D Hole({StairX-90,StairStart-20},{StairX+90,StairEnd+20});
    FBox2D RoofHole=Hole;
    RoofHole.Max.Y+=RoofStairExtension;
    RoofHole.Min.Y-=RoofStairExtension;
    constexpr int32 WallStairSteps=17;
    // Above the maximum climb plus the default jump apex from level ground.
    constexpr float WallHeight=WallStairSteps*20.f;
    constexpr float WallStairStart=-20.f-WallStairSteps*30.f;
    const TArray<FBox2D> Footprints{FBox2D({OuterMin,-D*.5f},{OuterMax,-Band}),FBox2D({MinI,-Band},{MaxI,0})};
    for(const auto& Rect:Footprints)
    {
        const FVector2D Center=Rect.GetCenter(),Size=Rect.GetSize();
        BuildingBox({Center.X,Center.Y,-FloorThickness*.5f},{Size.X,Size.Y,FloorThickness},
            EERBlockMaterial::Concrete,true,true,true);
    }
    for(const auto& Rect:Footprints)for(int32 Copy=0;Copy<2;++Copy)
    {
        const FBox Bounds(FVector(Rect.Min,0)+L.CentralBuildingCenter,FVector(Rect.Max,L.CentralBuildingSize.Z)+L.CentralBuildingCenter);
        L.CentralFootprints.Add(Copy ? FBox(Mate(Bounds.Max).ComponentMin(Mate(Bounds.Min)),Mate(Bounds.Max).ComponentMax(Mate(Bounds.Min))):Bounds);
    }
    // Exact rectangular union. Emit boundary walls inside occupied cells;
    // the Y=0 seam stays open until the selected separator is added.
    TArray<float> X{MinI,MaxI};X.AddUnique(OuterMin);X.AddUnique(OuterMax);X.Sort();
    const TArray<float> Y{-D*.5f,-Band,0};const int32 NX=X.Num()-1;
    TArray<uint8> Occupied;Occupied.Init(0,NX*2);
    for(int32 J=0;J<2;++J)for(int32 I=0;I<NX;++I)
        Occupied[J*NX+I]=(X[I]+X[I+1])*.5f>Footprints[J].Min.X-.01 && (X[I]+X[I+1])*.5f<Footprints[J].Max.X+.01 ? 1:0;
    // The upper facade sits behind the edge of the ground-floor slab.
    constexpr float TerraceDepth=240;
    int32 TerraceBay=INDEX_NONE;
    for(int32 I=0;I<NX;++I)
        if(Occupied[I] && (TerraceBay==INDEX_NONE || X[I+1]-X[I]>X[TerraceBay+1]-X[TerraceBay]))TerraceBay=I;
    auto Wall=[&](bool AlongX,float At,float Lo,float Hi,float FloorZ,bool Door,bool Front=false)
    {
        if(Hi-Lo<1)return;
        auto Part=[&](float A,float B,float Bottom,float Top,EERBlockMaterial Material=EERBlockMaterial::Concrete)
        {
            if(B-A<1 || Top-Bottom<1)return;
            BuildingBox(AlongX ? FVector((A+B)*.5f,At,FloorZ+(Bottom+Top)*.5f):FVector(At,(A+B)*.5f,FloorZ+(Bottom+Top)*.5f),AlongX ? FVector(B-A,20,Top-Bottom):FVector(20,B-A,Top-Bottom),Material);
        };
        const bool Window=!Door && Hi-Lo>=(Front && Waterfront ? 320.f:450.f);
        if(Door || Window)
        {
            const float Center=Door && !AlongX ? DoorY:(Lo+Hi)*.5f;
            const float Half=Door ? 120 : Front ? FMath::Min(300.f,(Hi-Lo)*.37f) : FMath::Min(160.f,(Hi-Lo)*.32f);
            const float Sill=Door ? 0 : Front ? 30 : 85;
            const float Head=Door ? 240 : Front ? 270 : 245;
            Part(Lo,Center-Half,0,300);
            Part(Center+Half,Hi,0,300);
            Part(Center-Half,Center+Half,0,Sill);
            Part(Center-Half,Center+Half,Head,300);
            if(Window)
            {
                const int32 Panes=Front ? FMath::CeilToInt32(2*Half/200) : 1;
                const float Left=Center-Half,Pitch=2*Half/Panes;
                for(int32 Pane=0;Pane<Panes;++Pane)
                {
                    const float A=Left+Pane*Pitch,B=Left+(Pane+1)*Pitch;
                    Part(A+(Pane>0 ? 5 : 0),B-(Pane+1<Panes ? 5 : 0),Sill,Head,EERBlockMaterial::Glass);
                    if(Pane+1<Panes)Part(B-5,B+5,Sill,Head);
                }
            }
        }
        else Part(Lo,Hi,0,300);
        if(L.BuildingFloors==1)
            BuildingBox(AlongX ? FVector((Lo+Hi)*.5f,At,L.BuildingFloors*Stride+50):FVector(At,(Lo+Hi)*.5f,L.BuildingFloors*Stride+50),AlongX ? FVector(Hi-Lo,20,100):FVector(20,Hi-Lo,100),MainMaterial);
    };
    for(int32 Floor=0;Floor<L.BuildingFloors;++Floor)
    {
        const float Z=Floor*Stride;
        const bool PoolRoof=RoofPool && Floor==L.BuildingFloors-1;
        const bool StairAbove=Floor+1<L.BuildingFloors || PoolRoof;
        const float Rise=Stride+(PoolRoof ? RoofRise:0);
        const float StairFinish=StairTopY+(PoolRoof ? StairDirection*RoofStairExtension:0);
        const FBox2D& DeckHole=PoolRoof ? RoofHole:Hole;
        if(Waterfront && Floor==0)
        {
            Wall(true,Y[0]+10,OuterMin,HallX-120,Z,false,true);
            Wall(true,Y[0]+10,HallX-120,HallX+120,Z,true,true);
            Wall(true,Y[0]+10,HallX+120,OuterMax,Z,false,true);
        }
        const float FrontY=Y[0]+(Floor>0 ? TerraceDepth : 0.f);
        for(int32 J=0;J<2;++J)for(int32 I=0;I<NX;++I)if(Occupied[J*NX+I])
        {
            const bool Bottom=J==0 || !Occupied[(J-1)*NX+I],Top=J==0 && !Occupied[(J+1)*NX+I];
            if(Bottom)
            {
                const bool TerraceDoor=Floor==1 && J==0 && I==TerraceBay;
                const float Edge=J==0 ? FrontY : Y[J];
                if(!Waterfront || Floor!=0 || J!=0)
                    Wall(true,Edge+10,X[I],X[I+1],Z,TerraceDoor,J==0);
                if(TerraceDoor)
                {
                    const float DoorX=BuildingX+(X[I]+X[I+1])*.5f;
                    TArray<FVector> Terrace{{DoorX,Edge-130,Base+Z+100},{DoorX,Edge+65,Base+Z+100}};
                    L.RoomAccessRoutes.Add(Terrace);for(auto& Point:Terrace)Point=Mate(Point);L.RoomAccessRoutes.Add(Terrace);
                }
            }
            if(Top)Wall(true,Y[J+1]-10,X[I],X[I+1],Z,false);
            const float Lo=(J==0 ? FrontY : Y[J])+(Bottom ? 20:0),Hi=Y[J+1]-(Top ? 20:0);
            if(I==0 || !Occupied[J*NX+I-1])Wall(false,X[I]+10,Lo,Hi,Z,Floor==0 && J==0 && FMath::IsNearlyEqual(X[I],OuterMin));
            if(I==NX-1 || !Occupied[J*NX+I+1])Wall(false,X[I+1]-10,Lo,Hi,Z,false);
        }
        // A side room beside the circulation spine, with a full-width doorway.
        if(OuterMax-HallX>=300)
        {
            const float At=HallX+(OuterMax-HallX)*.55f;
            const float Lo=FrontY+40,Hi=-Band-20;
            if(OuterMax-HallX>=420 && Hi-Lo>350 &&
                (Floor!=1 || FMath::Abs(At-(X[TerraceBay]+X[TerraceBay+1])*.5f)>45))
            {
                const float Gap=(Lo+Hi)*.5f;
                BuildingBox({At,(Lo+Gap-110)*.5f,Z+150},{10,Gap-110-Lo,300},EERBlockMaterial::Drywall);
                BuildingBox({At,(Gap+110+Hi)*.5f,Z+150},{10,Hi-Gap-110,300},EERBlockMaterial::Drywall);
                BuildingBox({At,Gap,Z+270},{10,220,60},EERBlockMaterial::Drywall);
                TArray<FVector> Room{{BuildingX+At-120,Gap,Base+Z+100},{BuildingX+At+120,Gap,Base+Z+100}};L.RoomAccessRoutes.Add(Room);for(auto& P:Room)P=Mate(P);L.RoomAccessRoutes.Add(Room);
                Interior({At+8,Lo},{OuterMax-24,Hi+12},Z,EERInteriorKind::Home,1|2|8,{{At,Gap,Z}});
            }
            if(Hi-Lo>250)
                BuildingBox({OuterMax-90,Lo+95,Z+45},{100,70,90},EERBlockMaterial::Wood);
        }
        // A second room on every level has an open doorway and two low
        // furnishings. The central passage and stair approach stay clear.
        const float RoomAt=HallX+FMath::Min(230.f,(MaxI-HallX)*.55f);
        const float RoomLo=-Band+20,RoomHi=-20,RoomDoor=-Band*.5f;
        if(RoomAt+125<MaxI && RoomDoor-110>RoomLo && RoomDoor+110<RoomHi)
        {
            BuildingBox({RoomAt,(RoomLo+RoomDoor-110)*.5f,Z+150},
                {10,RoomDoor-110-RoomLo,300},EERBlockMaterial::Drywall);
            BuildingBox({RoomAt,(RoomDoor+110+RoomHi)*.5f,Z+150},
                {10,RoomHi-RoomDoor-110,300},EERBlockMaterial::Drywall);
            BuildingBox({RoomAt,RoomDoor,Z+270},{10,220,60},EERBlockMaterial::Drywall);
            BuildingBox({MaxI-85,RoomLo+85,Z+45},{110,70,90},EERBlockMaterial::Wood);
            const float TableX=MaxI-90,TableY=-85;
            BuildingBox({TableX,TableY,Z+74},{120,70,2},EERBlockMaterial::Wood,false,true,true);
            for(float LegX:{-50.f,50.f})for(float LegY:{-25.f,25.f})
                BuildingBox({TableX+LegX,TableY+LegY,Z+36.5f},
                    {8,8,73},EERBlockMaterial::Wood);
            TArray<FVector> Room{{BuildingX+RoomAt-90,RoomDoor,Base+Z+100},
                                 {BuildingX+RoomAt+90,RoomDoor,Base+Z+100}};
            L.RoomAccessRoutes.Add(Room);for(auto& P:Room)P=Mate(P);L.RoomAccessRoutes.Add(Room);
            Interior({RoomAt+8,RoomLo},{MaxI-24,RoomHi-20},Z,EERInteriorKind::Home,2|8,{{RoomAt,RoomDoor,Z}});
            L.FeatureNames.AddUnique(TEXT("VillaInteriorRooms"));
        }
        // The entrance side of each storey is a lounge around the hall and the stairwell.
        Interior({OuterMin+24,FrontY+24},{HallX-110,-Band-4},Z,EERInteriorKind::Home,1|8,{{OuterMin,DoorY,Z}});
        Interior({MinI+24,-Band+4},{StairX-110,-40},Z,EERInteriorKind::Home,8);
        // Deck rectangles partition the union and subtract the actual stairwell.
        for(const auto& Rect:Footprints)
        {
            const double DeckFront=FMath::Max(Rect.Min.Y,double(FrontY));
            TArray<double> CX{Rect.Min.X,Rect.Max.X},CY{DeckFront,Rect.Max.Y};
            double LampBayArea=0;
            FVector2D LampBayCenter=FVector2D::ZeroVector;
            if(StairAbove)
            {
                for(double Cut:{DeckHole.Min.X,DeckHole.Max.X})if(Cut>Rect.Min.X && Cut<Rect.Max.X)CX.AddUnique(Cut);
                for(double Cut:{DeckHole.Min.Y,DeckHole.Max.Y})if(Cut>DeckFront && Cut<Rect.Max.Y)CY.AddUnique(Cut);
            }
            if(RoofPool && Floor==L.BuildingFloors-1)
            {
                for(double Cut:{RoofPoolHole.Min.X,RoofPoolHole.Max.X})if(Cut>Rect.Min.X && Cut<Rect.Max.X)CX.AddUnique(Cut);
                for(double Cut:{RoofPoolHole.Min.Y,RoofPoolHole.Max.Y})if(Cut>DeckFront && Cut<Rect.Max.Y)CY.AddUnique(Cut);
            }
            CX.Sort();CY.Sort();
            for(int32 IX=1;IX<CX.Num();++IX)for(int32 IY=1;IY<CY.Num();++IY)
            {
                const FVector2D Mid((CX[IX-1]+CX[IX])*.5,(CY[IY-1]+CY[IY])*.5);
                if((StairAbove && DeckHole.IsInside(Mid)) ||
                   (RoofPool && Floor==L.BuildingFloors-1 && RoofPoolHole.IsInside(Mid)))continue;
                const FVector2D Span(CX[IX]-CX[IX-1],CY[IY]-CY[IY-1]);
                BuildingBox({Mid.X,Mid.Y,Z+300+(Rise-300)*.5f},{CX[IX]-CX[IX-1],CY[IY]-CY[IY-1],Rise-300},EERBlockMaterial::Metal,true,true,true);
                // Mount within the largest uninterrupted ceiling bay, clear of the stairs and pool.
                if(Span.X>=120. && Span.Y>=120. && Span.X*Span.Y>LampBayArea)
                {LampBayArea=Span.X*Span.Y;LampBayCenter=Mid;}
            }
            if(LampBayArea>0)
            {
                const FVector Fixture=L.CentralBuildingCenter+FVector(LampBayCenter,Z+297);
                L.CentralCeilingMounts.Add(Fixture);
                L.CentralCeilingMounts.Add(Mate(Fixture));
            }
        }
        if(StairAbove)
        {
            TArray<FVector> Stair{{BuildingX+StairX,StairBottomY-StairDirection*60,Base+Z+100}};
            for(int32 Step=0;Step<FMath::RoundToInt32(Rise/20.f);++Step)
            {
                const float At=StairBottomY+StairDirection*(Step+.5f)*25,Top=(Step+1)*20;
                auto& Tread=BuildingBox({StairX,At,Z+Top-10},{150,25,20},EERBlockMaterial::Metal,true,false,true);
                Tread.bAsymmetricCentralStair=true;
                // Narrow glass panes climb with the treads, keeping their tops
                // 100cm above each step. The upper-deck guard remains separate.
                for(float SideX:{-1.f,1.f})
                {
                    auto& Guard=BuildingBox({StairX+SideX*82,At,Z+Top+50},
                        {10,25,100},EERBlockMaterial::Glass,false,false);
                    Guard.bAsymmetricCentralStair=true;
                    Guard.Anchored=true;
                }
                Stair.Add({BuildingX+StairX,At,Base+Z+Top+120});
            }
            Stair.Add({BuildingX+StairX,StairFinish+StairDirection*100,Base+Z+Rise+100});
            L.StairRoutes.Add(MoveTemp(Stair));
            L.FeatureNames.AddUnique(TEXT("VillaSingleStair"));
            L.FeatureNames.AddUnique(TEXT("VillaSlopedGlassGuard"));
            // A slim continuous cap makes the stepped glass read as one
            // sloping balustrade; the glass panes supply the collision.
            const float Run=FMath::Abs(StairFinish-StairBottomY);
            const float RailLength=FMath::Sqrt(FMath::Square(Run)+FMath::Square(Rise));
            const FQuat RailTilt(FVector::ForwardVector,FMath::Atan2(StairDirection*Rise,Run));
            for(float SideX:{-1.f,1.f})
            {
                FERLayoutFixedPiece Rail;
                Rail.Transform=FTransform(RailTilt,L.CentralBuildingCenter+FVector(StairX+SideX*82,(StairBottomY+StairFinish)*.5f,Z+110+Rise*.5f),
                    FVector(6,RailLength,6)/100);
                Rail.Material=EERBlockMaterial::Metal;
                Rail.Surface=V.SteelSurface.Get();
                Rail.VisualOnly=true;
                L.FixedPieces.Add(Rail);
            }
            // One central glazed stairwell, with one ramp per storey.
            for(float SideX:{-1.f,1.f})
                BuildingBox({StairX+SideX*100,0,Z+Rise+50},
                    {20,float(DeckHole.GetSize().Y),100},EERBlockMaterial::Glass,false,false).Anchored=true;
        }
        if(Floor==0 && !Waterfront)
        {
            TArray<FVector> Entry{{BuildingX+OuterMin-100,DoorY,Base+100},{BuildingX+OuterMin+160,DoorY,Base+100}};
            L.RoomAccessRoutes.Add(Entry);for(auto& Point:Entry)Point=Mate(Point);L.RoomAccessRoutes.Add(Entry);
        }
    }
    FBox2D ExteriorAccess(ForceInit);
    if(L.BuildingFloors>1)
    {
        const float AccessX=OuterMax-115;
        const float ScreenY=FMath::Min(-RadiusY+(WaterfrontCover ? 1100.f:700.f),Y[0]-180);
        const bool FrontStairs=Waterfront && ScreenY+240<Y[0]-450;
        const bool SideStairs=L.CentralHill;
        const TArray<FVector2D> FrontSpans=FrontStairs ?
            TArray<FVector2D>{{OuterMin,AccessX-90},{AccessX+90,OuterMax}}:
            TArray<FVector2D>{{OuterMin,OuterMax}};
        for(const auto& Span:FrontSpans)
            BuildingBox({(Span.X+Span.Y)*.5f,Y[0]+10,Stride+50},{Span.Y-Span.X,20,100},MainMaterial);
        BuildingBox({OuterMin+10,Y[0]+(TerraceDepth+20)*.5f,Stride+50},{20,TerraceDepth-20,100},MainMaterial);
        if(!SideStairs)
            BuildingBox({OuterMax-10,Y[0]+(TerraceDepth+20)*.5f,Stride+50},{20,TerraceDepth-20,100},MainMaterial);
        if(FrontStairs || SideStairs)
        {
            const FVector2D Landing=FrontStairs ? FVector2D(BuildingX+AccessX,Y[0]):
                FVector2D(BuildingX+OuterMax,Y[0]+130);
            const FVector2D Out=FrontStairs ? FVector2D(0,-1):FVector2D(1,0);
            TArray<FVector> Flight{FVector(Landing-Out*65,Base+Stride+100)};
            for(int32 Step=0;Step<32;++Step)
            {
                const FVector2D At=Landing+Out*(Step+.5f)*25.f;
                const float Top=Base+Stride-Step*20.f;
                if(Top<=CentralGroundHeight(L,At)+1.f)break;
                Box(FVector(At,Top*.5f),FrontStairs ? FVector(180,25,Top):FVector(25,180,Top),
                    EERBlockMaterial::Concrete,true,true,true);
                Flight.Add(FVector(At,Top+100));
                if(Top<=20.f)break;
            }
            const FVector2D Start=FVector2D(Flight.Last())+Out*70;
            Flight.Add(FVector(Start,CentralGroundHeight(L,Start)+100));
            ExteriorAccess+=Start;
            ExteriorAccess+=Landing;
            Algo::Reverse(Flight);
            AddPairedStair(MoveTemp(Flight));
            L.FeatureNames.Add(TEXT("VillaExteriorStairs"));
        }
        L.FeatureNames.Add(TEXT("VillaTerraces"));
    }
    L.CentralEntrances.Emplace(FRotator(0,180,0),FVector(BuildingX+OuterMin,DoorY,Base));
    L.CentralEntrances.Emplace(FRotator(0,180,0),FVector(BuildingX+OuterMin,-DoorY,Base));
    auto RoofLadder=[&](FVector BasePoint,float DeckHeight,EERBlockMaterial Material)
    {
        const float RailHeight=DeckHeight+110;
        for(float YOffset:{-39.f,39.f})
            Box(BasePoint+FVector(0,YOffset,RailHeight*.5f),{8,8,RailHeight},Material,true);
        for(float Z=25;Z<DeckHeight;Z+=25)
            Box(BasePoint+FVector(0,0,Z),{6,70,6},Material,true);
        for(const auto& Point:{BasePoint,Mate(BasePoint)})
            L.Ladders.Add({FTransform(Point),DeckHeight+5,DeckHeight});
    };
    // Enterable secondary buildings flank the lateral hill, with level pads.
    float FlankX=L.CentralHill ? BuildingX+W*.5f+800:RadiusX*.48f;
    if(!L.CentralHill)
    {
        const int32 Pairs=T.SizingTeamSize<3 ? 1:1+int32(HashCombineFast(uint32(Choice),0x484f5553u)&1u);
        const float HouseX=RadiusX*(T.SizingTeamSize==1 ? .60f:.55f);
        for(int32 I=0;I<Pairs;++I)
        {
            const float HW=T.SizingTeamSize==1 ? 500:Pick(600,1000),HD=T.SizingTeamSize==1 ? 500:Pick(600,800);
            const float HouseY=-RadiusY*(I ? .17f:T.SizingTeamSize==1 ? .35f:.48f);
            const FVector2D Center(HouseX,HouseY);const float H=CentralGroundHeight(L,Center);
            const uint32 HouseFinish=(HashCombineFast(uint32(Choice),0x4d41544cu)+uint32(I))%3u;
            const auto HouseMaterial=HouseFinish==0 ? EERBlockMaterial::Concrete:
                HouseFinish==1 ? EERBlockMaterial::Brick:EERBlockMaterial::Wood;
            const float RoofThickness=HouseMaterial==EERBlockMaterial::Wood ? FERCellSettings().WoodDepth:20.f;
            const float RoofHeight=300+RoofThickness;
            const int32 HouseProfile=int32(HashCombineFast(uint32(Choice),uint32(I)+0x53484144u)%3u);
            if(FMath::Square((HouseX+HW*.5f)/(RadiusX-100))+FMath::Square((HouseY-HD*.5f)/(RadiusY-100))>1)return false;
            for(int32 Copy=0;Copy<2;++Copy)
            {
                const FVector C=Copy ? Mate(FVector(Center,H)):FVector(Center,H);
                L.TerrainPads.Add(FBox(C-FVector(HW*.5f+180,HD*.5f+180,0),C+FVector(HW*.5f+180,HD*.5f+180,0)));
                L.SecondaryStructures.Add({FTransform(C),{HW,HD,RoofHeight},1,HouseMaterial,false,true});
                L.TownBuildings.Add(FBox(C-FVector(HW*.5f,HD*.5f,0),C+FVector(HW*.5f,HD*.5f,RoofHeight)));
            }
            Box({HouseX+HW*.5f-10,HouseY,H+150},{20,HD-40,300},HouseMaterial);
            for(float Sign:{-1.f,1.f})Box({HouseX,HouseY+Sign*(HD*.5f-10),H+150},{HW,20,300},HouseMaterial);
            for(float Sign:{-1.f,1.f})Box({HouseX-HW*.5f+10,HouseY+Sign*(HD+140)*.25f,H+150},{20,(HD-220)*.5f,300},HouseMaterial);
            Box({HouseX-HW*.5f+10,HouseY,H+270},{20,180,60},HouseMaterial);
            Box({HouseX,HouseY,H+300+RoofThickness*.5f},{HW,HD,RoofThickness},HouseMaterial,false,true,true);
            if(HouseProfile==1)
            {
                for(float Sign:{-1.f,1.f})Box({HouseX,HouseY+Sign*(HD*.5f-15),H+RoofHeight+30},{HW,30,60},HouseMaterial);
                Box({HouseX-HW*.5f+15,HouseY,H+RoofHeight+30},{30,HD-60,60},HouseMaterial);
                const float ReturnLength=(HD-60-130)*.5f;
                for(float Sign:{-1.f,1.f})
                    Box({HouseX+HW*.5f-15,HouseY+Sign*(65+ReturnLength*.5f),H+RoofHeight+30},
                        {30,ReturnLength,60},HouseMaterial);
            }
            else if(HouseProfile==2)
            {
                Box({HouseX-HW*.5f-100,HouseY,H+285},{180,290,20},EERBlockMaterial::Metal,true);
                Box({HouseX-HW*.5f-180,HouseY,H+137.5f},{20,20,275},EERBlockMaterial::Metal,true);
            }
            RoofLadder({HouseX+HW*.5f+14,HouseY,H},RoofHeight,
                HouseMaterial==EERBlockMaterial::Wood ? EERBlockMaterial::Wood:EERBlockMaterial::Metal);
            TArray<FVector> Entry{{HouseX-HW*.5f-100,HouseY,H+100},{HouseX,HouseY,H+100}};L.RoomAccessRoutes.Add(Entry);for(auto& P:Entry)P=Mate(P);L.RoomAccessRoutes.Add(Entry);
            {
                // Outbuildings read as stores, kitchens or offices around their single doorway.
                FERLayoutInterior Space;Space.Frame=FTransform(FVector(Center,H));Space.Half={HW*.5f-24,HD*.5f-24};
                Space.Kind=EERInteriorKind::BackRoom;Space.Height=300;Space.Doors.Add({HouseX-HW*.5f,HouseY,H});
                AddInteriorPair(L,MoveTemp(Space),true);
            }
            FlankX=FMath::Min(FlankX,(BuildingX+FMath::Max(MaxI,OuterMax)+HouseX-HW*.5f)*.5f);
        }
    }
    const bool WallDivision=L.CrossingRule==EERCrossingRule::ClimbableWall;
    const bool RailingDivision=L.CrossingRule==EERCrossingRule::FencedCrossings;
    const float FlankGround=CentralGroundHeight(L,{FlankX,0});
    constexpr float AnnexHalfWidth=240,AnnexHalfDepth=350;
    if(WallDivision || RailingDivision)
        L.TerrainPads.Add(FBox(FVector(FlankX-AnnexHalfWidth-50,-AnnexHalfDepth-90,FlankGround),
                              FVector(FlankX+AnnexHalfWidth+50,AnnexHalfDepth+90,FlankGround)));
    if(RailingDivision)
    {
        // The side walls continue the divider across the seam. Two rooms
        // connect the opposed ground entrances through an interior doorway.
        for(float Side:{-1.f,1.f})
        {
            Box({FlankX+Side*(AnnexHalfWidth-10),0,FlankGround+150},
                {20,2*AnnexHalfDepth,300},EERBlockMaterial::Brick,false,false);
            Box({FlankX+Side*160,-AnnexHalfDepth+10,FlankGround+150},
                {120,20,300},EERBlockMaterial::Brick);
            Box({FlankX+Side*160,0,FlankGround+150},
                {120,20,300},EERBlockMaterial::Brick,false,false);
        }
        Box({FlankX,-AnnexHalfDepth+10,FlankGround+270},{200,20,60},EERBlockMaterial::Brick);
        Box({FlankX,0,FlankGround+270},{200,20,60},EERBlockMaterial::Brick,false,false);
        Box({FlankX,0,FlankGround+310},{2*AnnexHalfWidth,2*AnnexHalfDepth,20},EERBlockMaterial::Brick,false,false,true);
        L.SecondaryStructures.Add({FTransform(FVector(FlankX,0,FlankGround)),{2*AnnexHalfWidth,2*AnnexHalfDepth,320},1,EERBlockMaterial::Brick,false,true});
        RoofLadder({FlankX+AnnexHalfWidth+14,-180,FlankGround},320,EERBlockMaterial::Metal);
        L.FeatureNames.Add(TEXT("RailingCrossingAnnex"));
    }
    // The shoreline is a chord. Terrain and the water surface share the arena perimeter.
    if(Waterfront)
    {
        L.PoolWater.Emplace(FVector(-RadiusX,-RadiusY,Grade),FVector(WaterfrontShoreX,RadiusY,Grade));
        const float West=BuildingX+OuterMin;
        TArray<FVector> Flight{{West-215,DoorY,CentralGroundHeight(L,{West-215,DoorY})+100}};
        for(int32 Step=0;Step<5;++Step)
        {
            const float Top=-75.f+Step*20.f;
            Box({West-135.f+Step*30.f,DoorY,(Top-100.f)*.5f},
                {30,240,Top+100.f},EERBlockMaterial::Concrete,true,true,true);
            Flight.Add({West-135.f+Step*30.f,DoorY,Top+100});
        }
        Flight.Add({West+65,DoorY,Base+100});
        AddPairedStair(MoveTemp(Flight));
        // The villa slab bears on submerged foundations behind the water entrance.
        for(float LocalY:{-D*.5f+45,-Band-45})
            Box({West+45,LocalY,-45},{65,65,90},EERBlockMaterial::Concrete,true);
    }
    // Recessed courtyard basins: 20cm at either entrance, 80cm in the centre.
    // The terrain is carved below the solid floor; the water has no collision.
    const FVector2D PoolCenter(BuildingX-W*.5f-650,-650);
    const bool Pools=L.CentralHill && Radius>=2900 &&
        (HashCombineFast(uint32(Choice),0x504f4f4cu)&4)!=0;
    if(Pools)
    {
        const bool SinglePool=L.CrossingRule==EERCrossingRule::OpenYard && (uint32(Choice)%7u)<3u;
        if(SinglePool)
        {
            const FVector Center(PoolCenter.X,0,Grade),WaterHalf(220,700,0);
            L.PoolWater.Emplace(Center-WaterHalf,Center+WaterHalf);
            Box({Center.X,0,Grade-100},{480,1460,20},EERBlockMaterial::Concrete,true,false,true);
            for(float Side:{-1.f,1.f})
                Box({Center.X+Side*230,0,Grade-30},{20,1460,120},EERBlockMaterial::Concrete,true,false);
            Box({Center.X,-710,Grade-30},{440,20,120},EERBlockMaterial::Concrete,true,true);
            Box({Center.X,-600,Grade-55},{440,200,70},EERBlockMaterial::Concrete,true,true,true);
            L.FeatureNames.Add(TEXT("VillaPoolSingle"));
        }
        else
        {
            for(float Side:{-1.f,1.f})
            {
                const FVector Center(PoolCenter.X,Side*PoolCenter.Y,Grade),WaterHalf(220,390,0);
                L.PoolWater.Emplace(Center-WaterHalf,Center+WaterHalf);
            }
            Box(FVector(PoolCenter,Grade-100),{480,820,20},EERBlockMaterial::Concrete,true,true,true);
            for(float Side:{-1.f,1.f})
            {
                Box(FVector(PoolCenter+FVector2D(Side*230,0),Grade-30),{20,820,120},EERBlockMaterial::Concrete,true);
                Box(FVector(PoolCenter+FVector2D(0,Side*400),Grade-30),{440,20,120},EERBlockMaterial::Concrete,true);
                Box(FVector(PoolCenter+FVector2D(0,Side*290),Grade-55),{440,190,70},EERBlockMaterial::Concrete,true,true,true);
            }
            L.FeatureNames.Add(TEXT("VillaPools"));
        }
    }
    // The roof slab wraps a recessed basin; the interior stair reaches it.
    if(RoofPool)
    {
        const float Roof=Base+L.BuildingFloors*Stride+RoofRise;
        const float RoofPoolX=BuildingX+RoofPoolLocalX;
        L.PoolWater.Emplace(FVector(RoofPoolX-RoofPoolHalfX,-RoofPoolHalfY,Roof),
                            FVector(RoofPoolX+RoofPoolHalfX,RoofPoolHalfY,Roof));
        Box({RoofPoolX,0,Roof-110},{2*RoofPoolHalfX,2*RoofPoolHalfY,20},EERBlockMaterial::Concrete,true,false,true);
        for(float Side:{-1.f,1.f})
        {
            Box({RoofPoolX+Side*(RoofPoolHalfX+10),0,Roof-50},
                {20,2*(RoofPoolHalfY+20),100},EERBlockMaterial::Concrete,true,false);
        }
        Box({RoofPoolX,-RoofPoolHalfY-10,Roof-50},
            {2*RoofPoolHalfX,20,100},EERBlockMaterial::Concrete,true,true);
        // Five square treads descend in one corner of each mirrored half.
        for(int32 Step=0;Step<5;++Step)
        {
            const float Height=100.f-Step*20.f;
            Box({RoofPoolX-RoofPoolHalfX+15+Step*30,-RoofPoolHalfY+30,Roof-100+Height*.5f},
                {30,60,Height},EERBlockMaterial::Concrete,true,true,true);
        }
        L.FeatureNames.Add(TEXT("RooftopPool"));
    }
    // Small, varied cover groups; sample the north half and mirror every member.
    // Rear entries stay outside all return walls, even in the pocket profile.
    const int32 OriginalBands=2+int32(Radius>=4200 && (HashCombineFast(uint32(Choice),0x45585452u)&1u)!=0);
    TArray<FVector2D> CoverBands;
    for(int32 BandIndex=0;BandIndex<OriginalBands;++BandIndex)
    {
        const float XFraction=WaterfrontCover && BandIndex==0 ? .18f:BandIndex==2 ? .25f:.55f+BandIndex*.20f;
        const float YFraction=BandIndex==2 ? .75f:.55f-BandIndex*.35f;
        CoverBands.Add({RadiusX*XFraction,WaterfrontCover && BandIndex==0 ? -RadiusY+700.f:-RadiusY*YFraction});
    }
    // Large arenas add physically sized groups along their approaches on a 16m grid.
    // Exclusions can leave gaps; exposure metrics read the actual accepted coverage.
    if(T.SizingTeamSize>=4)for(float CoverY=-RadiusY+1400;CoverY<=-1000;CoverY+=1600)
        for(float CoverX=1000;CoverX<RadiusX-900;CoverX+=1600)CoverBands.Add({CoverX,CoverY});
    const int32 PairBudget=FMath::Clamp(FMath::CeilToInt32(PI*Radius*Radius/10000000.f),4,24);
    const float ApproachScreenY=FMath::Min(-RadiusY+(WaterfrontCover ? 1100.f:700.f),-D*.5f-180);
    const float DryTurnY=FMath::Max(ApproachScreenY+100,-RadiusY*.68f);
    const TArray<FVector2D> DryApproach{{0,ApproachScreenY-100},{650,ApproachScreenY-100},
        {650,DryTurnY},{BuildingX+HallX,DryTurnY},{BuildingX+HallX,-300}};
    const float ApproachDoorX=BuildingX+OuterMin-120;
    const TArray<FVector2D> WestApproach{{0,ApproachScreenY-100},{-650,ApproachScreenY-100},{-650,ApproachScreenY+100},
        {ApproachDoorX,-D*.5f-180},{ApproachDoorX,DoorY}};
    const auto& ClearApproach=Waterfront ? DryApproach:WestApproach;
    TArray<FVector2D> PlacedCover;int32 CoverPairs=0;
    for(int32 BandIndex=0;BandIndex<CoverBands.Num() && CoverPairs<PairBudget;++BandIndex)for(float Sign:{-1.f,1.f})
    {
        if(CoverPairs>=PairBudget)break;
        if(Waterfront && !WaterfrontCover)continue;
        const FVector2D P(Sign*CoverBands[BandIndex].X+(WaterfrontCover && BandIndex==0 ? 0.f:Geometry.RandRange(-4,4)*10.f),
            CoverBands[BandIndex].Y+(WaterfrontCover && BandIndex==0 ? 0.f:Geometry.RandRange(-4,4)*10.f));
        const float OutsideX=FMath::Abs(P.X-BuildingX)-W*.5f,OutsideY=FMath::Abs(P.Y)-D*.5f;
        const float PadHalf=450+Radius*.025f;
        // Keep the pad's terrain blend clear of the straight flank approach.
        const float EdgeMargin=FMath::Min(500.f,RadiusX*.2f);
        if((OutsideX<450 && OutsideY<450) ||
           FMath::Square(P.X/(RadiusX-EdgeMargin))+FMath::Square(P.Y/(RadiusY-EdgeMargin))>1 ||
           FMath::Abs(P.X-FlankX)<PadHalf+235)continue;
        if(Pools && FMath::Abs(P.X-PoolCenter.X)<850 && FMath::Abs(P.Y-PoolCenter.Y)<1000)continue;
        if(Waterfront && P.X<WaterfrontShoreX+450)continue;
        if(ExteriorAccess.bIsValid && ExteriorAccess.ExpandBy(PadHalf+90).IsInside(P))continue;
        bool Clear=true;for(const auto& S:L.SecondaryStructures)Clear&=FMath::Abs(P.X-S.Frame.GetLocation().X)>S.Size.X*.5+650 || FMath::Abs(P.Y-S.Frame.GetLocation().Y)>S.Size.Y*.5+650;
        for(const auto& Other:PlacedCover)Clear&=FVector2D::Distance(P,Other)>2*PadHalf+100;
        const FBox CoverBounds(FVector(P-FVector2D(400),-1000),FVector(P+FVector2D(400),2000));
        for(int32 I=1;I<ClearApproach.Num();++I)
            Clear&=!FMath::LineBoxIntersection(CoverBounds,FVector(ClearApproach[I-1],100),FVector(ClearApproach[I],100),FVector(ClearApproach[I]-ClearApproach[I-1],0));
        if(!Clear)continue;
        PlacedCover.Add(P);++CoverPairs;
        const float H=CentralGroundHeight(L,P);
        for(float PairSign:{-1.f,1.f})L.TerrainPads.Add(FBox(FVector(P.X,PairSign*P.Y,H)-FVector(PadHalf,PadHalf,0),FVector(P.X,PairSign*P.Y,H)+FVector(PadHalf,PadHalf,0)));
        const int32 Kind=int32(HashCombineFast(uint32(Choice),uint32(BandIndex*2+(Sign>0 ? 1:0))+0x434f5645u)%5);
        const float Yaw=Geometry.RandRange(0,1)!=0 ? 90.f:0.f;
        const FTransform Group(FRotator(0,Yaw,0),FVector(P,H));
        const float Height=Geometry.RandRange(0,1)!=0 ? 150.f:180.f;
        const int32 PaletteIndex=(BandIndex*2+(Sign>0 ? 1:0)+int32(HashCombineFast(uint32(Choice),0x50414c54u)%3u))%3;
        const EERBlockMaterial Material=PaletteIndex==0 ? EERBlockMaterial::Brick:PaletteIndex==1 ? EERBlockMaterial::Concrete:EERBlockMaterial::Wood;
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
    // The exterior divider stops at the villa facade; the interior remains connected.
    if(L.CrossingRule!=EERCrossingRule::OpenYard)
    {
        const bool Railing=L.CrossingRule==EERCrossingRule::FencedCrossings;
        TArray<float> Cuts{-RadiusX+30,BuildingX+MinI,BuildingX+MinI+20,BuildingX+MaxI-20,BuildingX+MaxI,RadiusX-30};
        if(Railing){Cuts.Add(FlankX-AnnexHalfWidth);Cuts.Add(FlankX+AnnexHalfWidth);}
        Cuts.Sort();
        for(int32 I=1;I<Cuts.Num();++I)
        {
            const float A=Cuts[I-1],B=Cuts[I];if(B-A<1)continue;
            const float Mid=(A+B)*.5f;
            if(Railing && FMath::Abs(Mid-FlankX)<AnnexHalfWidth)continue;
            if((Mid>BuildingX+MinI && Mid<BuildingX+MinI+20) || (Mid>BuildingX+MaxI-20 && Mid<BuildingX+MaxI))continue;
            const int32 Count=FMath::Max(1,FMath::CeilToInt32((B-A)/200));
            for(int32 J=0;J<Count;++J)
            {
                const float Lo=FMath::Lerp(A,B,float(J)/Count),Hi=FMath::Lerp(A,B,float(J+1)/Count),CX=(Lo+Hi)*.5f;
                const float H=FMath::Max3(CentralGroundHeight(L,{Lo,0}),CentralGroundHeight(L,{Hi,0}),CentralGroundHeight(L,{CX,0}));
                const bool InsideVilla=CX>BuildingX+MinI && CX<BuildingX+MaxI;
                if(InsideVilla)continue;
                const float DividerBase=FMath::Min3(CentralGroundHeight(L,{Lo,0}),CentralGroundHeight(L,{Hi,0}),CentralGroundHeight(L,{CX,0}))-80;
                const float DividerTop=H+WallHeight;
                if(Railing)
                {
                    const float Foundation=H;
                    Box({CX,0,Foundation+ERIndustrialFence::PlinthHeightCm*.5f},{Hi-Lo,12,ERIndustrialFence::PlinthHeightCm},EERBlockMaterial::Concrete,true,false);
                    Box({CX,0,Foundation+ERIndustrialFence::PlinthHeightCm+ERIndustrialFence::ModuleHeightCm*.5f},{Hi-Lo,12,ERIndustrialFence::ModuleHeightCm},EERBlockMaterial::Metal,true,false).PawnBarrier=true;
                }
                else
                    Box({CX,0,(DividerBase+DividerTop)*.5f},{Hi-Lo,40,DividerTop-DividerBase},EERBlockMaterial::Brick,false,false);
            }
        }
    }
    // One permanent perimeter follows the terrain and ends at its visible top.
    constexpr int32 BoundarySides=128;const float BoundaryRadiusX=RadiusX-20,BoundaryRadiusY=RadiusY-20;
    for(int32 I=0;I<BoundarySides/2;++I)
    {
        const double A=-PI+I*2*PI/BoundarySides,B=A+2*PI/BoundarySides;
        const FVector2D Start(FMath::Cos(A)*BoundaryRadiusX,FMath::Sin(A)*BoundaryRadiusY);
        const FVector2D End(FMath::Cos(B)*BoundaryRadiusX,FMath::Sin(B)*BoundaryRadiusY);
        const FVector2D P=(Start+End)*.5;const float H=CentralGroundHeight(L,P);
        const float Length=FVector2D::Distance(Start,End)-.5f;
        const float Yaw=FMath::RadiansToDegrees(FMath::Atan2(End.Y-Start.Y,End.X-Start.X));
        const float WallBase=Waterfront ? FMath::Max(0.f,H):H;
        auto& Plinth=Box(FVector(P,(H+WallBase+100)*.5f),{Length,12,WallBase+100-H},EERBlockMaterial::Concrete,true);Plinth.RotationYaw=Yaw;
        auto& Fence=Box(FVector(P,WallBase+100+ERIndustrialFence::ModuleHeightCm*.5f),{Length,12,ERIndustrialFence::ModuleHeightCm},EERBlockMaterial::Metal,true);Fence.RotationYaw=Yaw;Fence.PawnBarrier=true;
    }
    const float ScreenY=FMath::Min(-RadiusY+(WaterfrontCover ? 1100.f:700.f),-D*.5f-180),ScreenHalf=500,ScreenGround=CentralGroundHeight(L,{0,ScreenY});
    Box({0,ScreenY,ScreenGround+150},{1000,20,300},ScreenMaterial);L.FeatureNames.Add(TEXT("ScreenedSpawns"));
    const float SpawnY=-RadiusY+250;
    for(int32 I=0;I<10;++I)
    {
        const FVector2D P((I%5-2)*150,SpawnY+(I/5)*160);L.AlphaSpawns.Emplace(FRotator(0,90,0),FVector(P,CentralGroundHeight(L,P)+100));
    }
    // Approach the west-facing side door around the front corner, rather than
    // tracing a straight north/south line through opposed facade openings.
    const float DoorOutside=BuildingX+OuterMin-120;
    const TArray<FVector2D> Approach{{0,ScreenY-100},{-ScreenHalf-150,ScreenY-100},{-ScreenHalf-150,ScreenY+100},
        {DoorOutside,-D*.5f-180},{DoorOutside,DoorY},{BuildingX+OuterMin+160,DoorY},{BuildingX+HallX,DoorY}};
    const auto& EntryApproach=Waterfront ? DryApproach:Approach;
    TArray<FVector> North;
    for(int32 I=1;I<EntryApproach.Num();++I)Append(North,GroundPath(EntryApproach[I-1],EntryApproach[I]));
    Append(North,GroundPath(EntryApproach.Last(),{BuildingX+HallX,-300}));
    TArray<FVector> Through=North;
    Append(Through,GroundPath({BuildingX+HallX,-300},{BuildingX+HallX,300}));
    auto South=North;for(auto& P:South)P=Mate(P);Algo::Reverse(South);
    Append(Through,South);L.CrossingRoutes.Add(Through);Patrol=Through;
    // Follow the flank to the stairs or through the annex.
    TArray<FVector> Flank;
    const FVector2D FlankStart(-ScreenHalf-150,ScreenY+100);
    if(WaterfrontCover)Append(Flank,GroundPath(FlankStart,{-ScreenHalf-150,ScreenY+250}));
    Append(Flank,GroundPath(WaterfrontCover ? FVector2D(-ScreenHalf-150,ScreenY+250):FlankStart,{FlankX,-D*.5f-180}));
    Append(Flank,GroundPath({FlankX,-D*.5f-180},{FlankX,WallDivision ? WallStairStart-100:0.f}));
    if(WallDivision)
    {
        TArray<FVector> Flight{{FlankX,WallStairStart-100,FlankGround+100}};
        for(int32 Step=0;Step<WallStairSteps;++Step)
        {
            const float TreadY=WallStairStart+(Step+.5f)*30,Top=FlankGround+(Step+1)*20;
            Box({FlankX,TreadY,Top*.5f},{180,30,Top},EERBlockMaterial::Concrete,true,true,true);
            Flight.Add({FlankX,TreadY,Top+120});
        }
        const int32 LastTread=Flight.Num()-1;
        Flight.Add({FlankX,-10,FlankGround+WallHeight+120});Flight.Add({FlankX,10,FlankGround+WallHeight+120});
        for(int32 I=LastTread;I>=0;--I){const auto Point=Mate(Flight[I]);Flight.Add(Point);}
        L.StairRoutes.Add(MoveTemp(Flight));
        L.FeatureNames.Add(TEXT("ExteriorWallCrossingStairs"));
    }
    auto OtherFlank=Flank;
    for(auto& Point:OtherFlank)Point=Mate(Point);
    Algo::Reverse(OtherFlank);
    if(WallDivision)L.CrossingRoutes.Add(MoveTemp(OtherFlank));
    else Append(Flank,OtherFlank);
    L.CrossingRoutes.Add(MoveTemp(Flank));
    for(int32 I=Patrol.Num()-2;I>0;--I){const FVector Point=Patrol[I];Patrol.Add(Point);}
    L.FeatureNames.Add(L.CentralHill ? TEXT("CentralHill"):TEXT("OpposedHill"));return true;
}
}
