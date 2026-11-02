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
float WarehouseRoofHeight(float NaveWidth,int32 Naves,float Eave,float X)
{
    // Source arch: 16m span and 2m rise (17m radius); wider kits add a flat crown.
    // Each nave carries its own arch; valleys between naves sit at the eave.
    const float Total=NaveWidth*Naves;
    const int32 Nave=FMath::Clamp(FMath::FloorToInt((X+Total*.5f)/NaveWidth),0,Naves-1);
    const float Local=X+Total*.5f-NaveWidth*(Nave+.5f);
    const float Flat=FMath::Max(0.f,NaveWidth-1600)*.5f;
    const float A=FMath::Clamp(FMath::Abs(Local)-Flat,0.f,800.f);
    return Eave+FMath::Sqrt(1700.f*1700.f-A*A)-1500.f;
}

float WarehouseCatwalkHeight(const FERWarehouseWidthKit& Kit,int32 Naves,float Eave)
{
    // Source lamps hang 14.21cm above the eave at their lowest point. Greybox
    // tests use those measured kit offsets; production reads the actual meshes.
    const float Lamps=Kit.RoofLamps ? Kit.RoofLamps->GetBoundingBox().Min.Z:14.21f;
    // Crossings between naves also pass under the low roof valleys. Flanks sit
    // 1.2m from the wall, beyond the 85cm knee brackets below the outer ribs.
    const float Roof=Kit.RoofBay ? Kit.RoofBay->GetBoundingBox().Min.Z:-2.39f;
    const float Soffit=Eave+(Naves>1 ? FMath::Min(Lamps,Roof):Lamps);
    return FMath::FloorToFloat((Soffit-200.f)/5.f)*5.f;
}

