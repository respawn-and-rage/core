#include "ERMapTemplate.h"
#include "ERProceduralMap.h"
#include "ERIndustrialFence.h"
#include "Engine/StaticMesh.h"
#include "Paths/PCGExPath.h"
#include "Data/PCGPointData.h"
#include "PCGDataAsset.h"
#include "Metadata/PCGMetadata.h"
#if WITH_EDITOR
#include "AssetRegistry/AssetRegistryModule.h"
#include "Misc/PackageName.h"
#include "UObject/SavePackage.h"
#include "UObject/Package.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#endif

namespace ERProcedural
{
bool BuildCentralBuilding(const UERMapTemplate& T,int32 Seed,TArray<FERMapTemplatePiece>& Pieces,FERProceduralLayout& L,TArray<FVector>& Patrol,int32 CrossingOverride)
{
    if(T.CentralBuilding.CircularTerrain)return BuildCircularCentralBuilding(T,Seed,Pieces,L,Patrol);
    const auto& V=T.CentralBuilding;
    auto Range=[](FVector2D R,float Min,float Max)
    {return !R.ContainsNaN() && R.X>=Min && R.Y<=Max && R.X<=R.Y && FMath::Fmod(R.X,50)==0 && FMath::Fmod(R.Y,50)==0;};
    if(T.Symmetry!=EERTemplateSymmetry::HalfTurn ||
        !Range({V.ArenaMin.X,V.ArenaMax.X},T.SizingTeamSize>0 ? 3000:6400,T.SizingTeamSize>0 ? 20000:8000) || !Range({V.ArenaMin.Y,V.ArenaMax.Y},T.SizingTeamSize>0 ? 3400:8400,T.SizingTeamSize>0 ? 24000:10000) ||
        !Range(V.BuildingWidth,1800,2400) || !Range(V.BuildingDepth,2200,3000) ||
        V.MinFloors<1 || V.MaxFloors>3 || V.MinFloors>V.MaxFloors || T.RoofMesh || T.ShellWallMesh)return false;
    for(auto* Mesh:{V.FenceMesh.Get(),V.FenceModule.Get(),V.ContainerMesh.Get(),V.TankMesh.Get(),V.BarrelMesh.Get()})
        if(Mesh && (Mesh->GetBoundingBox().GetSize().ContainsNaN() || Mesh->GetBoundingBox().GetSize().GetMin()<.1))return false;
    if(V.ContainerSurfaces.Num()>8 || V.ContainerSurfaces.Contains(nullptr))return false;
    for(const auto* Palette:{&V.ConcreteSurfaces,&V.BrickSurfaces,&V.WoodSurfaces,&V.GroundSurfaces})
        if(Palette->Num()>8 || Palette->Contains(nullptr))return false;
    auto Surface=[&](const TArray<TObjectPtr<UMaterialInterface>>& Palette,uint32 Salt)->UMaterialInterface*
    {return Palette.IsEmpty() ? nullptr:Palette[HashCombineFast(uint32(Seed),Salt)%Palette.Num()].Get();};
    auto* Concrete=Surface(V.ConcreteSurfaces,0x434f4e43u);
    auto* Brick=Surface(V.BrickSurfaces,0x42524943u);
    auto* Wood=Surface(V.WoodSurfaces,0x574f4f44u);
    L.GroundSurface=Surface(V.GroundSurfaces,0x47524f55u);
    FRandomStream R(Seed);
    auto Pick=[&](float A,float B){return float(R.RandRange(int32(A/50),int32(B/50))*50);};
    L.CrossingRule=CrossingOverride ? EERCrossingRule(CrossingOverride):EERCrossingRule(R.RandRange(1,3));
    L.BuildingFloors=R.RandRange(V.MinFloors,V.MaxFloors);
    const float W=Pick(V.BuildingWidth.X,V.BuildingWidth.Y),D=Pick(V.BuildingDepth.X,V.BuildingDepth.Y);
    const float Stride=320,Roof=L.BuildingFloors*Stride;
    L.ArenaSize=FVector(Pick(V.ArenaMin.X,V.ArenaMax.X),Pick(V.ArenaMin.Y,V.ArenaMax.Y),1600);
    L.CentralBuildingSize=FVector(W,D,Roof);
    const FVector Half=L.ArenaSize*.5;
    const bool Terraced=L.CrossingRule==EERCrossingRule::OpenYard && L.BuildingFloors>1;
    const float CoreX=(T.SizingTeamSize==0 || T.SizingTeamSize>=4) && L.CrossingRule==EERCrossingRule::OpenYard && (uint32(Seed)&1) ? ((uint32(Seed)&2) ? -800.f:800.f):0.f;
    L.CentralBuildingCenter={CoreX,0,0};L.TerracedBuilding=Terraced;
    Pieces.Reset();L.AlphaSpawns.Reset();L.OmegaSpawns.Reset();
    auto Box=[&](FVector P,FVector Size,EERBlockMaterial M,bool Fixed=false,UStaticMesh* Mesh=nullptr,bool Pair=true,bool Deck=false)->FERMapTemplatePiece&
    {
        auto& B=Pieces.AddDefaulted_GetRef();B.Center=P;B.Size=Size;B.Material=M;B.Destructible=!Fixed;
        B.Mirrored=Pair;B.FixedMesh=Mesh;B.WalkableSurface=Deck;
        const float Bottom=P.Z-Size.Z*.5;
        // Only a real upper slab seeds support. Door/window headers must still
        // depend on their jambs, rather than becoming floating permanent anchors.
        B.Anchored=Bottom>0 && FMath::IsNearlyZero(FMath::Fmod(Bottom,Stride),.01f);
        if(Mesh && Mesh==V.ContainerMesh)B.MeshRotation=FRotator(0,90,0);
        if(M==EERBlockMaterial::Metal && !Mesh)
        {
            if(Fixed)B.FixedSurface=Deck && V.DeckSurface ? V.DeckSurface:V.SteelSurface;
            else B.CellSurface=V.SteelSurface;
        }
        else if(!Mesh)
        {
            auto* Finish=M==EERBlockMaterial::Concrete ? Concrete:M==EERBlockMaterial::Brick ? Brick:M==EERBlockMaterial::Wood ? Wood:nullptr;
            if(Fixed)B.FixedSurface=Finish;else B.CellSurface=Finish;
        }
        return B;
    };
    auto LowFence=[&](FVector Center,float Width,bool AlongX=true,float BaseZ=0)
    {
        const FVector Footprint=AlongX ? FVector(Width,12,0):FVector(12,Width,0);
        Box(Center+FVector(0,0,BaseZ+ERIndustrialFence::PlinthHeightCm*.5),
            Footprint+FVector(0,0,ERIndustrialFence::PlinthHeightCm),EERBlockMaterial::Concrete,true);
        Box(Center+FVector(0,0,BaseZ+ERIndustrialFence::PlinthHeightCm+ERIndustrialFence::ModuleHeightCm*.5),
            Footprint+FVector(0,0,ERIndustrialFence::ModuleHeightCm),EERBlockMaterial::Metal,true);
        Pieces.Last().PawnBarrier=true;
    };
    auto Slab=[&](float X0,float X1,float Y0,float Y1,float Z,bool Pair=true)
    {Box({(X0+X1)*.5,(Y0+Y1)*.5,Z-10},{X1-X0,Y1-Y0,20},EERBlockMaterial::Metal,true,nullptr,Pair,true);};
    auto Place=[&](const FTransform& Frame,FVector P,FVector Size,EERBlockMaterial Material,bool Deck=false)->FERMapTemplatePiece&
    {
        auto& B=Box(Frame.TransformPosition(P),Size,Material,false,nullptr,true,Deck);
        B.RotationYaw=Frame.Rotator().Yaw;B.Anchored=false;return B;
    };
    auto TimberPlate=[&](const FTransform& Frame,float X0,float X1,float Y0,float Y1,float Top)
    {if(X1>X0 && Y1>Y0)Place(Frame,{(X0+X1)*.5,(Y0+Y1)*.5,Top-1},{X1-X0,Y1-Y0,2},EERBlockMaterial::Wood,true);};
    auto TimberFlight=[&](const FTransform& Frame,float X,float StartY,int32 Floor,bool WallAccess)
    {
        const float Base=Floor*Stride;
        for(int32 I=0;I<16;++I)
        {
            const float Top=Base+(I+1)*20,Y=StartY+(I+.5)*30;
            Place(Frame,{X,Y,Top-1},{180,30,2},EERBlockMaterial::Wood,true);
            // Higher flights rest on the previous flight's treads through the
            // open stairwell. No invisible fixed base or upper anchor remains.
            const float Bottom=Floor ? Base-Stride+(I+1)*20:0,Height=Top-2-Bottom;
            for(float S:{-1.f,1.f})Place(Frame,{X+S*78,Y,Bottom+Height*.5},{12,2,Height},EERBlockMaterial::Wood);
        }
        for(int32 Copy=0;Copy<2;++Copy)
        {
            const FTransform Pair=Frame*FTransform(FRotator(0,Copy ? 180:0,0));
            TArray<FVector> Path{Pair.TransformPosition({X,StartY-100,Base+100})};
            for(int32 I=0;I<16;++I)Path.Add(Pair.TransformPosition({X,StartY+(I+.5)*30,Base+(I+1)*20+120}));
            Path.Add(Pair.TransformPosition({X,WallAccess ? 0:StartY+580,Base+420}));
            if(WallAccess)L.WallAccessFlights.Add(L.StairRoutes.Num());
            L.StairRoutes.Add(MoveTemp(Path));
        }
    };
    auto SteelLadder=[&](const FTransform& Frame,FVector Origin,float DeckHeight)
    {
        const float Height=DeckHeight+110;
        // Calibrated 2mm folded steel; paired geometry and movement metadata.
        for(float Y:{-42.f,42.f})
        {
            for(float X:{-3.9f,3.9f})Place(Frame,Origin+FVector(X,Y,Height*.5),{.2f,8,Height},EERBlockMaterial::Metal);
            for(float Edge:{-3.9f,3.9f})Place(Frame,Origin+FVector(0,Y+Edge,Height*.5),{7.6f,.2f,Height},EERBlockMaterial::Metal);
        }
        for(float Z=25;Z<DeckHeight;Z+=25)Place(Frame,Origin+FVector(0,0,Z),{.2f,76,4},EERBlockMaterial::Metal);
        const FTransform LadderFrame(Frame.GetRotation(),Frame.TransformPosition(Origin));
        L.Ladders.Add({LadderFrame,Height,DeckHeight});
        L.Ladders.Add({LadderFrame*FTransform(FRotator(0,180,0)),Height,DeckHeight});
    };
    auto Container=[&](FVector Center,FVector Size,float Yaw)
    {
        auto& B=Box(Center,Size,EERBlockMaterial::Metal);B.RotationYaw=Yaw;B.Strength=8;B.Anchored=false;
        // Visual rolls are independent for the two copies and never consume geometry RNG.
        if(!V.ContainerSurfaces.IsEmpty())
        {
            const uint32 PaletteSeed=HashCombineFast(uint32(Seed),0x46414354u);
            B.CellSurface=V.ContainerSurfaces[HashCombineFast(PaletteSeed,uint32(L.Containers.Num()))%V.ContainerSurfaces.Num()];
            B.MirroredCellSurface=V.ContainerSurfaces[HashCombineFast(PaletteSeed,uint32(L.Containers.Num()+1))%V.ContainerSurfaces.Num()];
        }
        for(int32 Copy=0;Copy<2;++Copy)L.Containers.Add({FTransform(FRotator(0,Yaw+(Copy ? 180:0),0),Copy ? FVector(-Center.X,-Center.Y,Center.Z):Center),Size});
    };
    const auto MainMaterial=R.RandRange(0,1) ? EERBlockMaterial::Concrete:EERBlockMaterial::Brick;
    // Half-turn pairs include the central building itself, both stairwells and
    // all floors. Stairs repeat every storey with a genuinely open floor well.
    const float SX=-W*.5+180,Start=-D*.5+220,End=Start+480;
    for(int32 Floor=0;Floor<L.BuildingFloors;++Floor)
    {
        const float Z=Floor*Stride,FloorW=W-(Terraced ? Floor*500:0),FloorD=D-(Terraced ? Floor*200:0);
        // South facade: offset door and two windows. The north door is rotated.
        const float Door=CoreX!=0 ? 0:-300,DoorHalf=140;
        auto Facade=[&](float A,float B,float Lo,float Hi)
        {if(B>A && Hi>Lo)Box({(A+B)*.5,-FloorD*.5+10,Z+(Lo+Hi)*.5},{B-A,20,Hi-Lo},MainMaterial);};
        // Residential facade: repeated windows and a full-height balcony door.
        // Keep the authored offset entrance and its circulation corridor intact.
        struct FOpening { float Center,HalfWidth,Sill,Head; };
        TArray<FOpening> Openings;
        if(Floor==0)
        {
            Openings.Add({Door,DoorHalf,0,240});
            Openings.Add({FloorW*.30f,80,90,230});
            if(Door==0)Openings.Add({-FloorW*.30f,80,90,230});
        }
        else
        {
            Openings.Add({-FloorW*.25f,90,90,230});
            Openings.Add({FloorW*.25f,90,0,240});
        }
        Openings.Sort([](const FOpening& A,const FOpening& B){return A.Center<B.Center;});
        float Cursor=-FloorW*.5f;
        for(const auto& Opening:Openings)
        {
            Facade(Cursor,Opening.Center-Opening.HalfWidth,0,300);
            Facade(Opening.Center-Opening.HalfWidth,Opening.Center+Opening.HalfWidth,0,Opening.Sill);
            Facade(Opening.Center-Opening.HalfWidth,Opening.Center+Opening.HalfWidth,Opening.Head,300);
            Cursor=Opening.Center+Opening.HalfWidth;
        }
        Facade(Cursor,FloorW*.5f,0,300);
        if(Floor>0)
        {
            const float BX=FloorW*.25f,BY=-FloorD*.5f;
            Slab(BX-150,BX+150,BY-160,BY-(Terraced ? 100:0),Z);
            // Solid masonry guards are supported by the balcony slab.
            Box({BX,BY-150,Z+50},{300,20,100},MainMaterial);
            for(float Sign:{-1.f,1.f})
                Box({BX+Sign*140,BY-75,Z+50},{20,130,100},MainMaterial);
            for(float Sign:{-1.f,1.f})L.RoomAccessRoutes.Add({{Sign*BX,Sign*(BY+130),Z+100},{Sign*BX,Sign*(BY-80),Z+100}});
        }
        // Paired room wings flank a broad central hall. Open doorways connect
        // the rooms without intruding into either stairwell or the cross-map route.
        const float RoomX=FloorW*.5f-340,RoomY=-FloorD*.23f;
        if(RoomX>450)
        {
            Box({-(RoomX+457.5f)*.5f,RoomY,Z+150},{RoomX-457.5f,15,300},MainMaterial);
            Box({-450,(RoomY+140-200)*.5f,Z+150},{15,-200-RoomY-140,300},MainMaterial);
            Box({-450,RoomY+70,Z+270},{15,140,60},MainMaterial);
            for(float Sign:{-1.f,1.f})L.RoomAccessRoutes.Add({{Sign*-320,Sign*(RoomY+70),Z+100},{Sign*-580,Sign*(RoomY+70),Z+100}});
        }
        if(Terraced && Floor>0)
        {
            const float DoorY=End+110,MinY=-FloorD*.5f+20,MaxY=FloorD*.5f-20;
            for(const auto Interval:{FVector2D(MinY,DoorY-120),FVector2D(DoorY+120,MaxY)})
                Box({-FloorW*.5f+10,(Interval.X+Interval.Y)*.5f,Z+150},{20,Interval.Y-Interval.X,300},MainMaterial);
            Box({-FloorW*.5f+10,DoorY,Z+270},{20,240,60},MainMaterial);
            for(float Sign:{-1.f,1.f})L.RoomAccessRoutes.Add({{Sign*(-FloorW*.5f-100),Sign*DoorY,Z+100},{Sign*(-FloorW*.5f+200),Sign*DoorY,Z+100}});
        }
        else
        {
            auto SideWall=[&](float A,float B,float Lo,float Hi)
            {if(B>A && Hi>Lo)Box({-FloorW*.5f+10,(A+B)*.5f,Z+(Lo+Hi)*.5f},{20,B-A,Hi-Lo},MainMaterial);};
            float Edge=-FloorD*.5f+20;
            for(float Y:{-FloorD*.25f,FloorD*.25f})
            {
                SideWall(Edge,Y-80,0,300);
                SideWall(Y-80,Y+80,0,90);SideWall(Y-80,Y+80,230,300);
                Edge=Y+80;
            }
            SideWall(Edge,FloorD*.5f-20,0,300);
        }
        const float DeckZ=Z+Stride;
        if(Terraced && Floor>0)
            Slab(-FloorW*.5,FloorW*.5,-FloorD*.5,FloorD*.5,DeckZ,false);
        else
        {
        Slab(-W*.5+290,W*.5-290,-D*.5,D*.5,DeckZ,false);
        Slab(-W*.5,-W*.5+70,-D*.5,D*.5,DeckZ);
        Slab(-W*.5+70,-W*.5+290,-D*.5,Start,DeckZ);
        Slab(-W*.5+70,-W*.5+290,End,D*.5,DeckZ);
        // 20cm risers and 30cm treads; the landing is part of the solid deck.
        for(int32 I=0;I<16;++I)Box({SX,Start+(I+.5)*30,Z+(I+1)*20-10},{180,30,20},EERBlockMaterial::Metal,true,nullptr,true,true);
        for(int32 Copy=0;Copy<2;++Copy)
        {
            const FTransform Frame(FRotator(0,Copy ? 180:0,0),FVector::ZeroVector);
            TArray<FVector> Path{Frame.TransformPosition({SX,Start-100,Z+100})};
            // The standing capsule reaches the next riser before its centre;
            // reserve that step-up clearance in the route sample as well.
            for(int32 I=0;I<16;++I)Path.Add(Frame.TransformPosition({SX,Start+(I+.5)*30,Z+(I+1)*20+120}));
            Path.Add(Frame.TransformPosition({SX,End+100,DeckZ+100}));L.StairRoutes.Add(Path);
        }
        }
        if(L.CrossingRule==EERCrossingRule::FencedCrossings)
        {
            const int32 N=FMath::CeilToInt((W*.5-20)/300);const float Width=(W*.5-20)/N;
            for(int32 I=0;I<N;++I)LowFence({-W*.5+20+(I+.5)*Width,0,0},Width,true,Z);
        }
    }
    // Roof parapets leave the two stairs and the crossing over their landings open.
    // Paired exterior access supplements, rather than replaces, the main stairs.
    const float RoofW=W-(Terraced ? (L.BuildingFloors-1)*500:0),RoofD=D-(Terraced ? (L.BuildingFloors-1)*200:0);
    const float MainLadderY=-RoofD*.25;
    if(Terraced)for(int32 Floor=0;Floor<L.BuildingFloors;++Floor)
        SteelLadder(FTransform::Identity,{(W-Floor*500)*.5f+35,-(D-Floor*200)*.25f,Floor*Stride},Stride);
    else SteelLadder(FTransform::Identity,{W*.5+35,MainLadderY,0},Roof);
    for(const auto& Interval:{FVector2D(-RoofD*.5,-MainLadderY-80),FVector2D(-MainLadderY+80,RoofD*.5)})
        Box({-RoofW*.5+10,(Interval.X+Interval.Y)*.5,Roof+50},{20,Interval.Y-Interval.X,100},EERBlockMaterial::Concrete);
    Box({0,-RoofD*.5+10,Roof+50},{RoofW-40,20,100},EERBlockMaterial::Concrete);
    // Guard the stairwell's inward side, but leave its top mouth open.
    if(!Terraced)Box({SX+120,(Start+End)*.5,Roof+50},{20,End-Start,100},EERBlockMaterial::Metal,true);
    for(auto& Piece:Pieces){Piece.Center.X+=CoreX;Piece.SymmetryCenter.X=CoreX;}
    for(auto& Ladder:L.Ladders)Ladder.Frame.AddToTranslation({CoreX,0,0});
    for(auto* Routes:{&L.StairRoutes,&L.RoomAccessRoutes})for(auto& Route:*Routes)for(auto& Point:Route)Point.X+=CoreX;
    const float SideX=Half.X-650;
    const bool SideBuildings=(T.SizingTeamSize==0 || T.SizingTeamSize>=4) && CoreX==0 && (L.CrossingRule!=EERCrossingRule::OpenYard || R.RandRange(0,1)==1);
    const float SideW=Pick(700,800),SideD=Pick(1000,1200);
    if(SideBuildings)
    {
        const int32 Floors=R.RandRange(1,T.SizingTeamSize>0 ? V.MaxFloors:3);
        const bool Tower=L.CrossingRule!=EERCrossingRule::FencedCrossings && R.RandRange(0,1)==1;
        const bool Yard=L.CrossingRule==EERCrossingRule::ClimbableWall;
        const float SW=Yard ? 700:SideW,SD=Yard ? 1000:SideD;
        const float Yaw=L.CrossingRule==EERCrossingRule::FencedCrossings ? 0:R.RandRange(-1,1)*20;
        const FTransform Frame(FRotator(0,Yaw,0),FVector(-SideX,Yard ? -D*.5-950:0,0));
        const auto Material=R.RandRange(0,1) ? EERBlockMaterial::Brick:EERBlockMaterial::Concrete;
        // Towers always use ladders; reserve full stairwells for the broadest buildings.
        const bool LadderAccess=Tower || SW<800 || SD<1150;
        const float StairX=-SW*.5+180,StairStart=-SD*.5+220,StairEnd=StairStart+480;
        for(int32 Copy=0;Copy<2;++Copy)L.SecondaryStructures.Add({Frame*FTransform(FRotator(0,Copy ? 180:0,0)),{SW,SD,Floors*Stride},Floors,Material,Tower,LadderAccess});
        for(int32 Floor=0;Floor<Floors;++Floor)
        {
            const float Base=Floor*Stride,Top=Base+Stride,H=Stride-2;
            if(Tower)
            {
                for(float X:{-SW*.5+35,SW*.5-35})for(float Y:{-SD*.5+35,SD*.5-35})
                    Place(Frame,{X,Y,Base+H*.5},{12,2,H},EERBlockMaterial::Wood);
            }
            else
            {
                const auto M=Floor%2 ? (Material==EERBlockMaterial::Brick ? EERBlockMaterial::Concrete:EERBlockMaterial::Brick):Material;
                for(float X:{-SW*.5+10,SW*.5-10})Place(Frame,{X,0,Base+H*.5},{20,SD-40,H},M);
                for(float Y:{-SD*.5+10,SD*.5-10})
                {
                    const float Gap=Floor ? 220:280;
                    for(float Sign:{-1.f,1.f})Place(Frame,{Sign*(SW+Gap)*.25,Y,Base+H*.5},{(SW-Gap)*.5,20,H},M);
                    if(Floor)Place(Frame,{0,Y,Base+50},{Gap,20,100},M);
                    const float Lintel=Floor ? 210:240;
                    Place(Frame,{0,Y,Base+(Lintel+H)*.5},{Gap,20,H-Lintel},M);
                }
            }
            if(LadderAccess)
            {
                // Alternate the hatch between storeys. Each ladder rests on a
                // solid lower deck and reaches every floor, not just the roof.
                const float HatchX=-SW*.5+160,HatchY=(Floor%2 ? 1.f:-1.f)*SD*.25;
                TimberPlate(Frame,-SW*.5,HatchX-80,-SD*.5,SD*.5,Top);
                TimberPlate(Frame,HatchX+80,SW*.5,-SD*.5,SD*.5,Top);
                TimberPlate(Frame,HatchX-80,HatchX+80,-SD*.5,HatchY-80,Top);
                TimberPlate(Frame,HatchX-80,HatchX+80,HatchY+80,SD*.5,Top);
                // Face the left-hand hatch toward the room, keeping the
                // Bank crossing through the two doors unobstructed below.
                const FTransform Inner(FRotator(0,180,0),FVector(HatchX+35,HatchY,Base));
                SteelLadder(Inner*Frame,FVector::ZeroVector,Stride);
            }
            else
            {
                TimberPlate(Frame,-SW*.5,StairX-110,-SD*.5,SD*.5,Top);
                TimberPlate(Frame,StairX+110,SW*.5,-SD*.5,SD*.5,Top);
                TimberPlate(Frame,StairX-110,StairX+110,-SD*.5,StairStart,Top);
                TimberPlate(Frame,StairX-110,StairX+110,StairEnd,SD*.5,Top);
                TimberFlight(Frame,StairX,StairStart,Floor,false);
            }
        }
        // Exterior steel ladder remains an alternative route on either type.
        const float LadderY=-SD*.25;
        SteelLadder(Frame,{SW*.5+35,LadderY,0},Floors*Stride);
        // The handles extend above the roof; rungs stop below its lip, leaving
        // a clear entry between the handles for descending from the roof.
        Place(Frame,{-SW*.5+1,0,Floors*Stride+50},{2,SD-4,100},EERBlockMaterial::Wood);
        for(const auto& Interval:{FVector2D(-SD*.5+2,LadderY-80),FVector2D(LadderY+80,SD*.5-2)})
            Place(Frame,{SW*.5-1,(Interval.X+Interval.Y)*.5,Floors*Stride+50},{2,Interval.Y-Interval.X,100},EERBlockMaterial::Wood);
        for(float Y:{-SD*.5+1,SD*.5-1})Place(Frame,{0,Y,Floors*Stride+50},{SW,2,100},EERBlockMaterial::Wood);
    }
    auto Fence=[&](float A,float B,float Y)
    {
        if(B<=A)return;
        const int32 N=FMath::CeilToInt((B-A)/400);const float Width=(B-A)/N;
        for(int32 I=0;I<N;++I)LowFence({A+(I+.5)*Width,Y,0},Width);
    };
    if(L.CrossingRule==EERCrossingRule::FencedCrossings)
    {
        // Ground approaches remain closed. Elevated routes may cross the low
        // separator; the plinth and steel remain fixed through destruction.
        Fence(-Half.X+12,-SideX-SideW*.5,0);
        Fence(-SideX+SideW*.5,-W*.5,0);
        for(float X:{-SideX,SideX})L.CrossingRoutes.Add({{X,-SideD*.5-150,100},{X,SideD*.5+150,100}});
        L.CrossingRoutes.Add({{SX,End+110,Roof+100},{-SX,-End-110,Roof+100}});
    }
    else
    {
        if(L.CrossingRule==EERCrossingRule::ClimbableWall)
        {
            // Brick can be breached; timber access can be cut down/collapsed.
            Box({-(Half.X-12+W*.5)*.5,0,150},{Half.X-12-W*.5,30,300},EERBlockMaterial::Brick);
            const float X=-W*.5-400;
            const FTransform Bridge(FVector(X,0,0));
            TimberPlate(Bridge,-100,100,-120,120,320);
            for(float PX:{-80.f,80.f})for(float PY:{-90.f,90.f})Place(Bridge,{PX,PY,159},{12,2,318},EERBlockMaterial::Wood);
            L.WallBridges={{X,0,320},{-X,0,320}};
            for(float Sign:{-1.f,1.f})
            {
                TimberFlight(FTransform(FRotator(0,Sign>0 ? 180:0,0),FVector(X,0,0)),0,-600,0,true);
                for(int32 Copy=0;Copy<2;++Copy)L.ClimbPoints.Add({Copy ? -X:X,Sign*700,100});
            }
            for(float WingX:{X,-X})L.CrossingRoutes.Add({{WingX,-70,420},{WingX,70,420}});
        }
        else
            for(float X:{CoreX-(W*.5+400),CoreX+W*.5+400})L.CrossingRoutes.Add({{X,-200,100},{X,200,100}});
        // Ground route doglegs through the opposing offset doors.
        const float DoorOffset=CoreX!=0 ? 0:300;
        L.CrossingRoutes.Add({{CoreX-DoorOffset,-D*.5-150,100},{CoreX-DoorOffset,-200,100},{CoreX+DoorOffset,200,100},{CoreX+DoorOffset,D*.5+150,100}});
    }
    // One original-height steel course on a fixed one-metre concrete plinth.
    // The physical boundary ends at the visible top; no taller invisible wall.
    Fence(-Half.X,Half.X,-Half.Y+6);
    const int32 Count=FMath::CeilToInt((L.ArenaSize.Y-24)/400);
    for(int32 I=0;I<Count;++I)
    {
        const float Length=(L.ArenaSize.Y-24)/Count;
        LowFence({-Half.X+6,-Half.Y+12+(I+.5)*Length,0},Length,false);
    }
    // Cover geometry is paired for balance; its cosmetic palette may differ.
    // Keep a clear rear spawn strip, side-building lanes and central approach.
    const float CargoApproach=T.SizingTeamSize>0 && L.ArenaSize.Y<5200 ? 800.f:1000.f;
    if(L.ArenaSize.Y>=4400)Container({CoreX!=0 ? -Half.X+700:T.SizingTeamSize>0 && T.SizingTeamSize<=4 ? 1100:-1100,-Half.Y+CargoApproach,140},{Pick(1000,1200),260,280},R.RandRange(-1,1)*15);
    // Reserve circulation before furnishing the yards. Cover is placed as
    // usable groups rather than isolated decorative props in one narrow row.
    for(int32 I=0;I<10;++I)L.AlphaSpawns.Add(FTransform(FRotator(0,90,0),FVector((CoreX!=0 ? 0:-850)+(I%5-2)*180,-Half.Y+220+(I/5)*200,100)));
    const float EndY=Half.Y-600;
    if(L.CrossingRule==EERCrossingRule::FencedCrossings)
        Patrol={{-SideX,-EndY,100},{-SideX,EndY,100},{SideX,EndY,100},{SideX,-EndY,100}};
    else
        Patrol={{-300,-EndY,100},{-300,-D*.5-150,100},{-300,-200,100},{300,200,100},{300,D*.5+150,100},{300,EndY,100},
                {200,EndY,100},{200,D*.5+150,100},{300,250,100},{-300,-250,100},{-200,-D*.5-150,100},{-200,-EndY,100}};
    if(CoreX!=0)Patrol={{-200,-EndY,100},{CoreX,-D*.5-150,100},{CoreX,-200,100},{CoreX,200,100},{CoreX,D*.5+150,100},{200,EndY,100},
        {-200,EndY,100},{CoreX,D*.5+150,100},{CoreX,250,100},{CoreX,-250,100},{CoreX,-D*.5-150,100},{200,-EndY,100}};
    TArray<FTransform> Occupied,Reserved;
    auto Frames=[&](int32 First,TArray<FTransform>& Out)
    {
        for(int32 I=First;I<Pieces.Num();++I)
        {
            const auto& P=Pieces[I];if(P.VisualOnly)continue;
            for(int32 Copy=0;Copy<(P.Mirrored ? 2:1);++Copy)
                Out.Add(FTransform(FRotator(0,P.RotationYaw+(Copy ? 180:0),0),Copy ? FVector(2*P.SymmetryCenter.X-P.Center.X,-P.Center.Y,P.Center.Z):P.Center,P.Size));
        }
    };
    Frames(0,Occupied);
    Reserved.Add(FTransform(FQuat::Identity,{CoreX,0,Roof*.5},{W+180,D+180,Roof+240}));
    for(const auto& S:L.SecondaryStructures)
        Reserved.Add(FTransform(S.Frame.GetRotation(),S.Frame.TransformPosition({0,0,S.Size.Z*.5}),S.Size+FVector(160,160,240)));
    for(const auto& Ladder:L.Ladders)
        Reserved.Add(FTransform(Ladder.Frame.GetRotation(),Ladder.Frame.TransformPosition({10,0,Ladder.Height*.5}),{320,180,Ladder.Height+370}));
    auto Overlap=[](const FTransform& A,const FTransform& B,float Margin)
    {
        const FVector SA=A.GetScale3D(),SB=B.GetScale3D();
        if(FMath::Abs(A.GetLocation().Z-B.GetLocation().Z)>=(SA.Z+SB.Z)*.5-.01f)return false;
        for(const FVector Axis:{A.GetRotation().GetAxisX(),A.GetRotation().GetAxisY(),B.GetRotation().GetAxisX(),B.GetRotation().GetAxisY()})
        {
            auto Radius=[&](const FTransform& F)
            {return .5*(F.GetScale3D().X*FMath::Abs(FVector::DotProduct(F.GetRotation().GetAxisX(),Axis))+F.GetScale3D().Y*FMath::Abs(FVector::DotProduct(F.GetRotation().GetAxisY(),Axis)));};
            if(FMath::Abs(FVector::DotProduct(A.GetLocation()-B.GetLocation(),Axis))>=Radius(A)+Radius(B)+Margin-.01f)return false;
        }
        return true;
    };
    auto Blocked=[](FVector A,FVector B,const FTransform& Solid,float HalfHeight)
    {
        const FBox Bounds=FBox(-Solid.GetScale3D()*.5,Solid.GetScale3D()*.5).ExpandBy({36,36,HalfHeight});
        A=Solid.InverseTransformPositionNoScale(A);B=Solid.InverseTransformPositionNoScale(B);
        return FMath::LineBoxIntersection(Bounds,A,B,B-A);
    };
    auto Cover=[&](FVector Center,float Yaw,int32 Kind)
    {
        const int32 First=Pieces.Num(),FirstContainer=L.Containers.Num();
        const FTransform Frame(FRotator(0,Yaw,0),Center);
        const float Span=Kind==8 ? 320.f:Kind==4 ? 350.f:Kind==5 ? 220.f:float(R.RandRange(7,10)*50);
        const float Height=Kind==5 ? 130.f:Kind==3 ? 140.f:Kind==4 ? 260.f:230.f;
        const float Depth=Kind==4 ? 220.f:Kind==5 ? 180.f:20.f;
        const auto Material=Kind==4 ? EERBlockMaterial::Metal:(Kind==1 || Kind==2 || Kind==5) ? EERBlockMaterial::Wood:Kind==7 ? EERBlockMaterial::Concrete:EERBlockMaterial::Brick;
        if(Kind==4)Container(Center+FVector(0,0,Height*.5),{Span,Depth,Height},Yaw);
        else
        {
            Place(Frame,{0,0,Height*.5},{Span,Depth,Height},Material);
            if(Kind<=2 || Kind==7)
                Place(Frame,{(Span-20)*.5,-110,Height*.5},{20,200,Height},Material);
            if(Kind==2)Place(Frame,{-(Span-20)*.5,-110,Height*.5},{20,200,Height},Material);
        }
        TArray<FTransform> Added;Frames(First,Added);bool Valid=true;
        for(int32 I=0;I<Added.Num() && Valid;++I)
        {
            const auto& F=Added[I];const auto Bounds=FBox(-F.GetScale3D()*.5,F.GetScale3D()*.5).TransformBy(FTransform(F.GetRotation(),F.GetLocation()));
            Valid=Bounds.Min.X>=-Half.X+140 && Bounds.Max.X<=Half.X-140 && Bounds.Min.Y>=-Half.Y+700 && Bounds.Max.Y<=Half.Y-700;
            for(const auto& O:Occupied)if(Valid && Overlap(F,O,80))Valid=false;
            for(const auto& O:Reserved)if(Valid && Overlap(F,O,0))Valid=false;
            for(int32 J=0;J<I && Valid;++J)if(Overlap(F,Added[J],0))Valid=false;
            for(int32 J=0;J<Patrol.Num() && Valid;++J)if(Blocked(Patrol[J],Patrol[(J+1)%Patrol.Num()],F,93))Valid=false;
            for(const auto& Site:L.CoverSites)if(Valid && (Blocked(Site.Entry,Site.Hide,F,Site.HalfHeight) || Blocked(Site.SightFrom,Site.SightTo,F,2)))Valid=false;
            for(const auto* Paths:{&L.CrossingRoutes,&L.StairRoutes})for(const auto& Path:*Paths)
                for(int32 J=1;J<Path.Num() && Valid;++J)if(Blocked(Path[J-1],Path[J],F,93))Valid=false;
        }
        TArray<FERLayoutCover> Sites;
        for(int32 Copy=0;Copy<2 && Valid;++Copy)
        {
            const FTransform Pair=Frame*FTransform(FRotator(0,Copy ? 180:0,0));
            const float HalfHeight=Height<160 ? 62.5f:92.5f,Eye=Height<160 ? 115.f:172.5f;
            const float HideY=Depth>30 ? -Depth*.5-90:-100;
            const FVector Hide=Pair.TransformPosition({0,HideY,HalfHeight+2.5f}),Entry=Pair.TransformPosition({0,HideY-180,HalfHeight+2.5f});
            for(const auto& F:Occupied)if(Blocked(Entry,Hide,F,HalfHeight))Valid=false;
            for(const auto& F:Added)if(Blocked(Entry,Hide,F,HalfHeight))Valid=false;
            const FVector SightFrom=Pair.TransformPosition({0,Depth*.5+150,172.5f}),SightTo=Pair.TransformPosition({0,HideY,Eye});
            for(const auto& F:Occupied)if(Blocked(SightFrom,SightTo,F,2))Valid=false;
            Sites.Add({Pair,{Span,Depth,Height},Hide,Entry,Pair.TransformPosition({0,Depth*.5+150,172.5f}),Pair.TransformPosition({0,HideY,Eye}),Material,HalfHeight});
        }
        if(!Valid){Pieces.SetNum(First);L.Containers.SetNum(FirstContainer);return false;}
        Occupied.Append(Added);L.CoverSites.Append(Sites);L.CoverAngles.Append({Yaw,Yaw+180});
        L.ModuleKinds.Add(Kind==4 ? EERTemplateModule::Cargo:Kind==5 ? EERTemplateModule::Crates:Kind==2 ? EERTemplateModule::UCover:EERTemplateModule::LCover);
        return true;
    };
    // Open yards need mid-flank screens in addition to the approach pockets.
    // These interrupt eye-height sightlines without sealing either ground crossing.
    if(L.CrossingRule==EERCrossingRule::OpenYard)
        for(float Sign:{-1.f,1.f})Cover({-W*.5-500,Sign*D*.25,0},0,8);
    for(float Sign:{-1.f,1.f})Cover({-W*.5-650,Sign*(D*.5+350),0},0,8);
    const float FrontStart=-Half.Y+1500,FrontEnd=-D*.5-500;
    const int32 Bands=FMath::Max(2,FMath::FloorToInt((FrontEnd-FrontStart)/550)+1);
    for(int32 Row=0;Row<Bands;++Row)
        for(double X:{-Half.X*.72,-Half.X*.46,-750.,750.,Half.X*.46,Half.X*.72})
        {
            const float Y=FMath::Lerp(FrontStart,FrontEnd,float(Row)/(Bands-1));
            const int32 Kind=R.RandRange(0,7);const float Yaw=R.RandRange(-2,2)*15.f;
            if(!Cover({X,Y,0},Yaw,Kind))Cover({X,Y,0},0,8);
        }
    // PCGEx's path representation is shared with the authoring preview. Reject
    // degenerate paths before they can become patrol or crossing instructions.
    for(const auto& Route:L.CrossingRoutes)
    {
        TArray<FTransform> Transforms;for(const auto& P:Route)Transforms.Add(FTransform(P));
        const PCGExPaths::FPath Path(TConstPCGValueRange<FTransform>(MakeConstStridedView(Transforms)),false);
        if(Path.TotalLength<100 || !FMath::IsFinite(Path.TotalLength))return false;
        for(const auto& Edge:Path.Edges)if(!Path.IsEdgeValid(Edge))return false;
    }
    return Pieces.Num()<=1536;
}
}

