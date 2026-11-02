#include "ERProceduralMap.h"
#include "ERIndustrialFence.h"

namespace ERProcedural
{
bool BuildCircularCentralBuilding(const UERMapTemplate& T,int32 Seed,TArray<FERMapTemplatePiece>& Pieces,FERProceduralLayout& L,TArray<FVector>& Patrol)
{
    const auto& V=T.CentralBuilding;
    if(V.MinFloors<1 || V.MaxFloors>3 || V.MinFloors>V.MaxFloors || T.RoofMesh || T.ShellWallMesh)return false;
    for(const auto* Palette:{&V.ConcreteSurfaces,&V.BrickSurfaces,&V.WoodSurfaces,&V.GroundSurfaces,&V.ContainerSurfaces})
        if(Palette->Num()>8 || Palette->Contains(nullptr))return false;
    FRandomStream Geometry(Seed);
    L.CircularArena=true;L.GradedTerrain=true;L.LayoutSymmetry=EERTemplateSymmetry::Mirror;
    const float Diameter=T.SizingTeamSize>0 ? T.SizingArena.X:7600;
    if(!FMath::IsFinite(Diameter) || Diameter<3500 || Diameter>15000)return false;
    const float Radius=Diameter*.5;
    L.ArenaSize={Diameter,Diameter,2000};L.CrossingRule=EERCrossingRule::OpenYard;
    L.CentralHill=(HashCombineFast(uint32(Seed),0x48494c4cu)&1)!=0;
    const float W=1800,D=2200,Stride=320;
    const float SideRoom=FMath::Max(0.f,float(FMath::Sqrt(FMath::Max(0.f,FMath::Square(Radius-100)-FMath::Square(D*.5f+100)))-W*.5f-100));
    const float BuildingX=L.CentralHill ? 0:-FMath::Min3(Radius*.32f,1500.f,SideRoom);
    L.BuildingFloors=Geometry.RandRange(V.MinFloors,V.MaxFloors);
    L.CentralBuildingSize={W,D,L.BuildingFloors*Stride};
    L.HillHeight=FMath::Min(320.f,Radius*.065f);L.HillFalloff=FMath::Max(650.f,Radius*.38f);
    L.HillPlateau=Radius*.10f;
    const float HillX=FMath::Min(Radius-250,FMath::Max(Radius*.48f,BuildingX+W*.5f+180+L.HillPlateau+200));
    L.HillCenter={L.CentralHill ? 0:HillX,0,0};
    const float Base=L.CentralHill ? L.HillHeight:0;
    L.CentralBuildingCenter={BuildingX,0,Base};
    auto Surface=[&](const TArray<TObjectPtr<UMaterialInterface>>& Palette,uint32 Salt)->UMaterialInterface*
    {return Palette.IsEmpty() ? nullptr:Palette[HashCombineFast(uint32(Seed),Salt)%Palette.Num()].Get();};
    L.GroundSurface=Surface(V.GroundSurfaces,0x47524f55u);
    const auto MainMaterial=Geometry.RandRange(0,1)!=0 ? EERBlockMaterial::Brick:EERBlockMaterial::Concrete;
    auto Box=[&](FVector Center,FVector Size,EERBlockMaterial Material,bool Fixed=false,bool Pair=true,bool Deck=false)->FERMapTemplatePiece&
    {
        auto& P=Pieces.AddDefaulted_GetRef();P.Center=Center;P.Size=Size;P.Material=Material;P.Destructible=!Fixed;P.Mirrored=Pair;P.WalkableSurface=Deck;
        auto* Finish=Material==EERBlockMaterial::Metal ? (Deck && V.DeckSurface ? V.DeckSurface.Get():V.SteelSurface.Get()):
            Material==EERBlockMaterial::Brick ? Surface(V.BrickSurfaces,0x42524943u):Material==EERBlockMaterial::Wood ? Surface(V.WoodSurfaces,0x574f4f44u):Surface(V.ConcreteSurfaces,0x434f4e43u);
        if(Fixed)P.FixedSurface=Finish;else P.CellSurface=Finish;
        return P;
    };
    auto BuildingBox=[&](FVector P,FVector Size,EERBlockMaterial Material,bool Fixed=false,bool Pair=true,bool Deck=false)->FERMapTemplatePiece&
    {
        auto& Piece=Box(P+L.CentralBuildingCenter,Size,Material,Fixed,Pair,Deck);
        const float Bottom=P.Z-Size.Z*.5f;
        Piece.Anchored=Bottom>0 && FMath::IsNearlyZero(FMath::Fmod(Bottom,Stride),.01f);
        return Piece;
    };
    auto Slab=[&](float X0,float X1,float Y0,float Y1,float Z)
    {BuildingBox({(X0+X1)*.5f,(Y0+Y1)*.5f,Z-10},{X1-X0,Y1-Y0,20},EERBlockMaterial::Metal,true,true,true);};
    // Build the negative-Y half once. Every facade, stair and opening is reflected.
    for(int32 Floor=0;Floor<L.BuildingFloors;++Floor)
    {
        const float Z=Floor*Stride;
        auto Facade=[&](float A,float B,float Lo,float Hi)
        {if(B>A && Hi>Lo)BuildingBox({(A+B)*.5f,-D*.5f+10,Z+(Lo+Hi)*.5f},{B-A,20,Hi-Lo},MainMaterial);};
        float Cursor=-W*.5f;
        for(float X:{-550.f,0.f,550.f})
        {
            const float HalfWidth=X==0 ? 140:90,Sill=X==0 ? 0:90;
            Facade(Cursor,X-HalfWidth,0,300);Facade(X-HalfWidth,X+HalfWidth,0,Sill);Facade(X-HalfWidth,X+HalfWidth,240,300);Cursor=X+HalfWidth;
        }
        Facade(Cursor,W*.5f,0,300);
        for(float X:{-W*.5f+10,W*.5f-10})
        {
            BuildingBox({X,-D*.25f,Z+150},{20,D*.5f-40,300},MainMaterial);
            // The short seam segment is centered and generated only once.
            BuildingBox({X,0,Z+150},{20,40,300},MainMaterial,false,false);
        }
        if(Floor>0)
        {
            Slab(-250,250,-D*.5f-180,-D*.5f,Z);
            BuildingBox({0,-D*.5f-170,Z+50},{500,20,100},MainMaterial);
            for(float X:{-240.f,240.f})BuildingBox({X,-D*.5f-85,Z+50},{20,150,100},MainMaterial);
        }
        // Two room wings, with open doorways onto the central hall.
        for(float X:{-350.f,350.f})
        {
            BuildingBox({X,-665,Z+150},{15,830,300},MainMaterial);
            BuildingBox({X,-15,Z+150},{15,30,300},MainMaterial);
            BuildingBox({X,-140,Z+270},{15,220,60},MainMaterial);
        }
        // A full-height landing at Y=0 joins the mirrored stairwells.
        Slab(-W*.5f,W*.5f,-80,0,Z+Stride);
        Slab(-W*.5f+290,W*.5f,-D*.5f,-80,Z+Stride);
        Slab(-W*.5f,-W*.5f+70,-D*.5f,-80,Z+Stride);
        Slab(-W*.5f+70,-W*.5f+290,-D*.5f,-950,Z+Stride);
        Slab(-W*.5f+70,-W*.5f+290,-470,-80,Z+Stride);
        TArray<FVector> Stair{{BuildingX-W*.5f+180,-1050,Base+Z+100}};
        for(int32 Step=0;Step<16;++Step)
        {
            const float Y=-950+(Step+.5f)*30,Top=(Step+1)*20;
            BuildingBox({-W*.5f+180,Y,Z+Top-10},{180,30,20},EERBlockMaterial::Metal,true,true,true);
            Stair.Add({BuildingX-W*.5f+180,Y,Base+Z+Top+120});
        }
        Stair.Add({BuildingX-W*.5f+180,-370,Base+Z+Stride+100});L.StairRoutes.Add(Stair);
        for(auto& P:Stair)P.Y=-P.Y;L.StairRoutes.Add(Stair);
        L.RoomAccessRoutes.Add({{BuildingX,-D*.5f-100,Base+Z+100},{BuildingX,-D*.5f+180,Base+Z+100}});
        auto Door=L.RoomAccessRoutes.Last();for(auto& P:Door)P.Y=-P.Y;L.RoomAccessRoutes.Add(Door);
    }
    const float Roof=L.BuildingFloors*Stride;
    BuildingBox({0,-D*.5f+10,Roof+50},{W,20,100},MainMaterial);
    for(float X:{-W*.5f+10,W*.5f-10})BuildingBox({X,-D*.25f,Roof+50},{20,D*.5f-40,100},MainMaterial);
    // A regular 128-sided perimeter follows the circular terrain. Adjacent
    // segments stop at their mitres, so collision never overlaps at corners.
    constexpr int32 BoundarySides=128;
    const float BoundaryRadius=Radius-20;
    for(int32 I=0;I<BoundarySides/2;++I)
    {
        const double A=-PI+(I+.5)*2*PI/BoundarySides;
        const float Length=2*BoundaryRadius*FMath::Tan(PI/BoundarySides)-.5f;
        const FVector2D P(FMath::Cos(A)*BoundaryRadius,FMath::Sin(A)*BoundaryRadius);
        const float H=CentralGroundHeight(L,P);
        auto& Plinth=Box(FVector(P,H+50),{Length,12,100},EERBlockMaterial::Concrete,true);
        Plinth.RotationYaw=FMath::RadiansToDegrees(A)+90;
        auto& Fence=Box(FVector(P,H+100+ERIndustrialFence::ModuleHeightCm*.5f),{Length,12,ERIndustrialFence::ModuleHeightCm},EERBlockMaterial::Metal,true);
        Fence.RotationYaw=FMath::RadiansToDegrees(A)+90;Fence.PawnBarrier=true;
    }
    const float FlankX=L.CentralHill ? W*.5f+350:Radius*.48f;
    // Freestanding paired cover along both flanks; avoid doors, stairs, spawns
    // and the hilltop. Pad heights are fixed before collision is emitted.
    for(int32 Band=0;Band<2;++Band)for(float Sign:{-1.f,1.f})
    {
        const FVector2D P(Sign*Radius*(.55f+Band*.20f),-Radius*(.55f-Band*.35f));
        const float OutsideX=FMath::Abs(P.X-BuildingX)-W*.5f,OutsideY=FMath::Abs(P.Y)-D*.5f;
        if((OutsideX<450 && OutsideY<450) || P.Size()>Radius-500 || FMath::Abs(P.X-FlankX)<600)continue;
        const float H=CentralGroundHeight(L,P);
        for(float PairSign:{-1.f,1.f})L.TerrainPads.Add(FBox(FVector(P.X,PairSign*P.Y,H)-FVector(400,300,0),FVector(P.X,PairSign*P.Y,H)+FVector(400,300,0)));
        const float Height=Geometry.RandRange(0,1)!=0 ? 150.f:180.f;
        Box(FVector(P,H+Height*.5f),{360,40,Height},MainMaterial);
        const float HalfHeight=Height<160 ? 62.5f:92.5f,Eye=Height<160 ? 115.f:172.5f;
        FERLayoutCover Cover;Cover.Frame=FTransform(FVector(P,H));Cover.Size={360,40,Height};Cover.Material=MainMaterial;Cover.HalfHeight=HalfHeight;
        Cover.Hide=FVector(P+FVector2D(0,-80),H+HalfHeight+2.5f);Cover.Entry=Cover.Hide+FVector(220,0,0);
        Cover.SightFrom=FVector(P+FVector2D(0,150),H+172.5f);Cover.SightTo=FVector(P+FVector2D(0,-80),H+Eye);
        L.CoverSites.Add(Cover);Cover.Frame=FTransform(FRotator(0,180,0),FVector(P.X,-P.Y,H));
        for(auto* Point:{&Cover.Hide,&Cover.Entry,&Cover.SightFrom,&Cover.SightTo})Point->Y=-Point->Y;
        L.CoverSites.Add(Cover);
    }
    for(auto& Cover:L.CoverSites)
    {
        Cover.Hide.Z=CentralGroundHeight(L,FVector2D(Cover.Hide))+Cover.HalfHeight+2.5f;
        Cover.Entry.Z=CentralGroundHeight(L,FVector2D(Cover.Entry))+Cover.HalfHeight+2.5f;
        Cover.SightFrom.Z=CentralGroundHeight(L,FVector2D(Cover.SightFrom))+172.5f;
        Cover.SightTo.Z=CentralGroundHeight(L,FVector2D(Cover.SightTo))+(Cover.HalfHeight<90 ? 115.f:172.5f);
    }
    // Opaque, destructible spawn screens stop every initial base-to-base ray.
    // Both side exits remain open; routes dogleg around the same mirrored screen.
    const float ScreenY=FMath::Min(-Radius+700,-D*.5f-180),ScreenHalf=500;
    const float ScreenGround=CentralGroundHeight(L,{0,ScreenY});
    Box({0,ScreenY,ScreenGround+150},{ScreenHalf*2,20,300},MainMaterial);
    L.FeatureNames.Add(TEXT("ScreenedSpawns"));
    // Wide rear strips stay flat and clear for all ten starts on each team.
    const float SpawnY=-Radius+250;
    for(int32 I=0;I<10;++I)
    {
        const FVector2D P((I%5-2)*150,SpawnY+(I/5)*160);
        L.AlphaSpawns.Add(FTransform(FRotator(0,90,0),FVector(P,CentralGroundHeight(L,P)+100)));
    }
    // Routes sample the actual terrain triangles rather than an ideal hill.
    auto GroundPath=[&](FVector2D A,FVector2D B)
    {
        TArray<FVector> Path;const int32 Steps=FMath::CeilToInt32(FVector2D::Distance(A,B)/60);
        for(int32 I=0;I<=Steps;++I){const auto P=FMath::Lerp(A,B,float(I)/Steps);Path.Add(FVector(P,CentralGroundHeight(L,P)+100));}return Path;
    };
    const float BypassX=-ScreenHalf-150;
    const TArray<FVector2D> Approach{{BuildingX,ScreenY-100},{BypassX,ScreenY-100},{BypassX,ScreenY+90},{BuildingX,ScreenY+90},{BuildingX,-D*.5f+180}};
    TArray<FVector> MainRoute;
    auto AppendLeg=[&](FVector2D A,FVector2D B)
    {auto Leg=GroundPath(A,B);if(!MainRoute.IsEmpty())Leg.RemoveAt(0);MainRoute.Append(Leg);};
    for(int32 I=1;I<Approach.Num();++I)if(!Approach[I-1].Equals(Approach[I],.01))AppendLeg(Approach[I-1],Approach[I]);
    AppendLeg(Approach.Last(),{Approach.Last().X,-Approach.Last().Y});
    for(int32 I=Approach.Num()-1;I>0;--I)if(!Approach[I].Equals(Approach[I-1],.01))
        AppendLeg({Approach[I].X,-Approach[I].Y},{Approach[I-1].X,-Approach[I-1].Y});
    L.CrossingRoutes.Add(MainRoute);
    L.CrossingRoutes.Add(GroundPath({FlankX,-Radius*.55f},{FlankX,Radius*.55f}));
    Patrol=MainRoute;
    for(int32 I=Patrol.Num()-2;I>0;--I){const FVector Point=Patrol[I];Patrol.Add(Point);}
    L.FeatureNames.Add(L.CentralHill ? TEXT("CentralHill"):TEXT("OpposedHill"));
    return true;
}
}
