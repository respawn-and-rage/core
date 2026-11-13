#pragma once
#include "ERMapLighting.h"
#include "CoreMinimal.h"
#include "ERServerSettings.h"
#include "GameFramework/Actor.h"
#include "ERBlockWorld.h"
#include "ERBlockCombat.h"
#include "ERBlockProfile.h"
#include "ERCellSupport.h"
#include "ERMeshCells.h"
#include "ERCellWorld.h"
#include "ERCellMesher.h"
#include "ERProceduralConfig.h"
#include "ERBlockRoom.generated.h"

USTRUCT(BlueprintType)
struct FERBlockCollapseSettings
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite) bool Enabled=true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) bool GroundAnchors=true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float GroundZ=0;
    // Work units are 64 graph/cell operations; the time budget also applies.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="1",ClampMax="16384")) int32 WorkPerStep=4096;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="0.01",ClampMax="4")) float GraphTimeMs=1.5;
    // Unsupported components detached per timer step.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="1",ClampMax="256")) int32 RemovalsPerStep=128;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="0",ClampMax="64")) int32 MaxDebris=32;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="0",ClampMax="8")) int32 DebrisPerStep=4;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="1",ClampMax="20")) float DebrisLifetime=8;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="0.1",ClampMax="20")) float DebrisSettleAfter=2;
};

// Protocol 70: furnished interiors, Town storefronts and market squares, template area densities.
// Protocol 64: Central Building secondary roofs and ladders.
// Protocol 63: waterfront basin, villa access stairs, drywall and raised pool roof.
// Protocol 62: one cell component per panel tile, wider Village passages and spawns.
// Protocol 61: permanent Alley stair foundations and terrain-matched underpass entries.
// Protocol 60: scaled Central flank cover and wider paired Town exits.
// Protocol 58: villa table pieces and stair glass guards use destructible cells.
// Protocol 57: glass guards follow the Central Building villa stair treads.
// Protocol 56: seeded approach direction for the Central Building villa stair.
// Protocol 55: brick house shells throughout Middle Eastern Alleys.
// Protocol 54: Town corner service buildings. Protocol 53: offset Town chambers and bent street shoulders. Protocol 52: Fire Training office clusters and rotated interior rooms.
// Protocol 51: larger Village central blocks and flat hut roofs.
// Protocol 50: compact taller villas with ground slabs and glazing.
// Protocol 49: compact Central Building villas and paired pools.
// Protocol 44: square office ceiling lights and tree-free Village. Protocol 43: connected Town central galleries. Protocol 42: irregular Town parcels and polygonal inaccessible ground. Protocol 41: urban Town loop and ground-floor shops. Protocol 40: circular Central Building ground railing passage. Protocol 39: longitudinal Village terrain. Protocol 38: dense longitudinal Fire Training and bent high routes. Protocol 37: concrete Fire Training shell. Protocol 36: fully procedural closed Fire Training. Protocol 35: rear cover firing steps. Protocol 34: varied circular cover groups. Protocol 33: varied circular Central Building geometry and crossings. Protocol 32: paired seeded spawn zones. Protocol 31: thin brick Alleys towers. Protocol 30: irregular 3v3 Alleys streets and lots. Protocol 29: dense 3v3 Alleys core. Protocol 28: 750m²/player and Rooms primary-floor sizing. Protocol 27: mandatory initial spawn occlusion. Protocol 26: circular graded Central Building and reflected template layouts. Protocol 25: Alleys roof links, lateral wall access and firing slits. Protocol 24: Central Building residential windows, room wings and accessible balconies. Protocol 23: no Village piece stands unsupported on a fresh map. Protocol 22: Village back gallery rail rests on the deck. Protocol 21: authority-chosen map candidate in the descriptor. Protocol 20: optional baked Rooms macro graph in the descriptor. Protocol 19:per-template palettes and Stone alley houses; optional hall passage and varied Rooms hall shapes and diagonal corner bases with four through-rooms, longitudinal Fire Training and distributed Village ground cover and variable Rooms/Village graphs at500m²/player density; expected team size plus material-rule snapshot and bounded compressed skin masks; 5cm cells with complete shard-payload witnesses. BlockCount holds the cell count, PageVersions the
// versions of the chunk-mask shards (AERCellNetShard).
namespace ERCellProtocol { inline constexpr uint32 Version=70; }
USTRUCT()
struct FERBlockNetworkDescriptor
{
    GENERATED_BODY()
    UPROPERTY() uint32 Epoch=0;
    UPROPERTY() uint32 Protocol=ERCellProtocol::Version;
    UPROPERTY() uint32 LayoutDigest=0;
    UPROPERTY() uint32 Sequence=0;
    UPROPERTY() int32 BlockCount=0;
    UPROPERTY() FERBlockRoomConfig Blocks;
    UPROPERTY() FERProceduralConfig Layout;
    UPROPERTY() bool Procedural=false;
    UPROPERTY() bool Ceilings=true;
    UPROPERTY() TArray<uint32> PageVersions;
    UPROPERTY() bool Cells=true;
    UPROPERTY() FERCellSettings CellSettings;
    UPROPERTY() FERServerRules Rules;
};
// Cosmetic falling fragment of a detached component (panel-local box).
USTRUCT()
struct FERCellDebris
{
    GENERATED_BODY()
    UPROPERTY() FTransform Transform;
    UPROPERTY() uint8 Material=0;
};

