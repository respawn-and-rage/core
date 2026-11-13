#include "ERBlockRoom.h"
#include "ERClaymore.h"
#include "ERPerformanceSettings.h"
#include "ERBlockDebris.h"
#include "ERCellNetShard.h"
#include "ERProceduralMap.h"
#include "ERPlayerController.h"
#include "Net/UnrealNetwork.h"
#include "Misc/Crc.h"
#include "TimerManager.h"
#include "ERGrenadeExplosion.h"
#include "Components/DecalComponent.h"
#include "Components/DynamicMeshComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "Kismet/GameplayStatics.h"
#include "EngineUtils.h"
#include "Misc/App.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SpotLightComponent.h"
#include "Engine/DamageEvents.h"
#include "Engine/StaticMesh.h"
#include "HAL/PlatformTime.h"
#include "HAL/PlatformMemory.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "PhysicsEngine/BodyInstance.h"
#include "PhysicsEngine/BodySetup.h"
#include "ProfilingDebugging/ResourceSize.h"
#include "UObject/ConstructorHelpers.h"
#include "Sound/SoundAttenuation.h"
#include "Sound/SoundWave.h"

AERBlockRoom::AERBlockRoom()
{
    bReplicates=true;bAlwaysRelevant=true;SetReplicateMovement(true);SetNetUpdateFrequency(20);
    PrimaryActorTick.bCanEverTick = false;
    RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
    BlockWorld = CreateDefaultSubobject<UERBlockWorld>(TEXT("BlockWorld"));
    MaterialCatalog = CreateDefaultSubobject<UERBlockMaterialCatalog>(TEXT("DefaultMaterials"));
    Floor = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("IndestructibleFloor"));
    Floor->SetupAttachment(RootComponent);
    Floor->SetCollisionProfileName(TEXT("BlockAll"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube"));
    BlockMesh = Cube.Object;
    Floor->SetStaticMesh(Cube.Object);
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> Lamp(ERMapLighting::LampMaterialPath);
    MapLampSurface = Lamp.Object;
    static ConstructorHelpers::FObjectFinder<USoundWave> GlassBreakUnfa(
        TEXT("/Game/EclipseRecoil/Audio/GlassBreak/S_ER_GlassBreak_221528"));
    static ConstructorHelpers::FObjectFinder<USoundWave> GlassBreakDatasoundsample(
        TEXT("/Game/EclipseRecoil/Audio/GlassBreak/S_ER_GlassBreak_41348"));
    static ConstructorHelpers::FObjectFinder<USoundAttenuation> GlassAttenuation(
        TEXT("/Game/EclipseRecoil/Audio/GlassBreak/SA_ER_GlassBreak"));
    if (GlassBreakUnfa.Object) GlassBreakSounds.Add(GlassBreakUnfa.Object);
    if (GlassBreakDatasoundsample.Object) GlassBreakSounds.Add(GlassBreakDatasoundsample.Object);
    GlassBreakAttenuation = GlassAttenuation.Object;
}
bool AERBlockRoom::IsStandaloneLab() const
{
    return CanMutateBlocks() && GetNetMode()==NM_Standalone;
}
void AERBlockRoom::BeginPlay()
{
    Super::BeginPlay();
    bNetworkBegunPlay=true;
    if (HasAuthority()) Regenerate();
    else {SetMapCollision(false);OnRep_Network();}
}
bool AERBlockRoom::BuildCellWorld(const TArray<FERCellPanelDesc>& Panels)
{
    if(HasAuthority()){MaterialRules=ERServer::Rules(GetWorld());MaterialRules.Ready=true;}
    const bool HasMaterialOverrides=MaterialRules.Values.ContainsByPredicate([](const auto& V)
    {const FString Key=V.Key.ToString();return Key.StartsWith(TEXT("Structure.")) || Key.StartsWith(TEXT("Material."));});
    if(HasMaterialOverrides && MaterialCatalog)
    {
        if(!AuthoredMaterialCatalog)AuthoredMaterialCatalog=MaterialCatalog;
        MaterialCatalog=DuplicateObject<UERBlockMaterialCatalog>(AuthoredMaterialCatalog,this);
        ERServer::TuneMaterials(MaterialCatalog,MaterialRules);
    }
    else if(AuthoredMaterialCatalog)MaterialCatalog=AuthoredMaterialCatalog;
    FString Error;
    if (!MaterialCatalog || !CellWorld.Build(Panels, CellSettings, MaterialCatalog, Error))
    {
        UE_LOG(LogTemp, Error, TEXT("ER_CELL_GENERATION_REJECTED %s"), *Error);
        return false;
    }
    return true;
}
bool AERBlockRoom::GenerateLogicalWorld()
{
    TArray<FERCellPanelDesc> Out;
    const float T = Config.BlockSize.Y, H = Config.Height, I = Config.InteriorSize;
    for (int32 Wall = 0; Wall < 4; ++Wall)
    {
        // Returns own the corners; materials Drywall, Brick, Concrete, Drywall.
        const bool bAlongY = Wall % 2 != 0;
        const float Length = I + (bAlongY ? 2 * T : 0), Offset = (I + T) * .5f;
        const FTransform Slot = bAlongY ? FTransform(FRotator(0, 90, 0), FVector((Wall == 1 ? Offset : -Offset) + T * .5f, -Length * .5f, 0))
                                        : FTransform(FRotator::ZeroRotator, FVector(-Length * .5f, (Wall == 0 ? -Offset : Offset) - T * .5f, 0));
        FERCellWorld::AddConstruction(Wall, static_cast<EERBlockMaterial>(Wall % 3), Slot, FVector(Length, T, H), CellSettings, Out);
    }
    // Free-standing samples of the thin constructions, west half of the room.
    const FVector Sample(150, T, 225);
    FERCellWorld::AddConstruction(4, EERBlockMaterial::Wood, FTransform(FRotator(0, 90, 0), FVector(-150 + T * .5f, -230, 0)), Sample, CellSettings, Out);
    FERCellWorld::AddConstruction(5, EERBlockMaterial::Metal, FTransform(FRotator(0, 90, 0), FVector(-150 + T * .5f, 80, 0)), Sample, CellSettings, Out);
    for (auto& P : Out) P.bAnchorBottom = true;
    return BuildCellWorld(Out);
}
bool AERBlockRoom::Regenerate()
{
    TRACE_CPUPROFILER_EVENT_SCOPE(ERBlock_Regenerate);
    const double Start = FPlatformTime::Seconds();
    if(HasAuthority()){bGenerationValid=false;ClearCellRecoveries();}
    if ((!CanMutateBlocks() && !bApplyingNetwork) || !BlockMesh || !GenerateLogicalWorld()) return false;
    if(HasAuthority())AERClaymore::RemoveFor(GetWorld());
    ClearCollapse();
    RebuildProfile={}; GenerationProfile={};
    if (HasAuthority() && Collapse.Enabled && !Support.Build(CellWorld, Collapse.GroundAnchors, Collapse.GroundZ))
        UE_LOG(LogTemp,Warning,TEXT("ER_BLOCK_SUPPORT_UNAVAILABLE contact budget exceeded"));
    GenerationProfile.LogicalMs=(FPlatformTime::Seconds()-Start)*1000;
    ClearCells();
    LastHit = TEXT("none"); LastCombat = TEXT("none"); LastUpdateMs = 0; LastGroupsRebuilt = 0; DamageRevision = 0;
    ClearOwnedEffects();
    ConfigureFloors();
    ConfigureMapLamps();
    const double CellStart=FPlatformTime::Seconds();
    BuildCellComponents();
    GenerationProfile.AddMs=(FPlatformTime::Seconds()-CellStart)*1000;
    GenerationProfile.MeshMs=RebuildProfile.MeshMs; GenerationProfile.CollisionMs=RebuildProfile.CollisionMs;
    GenerationProfile.Quads=RebuildProfile.Quads; GenerationProfile.Boxes=RebuildProfile.Boxes; GenerationProfile.Groups=RebuildProfile.Groups;
    GenerationMs = float((FPlatformTime::Seconds() - Start) * 1000);
    GenerationProfile.TotalMs=GenerationMs;
    if(HasAuthority())SetMapCollision(true);
    if(HasAuthority() && !ValidateGeneratedCollision())
    {
        SetMapCollision(false);
        UE_LOG(LogTemp,Error,TEXT("ER_MAP_INITIAL_SPAWN_REJECTED real visibility or projectile line remains open"));
        return false;
    }
    bGenerationValid=true;
    if (HasAuthority() && GetNetMode()!=NM_Standalone) PublishCellGeneration();
    UE_LOG(LogTemp, Display, TEXT("ER_BLOCK_GENERATED %s support_contacts=%d support_build=%.2fms"), *GetStatistics(), Support.GetContactCount(), Support.BuildMs);
    OnGenerationFinished();
    return true;
}

void AERBlockRoom::ConfigureMapLamps()
{
    for (const auto& Emitter : MapLampEmitters)
    {
        RemoveInstanceComponent(Emitter.Get());
        Emitter->DestroyComponent();
    }
    MapLampEmitters.Reset();
    LastLampRemovalCount = -1;
    if (GetNetMode() == NM_DedicatedServer || !FApp::CanEverRender() || !GetWorld()->IsGameWorld()) return;
    int32 Banks = 0;
    for (const FERMapLamp& Lamp : MapLamps)
    {
        auto* Emitter = NewObject<USpotLightComponent>(this);
        AddInstanceComponent(Emitter);
        Emitter->SetupAttachment(GetRootComponent());
        Emitter->SetMobility(EComponentMobility::Movable);
        Emitter->SetRelativeLocationAndRotation(Lamp.Position, Lamp.Direction);
        Emitter->SetIntensityUnits(ELightUnits::Candelas);
        Emitter->SetAttenuationRadius(Lamp.Radius);
        Emitter->SetOuterConeAngle(Lamp.Floodlight ? 35.f : 49.f);
        Emitter->SetInnerConeAngle(Lamp.Floodlight ? 21.f : 30.f);
        Emitter->SetSourceRadius(Lamp.Floodlight ? 38.f : 24.f);
        Emitter->SetLightColor(Lamp.Floodlight ? FLinearColor(.76f,.85f,1.f) : FLinearColor(.9f,.94f,1.f));
        Emitter->SetCastShadows(false);
        if (Lamp.Floodlight) Emitter->SetAffectTranslucentLighting(false);
        Emitter->SetAffectGlobalIllumination(false);
        Emitter->SetVolumetricScatteringIntensity(0.f);
        Emitter->SetSpecularScale(.35f);
        Emitter->RegisterComponent();
        MapLampEmitters.Add(Emitter);
        Banks += Lamp.Floodlight ? 1 : 0;
    }
    RefreshMapLamps();
    if (!MapLamps.IsEmpty())
        UE_LOG(LogTemp, Display, TEXT("ER_MAP_LIGHTS banks=%d indoor=%d shadow_maps=0"),
            Banks, MapLamps.Num() - Banks);
}

void AERBlockRoom::RefreshMapLamps()
{
    if (MapLampEmitters.IsEmpty() || LastLampRemovalCount == CellWorld.GetRemovedCount()) return;
    LastLampRemovalCount = CellWorld.GetRemovedCount();
    for (int32 Index = 0; Index < MapLamps.Num(); ++Index)
    {
        const FERMapLamp& Lamp = MapLamps[Index];
        auto* Emitter = MapLampEmitters[Index].Get();
        const int32 Working = Lamp.WorkingLenses(CellWorld);
        const float Intensity = Lamp.Candelas * float(Working) / float(Lamp.Lenses.Num());
        if (Emitter->Intensity != Intensity) Emitter->SetIntensity(Intensity);
        if (Emitter->IsVisible() != (Working > 0)) Emitter->SetVisibility(Working > 0);
    }
}
void AERBlockRoom::ConfigureFloors()
{
    Floor->SetRelativeLocation(FVector(0, 0, -10));
    Floor->SetRelativeScale3D(FVector(Config.InteriorSize + 2 * Config.BlockSize.Y + 800, Config.InteriorSize + 2 * Config.BlockSize.Y + 800, 20) / 100);
    if (FloorSurface) Floor->SetMaterial(0, FloorSurface);
}
FString AERBlockRoom::GetLayoutStatistics() const { return FString::Printf(TEXT("seed=%d rooms=1 openings=0"), Config.Seed); }
float AERBlockRoom::TakeDamage(float Amount, const FDamageEvent& Event, AController* DamageInstigator, AActor* Causer)
{
    if (!Event.IsOfType(FPointDamageEvent::ClassID)) return 0;
    const auto& Point = static_cast<const FPointDamageEvent&>(Event);
    FERCellId Cell;
    if (!ResolveCell(Point.HitInfo, Cell)) return 0;
    const float Before = CellWorld.GetHealth(Cell);
    TMap<FERCellId, float> Damage; Damage.Add(Cell, Amount);
    return ApplyCellBatch(Damage).Damaged > 0 ? Before - CellWorld.GetHealth(Cell) : 0;
}
FERBlockBatchResult AERBlockRoom::ApplyWorldExplosion(UWorld* World, const FVector& Origin, float Radius, float Damage, AActor* IgnoredActor)
{
    if (!FMath::IsFinite(Radius) || Radius <= 0 || !FMath::IsFinite(Damage) || Damage <= 0) return {};
    return ApplyWorldExplosion(World, Origin, FERBlockExplosionProfile::Linear(Radius, Damage), IgnoredActor);
}
FERBlockBatchResult AERBlockRoom::ApplyWorldExplosion(UWorld* World, const FVector& Origin, const FERBlockExplosionProfile& Profile, AActor* IgnoredActor)
{
    TRACE_CPUPROFILER_EVENT_SCOPE(ERBlock_Explosion);
    const double Start=FPlatformTime::Seconds();
    FERBlockBatchResult Result;
    if (!World || World->GetNetMode() == NM_Client || Origin.ContainsNaN() || !Profile.IsValid() || Profile.BaseDamage <= 0) return Result;
    // Cell walls attenuate through their own voxel exposure, not as static occluders.
    FCollisionQueryParams Query(SCENE_QUERY_STAT(ERBlockExplosion), false, IgnoredActor);
    for (TActorIterator<AERBlockRoom> It(World); It; ++It)
    { TArray<UPrimitiveComponent*> Components; It->GetCellComponents(Components); for (auto* C : Components) Query.AddIgnoredComponent(C); }
    // Every room is sampled against the unchanged snapshot before any removal.
    TArray<TPair<AERBlockRoom*, TMap<FERCellId, float>>> Pending;
    for (TActorIterator<AERBlockRoom> It(World); It; ++It)
    {
        if (!It->CanMutateBlocks() || !It->MaterialCatalog) continue;
        auto& Event = Pending.Emplace_GetRef(*It, TMap<FERCellId, float>());
        It->CollectCellExplosion(Origin, Profile, Query, Event.Value, Result);
    }
    // Damage accumulated from the snapshot is applied together. Excess damage
    // never propagates to neighbouring cells.
    const double MutationStart=FPlatformTime::Seconds();
    for (auto& Event : Pending)
    {
        const auto Batch = Event.Key->ApplyCellBatch(Event.Value);
        Result.Damaged += Batch.Damaged; Result.Destroyed += Batch.Destroyed; Result.GroupsRebuilt += Batch.GroupsRebuilt;
    }
    Result.MutationMs=(FPlatformTime::Seconds()-MutationStart)*1000;
    Result.TotalMs=(FPlatformTime::Seconds()-Start)*1000;
    for (auto& Event : Pending)
        Event.Key->LastCombat = FString::Printf(TEXT("blast candidates=%d covered=%d attenuated=%d damaged=%d removed=%d rebuilt=%d"), Result.Candidates, Result.Occluded, Result.Attenuated, Result.Damaged, Result.Destroyed, Result.GroupsRebuilt);
    UE_LOG(LogTemp, Display, TEXT("ER_BLOCK_EXPLOSION candidates=%d covered=%d attenuated=%d damaged=%d removed=%d rebuilt=%d ms=%.2f"), Result.Candidates, Result.Occluded, Result.Attenuated, Result.Damaged, Result.Destroyed, Result.GroupsRebuilt, Result.TotalMs);
    return Result;
}
bool AERBlockRoom::CanShowEffects(const FVector& Position) const
{
    if (Position.ContainsNaN() || !FMath::IsFinite(EffectDistance) || EffectDistance<0 || !FApp::CanEverRender() || GetNetMode() == NM_DedicatedServer) return false;
    const auto* Camera = UGameplayStatics::GetPlayerCameraManager(GetWorld(), 0);
    return Camera && FVector::DistSquared(Camera->GetCameraLocation(), Position) <= double(EffectDistance) * EffectDistance;
}
void AERBlockRoom::SpawnMaterialImpact(const FERBlockMaterialProperties& Material, bool bDecal, const FVector& Position, const FVector& Normal)
{
    if (!CanShowEffects(Position) || MaxImpactEffects <= 0 || Normal.ContainsNaN() || Normal.IsNearlyZero()) return;
    const auto* Profile = &Material;
    const int32 Budget = FMath::Min(FMath::Clamp(MaxImpactEffects, 0, 64), ERPerformance::Get().ImpactEffectLimit());
    ImpactDecals.RemoveAll([](const auto& Item) { return !Item.IsValid(); });
    ImpactEmitters.RemoveAll([](const auto& Item) { return !Item.IsValid() || !Item->IsActive(); });
    if (bDecal && Profile->ImpactDecal)
    {
        while (ImpactDecals.Num() >= Budget) { ImpactDecals[0]->DestroyComponent(); ImpactDecals.RemoveAt(0); }
        if (auto* Decal = UGameplayStatics::SpawnDecalAtLocation(GetWorld(), Profile->ImpactDecal, FVector(.4f, 1.5f, 1.5f), Position + Normal * .05f, Normal.Rotation(), 8))
        { ImpactDecals.Add(Decal); Decal->SetFadeOut(6, 2); }
    }
    if (Profile->ImpactSystem)
    {
        while (ImpactEmitters.Num() >= Budget) { if (ImpactEmitters[0].IsValid()) ImpactEmitters[0]->DestroyComponent(); ImpactEmitters.RemoveAt(0); }
        if (auto* Emitter = UNiagaraFunctionLibrary::SpawnSystemAtLocation(GetWorld(), Profile->ImpactSystem, Position, Normal.Rotation())) ImpactEmitters.Add(Emitter);
    }
}
void AERBlockRoom::SpawnExplosionEffect(const FVector& Origin)
{
    if (!ExplosionEffectClass || !CanShowEffects(Origin)) return;
    TArray<AERGrenadeExplosion*> Effects;
    for (TActorIterator<AERGrenadeExplosion> It(GetWorld()); It; ++It) Effects.Add(*It);
    if (Effects.Num() >= 8)
    {
        Effects.Sort([](const auto& A, const auto& B) { return A.GetGameTimeSinceCreation() > B.GetGameTimeSinceCreation(); });
        Effects[0]->Destroy();
    }
    OwnedBursts.RemoveAll([](const auto& Item) { return !Item.IsValid(); });
    if (auto* Burst=GetWorld()->SpawnActor<AERGrenadeExplosion>(ExplosionEffectClass,Origin,FRotator::ZeroRotator)) OwnedBursts.Add(Burst);
}
FString AERBlockRoom::GetStatistics() const
{
    int32 Quads=0,Boxes=0,Debris=0,Physics=0; for (int32 Q:ChunkQuads) Quads+=Q; for (int32 B:ChunkBoxes) Boxes+=B;
    for(const auto& Entry:SkinMeshes)if(Entry.Value){Entry.Value->ProcessMesh([&](const UE::Geometry::FDynamicMesh3& M){Quads+=FMath::DivideAndRoundUp(M.TriangleCount(),2);});Boxes+=Entry.Value->GetSimpleCollisionShapes().BoxElems.Num();}
    GetDebrisCounts(Debris,Physics);
    return FString::Printf(TEXT("%s resolution=%.0fcm cells=%lld alive=%lld destroyed=%lld detached=%lld panels=%d chunks=%d groups=%d quads=%d boxes=%d generation=%.2fms update=%.2fms chunks_updated=%d mesh=%.2fms collision=%.2fms debris=%d physics=%d support=%.2fms pending=%d"),
        *GetLayoutStatistics(), CellSettings.CellSize, CellWorld.GetCellCount(), CellWorld.GetCellCount()-CellWorld.GetRemovedCount(), CellWorld.GetRemovedCount()-CellWorld.GetDetachedCount(), CellWorld.GetDetachedCount(),
        CellWorld.GetPanels().Num(), CellWorld.GetChunks().Num(), CellGroupChunks.Num()+SkinMeshes.Num(), Quads, Boxes, GenerationMs, LastUpdateMs, LastGroupsRebuilt, UpdateProfile.MeshMs, UpdateProfile.CollisionMs, Debris, Physics, LastSupportMs, bSupportPending);
}
FERBlockMemoryProfile AERBlockRoom::GetMemoryProfile() const
{
    FERBlockMemoryProfile Result;
    Result.NetworkBytes=Network.PageVersions.GetAllocatedSize()+AuthorityShards.GetAllocatedSize()+ReceivedShards.GetAllocatedSize()+AppliedChunkRevisions.GetAllocatedSize()+AppliedPageVersions.GetAllocatedSize();
    if(HasAuthority()){for(const auto& S:AuthorityShards)if(S.IsValid())Result.NetworkBytes+=S->GetPayloadBytes();}
    else {for(const auto& S:ReceivedShards)if(S.Value.IsValid())Result.NetworkBytes+=S.Value->GetPayloadBytes();}
    Result.SupportBytes=Support.GetAllocatedBytes()+OwnedDebris.GetAllocatedSize()+PendingDetach.GetAllocatedSize();
    GetDebrisCounts(Result.Debris,Result.SimulatingDebris);
    Result.LogicalBytes=CellWorld.GetAllocatedBytes(); Result.LayoutBytes=GetLayoutBytes();
    Result.MappingBytes=CellMeshes.GetAllocatedSize()+CellComponentChunks.GetAllocatedSize()+ChunkQuads.GetAllocatedSize()+ChunkBoxes.GetAllocatedSize()+ImpactDecals.GetAllocatedSize()+ImpactEmitters.GetAllocatedSize()+OwnedBursts.GetAllocatedSize();
    Result.Cells=CellWorld.GetCellCount(); Result.RemovedCells=CellWorld.GetRemovedCount();
    Result.MappingBytes+=ChunkGroups.GetAllocatedSize()+CellGroupChunks.GetAllocatedSize()+CellGroupBoxes.GetAllocatedSize()+ChunkFaceCache.GetAllocatedSize()+ChunkBoxCache.GetAllocatedSize();
    for (const auto& Faces:ChunkFaceCache) Result.MappingBytes+=Faces.GetAllocatedSize();
    for (const auto& Boxes:ChunkBoxCache) Result.MappingBytes+=Boxes.GetAllocatedSize();
    for (const auto& Members:CellGroupChunks)
    {
        Result.MappingBytes+=Members.GetAllocatedSize();
        auto* Mesh=CellMeshes[Members[0]].Get();
        if (!Mesh) continue;
        // CPU mesh copy; GPU vertex/index buffers are outside tracked memory.
        FResourceSizeEx Size(EResourceSizeMode::Exclusive); Mesh->GetResourceSizeEx(Size);
        Result.InstanceResourceBytes+=Size.GetTotalMemoryBytes();
        Mesh->ProcessMesh([&Result](const UE::Geometry::FDynamicMesh3& M){Result.InstanceResourceBytes+=M.GetByteCount();});
        if (Mesh->GetBodyInstance() && Mesh->GetBodyInstance()->IsValidBodyInstance()) ++Result.CollisionBodies;
        for (int32 Chunk:Members) { Result.Quads+=ChunkQuads[Chunk]; Result.CollisionBoxes+=ChunkBoxes[Chunk]; }
    }
    Result.MappingBytes+=SkinMeshes.GetAllocatedSize()+SkinComponentPanels.GetAllocatedSize()+DirtySkinPanels.GetAllocatedSize();
    Result.MappingBytes+=SkinClipCaches.GetAllocatedSize();
    for(const auto& Cache:SkinClipCaches)Result.MappingBytes+=Cache.Value.GetAllocatedBytes();
    for(const auto& Entry:SkinMeshes)if(auto* Mesh=Entry.Value.Get())
    {
        FResourceSizeEx Size(EResourceSizeMode::Exclusive);Mesh->GetResourceSizeEx(Size);Result.InstanceResourceBytes+=Size.GetTotalMemoryBytes();
        Mesh->ProcessMesh([&](const UE::Geometry::FDynamicMesh3& Geometry){Result.InstanceResourceBytes+=Geometry.GetByteCount();Result.Quads+=FMath::DivideAndRoundUp(Geometry.TriangleCount(),2);});
        Result.CollisionBoxes+=Mesh->GetSimpleCollisionShapes().BoxElems.Num();
        if(Mesh->GetBodyInstance() && Mesh->GetBodyInstance()->IsValidBodyInstance())++Result.CollisionBodies;
    }
    Result.Components=GetInstanceComponents().Num();
    GetEffectCounts(Result.Decals,Result.Emitters,Result.Bursts);
    Result.ResidentBytes=FPlatformMemory::GetStats().UsedPhysical;
    return Result;
}
void AERBlockRoom::GetEffectCounts(int32& Decals,int32& Emitters,int32& Bursts) const
{
    Decals=Emitters=Bursts=0;
    for (const auto& Item:ImpactDecals) if (Item.IsValid()) ++Decals;
    for (const auto& Item:ImpactEmitters) if (Item.IsValid() && Item->IsActive()) ++Emitters;
    for (TActorIterator<AERGrenadeExplosion> It(GetWorld()); It; ++It) ++Bursts;
}
void AERBlockRoom::ClearOwnedEffects()
{
    for (auto& Decal:ImpactDecals) if (Decal.IsValid()) Decal->DestroyComponent();
    for (auto& Emitter:ImpactEmitters) if (Emitter.IsValid()) Emitter->DestroyComponent();
    for (auto& Burst:OwnedBursts) if (Burst.IsValid()) Burst->Destroy();
    ImpactDecals.Reset(); ImpactEmitters.Reset(); OwnedBursts.Reset();
}
void AERBlockRoom::EndPlay(const EEndPlayReason::Type Reason)
{
    ClearCellRecoveries();
    for(auto& Shard:AuthorityShards)if(Shard.IsValid())Shard->Destroy();
    AuthorityShards.Empty();ReceivedShards.Empty();
    ClearCollapse();
    ClearOwnedEffects();
    Super::EndPlay(Reason);
}

void AERBlockRoom::ClearCollapse()
{
    if (GetWorld()) GetWorldTimerManager().ClearTimer(SupportTimer);
    for (auto& Item:OwnedDebris) if (Item.IsValid()) Item->Destroy();
    OwnedDebris.Empty();Support.Reset();PendingDetach.Empty();bSupportPending=bSupportRunning=false;
    DetachOffset=0;DetachCursor=SupportSteps=LastUnsupported=LastUnsupportedComponents=LastSupportWork=LastDetached=LastSpawned=0;
    LastSupportMs=PeakSupportMs=0;
}
void AERBlockRoom::RequestSupportCheck()
{
    if (!CanMutateBlocks() || !Collapse.Enabled || !Support.IsBuilt()) return;
    bSupportPending=true;
    if (!GetWorldTimerManager().IsTimerActive(SupportTimer))
    {
        FTimerManagerTimerParameters Parameters;Parameters.bLoop=true;Parameters.bMaxOncePerFrame=true;
        GetWorldTimerManager().SetTimer(SupportTimer,this,&AERBlockRoom::StepSupport,.05f,Parameters);
    }
}
void AERBlockRoom::GetDebrisCounts(int32& Alive,int32& Simulating) const
{
    Alive=Simulating=0;
    for (const auto& Item:OwnedDebris) if (Item.IsValid()) {++Alive;if (Item->IsSimulating()) ++Simulating;}
}
FERCellDebris AERBlockRoom::MakeDebris(const TArray<FERCellId>& Component) const
{
    // One cube per detached component, fitted to its cells in the first panel.
    const int32 Panel = Component[0].Panel;
    FBox Box(ForceInit);
    for (const auto& Id : Component) if (Id.Panel == Panel) Box += CellWorld.GetCellBox(Id);
    const auto& P = CellWorld.GetPanels()[Panel];
    FERCellDebris Debris;
    Debris.Transform = FTransform(P.Transform.GetRotation(), P.Transform.TransformPosition(Box.GetCenter()), Box.GetSize() / 100) * GetActorTransform();
    Debris.Material = uint8(P.Material);
    return Debris;
}
void AERBlockRoom::SpawnCellDebris(const TArray<FERCellDebris>& Debris)
{
    if (GetNetMode()==NM_DedicatedServer || !BlockMesh) return;
    OwnedDebris.RemoveAll([](const auto& Item){return !Item.IsValid();});
    int32 Global=0; for (TActorIterator<AERBlockDebris> It(GetWorld());It;++It) ++Global;
    const int32 Quality=ERPerformance::Get().Effects;
    const int32 Limit=ERPerformance::Get().Defaults ? 64:Quality==0 ? 0:Quality==1 ? 8:Quality==2 ? 32:64;
    for (const auto& D : Debris)
    {
        if (Global>=FMath::Min(Limit,FMath::Clamp(Collapse.MaxDebris,0,64)) || D.Material>uint8(EERBlockMaterial::Glass) || D.Transform.ContainsNaN()) break;
        auto* Actor=GetWorld()->SpawnActorDeferred<AERBlockDebris>(AERBlockDebris::StaticClass(),D.Transform,this,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        if (!Actor) continue;
        const auto* Profile=MaterialCatalog->Find(EERBlockMaterial(D.Material));
        auto* Surface=Profile ? Profile->Surface.Get():nullptr;
        // The existing debris payload contains its original centre. Resolve the
        // local panel palette on each peer without changing the network schema.
        const FVector Center=GetActorTransform().InverseTransformPosition(D.Transform.GetLocation());
        for(const auto& P:CellWorld.GetPanels())if(P.Surface && (P.Skin.IsEmpty() || P.bSolidSkin) && uint8(P.Material)==D.Material &&
            FBox(FVector::ZeroVector,P.Size).ExpandBy(.01).IsInsideOrOn(P.Transform.InverseTransformPosition(Center)))
        {Surface=P.Surface;break;}
        Actor->Initialize(BlockMesh,Surface,D.Transform,Collapse.DebrisLifetime,Collapse.DebrisSettleAfter);
        Actor->FinishSpawning(Actor->GetActorTransform());OwnedDebris.Add(Actor);++LastSpawned;++Global;
    }
}
void AERBlockRoom::MulticastCellDebris_Implementation(uint32 Epoch,const TArray<FERCellDebris>& Debris)
{
    // Cosmetic only: never replayed for late joiners, never cover or damage.
    if(Epoch!=Network.Epoch || !IsMapReady() || Debris.Num()>8)return;
    SpawnCellDebris(Debris);
}
void AERBlockRoom::StepSupport()
{
    TRACE_CPUPROFILER_EVENT_SCOPE(ERBlock_CollapseStep);
    if (!bSupportPending || !CanMutateBlocks() || !Collapse.Enabled || !Support.IsBuilt())
    { bSupportPending=false;GetWorldTimerManager().ClearTimer(SupportTimer);return; }
    const double Begin=FPlatformTime::Seconds();++SupportSteps;
    LastSupportWork=LastDetached=LastSpawned=0;
    if (!bSupportRunning)
    {
        // Evaluated on a timer, never inside a blast snapshot. New damage while
        // components detach is coalesced into one more check afterwards.
        SupportRevision=DamageRevision;DetachCursor=DetachOffset=0;bSupportRunning=true;
        PendingDetach.Reset();Support.BeginEvaluation(CellWorld);
    }
    if(Support.IsEvaluating())
    {
        const bool Complete=Support.StepEvaluation(PendingDetach,FMath::Clamp(Collapse.WorkPerStep,1,16384)*64,FMath::Clamp(double(Collapse.GraphTimeMs),.01,4.));
        if(!Complete)
        {
            LastSupportWork=Support.LastSliceWork;LastSupportMs=(FPlatformTime::Seconds()-Begin)*1000;
            PeakSupportMs=FMath::Max(PeakSupportMs,LastSupportMs);return;
        }
        LastSupportWork=Support.LastWork;LastUnsupportedComponents=PendingDetach.Num();LastUnsupported=0;
        for (const auto& C:PendingDetach) LastUnsupported+=C.Num();
        UE_LOG(LogTemp,Display,TEXT("ER_BLOCK_SUPPORT unsupported=%d components=%d relabelled=%d graph=%.2fms revision=%llu"),LastUnsupported,LastUnsupportedComponents,Support.LastRelabelled,Support.LastMs,SupportRevision);
    }
    // Components per step, and at most 32768 cells: a huge component spreads over frames.
    TArray<FERCellId> Cells; TArray<FERCellDebris> Debris;
    const int32 Limit=FMath::Clamp(Collapse.RemovalsPerStep,1,256), DebrisLimit=FMath::Clamp(Collapse.DebrisPerStep,0,8), CellBudget=32768;
    for (int32 N=0; N<Limit && DetachCursor<PendingDetach.Num() && Cells.Num()<CellBudget; )
    {
        const auto& Component=PendingDetach[DetachCursor];
        if (DetachOffset==0 && Debris.Num()<DebrisLimit) Debris.Add(MakeDebris(Component));
        const int32 Take=FMath::Min(Component.Num()-DetachOffset,CellBudget-Cells.Num());
        Cells.Append(Component.GetData()+DetachOffset,Take); DetachOffset+=Take;
        if (DetachOffset>=Component.Num()) { ++DetachCursor; DetachOffset=0; ++N; }
    }
    if (!Cells.IsEmpty())
    {
        RebuildProfile={};
        const auto Batch=CellWorld.DetachCells(Cells);
        // Static collision of the whole batch goes before any debris wakes Chaos.
        for (int32 Chunk:Batch.DirtyChunks) RebuildCellChunk(Chunk);
        FlushSkinMeshes();
        LastDetached=Batch.Destroyed;
        PublishCellChanges(Batch.ChangedChunks);
        EmitGlassBreaks(Batch.BrokenGlassPanes);
        if (!Debris.IsEmpty()) { if (GetNetMode()==NM_Standalone) SpawnCellDebris(Debris); else MulticastCellDebris(Network.Epoch,Debris); }
    }
    if (DetachCursor>=PendingDetach.Num())
    {
        bSupportRunning=false;PendingDetach.Reset();
        bSupportPending=DamageRevision!=SupportRevision;
        if (!bSupportPending) GetWorldTimerManager().ClearTimer(SupportTimer);
    }
    LastSupportMs=(FPlatformTime::Seconds()-Begin)*1000;
    PeakSupportMs=FMath::Max(PeakSupportMs,LastSupportMs);
}

bool AERBlockRoom::CanMutateBlocks() const {return GetWorld() && HasAuthority() && GetNetMode()!=NM_Client;}
void AERBlockRoom::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);DOREPLIFETIME(AERBlockRoom,Network);
}
uint32 AERBlockRoom::GetLayoutDigest() const
{
    uint32 H=HashCombine(7,CellWorld.GetLayoutDigest());
    if(const auto* Map=Cast<AERProceduralMap>(this))if(Map->LayoutConfig.TemplateAsset) H=HashCombine(H,Map->GetTemplateGeometryDigest());
    for(int32 I=0;I<7;++I)if(const auto* P=MaterialCatalog->Find(static_cast<EERBlockMaterial>(I)))H=HashCombine(H,FCrc::StrCrc32(P->Surface ? *P->Surface->GetPathName():TEXT("")));
    H=HashCombine(H,FCrc::StrCrc32(BlockMesh ? *BlockMesh->GetPathName():TEXT("")));
    return H;
}
uint32 AERBlockRoom::GetStateDigest() const
{
    // Alive bits only: HP never leave the server.
    return HashCombine(GetLayoutDigest(),CellWorld.GetAliveDigest());
}
void AERBlockRoom::SetMapCollision(bool Enabled)
{
    TInlineComponentArray<UPrimitiveComponent*> Components(this);
    for(auto* C:Components)
    {C->SetCollisionEnabled(Enabled && !C->ComponentHasTag(TEXT("ERTemplateVisual")) ? ECollisionEnabled::QueryAndPhysics:ECollisionEnabled::NoCollision);C->SetVisibility(Enabled);}
    if(Enabled)DisableEmptyCellCollision();
}
void AERBlockRoom::OnRep_Network()
{
    if(HasAuthority() || !bNetworkBegunPlay || Network.Epoch==0)return;
    if(BuiltEpoch!=Network.Epoch)
    {
        ClearCellRecoveries();SyncStartedAt=FPlatformTime::Seconds();
        bMapReady=false;bNetworkError=false;SetMapCollision(false);
        if(!OnRepCellDescriptor())return;
    }
    TryApplyCellNetwork();
    TryApplyCellRecovery();
}
