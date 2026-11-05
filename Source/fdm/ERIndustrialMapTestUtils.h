#pragma once
#include "ERIndustrialFence.h"
#include "ERProceduralMap.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "Components/DynamicMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/InstancedStaticMeshComponent.h"

// Deterministic authority cuts for probes: gameplay still uses real shot/blast
// damage. These isolate structural connectivity and route changes in tests.
namespace ERIndustrialTest
{
// Palette identity is part of the layout digest; verify the actual rendered chunks as well.
inline bool FactoryPaletteMatches(AERProceduralMap& Map)
{
    auto Fail=[](int32 Line){UE_LOG(LogTemp,Warning,TEXT("ER_PALETTE_MISMATCH check_line=%d"),Line);return false;};
    const auto* T=Map.LayoutConfig.TemplateAsset.Get();
    if(!T || !T->CentralBuilding.SteelSurface || T->CentralBuilding.ContainerSurfaces.IsEmpty())return Fail(__LINE__);
    const auto& V=T->CentralBuilding;
    const auto& L=Map.GetLayout();const bool Themed=L.ThemeIndex!=INDEX_NONE;
    auto* Steel=L.ThemeMetalDetails ? L.ThemeMetalDetails:Themed ? L.ThemeMaterials[uint8(EERBlockMaterial::Metal)]:V.SteelSurface.Get();
    if(V.FenceModule)
    {
        TInlineComponentArray<UInstancedStaticMeshComponent*> Instances(&Map);bool Found=false;
        for(const auto* Mesh:Instances)if(Mesh->GetName()==TEXT("IndustrialFenceModules"))
        {Found=true;if(Mesh->GetStaticMesh()!=V.FenceModule || Mesh->GetMaterial(0)!=Steel || Mesh->GetInstanceCount()==0)return Fail(__LINE__);}
        if(!Found)return Fail(__LINE__);
    }
    // Runtime drops the duplicate descriptor array after building its cells.
    FERProceduralLayout AuthoredLayout;
    if(Themed && !ERProcedural::Build(Map.LayoutConfig,Map.Config,AuthoredLayout,Map.CellSettings))return Fail(__LINE__);
    TMap<FString,UMaterialInterface*> Expected;
    for(const auto& P:Map.GetCellWorld().GetPanels())
    {
        if(Themed)
        {
            const auto* Authored=AuthoredLayout.Panels.FindByPredicate([&](const auto& D){return D.Id==P.Id;});
            if(!Authored || P.Surface!=Authored->Surface)return Fail(__LINE__);
        }
        if(!Themed && P.Material==EERBlockMaterial::Metal && (P.Strength==8 ? !V.ContainerSurfaces.Contains(P.Surface):P.Surface!=V.SteelSurface))return Fail(__LINE__);
        const auto* Palette=P.Material==EERBlockMaterial::Concrete ? &V.ConcreteSurfaces:P.Material==EERBlockMaterial::Brick ? &V.BrickSurfaces:P.Material==EERBlockMaterial::Wood ? &V.WoodSurfaces:nullptr;
        if(!Themed && Palette && !Palette->IsEmpty() && !Palette->Contains(P.Surface))return Fail(__LINE__);
        if(P.Surface)Expected.Add(FString::Printf(TEXT("Cells_P%d"),P.Id),P.Surface);
    }
    if(Map.GetLayout().GroundSurface)
    {
        TInlineComponentArray<UStaticMeshComponent*> Floors(&Map);bool Found=false;
        for(const auto* Mesh:Floors)if(Mesh->GetName()==TEXT("IndestructibleFloor"))
        {Found=true;if(Mesh->GetMaterial(0)!=Map.GetLayout().GroundSurface)return Fail(__LINE__);}
        if(!Found)return Fail(__LINE__);
    }
    TInlineComponentArray<UDynamicMeshComponent*> Components(&Map);int32 Matched=0;
    for(const auto* Mesh:Components)
    {
        FString Key,Chunk;
        if(Mesh->GetName().Split(TEXT("_C"),&Key,&Chunk))
            if(auto* Surface=Expected.Find(Key)){if(Mesh->GetMaterial(0)!=*Surface)return Fail(__LINE__);++Matched;}
        if(Mesh->GetName().StartsWith(TEXT("IndustrialFenceMetal_")) && Mesh->GetMaterial(0)!=Steel)return Fail(__LINE__);
    }
    return Matched>0;
}
inline const FERLayoutFixedPiece* PerimeterFence(const FERProceduralLayout& L)
{
    return L.FixedPieces.FindByPredicate([&](const auto& P)
    {
        if(!P.PawnBarrier)return false;
        const FVector Center=P.Transform.GetLocation();
        // The circular divider also uses fence modules. Only the outer ring
        // sits on the perimeter plinth checked by the client probe.
        if(L.CircularArena)return
            FMath::Square(Center.X/(L.ArenaSize.X*.5-25))+
            FMath::Square(Center.Y/(L.ArenaSize.Y*.5-25))>=1;
        return Center.Y<-L.ArenaSize.Y*.5+10 &&
            Center.Z<ERIndustrialFence::PlinthHeightCm+ERIndustrialFence::ModuleHeightCm;
    });
}
inline bool PerimeterMatches(AERProceduralMap& Map,const FCollisionQueryParams& Q)
{
    const auto* Fence=PerimeterFence(Map.GetLayout());if(!Fence)return false;
    const FVector Center=Fence->Transform.GetLocation(),Normal=Fence->Transform.GetRotation().GetAxisY();
    const float MetalHalf=ERIndustrialFence::ModuleHeightCm*.5f;
    auto Ray=[&](float Z){FHitResult Hit;const FVector P(Center.X,Center.Y,Z);return Map.GetWorld()->LineTraceSingleByChannel(Hit,P-Normal*100,P+Normal*100,ECC_Pawn,Q);};
    return Ray(Center.Z-MetalHalf-ERIndustrialFence::PlinthHeightCm*.5f) && Ray(Center.Z) && !Ray(Center.Z+MetalHalf+1);
}
inline bool FenceAperturesMatch(AERProceduralMap& Map,const FCollisionQueryParams& Q)
{
    const auto& L=Map.GetLayout();
    const auto* Fence=PerimeterFence(L);
    if(!Fence)return false;
    int32 Blocked=0;FHitResult Hit;
    const FVector Center=Fence->Transform.GetLocation(),Size=Fence->Transform.GetScale3D()*100;
    const FVector Along=Fence->Transform.GetRotation().GetAxisX(),Normal=Fence->Transform.GetRotation().GetAxisY();
    for(int32 I=0;I<64;++I)
    {
        const FVector P=Center+Along*((-.45+(I%8)*.9/7)*Size.X)+FVector(0,0,(-.45+(I/8)*.9/7)*Size.Z);
        const bool Metal=Map.GetWorld()->LineTraceSingleByChannel(Hit,P-Normal*60,P+Normal*60,ECC_GameTraceChannel1,Q);
        Blocked+=Metal;
        if(Map.GetWorld()->LineTraceSingleByChannel(Hit,P+Normal*60,P-Normal*60,ECC_GameTraceChannel1,Q)!=Metal)return false;
        if(Map.GetWorld()->LineTraceSingleByChannel(Hit,P-Normal*60,P+Normal*60,ECC_Visibility,Q)!=Metal)return false;
    }
    return Blocked>0 && Blocked<64;
}
inline bool IntermediateCoverMatches(AERProceduralMap& Map,bool Breached=false)
{
    auto* World=Map.GetWorld();if(!World || Map.GetLayout().CoverSites.IsEmpty())return false;
    FCollisionQueryParams Q(SCENE_QUERY_STAT(ERIntermediateCover),false);
    for(TActorIterator<APawn> It(World);It;++It)Q.AddIgnoredActor(*It);
    const auto& Frame=Map.GetActorTransform();FHitResult Hit;
    for(int32 I=0;I<Map.GetLayout().CoverSites.Num();++I)
    {
        const auto& Site=Map.GetLayout().CoverSites[I];
        if(World->SweepSingleByChannel(Hit,Frame.TransformPosition(Site.Entry),Frame.TransformPosition(Site.Hide),FQuat::Identity,ECC_Pawn,FCollisionShape::MakeCapsule(34,Site.HalfHeight),Q))return false;
        const bool Hidden=World->LineTraceSingleByChannel(Hit,Frame.TransformPosition(Site.SightFrom),Frame.TransformPosition(Site.SightTo),ECC_GameTraceChannel1,Q);
        if(Hidden!=(I!=0 || !Breached))return false;
    }
    return true;
}
inline FERBlockBatchResult Cut(AERProceduralMap& Map,const FTransform& Frame,const FBox& Region,EERBlockMaterial Material)
{
    TMap<FERCellId,float> Damage;const auto& W=Map.GetCellWorld();
    const FBox Broad=Region.TransformBy(Frame);
    for(int32 Index=0;Index<W.GetPanels().Num();++Index)
    {
        const auto& P=W.GetPanels()[Index];if(P.Material!=Material || !P.Bounds.Intersect(Broad))continue;
        const FBox Local=Region.TransformBy(Frame*P.Transform.Inverse());FIntVector Lo,Hi;
        for(int32 A=0;A<3;++A){Lo[A]=FMath::Max(0,FMath::FloorToInt(Local.Min[A]/P.Cell[A]));Hi[A]=FMath::Min(P.Dims[A]-1,FMath::FloorToInt(Local.Max[A]/P.Cell[A]));}
        for(int32 Z=Lo.Z;Z<=Hi.Z;++Z)for(int32 Y=Lo.Y;Y<=Hi.Y;++Y)for(int32 X=Lo.X;X<=Hi.X;++X)
        {
            const FERCellId Id{Index,X,Y,Z};
            if(W.IsAlive(Id) && Region.IsInsideOrOn(Frame.InverseTransformPosition(W.GetCellCenter(Id))))Damage.Add(Id,TNumericLimits<float>::Max());
        }
    }
    return Map.ApplyCellBatch(Damage);
}
inline void Settle(AERProceduralMap& Map)
{for(int32 I=0;I<512 && Map.IsSupportPending();++I)Map.StepSupport();}
inline bool ContainerBlocked(UWorld& World,const FERLayoutContainer& C,const FCollisionQueryParams& Q)
{
    FHitResult Hit;const float Z=-C.Size.Z*.5+100;
    return World.SweepSingleByChannel(Hit,C.Frame.TransformPosition({0,-C.Size.Y*.5-100,Z}),C.Frame.TransformPosition({0,C.Size.Y*.5+100,Z}),FQuat::Identity,ECC_Pawn,FCollisionShape::MakeCapsule(34,92.5),Q);
}
inline bool BridgeSupported(UWorld& World,FVector Top,const FCollisionQueryParams& Q)
{return World.LineTraceTestByChannel(Top+FVector(0,0,130),Top-FVector(0,0,5),ECC_WorldStatic,Q);}
inline FVector BreachPoint(const FERProceduralLayout& L)
{return FVector(-L.CentralBuildingSize.X*.5-950,0,0);}
inline bool WallBlocked(UWorld& World,FVector P,const FCollisionQueryParams& Q)
{FHitResult Hit;return World.SweepSingleByChannel(Hit,P+FVector(0,-100,100),P+FVector(0,100,100),FQuat::Identity,ECC_Pawn,FCollisionShape::MakeCapsule(34,92.5),Q);}
inline void BreachContainer(AERProceduralMap& Map,const FERLayoutContainer& C)
{Cut(Map,C.Frame,FBox(FVector(-120,-C.Size.Y*.5-2,-C.Size.Z*.5-1),FVector(120,C.Size.Y*.5+2,100)),EERBlockMaterial::Metal);}
inline void CutBridgeSupports(AERProceduralMap& Map,FVector Top)
{Cut(Map,FTransform(FVector(Top.X,Top.Y,0)),FBox(FVector(-105,-610,0),FVector(105,610,80)),EERBlockMaterial::Wood);}
inline void BreachWall(AERProceduralMap& Map)
{Cut(Map,FTransform(BreachPoint(Map.GetLayout())),FBox(FVector(-100,-20,0),FVector(100,20,245)),EERBlockMaterial::Brick);}
}