// Destructible map made of logical 5cm cells: one dynamic mesh (visible faces
// only) and one aggregate-box body per chunk, rebuilt for dirty chunks only.
struct FERCellSyncStatus
{
    // RecoveryId is the active checkpoint, or the last completed id when idle.
    uint32 Epoch=0,TargetSequence=0,AppliedSequence=0,RecoveryId=0;
    uint64 ProgressSerial=0;
    int32 ExpectedPages=0,VerifiedPages=0;
    bool Ready=false,Error=false,Recovering=false;
    double LastAppliedAgeSeconds=0;
    float LastApplyMs=0;
};

UCLASS(Blueprintable)
class FDM_API AERBlockRoom : public AActor
{
    GENERATED_BODY()
public:
    AERBlockRoom();
    // Legacy subobject kept so saved maps load; it stores nothing.
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) TObjectPtr<UERBlockWorld> BlockWorld;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) TObjectPtr<class UStaticMeshComponent> Floor;
    // BlockSize.Y is the wall slot thickness, Height the wall height.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Blocks") FERBlockRoomConfig Config;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Blocks") TObjectPtr<UERBlockMaterialCatalog> MaterialCatalog;
    UPROPERTY() FERServerRules MaterialRules;
    UPROPERTY(Transient) TObjectPtr<UERBlockMaterialCatalog> AuthoredMaterialCatalog;
    // Cube used by floors, slabs and debris.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Blocks") TObjectPtr<class UStaticMesh> BlockMesh;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Blocks") TObjectPtr<class UMaterialInterface> FloorSurface;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Blocks|Effects") TSubclassOf<class AERGrenadeExplosion> ExplosionEffectClass;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Blocks|Effects", meta=(ClampMin="0", ClampMax="64")) int32 MaxImpactEffects = 24;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Blocks|Effects", meta=(ClampMin="0")) float EffectDistance = 1500;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Blocks|Collapse") FERBlockCollapseSettings Collapse;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Blocks|Cells") FERCellSettings CellSettings;
    UFUNCTION(BlueprintCallable, Category="Blocks") bool Regenerate();
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    virtual float TakeDamage(float Amount, const FDamageEvent& Event, AController* DamageInstigator, AActor* Causer) override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

    // Cells.
    const FERCellWorld& GetCellWorld() const { return CellWorld; }
    bool ResolveCell(const FHitResult& Hit, FERCellId& Id) const;
    bool ApplyHitDamage(const FHitResult& Hit, float Amount);
    FERBlockBatchResult ApplyCellBatch(const TMap<FERCellId, float>& Damage);
    struct FERCellShotStep { bool bStop=true, bLimited=false; int32 Cells=0, Runs=0; FVector Exit=FVector::ZeroVector; };
    // Each crossed cell absorbs its current HP from the shared bullet damage budget.
    FERCellShotStep ShootCells(const FHitResult& Hit, const FVector& Cursor, const FVector& Ray, double Range,
        float& RemainingDamage, int32 RunsLeft, bool bSpawnMaterialImpact = true);
    // Pre-event snapshot only: never mutates. Query must ignore every cell chunk component.
    void CollectCellExplosion(const FVector& Origin, const FERBlockExplosionProfile& Profile, const FCollisionQueryParams& Query,
        TMap<FERCellId, float>& Out, FERBlockBatchResult& Result) const;
    static FERBlockBatchResult ApplyWorldExplosion(UWorld* World, const FVector& Origin, float Radius, float Damage, AActor* IgnoredActor = nullptr);
    // Structural blast: F(d) * profile * material multiplier * exposure per cell.
    static FERBlockBatchResult ApplyWorldExplosion(UWorld* World, const FVector& Origin, const FERBlockExplosionProfile& Profile, AActor* IgnoredActor = nullptr);
    void GetCellComponents(TArray<UPrimitiveComponent*>& Out) const;
    bool ValidateCells() const;
    FString DescribeCell(const FERCellId& Id) const;
    FTransform GetCellTransform(const FERCellId& Id) const;
    int32 GetChunkCount() const { return CellWorld.GetChunks().Num(); }

    // Effects.
    void SpawnCellImpact(EERBlockMaterial Material, const FVector& Position, const FVector& Normal);
    void SpawnExplosionEffect(const FVector& Origin);
    void GetEffectCounts(int32& Decals,int32& Emitters,int32& Bursts) const;

    // Structural support: authority timer, bounded detachment and debris.
    void RequestSupportCheck();
    void StepSupport();
    bool IsSupportPending() const { return bSupportPending; }
    void GetDebrisCounts(int32& Alive,int32& Simulating) const;
    double GetSupportBuildMs() const { return Support.BuildMs; }
    const FERCellSupport& GetSupport() const { return Support; }
    double LastSupportMs=0, PeakSupportMs=0;
    int32 SupportSteps=0, LastUnsupported=0, LastUnsupportedComponents=0;
    int32 LastSupportWork=0, LastDetached=0, LastSpawned=0;

    // Replication and loading gate.
    bool CanMutateBlocks() const;
    bool IsStandaloneLab() const;
    bool IsMapReady() const { return HasAuthority() ? bGenerationValid:(bMapReady && !bNetworkError); }
    bool IsSnapshotCurrent() const { return IsMapReady() && (HasAuthority() || AppliedSequence==Network.Sequence); }
    uint32 GetMapEpoch() const { return Network.Epoch; }
    uint32 GetMapSequence() const { return Network.Sequence; }
    uint32 GetAppliedMapSequence() const {return AppliedSequence;}
    int32 GetNetworkPageCount() const {return Network.PageVersions.Num();}
    uint32 GetStateDigest() const;
    uint32 GetLayoutDigest() const;
    void ReceiveCellShard(class AERCellNetShard* Shard);
    void ReceiveCellRecovery(class AERCellRecoveryManifest* Manifest);
    bool StartCellRecovery(class AERPlayerController* Player,uint32 Epoch,FString& Error);
    bool AcknowledgeCellRecovery(class AERPlayerController* Player,uint32 RecoveryId);
    FERCellSyncStatus GetCellSyncStatus() const;

    // Diagnostics.
    FERBlockOperationProfile GenerationProfile, UpdateProfile;
    FERBlockMemoryProfile GetMemoryProfile() const;
    float GenerationMs = 0;
    float LastUpdateMs = 0;
    FString LastHit = TEXT("none");
    FString LastCombat = TEXT("none");
    int32 LastGroupsRebuilt = 0;
    FString GetStatistics() const;
    virtual FVector GetSpawnLocation() const { return GetActorTransform().TransformPosition(FVector(0, 0, 100)); }