UPCGDataAsset* UERMapTemplate::CreatePCGPreview(int32 Seed) const
{
    FERProceduralConfig C;C.TemplateAsset=const_cast<UERMapTemplate*>(this);C.Seed=Seed;
    FERProceduralLayout L;if(!ERProcedural::Build(C,FERBlockRoomConfig(),L))return nullptr;
    auto* Asset=NewObject<UPCGDataAsset>();
    auto Add=[&](FName Pin,const TArray<FTransform>& Transforms,int32 Route)
    {
        auto* Data=NewObject<UPCGPointData>(Asset);
        auto* Id=Data->Metadata->CreateAttribute<int32>(TEXT("RouteId"),Route,false,true);
        auto& Points=Data->GetMutablePoints();
        for(int32 I=0;I<Transforms.Num();++I)
        {
            auto& P=Points.AddDefaulted_GetRef();P.Transform=Transforms[I];P.Seed=int32(HashCombine(uint32(Seed),uint32(I)));
            P.MetadataEntry=Data->Metadata->AddEntry();Id->SetValue(P.MetadataEntry,Route);
        }
        auto& Tagged=Asset->Data.TaggedData.AddDefaulted_GetRef();Tagged.Data=Data;Tagged.Pin=Pin;Tagged.Tags.Add(Pin.ToString());
    };
    Add(TEXT("AlphaSpawns"),L.AlphaSpawns,-1);Add(TEXT("OmegaSpawns"),L.OmegaSpawns,-1);
    for(int32 I=0;I<L.CrossingRoutes.Num();++I)
    {TArray<FTransform> Path;for(const auto& P:L.CrossingRoutes[I])Path.Add(FTransform(P));Add(TEXT("Crossings"),Path,I);}
    TArray<FTransform> Fixed;for(const auto& P:L.FixedPieces)if(!P.VisualOnly)Fixed.Add(P.Transform);
    Add(TEXT("FixedVolumes"),Fixed,-1);
    TArray<FTransform> Cells;
    for(const auto& P:L.Panels)Cells.Add(FTransform(P.Transform.GetRotation(),P.Transform.TransformPosition(P.Size*.5),P.Size/100));
    Add(TEXT("CellVolumes"),Cells,-1);
    TArray<FTransform> Cover;for(const auto& Site:L.CoverSites)Cover.Add(FTransform(Site.Frame.GetRotation(),Site.Hide));
    Add(TEXT("CoverPockets"),Cover,-1);
    return Asset;
}

