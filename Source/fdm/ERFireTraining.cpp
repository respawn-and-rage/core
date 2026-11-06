#include "ERMapTemplate.h"
#include "ERProceduralMap.h"
#include "ERIndustrialFence.h"
#include "Engine/StaticMesh.h"

namespace
{
// 50cm plan raster: budgets use the union of projections, never a per-level sum.
struct FERPlanGrid
{
    FVector2D Min=FVector2D::ZeroVector;
    int32 NX=0,NY=0;
    static constexpr float Cell=50;
    TArray<uint8> Bits;
    void Init(FVector2D InMin,FVector2D InMax)
    {
        Min=InMin;NX=FMath::Max(1,FMath::RoundToInt((InMax.X-InMin.X)/Cell));NY=FMath::Max(1,FMath::RoundToInt((InMax.Y-InMin.Y)/Cell));
        Bits.Init(0,NX*NY);
    }
    void Add(const FBox& B,uint8 Mask)
    {
        for(int32 Y=0;Y<NY;++Y)for(int32 X=0;X<NX;++X)
        {
            const FVector2D P=Min+FVector2D((X+.5f)*Cell,(Y+.5f)*Cell);
            if(P.X>B.Min.X && P.X<B.Max.X && P.Y>B.Min.Y && P.Y<B.Max.Y)Bits[Y*NX+X]|=Mask;
        }
    }
    float Ratio(uint8 Mask) const
    {
        int32 Count=0;for(uint8 B:Bits)Count+=(B&Mask)!=0;
        return float(Count)/Bits.Num();
    }
    // Largest region open to the roof (cells without Mask), as a fraction of the
    // plan. Narrow walkways (Barrier unset) leave the volume above them joined.
    float LargestOpen(uint8 Mask,uint8 Barrier) const
    {
        TArray<uint8> Seen;Seen.Init(0,Bits.Num());int32 Best=0;TArray<int32> Stack;
        for(int32 Start=0;Start<Bits.Num();++Start)
        {
            if(Seen[Start] || (Bits[Start]&Barrier))continue;
            int32 Size=0;Stack.Reset();Stack.Add(Start);Seen[Start]=1;
            while(!Stack.IsEmpty())
            {
                const int32 I=Stack.Pop();Size+=(Bits[I]&Mask)==0;const int32 X=I%NX,Y=I/NX;
                for(const FIntPoint D:{FIntPoint(1,0),FIntPoint(-1,0),FIntPoint(0,1),FIntPoint(0,-1)})
                {
                    const int32 NXp=X+D.X,NYp=Y+D.Y;if(NXp<0 || NYp<0 || NXp>=NX || NYp>=NY)continue;
                    const int32 N=NYp*NX+NXp;if(!Seen[N] && !(Bits[N]&Barrier)){Seen[N]=1;Stack.Add(N);}
                }
            }
            Best=FMath::Max(Best,Size);
        }
        return float(Best)/Bits.Num();
    }
};
FBox HalfTurn(const FBox& B){return FBox(FVector(-B.Max.X,-B.Max.Y,B.Min.Z),FVector(-B.Min.X,-B.Min.Y,B.Max.Z));}
}