bool BuildFireTraining(const UERMapTemplate& T,int32 Seed,int32 Attempt,TArray<FERMapTemplatePiece>& Pieces,FERProceduralLayout& L,TArray<FVector>& Patrol)
{
    const auto& V=T.FireTraining;
    auto Ratio=[](float X){return FMath::IsFinite(X) && X>0 && X<=1;};
    if(T.Symmetry!=EERTemplateSymmetry::HalfTurn || V.ContinuousUpperFloors || V.FloorArea.ContainsNaN() || V.FloorArea.X<300 || V.FloorArea.Y>(T.SizingTeamSize>0 ? 26400:10000) || V.FloorArea.X>V.FloorArea.Y ||
        V.MinBays<6 || V.MaxBays>(T.SizingTeamSize>0 ? 64:36) || V.MinBays>V.MaxBays || V.MinHeightBands<1 || V.MaxHeightBands>5 ||
        V.MinNaves<1 || V.MaxNaves>4 || V.MinNaves>V.MaxNaves || !FMath::IsFinite(V.MaxAspect) || V.MaxAspect<1.5f || V.MaxPlazas<0 || V.MaxPlazas>3 || V.MinHeightBands>V.MaxHeightBands ||
        !Ratio(V.ElevatedTargetRatio) || !Ratio(V.ElevatedMaxRatio) || !Ratio(V.OverheadMaxRatio) || !Ratio(V.OpenRegionMinRatio) ||
        V.ElevatedTargetRatio>V.ElevatedMaxRatio || V.ElevatedMaxRatio>V.OverheadMaxRatio || V.Widths.IsEmpty() || V.Widths.Num()>3)return false;
    if(V.ParapetModule && (V.ParapetModule->GetBoundingBox().GetSize().ContainsNaN() || V.ParapetModule->GetBoundingBox().GetSize().GetMin()<.1f))return false;
    for(const auto* Palette:{&V.WoodSurfaces,&V.ConcreteSurfaces,&V.ContainerSurfaces})if(Palette->Num()>8 || Palette->Contains(nullptr))return false;
    // Either the complete prepared kit or a pure greybox (tests); never a mix.
    const TArray<UStaticMesh*> Shared={V.SideWallBay,V.SideWallLamps,V.Post,V.EndPortal};
    const bool Kit=!Shared.Contains(nullptr);
    if(!Kit && Shared.ContainsByPredicate([](UStaticMesh* M){return M!=nullptr;}))return false;
    auto Near=[](float A,float B,float Tolerance=2){return FMath::IsNearlyEqual(A,B,Tolerance);};
    auto Valid=[](UStaticMesh* M){return M && !M->GetBoundingBox().GetSize().ContainsNaN() && M->GetBoundingBox().GetSize().GetMin()>.1;};
    if(Kit)
    {
        for(auto* M:Shared)if(!Valid(M))return false;
        const FVector Wall=V.SideWallBay->GetBoundingBox().GetSize(),Post=V.Post->GetBoundingBox().GetSize();
        if(!Near(Wall.Y,380) || !Near(Wall.Z,300) || !Near(Post.Z,300) || !Near(Post.X,20) || !Near(Post.Y,20))return false;
    }
    TSet<int32> Seen;
    for(const auto& K:V.Widths)
    {
        if((K.WidthCm!=1600 && K.WidthCm!=2000 && K.WidthCm!=2400) || Seen.Contains(K.WidthCm))return false;
        Seen.Add(K.WidthCm);
        const TArray<UStaticMesh*> Parts={K.RoofBay,K.RoofArch,K.RoofLamps,K.EndGround,K.EndBand,K.EndCrown};
        if(!Kit){if(Parts.ContainsByPredicate([](UStaticMesh* M){return M!=nullptr;}))return false;continue;}
        for(auto* M:Parts)if(!Valid(M))return false;
        if(!Near(K.RoofArch->GetBoundingBox().GetSize().X,K.WidthCm) || !Near(K.RoofBay->GetBoundingBox().GetSize().Y,400) ||
           !Near(K.RoofArch->GetBoundingBox().GetSize().Y,20) || !Near(K.EndBand->GetBoundingBox().GetSize().Z,300) ||
           K.EndBand->GetBoundingBox().GetSize().X>K.WidthCm)return false;
    }
    // The last attempt is the verified reference layout: one narrowest nave,
    // twenty-two bays, two bands and the base structures only.
    const bool Reference=Attempt==FireTrainingAttempts-1;
    FRandomStream R(Reference ? 0x46495245:int32(HashCombine(uint32(Seed),uint32(Attempt)*0x9E3779B9u)));
    auto Pick=[&](float A,float B){return float(R.RandRange(int32(A/50),int32(B/50))*50);};
    auto Chance=[&](float P){return !Reference && R.FRand()<P;};
    const FERWarehouseWidthKit* Width=nullptr;int32 Bays=0,Naves=1;
    {
        struct FOption{const FERWarehouseWidthKit* Kit;int32 Naves,Bays;};
        TArray<FOption> Options;
        for(const auto& K:V.Widths)for(int32 Count=V.MinNaves;Count<=V.MaxNaves;++Count)for(int32 N=V.MinBays;N<=V.MaxBays;++N)
        {
            if(T.ReflectionBuild && N%2!=0)continue;
            const float Total=K.WidthCm*Count,Area=T.SizingTeamSize>0 ? (Total+60)*(420.f*N+80)/1e4f:Total*420.f*N/1e4f;
            if(Area>=V.FloorArea.X && Area<=V.FloorArea.Y && 420.f*N<=Total*V.MaxAspect && 420.f*N+80>=(Total+60)*1.2f)Options.Add({&K,Count,N});
        }
        if(Reference)
        {
            for(const auto& K:V.Widths)if(!Width || K.WidthCm<Width->WidthCm)Width=&K;
            Bays=FMath::Clamp(22,V.MinBays,V.MaxBays);Naves=V.MinNaves;
            if(T.SizingTeamSize>0)
            {
                if(Options.IsEmpty())return false;
                const auto& O=Options[Options.Num()/2];Width=O.Kit;Naves=O.Naves;Bays=O.Bays;
            }
        }
        else
        {
            if(Options.IsEmpty()){UE_LOG(LogTemp,Warning,TEXT("ER_FIRE_TRAINING_NO_SIZE seed=%d"),Seed);return false;}
            // Nave count, then kit width, then length: every width class is equally likely.
            TArray<int32> Counts;for(const auto& O:Options)Counts.AddUnique(O.Naves);
            Naves=Counts[R.RandRange(0,Counts.Num()-1)];
            TArray<int32> Widths;for(const auto& O:Options)if(O.Naves==Naves)Widths.AddUnique(O.Kit->WidthCm);
            const int32 Chosen=Widths[R.RandRange(0,Widths.Num()-1)];
            TArray<int32> Lengths;for(const auto& O:Options)if(O.Naves==Naves && O.Kit->WidthCm==Chosen){Width=O.Kit;Lengths.Add(O.Bays);}
            Bays=Lengths[R.RandRange(0,Lengths.Num()-1)];
        }
    }
    const int32 Bands=Reference ? FMath::Clamp(2,V.MinHeightBands,V.MaxHeightBands):R.RandRange(V.MinHeightBands,V.MaxHeightBands);
    const float NaveW=Width->WidthCm,W=NaveW*Naves,Length=420.f*Bays,HL=Length*.5f,Eave=300.f*Bands,Ridge=Eave+200;
    if(Length+80<(W+60)*1.2f)return false;
    L.ArenaSize=FVector(W+60,Length+80,Ridge+20);
    L.WarehouseBays=Bays;L.WarehouseBands=Bands;L.WarehouseNaves=Naves;L.WarehouseNaveWidth=NaveW;
    L.WarehouseWidth=W;L.IndoorCeiling=Eave;L.LayoutAttempt=Attempt;
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
    // Kit modules keep unit scale: the piece box is the mesh's own bounds.
    auto Module=[&](UStaticMesh* Mesh,FVector Origin,bool Pair,bool Visual,bool Roof=false)
    {
        const FBox B=Mesh->GetBoundingBox();
        auto& P=Pieces.AddDefaulted_GetRef();P.Center=Origin+B.GetCenter();P.Size=B.GetSize();
        if(P.Center.Z-P.Size.Z*.5f<0)P.Center.Z=P.Size.Z*.5f;
        if(!Pair){P.Center.X=0;P.Center.Y=0;}
        P.Material=EERBlockMaterial::Metal;P.Destructible=false;P.Mirrored=Pair;P.FixedMesh=Mesh;
        P.VisualOnly=Visual;P.MeshCollision=!Visual;P.Roof=Roof;
    };
    // Half-turn emission for a complete symmetric set: pieces centred on the
    // west/south side keep their pair, centred ones stay single, the rest are
    // the copies and are skipped.
    auto Emit=[&](UStaticMesh* Mesh,FVector Origin,bool Visual,bool Roof=false)
    {
        const FVector C=Origin+Mesh->GetBoundingBox().GetCenter();
        const bool CX=FMath::Abs(C.X)<1,CY=FMath::Abs(C.Y)<1;
        if(C.X>1 || (CX && C.Y>1))return;
        Module(Mesh,Origin,!(CX && CY),Visual,Roof);
    };
    // ---- Envelope: repeated modules only. Bands raise walls; no floor is added.
    const float XO=-W*.5f,XI0=XO+20,XI1=-XO-20;
    // Valleys between naves: open column lines under a steel gutter.
    TArray<float> Valleys;for(int32 J=1;J<Naves;++J)Valleys.Add(XO+NaveW*J);
    if(Kit)
    {
        for(int32 K=0;K<Bays;++K)
        {
            const float Y=-HL+420*K+210;
            for(int32 Band=0;Band<Bands;++Band)Emit(V.SideWallBay,{XO,Y,300.f*Band},false);
            Emit(V.SideWallLamps,{XO,Y,300.f*(Bands-1)},true);
            for(int32 J=0;J<Naves;++J)
            {
                const float X=XO+NaveW*(J+.5f);
                Emit(Width->RoofBay,{X,Y,Eave},false,true);
                if(K%2==0)Emit(Width->RoofLamps,{X,Y,Eave},true,true);
            }
        }
        for(int32 Boundary=0;Boundary<=Bays;++Boundary)
        {
            const float Y=-HL+420*Boundary;
            // Shared boundary members exist once: posts belong to the boundary, not a bay.
            const TArray<float> Offsets=Boundary==0 ? TArray<float>{0,20}:Boundary==Bays ? TArray<float>{-20,0}:TArray<float>{-20,0,20};
            for(int32 Band=0;Band<Bands;++Band)for(float D:Offsets)
            {
                Emit(V.Post,{XO,Y+D,300.f*Band},false);
                for(float X:Valleys)Emit(V.Post,{X-10,Y+D,300.f*Band},false);
            }
            for(int32 J=0;J<Naves;++J)Emit(Width->RoofArch,{XO+NaveW*(J+.5f),Y,Eave},false,true);
        }
        for(int32 J=0;J<Naves;++J)for(float Y:{-HL-10,HL+10})
        {
            const float X=XO+NaveW*(J+.5f);
            Emit(Width->EndGround,{X,Y,0},false);Emit(V.EndPortal,{X,Y,0},true);
            for(int32 Band=1;Band<Bands;++Band)Emit(Width->EndBand,{X,Y,300.f*Band},false);
            Emit(Width->EndCrown,{X,Y,Eave},false);
        }
    }
    else
    {
        Box({XO+10,0,Eave*.5f},{20,Length+20,Eave},EERBlockMaterial::Metal,true);
        Box({0,-HL-5,Ridge*.5f},{W-40,10,Ridge},EERBlockMaterial::Metal,true);
    }
    for(float X:Valleys)
    {
        if(X>1)continue;
        // Hidden column volumes keep routes, cover and cargo clear of the posts.
        for(int32 Boundary=0;Boundary<=Bays;++Boundary)
        {
            const float Y=-HL+420*Boundary,Y0=Boundary==0 ? Y:Y-30,Y1=Boundary==Bays ? Y:Y+30;
            if(FMath::Abs(X)<1 && (Y0+Y1)*.5f>1)continue;
            auto& C=Box({X,(Y0+Y1)*.5f,Eave*.5f},{20,Y1-Y0,Eave},EERBlockMaterial::Metal,true);
            C.CollisionOnly=Kit;C.FixedSurface=Kit ? nullptr:C.FixedSurface;C.Mirrored=!(FMath::Abs(X)<1 && FMath::Abs((Y0+Y1)*.5f)<1);
            if(!C.Mirrored)C.Center=FVector(0,0,C.Center.Z);
        }
        auto& Gutter=Box({X,0,Eave+10},{40,Length,20},EERBlockMaterial::Metal,true);
        Gutter.Mirrored=FMath::Abs(X)>=1;if(!Gutter.Mirrored)Gutter.Center.X=0;
    }
    // Hidden backstops behind the thin sheets; the hidden ceiling closes the top.
    auto Backstop=[&](FVector Center,FVector Size){auto& B=Box(Center,Size,EERBlockMaterial::Metal,true);B.CollisionOnly=true;B.FixedSurface=nullptr;};
    Backstop({XO-15,0,L.ArenaSize.Z*.5f},{30,Length+80,L.ArenaSize.Z});
    Backstop({0,-HL-25,L.ArenaSize.Z*.5f},{W,30,L.ArenaSize.Z});

    // ---- Plan: west catwalk band, two lanes around central cargo, east rooms.
    const float Dw=220,DeckZ=300,CatwalkX=XI0+100;
    const float CatwalkZ=WarehouseCatwalkHeight(*Width,Naves,Eave);
    const float Dr=W<=1600 ? 400:W<=2000 ? Pick(450,500):Pick(500,600);
    const float XR0=XI1-Dr,XWL=XI0+Dw+170,XEL=XR0-170,CB0=XWL+150,CB1=XEL-150;
    const float YFront=-HL+1350;
    const bool Elevated=Bands>=2 && WarehouseRoofHeight(NaveW,Naves,Eave,XI0)>=DeckZ+250;
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
    auto Headroom=[&](float X0,float X1,float Top,float Clearance=250){return FMath::Min(WarehouseRoofHeight(NaveW,Naves,Eave,X0),WarehouseRoofHeight(NaveW,Naves,Eave,X1))>=Top+Clearance;};
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
    auto Ladder=[&](FVector Base,float Yaw,float Rise,bool Anchored,bool LevelExit=false)
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
        Reserved.Add(FTransform(F.GetRotation(),F.TransformPosition({-60,0,Rise+100}),{140,180,200}));
    };
    Reserve({0,-HL+300,150},{W,620,300}); // Spawn strip and front crossing.

    // ---- Protected bases. The warehouse wall is never the base's only cover.
    // A concrete court with two offset gates surrounds an enterable one-storey
    // barracks. Its steel roof always has two independent exterior ladders.
    const float SpawnX=Naves%2 ? 0:-NaveW*.5f;
    const float BX0=SpawnX-300,BX1=SpawnX+300,BY0=-HL+70,BY1=-HL+650;
    const float CX0=SpawnX-600,CX1=SpawnX+600,CY0=-HL+40,CY1=-HL+1150;
    if(!Elevated || !Headroom(BX0,BX1,DeckZ))return false;
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

    // ---- Mandatory steel H-route: two full-length flank walkways joined by
    // a cross-map bridge. Only the ladder mouths and bridge junctions are open.
    const float YA=-HL+1500,YB=HL-1500;
    const FBox Flank(FVector(CatwalkX,YA,CatwalkZ-20),FVector(CatwalkX+Dw,YB,CatwalkZ));
    // Mid-bay crossings avoid full-height columns on every nave line. With
    // even bay counts, paired crossbars at +/-210cm pass either side of the rib.
    const bool DoubleBridge=Naves>1 && Bays%2==0;
    const float BridgeY=DoubleBridge ? 210:0;
    const FBox Bridge(FVector(CatwalkX+Dw,BridgeY-110,CatwalkZ-20),FVector(DoubleBridge ? -CatwalkX-Dw:0,BridgeY+110,CatwalkZ));
    if(YB-YA<600 || !Fits({Flank,Bridge}) || !Headroom(CatwalkX,CatwalkX+Dw,CatwalkZ,200))return false;
    Slab(CatwalkX,CatwalkX+Dw,YA,YB,CatwalkZ);Slab(Bridge.Min.X,Bridge.Max.X,Bridge.Min.Y,Bridge.Max.Y,CatwalkZ);
    Elevate(Flank,2);Elevate(Bridge,4);
    L.WarehouseCatwalks={Flank,HalfTurn(Flank),Bridge,HalfTurn(Bridge)};
    const float LadderX=CatwalkX+Dw*.5f;
    // End ladders avoid the ground patrol lanes and room walls. Their notched
    // mouths let the player step horizontally onto the deck below the lights.
    for(const FVector2D End:{FVector2D(YA,-90),FVector2D(YB,90)})
    {
        const FVector Base(LadderX,End.X+(End.Y<0 ? -35:35),0);
        Ladder(Base,End.Y,CatwalkZ,true,true);ReserveLadder(Base,End.Y,CatwalkZ);
        Rail(CatwalkX+5,LadderX-85,End.X<0 ? YA:YB-5,End.X<0 ? YA+5:YB,CatwalkZ);
        Rail(LadderX+85,CatwalkX+Dw-5,End.X<0 ? YA:YB-5,End.X<0 ? YA+5:YB,CatwalkZ);
    }
    Rail(CatwalkX,CatwalkX+5,YA,YB,CatwalkZ);
    TArray<FVector2D> Mouths{{BridgeY-110,BridgeY+110}};
    if(DoubleBridge)Mouths.Add({-BridgeY-110,-BridgeY+110});
    Mouths.Sort([](const FVector2D& A,const FVector2D& B){return A.X<B.X;});
    float Cursor=YA;
    for(const auto& Mouth:Mouths){Rail(CatwalkX+Dw-5,CatwalkX+Dw,Cursor,Mouth.X,CatwalkZ);Cursor=Mouth.Y;}
    Rail(CatwalkX+Dw-5,CatwalkX+Dw,Cursor,YB,CatwalkZ);
    Rail(Bridge.Min.X,Bridge.Max.X,Bridge.Min.Y,Bridge.Min.Y+5,CatwalkZ);
    Rail(Bridge.Min.X,Bridge.Max.X,Bridge.Max.Y-5,Bridge.Max.Y,CatwalkZ);
    // Metadata includes the entire continuous route, not only a half-bridge.
    L.CrossingRoutes.Add({{LadderX,YA+100,CatwalkZ+100},{LadderX,YB-100,CatwalkZ+100}});
    L.CrossingRoutes.Add({{-LadderX,YA+100,CatwalkZ+100},{-LadderX,YB-100,CatwalkZ+100}});
    L.CrossingRoutes.Add({{LadderX,BridgeY,CatwalkZ+100},{-LadderX,BridgeY,CatwalkZ+100}});
    if(DoubleBridge)L.CrossingRoutes.Add({{LadderX,-BridgeY,CatwalkZ+100},{-LadderX,-BridgeY,CatwalkZ+100}});
    L.FeatureNames.Append({TEXT("Catwalk"),TEXT("CatwalkBridge"),TEXT("SteelParapets")});
    // Keep the narrow high decks clear of the original wall knee brackets.
    // Local timber corridor beneath the south-west walkway, with two real doors.
    const float CorridorEnd=FMath::Min(-450.f,YA+1260);
    if(CorridorEnd-YA>=600)
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

    // ---- Larger halls reserve empty plazas in the central band before furnishing.
    const float Area=W*Length/1e4f;
    const int32 PlazaCount=Reference ? 0:FMath::Min(V.MaxPlazas,FMath::FloorToInt(Area/2000));
    for(int32 Try=0;Try<12 && L.Plazas.Num()<PlazaCount*2;++Try)
    {
        const float SX=FMath::Min(Pick(1000,1800),CB1-CB0),SY=Pick(1000,1800);
        const float YMin=-HL+1550+SY*.5f,YMax=-300-SY*.5f;
        if(SX<800 || YMax<YMin)break;
        const FVector C(FMath::Clamp(Pick(CB0,CB1),CB0+SX*.5f,CB1-SX*.5f),FMath::Clamp(Pick(YMin,YMax),YMin,YMax),300);
        const FBox Plaza(C-FVector(SX*.5f,SY*.5f,300),C+FVector(SX*.5f,SY*.5f,300));
        if(L.Plazas.ContainsByPredicate([&](const FBox& O){return O.ExpandBy(400).Intersect(Plaza);}))continue;
        L.Plazas.Add(Plaza);L.Plazas.Add(HalfTurn(Plaza));Reserve(C,Plaza.GetSize());
        L.FeatureNames.Add(TEXT("Plaza"));
    }

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
    auto Accept=[&](int32 First,int32 FirstLadder,float Margin)
    {
        TArray<FTransform> Added;Frames(First,Added);bool Ok=true;
        // New ladders need a clear standing approach in front of their rungs.
        for(int32 I=FirstLadder;I<L.Ladders.Num() && Ok;I+=2)
            if(L.Ladders[I].Frame.GetLocation().Z<1)for(const auto& O:Occupied)if(Overlap(Approach(L.Ladders[I]),O,0)){Ok=false;break;}
        for(int32 I=0;I<Added.Num() && Ok;++I)
        {
            const auto& F=Added[I];
            const FBox B=FBox(-F.GetScale3D()*.5,F.GetScale3D()*.5).TransformBy(FTransform(F.GetRotation(),F.GetLocation()));
            Ok=B.Min.X>=XI0+40 && B.Max.X<=XI1-40 && B.Min.Y>=-HL+40 && B.Max.Y<=HL-40;
            for(const auto& O:Occupied)if(Ok && Overlap(F,O,Margin))Ok=false;
            for(const auto& O:Reserved)if(Ok && (Overlap(F,O,0) || Overlap(F,FTransform(FRotator(0,180,0),FVector(-O.GetLocation().X,-O.GetLocation().Y,O.GetLocation().Z),O.GetScale3D()),0)))Ok=false;
            for(int32 J=0;J<Patrol.Num() && Ok;++J)if(Blocked(Patrol[J],Patrol[(J+1)%Patrol.Num()],F,93))Ok=false;
            for(const auto* Paths:{&L.RoomAccessRoutes,&L.CrossingRoutes,&L.StairRoutes})for(const auto& Path:*Paths)
                for(int32 J=1;J<Path.Num() && Ok;++J)
                    for(int32 Copy=0;Copy<2 && Ok;++Copy)
                    {
                        const FVector A=Copy ? FVector(-Path[J-1].X,-Path[J-1].Y,Path[J-1].Z):Path[J-1],B2=Copy ? FVector(-Path[J].X,-Path[J].Y,Path[J].Z):Path[J];
                        if(Blocked(A,B2,F,93))Ok=false;
                    }
        }
        if(!Ok){Pieces.SetNum(First);L.Ladders.SetNum(FirstLadder);return false;}
        Occupied.Append(Added);return true;
    };
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
    for(float Y=-HL+950;Y<-350;Y+=Pick(300,450))for(float X:CoverX)
    {
        if(!Chance(.55f))continue;
        const int32 Kind=R.RandRange(0,3);const int32 First=Pieces.Num();
        const FVector P(X+Pick(-100,100),Y+Pick(-100,100),0);
        if(Kind==0)Box(P+FVector(0,0,55),{110,110,110},EERBlockMaterial::Wood,false);
        else if(Kind==1){Box(P+FVector(0,0,55),{110,110,110},EERBlockMaterial::Wood,false);Box(P+FVector(0,0,165),{110,110,110},EERBlockMaterial::Wood,false);}
        else if(Kind==2)Box(P+FVector(0,0,45),R.RandBool() ? FVector(300,50,90):FVector(50,300,90),EERBlockMaterial::Concrete,false);
        else Box(P+FVector(0,0,100),R.RandBool() ? FVector(240,12,200):FVector(12,240,200),EERBlockMaterial::Wood,false);
        if(Accept(First,L.Ladders.Num(),60))++Cover;
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
    return Pieces.Num()<=4096;
}
}