protected:
    // Builds the cell panels of this room (lab: four walls and two samples).
    virtual bool GenerateLogicalWorld();
    virtual void ConfigureFloors();
    // Geometry and collision are complete on this peer.
    virtual bool ValidateGeneratedCollision() const { return true; }
    virtual void OnGenerationFinished() {}
    virtual FString GetLayoutStatistics() const;
    virtual SIZE_T GetLayoutBytes() const { return 0; }
    bool BuildCellWorld(const TArray<FERCellPanelDesc>& Panels);
    UPROPERTY() TObjectPtr<class UMaterialInterface> MapLampSurface;
    TArray<FERMapLamp> MapLamps;
    void ConfigureMapLamps();
private:
    UPROPERTY(Transient) TArray<TObjectPtr<class USpotLightComponent>> MapLampEmitters;
    int64 LastLampRemovalCount = -1;
    void RefreshMapLamps();
    friend class FERCellNetTransactionTest;
    friend class FERCellRecoveryTest;
    FERCellWorld CellWorld;
    FERCellSupport Support;
    // Per chunk: the component of its group. Ordinary panels draw as one component
    // per tile of up to CellGroupTile x CellGroupTile chunks, rather than one per chunk.
    UPROPERTY() TArray<TObjectPtr<class UDynamicMeshComponent>> CellMeshes;
    TMap<const UPrimitiveComponent*, int32> CellComponentChunks; // component -> first chunk of its group
    TArray<int32> ChunkGroups; // chunk -> group, INDEX_NONE for skins
    TArray<TArray<int32>> CellGroupChunks;
    TArray<int32> CellGroupBoxes;
    TArray<TArray<ERCellMesh::FQuad>> ChunkFaceCache;
    TArray<TArray<FBox>> ChunkBoxCache;
    TSet<int32> DirtyCellGroups;
    UPROPERTY() TMap<int32,TObjectPtr<class UDynamicMeshComponent>> SkinMeshes;
    TMap<const UPrimitiveComponent*,int32> SkinComponentPanels;
    TMap<int32,ERMeshCells::FCache> SkinClipCaches;
    TSet<int32> DirtySkinPanels;
    TArray<int32> ChunkQuads, ChunkBoxes;
    TArray<TWeakObjectPtr<class AERCellNetShard>> AuthorityShards;
    TMap<int32, TWeakObjectPtr<class AERCellNetShard>> ReceivedShards;
    struct FRecoveryTransfer
    {
        TWeakObjectPtr<class AERPlayerController> Player;
        uint32 Id=0;double ExpiresAt=0;SIZE_T Bytes=0;
        TArray<TWeakObjectPtr<AActor>> Actors;
    };
    TArray<FRecoveryTransfer> RecoveryTransfers;
    TWeakObjectPtr<class AERCellRecoveryManifest> RecoveryManifest;
    TMap<uint64,TWeakObjectPtr<class AERCellNetShard>> RecoveryShards;
    TMap<uint64,uint32> VerifiedPayloadVersions;
    uint32 NextRecoveryId=1,CompletedRecoveryId=0;
    uint64 SyncProgressSerial=0;
    int32 SyncExpectedPages=0,SyncVerifiedPages=0;
    double LastCellApplyAt=0,SyncStartedAt=0;
    float LastCellApplyMs=0;
    FTimerHandle RecoveryCleanupTimer;
    TArray<uint32> AppliedChunkRevisions, AppliedPageVersions;
    UPROPERTY(ReplicatedUsing=OnRep_Network) FERBlockNetworkDescriptor Network;
    uint32 BuiltEpoch=0,AppliedSequence=0;
    bool bGenerationValid=false;
    bool bApplyingNetwork=false,bMapReady=false,bNetworkError=false,bNetworkBegunPlay=false,bInCosmeticMulticast=false;
    TArray<TWeakObjectPtr<class UDecalComponent>> ImpactDecals;
    TArray<TWeakObjectPtr<class UNiagaraComponent>> ImpactEmitters;
    TArray<TWeakObjectPtr<class AERGrenadeExplosion>> OwnedBursts;
    TArray<TWeakObjectPtr<class AERBlockDebris>> OwnedDebris;
    FERBlockOperationProfile RebuildProfile;
    FTimerHandle SupportTimer;
    bool bSupportPending=false, bSupportRunning=false;
    uint64 SupportRevision=0, DamageRevision=0;
    TArray<TArray<FERCellId>> PendingDetach;
    int32 DetachCursor=0, DetachOffset=0;
    void BuildCellComponents();
    void ClearCells();
    void RebuildCellChunk(int32 Chunk);
    void FlushCellGroups();
    void DisableEmptyCellCollision();
    void FlushSkinMeshes();
    void PublishCellGeneration();
    void PublishCellChanges(const TArray<int32>& Chunks);
    void TryApplyCellNetwork();
    void TryApplyCellRecovery();
    bool ApplyCellSnapshot(const FERBlockNetworkDescriptor& Descriptor,const TMap<int32,TWeakObjectPtr<class AERCellNetShard>>& Pages,bool Recovery,const TArray<struct FERCellShardHeader>* RecoveryHeaders=nullptr);
    void RecordVerifiedPayload(const class AERCellNetShard* Shard);
    void ClearCellRecoveries();
    void CleanupCellRecoveries();
    void FailCellSynchronization(const TCHAR* Reason);
    bool OnRepCellDescriptor();
    UFUNCTION() void OnRep_Network();
    void SetMapCollision(bool Enabled);
    void ClearCollapse();
    void ClearOwnedEffects();
    bool CanShowEffects(const FVector& Position) const;
    void SpawnMaterialImpact(const FERBlockMaterialProperties& Profile, bool bDecal, const FVector& Position, const FVector& Normal);
    UPROPERTY() TArray<TObjectPtr<class USoundBase>> GlassBreakSounds;
    UPROPERTY() TObjectPtr<class USoundAttenuation> GlassBreakAttenuation;
    void EmitGlassBreaks(const TArray<FERCellId>& Panes);
    UFUNCTION(NetMulticast,Unreliable) void MulticastGlassBreak(uint32 Epoch, const TArray<FVector_NetQuantize>& Positions);
    void SpawnCellDebris(const TArray<FERCellDebris>& Debris);
    FERCellDebris MakeDebris(const TArray<FERCellId>& Component) const;
    UFUNCTION(NetMulticast,Unreliable) void MulticastCellImpact(uint32 Epoch,uint8 Material,FVector Position,FVector Normal);
    UFUNCTION(NetMulticast,Unreliable) void MulticastCellDebris(uint32 Epoch,const TArray<FERCellDebris>& Debris);
};