namespace ERProcedural
{
bool BuildFireTraining(const UERMapTemplate& T,int32 Seed,int32 Attempt,TArray<FERMapTemplatePiece>& Pieces,FERProceduralLayout& L,TArray<FVector>& Patrol)
{
    const auto& V=T.FireTraining;
    auto Fail=[&](const TCHAR* Stage){UE_LOG(LogTemp,Warning,TEXT("ER_FIRE_BUILD_REJECT seed=%d attempt=%d stage=%s"),Seed,Attempt,Stage);return false;};
    auto Ratio=[](float X){return FMath::IsFinite(X) && X>0 && X<=1;};
    if(T.Symmetry!=EERTemplateSymmetry::HalfTurn || V.ContinuousUpperFloors || V.FloorArea.ContainsNaN() || V.FloorArea.X<300 || V.FloorArea.Y>26400 || V.FloorArea.X>V.FloorArea.Y ||
        !FMath::IsFinite(V.MinCeilingHeight) || !FMath::IsFinite(V.MaxCeilingHeight) || V.MinCeilingHeight<700 || V.MaxCeilingHeight>1800 || V.MinCeilingHeight>V.MaxCeilingHeight || FMath::CeilToInt32(V.MinCeilingHeight/50)>FMath::FloorToInt32(V.MaxCeilingHeight/50) ||
        !FMath::IsFinite(V.MaxAspect) || V.MaxAspect<1 || V.MaxPlazas<0 || V.MaxPlazas>3 ||
        !Ratio(V.ElevatedTargetRatio) || !Ratio(V.ElevatedMaxRatio) || !Ratio(V.OverheadMaxRatio) || !Ratio(V.OpenRegionMinRatio) ||
        V.ElevatedTargetRatio>V.ElevatedMaxRatio || V.ElevatedMaxRatio>V.OverheadMaxRatio)return Fail(TEXT("recipe"));
    for(const auto* Palette:{&V.WoodSurfaces,&V.ConcreteSurfaces,&V.ContainerSurfaces})if(Palette->Num()>8 || Palette->Contains(nullptr))return Fail(TEXT("palette"));
    const bool Reference=Attempt==FireTrainingAttempts-1;
    FRandomStream R(Reference ? 0x46495245:int32(HashCombine(uint32(Seed),uint32(Attempt)*0x9E3779B9u)));
    auto Pick=[&](float A,float B){return float(R.RandRange(FMath::CeilToInt32(A/50),FMath::FloorToInt32(B/50))*50);};
    auto Chance=[&](float P){return !Reference && R.FRand()<P;};
    const float Target=T.SizingTeamSize>0 || Reference ? (V.FloorArea.X+V.FloorArea.Y)*.5f:R.FRandRange(V.FloorArea.X,V.FloorArea.Y);
    const float Aspect=FMath::Min(V.MaxAspect,1.5f);
    TArray<FVector2D> Options;
    for(float Width=3200;Width<=20000;Width+=100)
    {
        const float Depth=FMath::GridSnap(Target*10000/(Width+60)-80,100.f);
        const float Area=(Width+60)*(Depth+80)/10000;
        if(Depth+80>=(Width+60)*1.12f && Depth>=3800 && FMath::Max(Width+60,Depth+80)<=FMath::Min(Width+60,Depth+80)*Aspect && Area>=V.FloorArea.X && Area<=V.FloorArea.Y)Options.Add({Width,Depth});
    }
    if(Options.IsEmpty())return Fail(TEXT("dimensions"));
    const auto Dimensions=Options[Reference ? Options.Num()/2:R.RandRange(0,Options.Num()-1)];
    const float W=Dimensions.X,Length=Dimensions.Y,HL=Length*.5f;
    const float Eave=Reference ? FMath::Clamp(900.f,V.MinCeilingHeight,V.MaxCeilingHeight):Pick(V.MinCeilingHeight,V.MaxCeilingHeight);
    const int32 Bands=FMath::FloorToInt32(Eave/300); // cargo headroom classification, not wall modules
    L.ArenaSize={W+60,Length+80,Eave+20};
    L.WarehouseBays=0;L.WarehouseBands=0;L.WarehouseNaves=0;L.WarehouseNaveWidth=0;
    L.WarehouseWidth=W;L.WarehouseLength=Length;L.IndoorCeiling=Eave;L.LayoutAttempt=Attempt;
    L.GroundSurface=V.GroundSurface;
    auto Surface=[&](const TArray<TObjectPtr<UMaterialInterface>>& Palette,uint32 Salt)->UMaterialInterface*
    {return Palette.IsEmpty() ? nullptr:Palette[HashCombineFast(uint32(Seed),Salt)%Palette.Num()].Get();};
    auto* Wood=Surface(V.WoodSurfaces,0x574f4f44u);
    auto* Concrete=Surface(V.ConcreteSurfaces,0x434f4e43u);
    Pieces.Reset();L.AlphaSpawns.Reset();L.OmegaSpawns.Reset();Patrol.Reset();
    auto Box=[&](FVector P,FVector Size,EERBlockMaterial M,bool Fixed,bool Deck=false)->FERMapTemplatePiece&
    {
        auto& B=Pieces.AddDefaulted_GetRef();B.Center=P;B.Size=Size;B.Material=M;B.Destructible=!Fixed;B.Mirrored=true;B.WalkableSurface=Deck;
        auto* Finish=M==EERBlockMaterial::Metal ? (Fixed && Deck && V.DeckSurface ? V.DeckSurface.Get():V.SteelSurface.Get()):M==EERBlockMaterial::Wood ? Wood:M==EERBlockMaterial::Concrete ? Concrete:nullptr;
        if(Fixed)B.FixedSurface=Finish;else B.CellSurface=Finish;
        return B;
    };
    // Closed visible shell: collision comes from these same procedural solids.
    const float XO=-W*.5f,XI0=XO+20,XI1=-XO-20;
    Box({XO+10,0,Eave*.5f},{20,Length,Eave},EERBlockMaterial::Concrete,true);
    Box({0,-HL+10,Eave*.5f},{W-40,20,Eave},EERBlockMaterial::Concrete,true);
    auto& Roof=Box({0,0,Eave+10},{W,Length,20},EERBlockMaterial::Metal,true);
    Roof.Mirrored=false;Roof.Roof=true;
    // ---- Plan: west catwalk band, two lanes around central cargo, east rooms.
    const float Dw=Reference ? 220:R.RandRange(0,2)*40.f+180,DeckZ=300,CatwalkX=XI0+(Reference ? 100:Pick(100,400));
    const float CatwalkZ=Reference ? FMath::Min(500.f,Eave-250):Pick(350,Eave-250);
    L.WarehouseCatwalkVariant=Reference ? 0:R.RandRange(0,5);
    const float Dr=W<=1600 ? 400:W<=2000 ? Pick(450,500):Pick(500,600);
    const float XR0=XI1-Dr,XWL=XI0+Dw+170,XEL=XR0-170,CB0=XWL+150,CB1=XEL-150;
    const float YFront=-HL+1350;
    const bool Elevated=Eave>=DeckZ+250;
    FERPlanGrid Grid;Grid.Init({XI0,-HL},{XI1,HL});
    FERPlanGrid Trial;
    // Bits: 1 walkable above ground, 2 any overhead closure, 4 a wide closure.
    auto Mask=[](const FBox& B){return uint8(3|(FMath::Min(B.GetSize().X,B.GetSize().Y)>300 ? 4:0));};
    auto Fits=[&](const TArray<FBox>& Boxes)
    {
        Trial=Grid;for(const auto& B:Boxes){Trial.Add(B,Mask(B));Trial.Add(HalfTurn(B),Mask(B));}
        return Trial.Ratio(1)<=V.ElevatedMaxRatio+1e-4f && Trial.Ratio(2)<=V.OverheadMaxRatio+1e-4f && Trial.LargestOpen(2,4)>=V.OpenRegionMinRatio;
    };
    // Below the elevated target, optional structures become more likely.
    auto Wanted=[&](float Below,float Above){return Chance(Grid.Ratio(1)<V.ElevatedTargetRatio ? Below:Above);};
    auto Elevate=[&](const FBox& B,int32 Accesses)
    {
        for(const FBox& Copy:{B,HalfTurn(B)}){L.ElevatedDecks.Add(Copy);L.OverheadClosures.Add(Copy);L.ElevatedAccesses.Add(Accesses);}
        Grid.Add(B,Mask(B));Grid.Add(HalfTurn(B),Mask(B));
    };
    auto Headroom=[&](float X0,float X1,float Top,float Clearance=250){return Eave>=Top+Clearance;};
    auto Rail=[&](float X0,float X1,float Y0,float Y1,float Z)
    {
        if(X1-X0>1 && Y1-Y0>1)
        {
            const float H=ERIndustrialFence::ModuleHeightCm;
            Box({(X0+X1)*.5f,(Y0+Y1)*.5f,Z+H*.5f},{X1-X0,Y1-Y0,H},EERBlockMaterial::Metal,true).PawnBarrier=true;
        }
    };
    auto Slab=[&](float X0,float X1,float Y0,float Y1,float Z){Box({(X0+X1)*.5f,(Y0+Y1)*.5f,Z-10},{X1-X0,Y1-Y0,20},EERBlockMaterial::Metal,true,true);};
    // Destructible timber with door/window gaps; headers depend on their jambs.
    struct FOpening{float Center,Width,Sill,Top;};
    auto Timber=[&](float X0,float Y0,bool AlongY,float Span,float Base,float Height,TArray<FOpening> Openings)
    {
        Openings.Sort([](const FOpening& A,const FOpening& B){return A.Center<B.Center;});
        auto Part=[&](float A,float B,float Z0,float Z1)
        {
            if(B-A<5 || Z1-Z0<5)return;
            const FVector Size=AlongY ? FVector(12,B-A,Z1-Z0):FVector(B-A,12,Z1-Z0);
            const FVector Center=AlongY ? FVector(X0+6,Y0+(A+B)*.5f,Base+(Z0+Z1)*.5f):FVector(X0+(A+B)*.5f,Y0+6,Base+(Z0+Z1)*.5f);
            Box(Center,Size,EERBlockMaterial::Wood,false).Anchored=Base>0 && Z0<1;
        };
        float Cursor=0;
        for(const auto& O:Openings)
        {
            const float A=O.Center-O.Width*.5f,B=O.Center+O.Width*.5f;
            Part(Cursor,A,0,Height);Part(A,B,0,O.Sill);Part(A,B,O.Top,Height);Cursor=B;
        }
        Part(Cursor,Span,0,Height);
    };
    // Calibrated 2mm destructible steel, exactly as the industrial ladders.
    auto Ladder=[&](FVector Base,float Yaw,float Rise,bool Anchored,bool LevelExit=true)
    {
        const FTransform Frame(FRotator(0,Yaw,0),Base);const float Height=Rise+110;
        auto Part=[&](FVector P,FVector Size,bool Root){auto& B=Box(Frame.TransformPosition(P),Size,EERBlockMaterial::Metal,false);B.RotationYaw=Yaw;B.Anchored=Root && Anchored;};
        for(float Y:{-42.f,42.f})
        {
            for(float X:{-3.9f,3.9f})Part({X,Y,Height*.5f},{.2f,8,Height},true);
            for(float Edge:{-3.9f,3.9f})Part({0,Y+Edge,Height*.5f},{7.6f,.2f,Height},true);
        }
        for(float Z=25;Z<Rise;Z+=25)Part({0,0,Z},{.2f,76,4},false);
        // Notched high walkway ends allow stepping off at deck level instead of
        // climbing 110cm over a parapet into the lights. Keep the real handles.
        const float ExitHeight=LevelExit ? Rise+5:Height;
        L.Ladders.Add({Frame,ExitHeight,Rise});L.Ladders.Add({Frame*FTransform(FRotator(0,180,0)),ExitHeight,Rise});
    };
    TArray<FTransform> Reserved;
    auto Reserve=[&](FVector Center,FVector Size){Reserved.Add(FTransform(FQuat::Identity,Center,Size));};
    // Ladder approach and landing volumes stay free of later cover.
    auto ReserveLadder=[&](FVector Base,float Yaw,float Rise)
    {
        const FTransform F(FRotator(0,Yaw,0),Base);
        Reserved.Add(FTransform(F.GetRotation(),F.TransformPosition({90,0,100}),{160,180,200}));
        Reserved.Add(FTransform(F.GetRotation(),F.TransformPosition({55,0,(Rise+200)*.5f}),{100,180,Rise+200}));
        Reserved.Add(FTransform(F.GetRotation(),F.TransformPosition({-30,0,Rise+100}),{280,180,200}));
    };
    Reserve({0,-HL+300,150},{W,620,300}); // Spawn strip and front crossing.

    // ---- Protected bases. The warehouse wall is never the base's only cover.
    // A concrete court with two offset gates surrounds an enterable one-storey
    // barracks. Its steel roof always has two independent exterior ladders.
    const float SpawnX=0;
    const float BX0=SpawnX-300,BX1=SpawnX+300,BY0=-HL+70,BY1=-HL+650;
    const float CX0=SpawnX-600,CX1=SpawnX+600,CY0=-HL+40,CY1=-HL+1150;
    if(!Elevated || !Headroom(BX0,BX1,DeckZ))return Fail(TEXT("base"));
    auto ConcreteWall=[&](float X0,float X1,float Y0,float Y1)
    {Box({(X0+X1)*.5f,(Y0+Y1)*.5f,140},{X1-X0,Y1-Y0,280},EERBlockMaterial::Concrete,false);};
    ConcreteWall(CX0,CX1,CY0,CY0+20);
    ConcreteWall(CX0,CX0+20,CY0+20,CY1-20);
    ConcreteWall(CX1-20,CX1,CY0+20,CY1-20);
    // Wide side gates avoid a straight firing lane into the central spawn room.
    ConcreteWall(CX0,CX0+100,CY1-20,CY1);
    ConcreteWall(CX0+280,CX1-280,CY1-20,CY1);
    ConcreteWall(CX1-100,CX1,CY1-20,CY1);
    Timber(BX0,BY0,true,BY1-BY0,0,280,{});
    Timber(BX1-12,BY0,true,BY1-BY0,0,280,{});
    Timber(BX0+12,BY0,false,BX1-BX0-24,0,280,{});
    Timber(BX0+12,BY1-12,false,BX1-BX0-24,0,280,{{SpawnX-BX0-12,140,0,220}});
    Slab(BX0,BX1,BY0,BY1,DeckZ);
    const float BaseLadderY=BY1-150;
    for(float Side:{-1.f,1.f})
    {
        const float X=Side<0 ? BX0:BX1,Yaw=Side<0 ? 180:0;
        const FVector Base(X+Side*35,BaseLadderY,0);
        Ladder(Base,Yaw,DeckZ,true);ReserveLadder(Base,Yaw,DeckZ);
        Rail(X-5,X,BY0+5,BaseLadderY-85,DeckZ);
        Rail(X-5,X,BaseLadderY+85,BY1-5,DeckZ);
        // Walk out of the room, dogleg through either protected court gate.
        const float GateX=SpawnX+Side*410;
        L.RoomAccessRoutes.Add({{SpawnX,BY1-120,100},{SpawnX,BY1+120,100},
            {GateX,BY1+120,100},{GateX,YFront,100}});
    }
    Rail(BX0,BX1,BY0,BY0+5,DeckZ);Rail(BX0,BX1,BY1-5,BY1,DeckZ);
    Elevate(FBox(FVector(BX0,BY0,DeckZ-20),FVector(BX1,BY1,DeckZ)),2);
    for(int32 Copy=0;Copy<2;++Copy)
    {
        FERTemplateBase Base;Base.Frame=FTransform(FRotator(0,Copy ? 180:0,0),Copy ? FVector(-SpawnX,-(BY0+BY1)*.5f,0):FVector(SpawnX,(BY0+BY1)*.5f,0));
        Base.Size={BX1-BX0,BY1-BY0,DeckZ};Base.RoofZ=DeckZ;L.Bases.Add(Base);
        const FBox Court(FVector(CX0,CY0,0),FVector(CX1,CY1,280));L.WarehouseBaseCourts.Add(Copy ? HalfTurn(Court):Court);
        L.RooftopRoutes.Add({Base.Point({-220,140,DeckZ+100}),Base.Point({0,140,DeckZ+100}),Base.Point({220,140,DeckZ+100})});
    }
    Reserve({SpawnX,(CY0+CY1)*.5f,300},{1200,CY1-CY0,600});
    L.FeatureNames.Append({TEXT("FortifiedBase"),TEXT("BaseRoof")});
    for(int32 I=0;I<10;++I)L.AlphaSpawns.Add(FTransform(FRotator(0,90,0),FVector(SpawnX+(I%4-1.5f)*90.f,-HL+150+(I/4)*110.f,100)));

    // Independent orthogonal routes: straight, ring and bent crossings. Rails
    // follow the boundary of the complete deck union, leaving every junction open.
    const float EndInset=FMath::Min(Reference ? 1500.f:Pick(1200,1750),HL-500);
    const float YA=-HL+EndInset,YB=-YA;
    const FBox Flank(FVector(CatwalkX,YA,CatwalkZ-20),FVector(CatwalkX+Dw,YB,CatwalkZ));
    const float LadderX=CatwalkX+Dw*.5f,EastX=-LadderX;
    const float Bend=FMath::Min(YB-250,Reference ? 350.f:Pick(350,FMath::Max(350.f,YB*.65f)));
    const float Kink=Reference ? 400.f:Pick(350,FMath::Max(350.f,W*.18f));
    TArray<TArray<FVector>> HighPaths;
    auto Point=[&](float X,float Y){return FVector(X,Y,CatwalkZ+100);};
    const int32 Pattern=L.WarehouseCatwalkVariant;
    if(Pattern==0)HighPaths.Add({Point(LadderX,0),Point(EastX,0)});
    else if(Pattern==1 || Pattern==2)
    {
        const float Y=Pattern==1 ? Bend:YB-200;
        HighPaths.Add({Point(LadderX,-Y),Point(EastX,-Y)});
        HighPaths.Add({Point(LadderX,Y),Point(EastX,Y)});
    }
    else if(Pattern==3)
        HighPaths.Add({Point(LadderX,-Bend),Point(-Kink,-Bend),Point(-Kink,Bend),Point(EastX,Bend)});
    else if(Pattern==4)
        HighPaths.Add({Point(LadderX,-Bend),Point(-Kink,-Bend),Point(-Kink,0),Point(0,0)});
    else
        HighPaths.Add({Point(LadderX,-Bend),Point(-Kink,-Bend),Point(-Kink,0),Point(Kink,0),Point(Kink,Bend),Point(EastX,Bend)});
    TArray<FBox> Network{Flank};
    for(const auto& Path:HighPaths)for(int32 I=1;I<Path.Num();++I)
    {
        const auto& A=Path[I-1];const auto& B=Path[I];
        const bool Horizontal=FMath::IsNearlyEqual(A.Y,B.Y);
        Network.Add(FBox(FVector(FMath::Min(A.X,B.X)-(Horizontal ? 0:Dw*.5f),FMath::Min(A.Y,B.Y)-(Horizontal ? Dw*.5f:0),CatwalkZ-20),
            FVector(FMath::Max(A.X,B.X)+(Horizontal ? 0:Dw*.5f),FMath::Max(A.Y,B.Y)+(Horizontal ? Dw*.5f:0),CatwalkZ)));
    }
    // Join corner squares so turning a real capsule never leaves an unsupported gap.
    for(const auto& Path:HighPaths)for(int32 I=1;I<Path.Num()-1;++I)
    {
        const auto& P=Path[I];Network.Add(FBox(FVector(P.X-Dw*.5f,P.Y-Dw*.5f,CatwalkZ-20),FVector(P.X+Dw*.5f,P.Y+Dw*.5f,CatwalkZ)));
    }
    if(YB-YA<600 || !Fits(Network) || !Headroom(CatwalkX,CatwalkX+Dw,CatwalkZ,200)){UE_LOG(LogTemp,Warning,TEXT("ER_FIRE_NETWORK_REJECT span=%.0f elevated=%.3f overhead=%.3f open=%.3f"),YB-YA,Trial.Ratio(1),Trial.Ratio(2),Trial.LargestOpen(2,4));return Fail(TEXT("network"));}
    const auto HalfNetwork=Network;
    for(const auto& Deck:HalfNetwork)
    {
        const auto Paired=HalfTurn(Deck);
        if(!Network.ContainsByPredicate([&](const FBox& Existing){return Existing.Min.Equals(Paired.Min,.01) && Existing.Max.Equals(Paired.Max,.01);}))Network.Add(Paired);
    }
    L.WarehouseCatwalks={Flank,HalfTurn(Flank)};
    auto KeepCanonical=[&]()
    {
        auto& Piece=Pieces.Last();
        if(Piece.Center.Y>.01 || (FMath::IsNearlyZero(Piece.Center.Y,.01) && Piece.Center.X>.01)){Pieces.Pop();return;}
        Piece.Mirrored=!FMath::IsNearlyZero(Piece.Center.X,.01) || !FMath::IsNearlyZero(Piece.Center.Y,.01);
    };
    TArray<double> CutsX,CutsY;
    for(const auto& Deck:Network){CutsX.AddUnique(Deck.Min.X);CutsX.AddUnique(Deck.Max.X);CutsY.AddUnique(Deck.Min.Y);CutsY.AddUnique(Deck.Max.Y);}
    CutsX.Sort();CutsY.Sort();
    for(int32 I=1;I<CutsX.Num();++I)for(int32 J=1;J<CutsY.Num();++J)
    {
        const FVector Mid((CutsX[I-1]+CutsX[I])*.5,(CutsY[J-1]+CutsY[J])*.5,CatwalkZ-10);
        if(!Network.ContainsByPredicate([&](const FBox& Deck){return Deck.IsInside(Mid);}))continue;
        Slab(CutsX[I-1],CutsX[I],CutsY[J-1],CutsY[J],CatwalkZ);KeepCanonical();
    }
    for(const auto& Deck:Network)
    {
        Grid.Add(Deck,Mask(Deck));L.ElevatedDecks.Add(Deck);L.OverheadClosures.Add(Deck);L.ElevatedAccesses.Add(2);
    }
    for(int32 I=1;I<HalfNetwork.Num();++I){L.WarehouseCatwalks.Add(HalfNetwork[I]);L.WarehouseCatwalks.Add(HalfTurn(HalfNetwork[I]));}
    TArray<FVector4> Boundary;
    // Cut adjacent decks and ladder mouths out of each boundary interval.
    for(const auto& Deck:Network)for(int32 Edge=0;Edge<4;++Edge)
    {
        const bool AlongX=Edge<2;const float Sign=(Edge%2)==0 ? -1.f:1.f;
        const float Fixed=AlongX ? (Sign<0 ? Deck.Min.Y:Deck.Max.Y):(Sign<0 ? Deck.Min.X:Deck.Max.X);
        TArray<FVector2D> Segments{{AlongX ? Deck.Min.X:Deck.Min.Y,AlongX ? Deck.Max.X:Deck.Max.Y}};
        auto Cut=[&](float A,float B)
        {
            TArray<FVector2D> Kept;
            for(const auto& Segment:Segments)
            {
                if(B<=Segment.X || A>=Segment.Y)Kept.Add(Segment);
                else{if(A>Segment.X)Kept.Add({Segment.X,A});if(B<Segment.Y)Kept.Add({B,Segment.Y});}
            }
            Segments=MoveTemp(Kept);
        };
        for(const auto& Other:Network)
        {
            const float Outside=Fixed+Sign*.5f;
            if(AlongX ? Outside>Other.Min.Y && Outside<Other.Max.Y:Outside>Other.Min.X && Outside<Other.Max.X)
                Cut(AlongX ? Other.Min.X:Other.Min.Y,AlongX ? Other.Max.X:Other.Max.Y);
        }
        if(AlongX && (FMath::IsNearlyEqual(Fixed,YA) || FMath::IsNearlyEqual(Fixed,YB)))
            for(float X:{LadderX,EastX})Cut(X-85,X+85);
        for(const auto& Segment:Segments)
        {
            if(Segment.Y-Segment.X<2)continue;
            double A=Segment.X,B=Segment.Y;
            for(int32 I=0;I<Boundary.Num();)
            {
                const auto& Existing=Boundary[I];
                if(Existing.X==(AlongX ? 1:0) && FMath::IsNearlyEqual(Existing.Y,double(Fixed)) && Existing.Z<=B+.01 && Existing.W>=A-.01)
                {A=FMath::Min(A,Existing.Z);B=FMath::Max(B,Existing.W);Boundary.RemoveAt(I);I=0;}
                else ++I;
            }
            Boundary.Add(FVector4(AlongX ? 1:0,Fixed,A,B));
        }
    }
    for(const auto& Edge:Boundary)
    {
        if(Edge.W-Edge.Z<6)continue;
        if(Edge.X>0)Rail(Edge.Z+2.5f,Edge.W-2.5f,Edge.Y-2.5f,Edge.Y+2.5f,CatwalkZ);
        else Rail(Edge.Y-2.5f,Edge.Y+2.5f,Edge.Z,Edge.W,CatwalkZ);
        KeepCanonical();
    }
    for(const FVector2D End:{FVector2D(YA,-90),FVector2D(YB,90)})
    {
        const FVector Base(LadderX,End.X+(End.Y<0 ? -35:35),0);
        Ladder(Base,End.Y,CatwalkZ,true,true);ReserveLadder(Base,End.Y,CatwalkZ);
    }
    L.CrossingRoutes.Add({Point(LadderX,YA+100),Point(LadderX,YB-100)});
    L.CrossingRoutes.Add({Point(EastX,YA+100),Point(EastX,YB-100)});
    for(const auto& Path:HighPaths)
    {
        L.CrossingRoutes.Add(Path);TArray<FVector> Paired;
        for(const auto& P:Path)Paired.Add({-P.X,-P.Y,P.Z});L.CrossingRoutes.Add(MoveTemp(Paired));
    }
    L.FeatureNames.Append({TEXT("Catwalk"),TEXT("CatwalkBridge"),TEXT("SteelParapets")});
    // Keep the narrow high decks clear of the original wall knee brackets.
    // Local timber corridor beneath the south-west walkway, with two real doors.
    const float CorridorEnd=FMath::Min(-450.f,YA+1260);
    if(CorridorEnd-YA>=900)
    {
        const float DoorA=YA+540,DoorB=CorridorEnd-180;
        Timber(XI0+Dw,YA,true,CorridorEnd-YA,0,280,{{DoorA-YA,120,0,220},{DoorB-YA,120,0,220}});
        L.RoomAccessRoutes.Add({{XWL,DoorA,100},{XI0+100,DoorA,100},{XI0+100,DoorB,100},{XWL,DoorB,100}});
        Reserve({XI0+Dw+90,DoorA,110},{150,180,220});Reserve({XI0+Dw+90,DoorB,110},{150,180,220});
        L.FeatureNames.Add(TEXT("Corridor"));
    }
    // Ground rooms remain local below the opposite walkway; their walls stop
    // at its underside, so the connected upper route is never obstructed.
    float NextRoom=-HL+1700+(Reference ? 0:Pick(0,200));
    for(int32 Block=0;Block<(Reference ? 1:3);++Block)
    {
        const float YR0=NextRoom,YR1=FMath::Min(-450.f,YR0+(Reference ? 800:Pick(600,1100))),Lr=YR1-YR0;
        NextRoom=YR1+Pick(600,1100);
        if(Lr<500 || (Block>0 && !Chance(.75f)))break;
        const FBox RoomBounds(FVector(XR0-40,YR0-40,0),FVector(XI1+40,YR1+40,300));
        bool AccessClear=true;
        for(const auto& Access:Reserved)
        {
            const FBox Bounds=FBox(-Access.GetScale3D()*.5,Access.GetScale3D()*.5).TransformBy(FTransform(Access.GetRotation(),Access.GetLocation()));
            if(RoomBounds.Intersect(Bounds) || RoomBounds.Intersect(HalfTurn(Bounds))){AccessClear=false;break;}
        }
        if(!AccessClear)continue;
        const float Door=Lr*.5f,Mid=(XR0+XI1)*.5f;
        Timber(XR0,YR0,true,Lr,0,280,{{Door,120,0,220}});
        Timber(XR0+12,YR0,false,XI1-XR0-12,0,280,{});
        Timber(XR0+12,YR1-12,false,XI1-XR0-12,0,280,{{Mid-XR0-12,120,0,220}});
        L.RoomAccessRoutes.Add({{XEL,YR0+Door,100},{XR0-60,YR0+Door,100},{XR0+150,YR0+Door,100},{Mid,YR0+Door,100}});
        L.RoomAccessRoutes.Add({{Mid,YR1+90,100},{Mid,YR1-150,100}});
        Reserve({XR0-90,YR0+Door,110},{180,200,220});Reserve({Mid,YR1+90,110},{200,160,220});
        L.FeatureNames.Add(TEXT("Rooms"));
    }
    // Patrol stays outside the court walls, with free cross-map ground lanes.
    const TArray<FVector> South{{XEL,-250,100},{XEL,YFront,100},{XWL,YFront,100},{XWL,-250,100}};
    for(const auto& P:South)Patrol.Add(P);
    for(const auto& P:South)Patrol.Add({-P.X,-P.Y,P.Z});

    const float Area=W*Length/1e4f;

    // ---- Validated placement of cargo and low cover (complete groups only).
    TArray<FTransform> Occupied;
    auto Frames=[&](int32 First,TArray<FTransform>& Out)
    {
        for(int32 I=First;I<Pieces.Num();++I)
        {
            const auto& P=Pieces[I];if(P.VisualOnly || P.MeshCollision)continue;
            for(int32 Copy=0;Copy<(P.Mirrored ? 2:1);++Copy)
                Out.Add(FTransform(FRotator(0,P.RotationYaw+(Copy ? 180:0),0),Copy ? FVector(-P.Center.X,-P.Center.Y,P.Center.Z):P.Center,P.Size));
        }
    };
    Frames(0,Occupied);
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
    auto Approach=[](const FERLayoutLadder& Item){return FTransform(Item.Frame.GetRotation(),Item.Frame.TransformPosition({90,0,100}),{160,180,200});};
    auto ClimbSpace=[](const FERLayoutLadder& Item)
    {return FTransform(Item.Frame.GetRotation(),Item.Frame.TransformPosition({55,0,(Item.DeckHeight+200)*.5f}),{100,180,Item.DeckHeight+200});};
    auto LandingSpace=[](const FERLayoutLadder& Item)
    {return FTransform(Item.Frame.GetRotation(),Item.Frame.TransformPosition({-30,0,Item.DeckHeight+100}),{280,180,200});};
    int32 PlacementReject=0;
    auto Accept=[&](int32 First,int32 FirstLadder,float Margin)
    {
        TArray<FTransform> Added;Frames(First,Added);bool Ok=true;PlacementReject=0;
        // New ladders need a clear standing approach in front of their rungs.
        for(int32 I=FirstLadder;I<L.Ladders.Num() && Ok;I+=2)
            if(L.Ladders[I].Frame.GetLocation().Z<1)for(const auto& O:Occupied)if(Overlap(Approach(L.Ladders[I]),O,0) || Overlap(ClimbSpace(L.Ladders[I]),O,0) || Overlap(LandingSpace(L.Ladders[I]),O,0)){Ok=false;break;}
        for(int32 I=0;I<Added.Num() && Ok;++I)
        {
            const auto& F=Added[I];
            const FBox B=FBox(-F.GetScale3D()*.5,F.GetScale3D()*.5).TransformBy(FTransform(F.GetRotation(),F.GetLocation()));
            Ok=B.Min.X>=XI0+40 && B.Max.X<=XI1-40 && B.Min.Y>=-HL+40 && B.Max.Y<=HL-40;
            for(const auto& O:Occupied)if(Ok && Overlap(F,O,Margin)){Ok=false;PlacementReject=2;}
            for(const auto& O:Reserved)if(Ok && (Overlap(F,O,0) || Overlap(F,FTransform(FRotator(0,180,0),FVector(-O.GetLocation().X,-O.GetLocation().Y,O.GetLocation().Z),O.GetScale3D()),0))){Ok=false;PlacementReject=3;}
            for(int32 J=0;J<Patrol.Num() && Ok;++J)if(Blocked(Patrol[J],Patrol[(J+1)%Patrol.Num()],F,93)){Ok=false;PlacementReject=4;}
            for(const auto* Paths:{&L.RoomAccessRoutes,&L.CrossingRoutes,&L.StairRoutes})for(const auto& Path:*Paths)
                for(int32 J=1;J<Path.Num() && Ok;++J)
                    for(int32 Copy=0;Copy<2 && Ok;++Copy)
                    {
                        const FVector A=Copy ? FVector(-Path[J-1].X,-Path[J-1].Y,Path[J-1].Z):Path[J-1],B2=Copy ? FVector(-Path[J].X,-Path[J].Y,Path[J].Z):Path[J];
                        if(Blocked(A,B2,F,93)){Ok=false;PlacementReject=5;}
                    }
        }
        if(!Ok){Pieces.SetNum(First);L.Ladders.SetNum(FirstLadder);return false;}
        Occupied.Append(Added);return true;
    };
    // A pair of connected offices can replace an isolated through-room.
    // Place the complete suite and reserve its route before furnishing the hall.
    const uint32 InteriorPlan=HashCombineFast(uint32(Seed),0x4F464643u)%4;
    bool PlacedOffices=false;
    int32 GroundRooms=0;int32 RoomRejects[6]={};
    const int32 RoomsWanted=FMath::Clamp(FMath::RoundToInt32(Area/650),2,16);
    const float RoomX0=XWL+180,RoomX1=XEL-180;
    for(int32 Try=0;Try<240 && GroundRooms<RoomsWanted;++Try)
    {
        const bool Offices=!Reference && InteriorPlan!=0 && !PlacedOffices && Try<100 && Try%2==0;
        const bool AlongHall=!Reference && (InteriorPlan+uint32(Try/2))%2!=0;
        const float SX=Offices ? Pick(900,1150):Pick(500,850);
        const float SY=Offices ? Pick(600,800):Pick(500,850);
        const float Width=AlongHall ? SY:SX,Depth=AlongHall ? SX:SY;
        if(RoomX1-RoomX0<Width+150 || HL<1650+Depth)continue;
        const float X=Pick(RoomX0+Width*.5f,RoomX1-Width*.5f);
        const float Y=Try%3==0 ? -Depth*.5f-150:Pick(-HL+1500+Depth*.5f,-Depth*.5f-150);
        const float X0=-SX*.5f,Y0=-SY*.5f;
        auto WorldPoint=[&](float U,float V,float Z)
        {
            return AlongHall ? FVector(X+V,Y+U,Z):FVector(X+U,Y+V,Z);
        };
        auto Wall=[&](float U,float V,bool AlongV,float Span,float Height,TArray<FOpening> Openings)
        {
            const FVector Corner=WorldPoint(U,V,0);
            Timber(Corner.X,Corner.Y,AlongV!=AlongHall,Span,0,Height,MoveTemp(Openings));
        };
        const float DoorA=SY*.3f,DoorB=SY*.7f;
        const TArray<FVector> Route{
            WorldPoint(X0-120,Y0+DoorA,100),WorldPoint(X0+150,Y0+DoorA,100),
            WorldPoint(0,0,100),WorldPoint(-X0-150,Y0+DoorB,100),
            WorldPoint(-X0+120,Y0+DoorB,100)};
        if(Offices)
        {
            bool Clear=true;
            for(const auto& Solid:Occupied)for(int32 I=1;I<Route.Num() && Clear;++I)
                Clear=!Blocked(Route[I-1],Route[I],Solid,93) &&
                    !Blocked(FVector(-Route[I-1].X,-Route[I-1].Y,Route[I-1].Z),
                             FVector(-Route[I].X,-Route[I].Y,Route[I].Z),Solid,93);
            if(!Clear)continue;
        }
        const int32 First=Pieces.Num();
        Wall(X0,Y0,true,SY,280,{{DoorA,140,0,220}});
        Wall(-X0-12,Y0,true,SY,280,{{DoorB,140,0,220}});
        Wall(X0+12,Y0,false,SX-24,280,{{SX*.65f,160,100,200}});
        Wall(X0+12,-Y0-12,false,SX-24,280,{{SX*.35f,160,100,200}});
        if(Offices)
            Wall(-6,Y0+12,true,SY-24,280,{{SY*.5f-12,160,0,220},{SY*.8f-12,100,100,200}});
        else if(R.RandBool())
            Wall(0,Y0+12,true,SY*.3f,220,{});
        if(!Accept(First,L.Ladders.Num(),110)){++RoomRejects[PlacementReject];continue;}
        if(Offices){PlacedOffices=true;L.FeatureNames.Add(TEXT("OfficeCluster"));}
        L.RoomAccessRoutes.Add(Route);TArray<FVector> Paired;
        for(const auto& P:Route)Paired.Add({-P.X,-P.Y,P.Z});L.RoomAccessRoutes.Add(MoveTemp(Paired));
        for(const auto& P:Route)Reserve(P,{180,180,200});
        L.FeatureNames.Add(TEXT("GroundRoom"));++GroundRooms;
    }
    if(Area>=2200 && GroundRooms<2){UE_LOG(LogTemp,Warning,TEXT("ER_FIRE_ROOMS_REJECT seed=%d attempt=%d rooms=%d width=%.0f length=%.0f occupied=%d reserved=%d patrol=%d route=%d"),Seed,Attempt,GroundRooms,W,Length,RoomRejects[2],RoomRejects[3],RoomRejects[4],RoomRejects[5]);return false;}
    // Paired local islands on a different height plane, with two real accesses.
    if(!Reference && W>4500 && Chance(.7f))
    {
        const float Z=CatwalkZ>=650 ? CatwalkZ-300:CatwalkZ+300;
        const float X=Pick(XWL+400,XEL-400),Y=FMath::Clamp(-HL*.25f,-HL+1800,-600.f);
        const FBox Island(FVector(X-300,Y-400,Z-20),FVector(X+300,Y+400,Z));
        if(Headroom(X-300,X+300,Z) && Fits({Island}))
        {
            const int32 First=Pieces.Num(),FirstLadder=L.Ladders.Num();
            Slab(X-300,X+300,Y-400,Y+400,Z);
            for(float Side:{-1.f,1.f})
            {
                const float Edge=X+Side*300,Yaw=Side<0 ? 180:0;
                Ladder({Edge+Side*35,Y,0},Yaw,Z,true,true);
                Rail(Edge-2.5f,Edge+2.5f,Y-395,Y-85,Z);Rail(Edge-2.5f,Edge+2.5f,Y+85,Y+395,Z);
            }
            Rail(X-302.5f,X+302.5f,Y-400,Y-395,Z);Rail(X-302.5f,X+302.5f,Y+395,Y+400,Z);
            if(Accept(First,FirstLadder,40))
            {
                Elevate(Island,2);L.FeatureNames.Add(TEXT("UpperIsland"));
                for(int32 I=FirstLadder;I<L.Ladders.Num();I+=2)ReserveLadder(L.Ladders[I].Frame.GetLocation(),L.Ladders[I].Frame.Rotator().Yaw,Z);
                L.RooftopRoutes.Add({{X-200,Y,Z+100},{X+200,Y,Z+100}});
                L.RooftopRoutes.Add({{-X+200,-Y,Z+100},{-X-200,-Y,Z+100}});
            }
        }
    }
    // Containers: hollow reinforced steel cells; stacks only under a tall roof.
    auto Container=[&](FVector Center,FVector Size,int32 Index)->FERMapTemplatePiece&
    {
        auto& B=Box(Center,Size,EERBlockMaterial::Metal,false);B.Strength=8;
        if(!V.ContainerSurfaces.IsEmpty())
        {
            const uint32 PaletteSeed=HashCombineFast(uint32(Seed),0x46414354u);
            B.CellSurface=V.ContainerSurfaces[HashCombineFast(PaletteSeed,uint32(Index*2))%V.ContainerSurfaces.Num()];
            B.MirroredCellSurface=V.ContainerSurfaces[HashCombineFast(PaletteSeed,uint32(Index*2+1))%V.ContainerSurfaces.Num()];
        }
        for(int32 Copy=0;Copy<2;++Copy)L.Containers.Add({FTransform(FRotator(0,Copy ? 180:0,0),Copy ? FVector(-Center.X,-Center.Y,Center.Z):Center),Size});
        return B;
    };
    int32 Cargo=0;
    const int32 CargoWanted=Reference ? 1:(CB1-CB0>=700 ? R.RandRange(1,2):1)+(Wanted(.6f,.25f) ? 1:0)+(HL>=3500 ? R.RandRange(1,2):0)
        +R.RandRange(0,FMath::FloorToInt(Area/1500));
    for(int32 Try=0;Try<80 && Cargo<CargoWanted;++Try)
    {
        const bool AlongY=CB1-CB0<700 || R.RandBool();
        const float Long=(Reference || Bands<3 || !AlongY) ? 600:(R.RandBool() ? 1200:600);
        const FVector Size=AlongY ? FVector(244,Long,260):FVector(Long,244,260);
        if(Size.X>CB1-CB0)continue;
        const float X=FMath::Clamp(Pick(CB0,CB1),CB0+Size.X*.5f,CB1-Size.X*.5f);
        const float YMin=-HL+850+Size.Y*.5f,YMax=-400-Size.Y*.5f;if(YMax<YMin)continue;
        const float Y=Reference ? FMath::RoundToFloat((YMin+YMax)*.25f/50)*100:FMath::Clamp(Pick(YMin,YMax),YMin,YMax);
        const bool Stack=Bands>=3 && Wanted(.8f,.5f) && Headroom(X-Size.X*.5f,X+Size.X*.5f,520);
        TArray<FBox> Tops{FBox(FVector(X-Size.X*.5f,Y-Size.Y*.5f,258),FVector(X+Size.X*.5f,Y+Size.Y*.5f,260))};
        if(!Headroom(X-Size.X*.5f,X+Size.X*.5f,260) || !Fits(Tops))continue;
        const int32 First=Pieces.Num(),FirstLadder=L.Ladders.Num(),FirstContainer=L.Containers.Num();
        Container({X,Y,130},Size,Cargo*2);
        // Ladders on the end faces; the long sides stay clear of the lanes.
        const float Face=(AlongY ? Size.Y:Size.X)*.5f+35;
        const FVector Axis=AlongY ? FVector(0,1,0):FVector(1,0,0);
        const float YawPlus=AlongY ? 90:0;
        if(Stack && Long<1200)
        {
            // Equal boxes stacked flush; two full-height ladders reach the top.
            Container({X,Y,390},Size,Cargo*2+1);
            Ladder(FVector(X,Y,0)+Axis*Face,YawPlus,520,true);
            Ladder(FVector(X,Y,0)-Axis*Face,YawPlus+180,520,true);
            Tops[0]=FBox(FVector(X-Size.X*.5f,Y-Size.Y*.5f,518),FVector(X+Size.X*.5f,Y+Size.Y*.5f,520));
        }
        else if(Stack)
        {
            // Upper 6m box on the south/west half; two ladders reach each top.
            const FVector Upper=FVector(X,Y,390)-Axis*300;
            Container(Upper,AlongY ? FVector(244,600,260):FVector(600,244,260),Cargo*2+1);
            Ladder(FVector(X,Y,0)+Axis*Face,YawPlus,260,true);
            Ladder(FVector(X,Y,0)-Axis*Face,YawPlus+180,520,true);
            Ladder(FVector(X,Y,260)+Axis*(35),YawPlus,260,false);
            Tops.Add(FBox(FVector(Upper.X-122,Upper.Y-122,518),FVector(Upper.X+122,Upper.Y+122,520)).ExpandBy(AlongY ? FVector(0,178,0):FVector(178,0,0)));
        }
        else
        {
            Ladder(FVector(X,Y,0)+Axis*Face,YawPlus,260,true);
            Ladder(FVector(X,Y,0)-Axis*Face,YawPlus+180,260,true);
        }
        if(!Accept(First,FirstLadder,80)){L.Containers.SetNum(FirstContainer);continue;}
        for(int32 I=FirstLadder;I<L.Ladders.Num();I+=2)
            if(L.Ladders[I].Frame.GetLocation().Z<1)ReserveLadder(L.Ladders[I].Frame.GetLocation(),L.Ladders[I].Frame.Rotator().Yaw,L.Ladders[I].DeckHeight);
        for(const auto& Top:Tops)Elevate(Top,2);
        L.FeatureNames.Add(Stack ? TEXT("ContainerStack"):TEXT("Container"));
        ++Cargo;
    }
    // Low cover in staggered bands; each group keeps capsule-clear lanes.
    int32 Cover=0;
    TArray<float> CoverX{XWL-80,XEL+80};
    for(float X=CB0;X<=CB1+1;X+=FMath::Max(300.f,(CB1-CB0)/FMath::Max(2,FMath::RoundToInt((CB1-CB0)/650))))CoverX.Add(X);
    for(float Y=-HL+950;Y<-120;Y+=Pick(250,350))for(float X:CoverX)
    {
        if(!Chance(.85f))continue;
        const int32 Kind=R.RandRange(0,5);const int32 First=Pieces.Num();
        const FVector P(X+Pick(-100,100),Y+Pick(-100,100),0);
        if(Kind==0)Box(P+FVector(0,0,55),{110,110,110},EERBlockMaterial::Wood,false);
        else if(Kind==1){Box(P+FVector(0,0,55),{110,110,110},EERBlockMaterial::Wood,false);Box(P+FVector(0,0,165),{110,110,110},EERBlockMaterial::Wood,false);}
        else if(Kind==2)Box(P+FVector(0,0,45),R.RandBool() ? FVector(300,50,90):FVector(50,300,90),EERBlockMaterial::Concrete,false);
        else if(Kind==3)Box(P+FVector(0,0,100),R.RandBool() ? FVector(240,12,200):FVector(12,240,200),EERBlockMaterial::Wood,false);
        if(Kind==4){Box(P+FVector(0,0,60),{320,35,120},EERBlockMaterial::Concrete,false);Box(P+FVector(-142,100,60),{35,165,120},EERBlockMaterial::Concrete,false);}
        if(Kind==5){Box(P+FVector(0,0,65),{200,120,130},EERBlockMaterial::Wood,false);Box(P+FVector(45,0,175),{110,110,90},EERBlockMaterial::Wood,false);}
        if(Accept(First,L.Ladders.Num(),75))++Cover;
    }
    L.CentralLowCovers=Cover;
    // ---- Budgets and fairness checks on the complete composition.
    L.ElevatedRatio=Grid.Ratio(1);L.OverheadRatio=Grid.Ratio(2);L.OpenRegionRatio=Grid.LargestOpen(2,4);
    auto Reject=[&](const TCHAR* Reason){UE_LOG(LogTemp,Log,TEXT("ER_FIRE_TRAINING_ATTEMPT seed=%d attempt=%d reject=%s"),Seed,Attempt,Reason);return false;};
    if(L.ElevatedRatio>V.ElevatedMaxRatio+1e-4f || L.OverheadRatio>V.OverheadMaxRatio+1e-4f || L.OpenRegionRatio<V.OpenRegionMinRatio)return Reject(TEXT("budget"));
    for(int32 A:L.ElevatedAccesses)if(A<2)return Reject(TEXT("access"));
    for(const auto& D:L.ElevatedDecks)if(!Headroom(D.Min.X,D.Max.X,D.Max.Z,FMath::IsNearlyEqual(D.Max.Z,CatwalkZ) ? 200:250))return Reject(TEXT("headroom"));
    // Initial spawns: most direct start-to-start lines must meet real cover.
    TArray<FTransform> Solids;Frames(0,Solids);int32 Hidden=0,Lines=0;
    for(const auto& A:L.AlphaSpawns)for(const auto& B:L.AlphaSpawns)
    {
        const FVector From=A.GetLocation()+FVector(0,0,72.5f),To=FVector(-B.GetLocation().X,-B.GetLocation().Y,From.Z);++Lines;
        Hidden+=Solids.ContainsByPredicate([&](const FTransform& S){return S.GetScale3D().Z>=150 && Blocked(From,To,S,0);});
    }
    L.SpawnCoverRatio=float(Hidden)/FMath::Max(1,Lines);
    if(L.SpawnCoverRatio<.75f)return Reject(TEXT("spawn-lines"));
    L.CentralBuildingSize=FVector(W,Length,Eave);
    return Pieces.Num()<=4096 ? true:Fail(TEXT("pieces"));
}
}