UPCGDataAsset* UERMapTemplate::SavePCGPreview(const FString& PackageName,int32 Seed) const
{
#if WITH_EDITOR
    if(!PackageName.StartsWith(TEXT("/Game/EclipseRecoil/Art/MapTemplates/IndustrialPCG/")) || !FPackageName::IsValidLongPackageName(PackageName) || FPackageName::DoesPackageExist(PackageName))return nullptr;
    auto* Source=CreatePCGPreview(Seed);if(!Source)return nullptr;
    auto* Package=CreatePackage(*PackageName);
    auto* Saved=DuplicateObject<UPCGDataAsset>(Source,Package,*FPackageName::GetLongPackageAssetName(PackageName));
    Saved->SetFlags(RF_Public|RF_Standalone);FAssetRegistryModule::AssetCreated(Saved);Package->MarkPackageDirty();
    FSavePackageArgs Args;Args.TopLevelFlags=RF_Public|RF_Standalone;Args.SaveFlags=SAVE_NoError;
    const FString Filename=FPackageName::LongPackageNameToFilename(PackageName,FPackageName::GetAssetPackageExtension());
    return UPackage::SavePackage(Package,Saved,*Filename,Args) ? Saved:nullptr;
#else
    return nullptr;
#endif
}

bool UERMapTemplate::DeleteArchivedProceduralLab()
{
#if WITH_EDITOR
    // UE's forced World deletion leaves this legacy package behind. Follow
    // the project's existing unloaded-package cleanup path for this exact map.
    const FString Name=TEXT("/Game/EclipseRecoil/Maps/L_ProceduralLab");
    if(!IFileManager::Get().FileExists(*(FPaths::ProjectSavedDir()/TEXT("MapRefactorBackup/2026-10-19/receipt.json"))) || FindPackage(nullptr,*Name))return false;
    auto& Registry=FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
    TArray<FName> References;Registry.GetReferencers(FName(*Name),References,UE::AssetRegistry::EDependencyCategory::Package|UE::AssetRegistry::EDependencyCategory::Manage);
    if(!References.IsEmpty())return false;
    FString Filename;if(!FPackageName::DoesPackageExist(Name,&Filename))return false;
    if(!IFileManager::Get().Delete(*Filename,false,true))return false;
    Registry.PackageDeleted(CreatePackage(*Name));return true;
#else
    return false;
#endif
}
