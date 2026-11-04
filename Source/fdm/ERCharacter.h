#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "Components/ActorComponent.h"
#include "ERTeamAppearance.h"
#include "ERGameplayMessages.h"
#include "ERWeaponRecoil.h"
#include "ERAudio.h"
#include "ERAimPunch.h"
#include "ERCharacter.generated.h"

USTRUCT()
struct FERProtectionSurfaceState
{
    GENERATED_BODY()
    UPROPERTY() TObjectPtr<class UMeshComponent> Mesh;
    UPROPERTY() TArray<TObjectPtr<class UMaterialInterface>> Originals;
};

UCLASS(ClassGroup=(EclipseRecoil), meta=(BlueprintSpawnableComponent))
class FDM_API UERHealthComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    UERHealthComponent();
    UPROPERTY(Replicated, BlueprintReadOnly) float Health = 100;
    UPROPERTY(Replicated, BlueprintReadOnly) float Armor = 0;
    UPROPERTY(Replicated, BlueprintReadOnly) bool bHelmet = true;
    UPROPERTY(ReplicatedUsing=OnRep_Dead, BlueprintReadOnly) bool bDead = false;
    UPROPERTY(Replicated, BlueprintReadOnly) double ProtectionEndServerTime = 0;
    bool IsProtected() const;
    float ReceiveDamage(float Amount, AController* Killer, float ArmorRatio = 2, const FERMessageDamage& Facts = FERMessageDamage(), const FHitResult* BulletHit = nullptr);
    UFUNCTION() void OnRep_Dead();
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
};

UCLASS()
class FDM_API AERCharacter : public ACharacter
{
    GENERATED_BODY()
public:
    UPROPERTY(VisibleAnywhere) TObjectPtr<class UERWounds> Wounds;
    AERCharacter(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());
    virtual void FaceRotation(FRotator NewControlRotation, float DeltaTime = 0.f) override;
    UFUNCTION(BlueprintCallable, Category="Training|Editor")
    static bool ValidateFieldClothing(class USkeletalMesh* TargetMesh, bool Skirt);
    UFUNCTION(BlueprintCallable, Category="Training|Editor")
    static bool ConfigureFieldClothing(class USkeletalMesh* TargetMesh, bool Skirt, class USkeletalMesh* SourceMesh = nullptr, class UPhysicsAsset* SourceCollision = nullptr);
    UFUNCTION(BlueprintCallable, Category="Training|Editor")
    static FString InspectFieldClothing(class USkeletalMesh* TargetMesh);
    UFUNCTION(BlueprintCallable, Category="Training|Editor")
    static bool CopyTrainingGripSockets(class USkeletalMesh* TargetMesh, class USkeletalMesh* SourceMesh);
    UFUNCTION(BlueprintCallable, Category="Training|Editor")
    static bool SetMercenaryAnimationSkeleton(class UAnimSequence* Animation, class USkeletalMesh* TargetMesh);
    UFUNCTION(BlueprintCallable, Category="First person|Editor")
    static bool PrepareFirstPersonSkeleton(class USkeleton* Skeleton, class USkeletalMesh* PreviewMesh);
    UFUNCTION(BlueprintCallable, Category="First person|Editor")
    static bool ConfigureFirstPersonTranslationTracks(class USkeleton* Skeleton, const TArray<FName>& Bones);
    UFUNCTION(BlueprintCallable, Category="Respawn & Rage|Editor")
    static bool PrepareTeamSkeleton(class USkeleton* Skeleton, class USkeletalMesh* PreviewMesh);
    UFUNCTION(BlueprintCallable, Category="Assets|Editor")
    static bool ReplaceOwnedAssetReferences(class UObject* Asset, const TArray<class UObject*>& Sources, const TArray<class UObject*>& Replacements);
    UFUNCTION(BlueprintCallable, Category="Assets|Editor")
    static bool DeleteUnloadedParagonPackages(const TArray<FString>& Packages);
    UFUNCTION(BlueprintCallable, Category="Arsenal|Editor")
    static FVector GetWeaponMuzzleFromGeometry(class USkeletalMesh* WeaponMesh, class UAnimSequence* Animation = nullptr);
    UFUNCTION(BlueprintCallable, Category="Arsenal|Editor")
    static FTransform GetWeaponReferenceBone(class USkeletalMesh* WeaponMesh, FName Bone);
    UFUNCTION(BlueprintCallable, Category="Arsenal|Editor")
    static bool AssignWeaponMaterial(class USkeletalMesh* WeaponMesh, class UMaterialInterface* Material);
    UFUNCTION(BlueprintCallable, Category="Assets|Editor")
    static bool AssignWeaponMaterials(class USkeletalMesh* WeaponMesh, const TArray<class UMaterialInterface*>& Materials);
    UFUNCTION(BlueprintCallable, Category="Presentation|Editor")
    static TArray<FVector> GetPresentationVertices(class USkeletalMesh* PresentationMesh, FName Bone);
    UFUNCTION(BlueprintCallable, Category="Presentation|Editor")
    static bool SetPalmContactSocket(class USkeletalMesh* PresentationMesh, FName Name, FName Bone, FTransform Transform);
    UFUNCTION(BlueprintCallable, Category="Presentation|Editor")
    static bool ConfigureKnifeGripEditor(class USkeletalMesh* PreviewMesh, class UAnimSequence* Pose,
        class USkeletalMesh* KnifeMesh, FTransform KnifeRelativeToRoot);
    UFUNCTION(BlueprintCallable, Category="Presentation|Editor")
    static FVector GetWeaponSurfaceContact(class USkeletalMesh* WeaponMesh, FVector Start, FVector End, class UAnimSequence* Animation = nullptr);
    UFUNCTION(BlueprintCallable, Category="First person|Editor")
    static bool BuildFirstPersonForearms(class USkeletalMesh* Destination, class USkeletalMesh* SkinMesh,
        class USkeletalMesh* FirstPersonRig, const TArray<int32>& ExcludedMaterials, const TArray<FName>& ExcludedBones);
    UPROPERTY(Replicated) TObjectPtr<AERCharacter> DeathFocus;
    UPROPERTY(Replicated) FVector DeathFocusPosition = FVector::ZeroVector;
    UPROPERTY(Replicated) bool bHasDeathFocus = false;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) TObjectPtr<UERHealthComponent> Health;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) TObjectPtr<class UERFootsteps> Footsteps;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) TObjectPtr<class UCapsuleComponent> ProneHitVolume;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) TObjectPtr<class UCameraComponent> Camera;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) TObjectPtr<USkeletalMeshComponent> FirstPersonBody;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) TObjectPtr<USkeletalMeshComponent> FirstPersonRifle;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) TObjectPtr<USkeletalMeshComponent> WorldRifle;
    UPROPERTY(VisibleAnywhere) TObjectPtr<USkeletalMeshComponent> FirstPersonSight;
    UPROPERTY(VisibleAnywhere) TObjectPtr<USkeletalMeshComponent> WorldSight;
    UPROPERTY(VisibleAnywhere) TObjectPtr<class UStaticMeshComponent> TeamMarker;
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category="Team appearance") FERTeamAppearance AlphaAppearance;
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category="Team appearance") FERTeamAppearance OmegaAppearance;
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category="Team appearance") TArray<FERTeamAppearance> AlphaSkins;
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category="Team appearance") TArray<FERTeamAppearance> OmegaSkins;
    const FERTeamAppearance& GetTeamAppearance() const;
    // Existing engine surface; only a transient tint instance, no skin asset copies.
    UPROPERTY() TObjectPtr<class UMaterialInterface> ProtectionBaseMaterial;
    UPROPERTY(Transient) TObjectPtr<class UMaterialInstanceDynamic> ProtectionSurface;
    class UMaterialInterface* GetUnprotectedMaterial(class UMeshComponent* SurfaceMesh, int32 Slot) const;
    void RestoreProtectionMaterials();
    void UpdateProtectionPresentation();
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category="Training") TArray<TObjectPtr<class UMaterialInterface>> AlphaBodyMaterials;
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category="Training") TArray<TObjectPtr<class UMaterialInterface>> OmegaBodyMaterials;
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category="First person") TArray<TObjectPtr<class UMaterialInterface>> AlphaFirstPersonMaterials;
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category="First person") TArray<TObjectPtr<class UMaterialInterface>> OmegaFirstPersonMaterials;
    // Empty keeps every section visible (legacy/template viewmodel).
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category="First person") TArray<int32> FirstPersonVisibleMaterialSlots;
    UPROPERTY(Replicated, BlueprintReadOnly) TObjectPtr<class AERWeapon> Weapon;
    UPROPERTY(Replicated) TArray<TObjectPtr<class AERWeapon>> Inventory;
    // Fixed server-authored initial facing for the minimap enemy-base bearing.
    UPROPERTY(Replicated) float SpawnFacingYaw = 0;
    UPROPERTY(Replicated) FERWeaponRecoilState RecoilState;
    FRotator GetRecoilRotation() const;
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category="Damage feedback") FERAimPunchTuning AimPunchTuning;
    FRotator GetAimPunchRotation() const;
    FRotator GetEffectiveAimRotation() const;
    const FERAimPunchState& GetAimPunchState() const { return HasAuthority()?AuthorityPunch:OwnerPunch; }
    void ResetAimPunch();
    virtual void PossessedBy(AController* NewController) override;
    virtual void UnPossessed() override;
    virtual void OnRep_Controller() override;
    const TArray<FERDamageVisual>& GetDamageVisuals() const { return DamageVisuals; }
    virtual FRotator GetViewRotation() const override;
    void AddWeaponRecoil(const FVector2D& Impulse);
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category="Arsenal") TArray<TObjectPtr<class UERWeaponData>> Arsenal;
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category="Arsenal") TArray<TObjectPtr<class UERWeaponData>> ReferenceArsenal;
    UPROPERTY(Replicated) bool bReferenceInventory = false;
    UPROPERTY(Replicated) bool bPresetInventory = false;
    UPROPERTY(Replicated) FName ActivePreset;
    UPROPERTY(Replicated) FName ActiveVariant;
    int32 FindCatalogSlot(int32 CatalogIndex) const;
    bool UsesReferenceArsenal() const;
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite) TSubclassOf<class AERWeapon> WeaponClass;
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category="Damage feedback") TObjectPtr<class USoundBase> HitSound;
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category="Damage feedback") TObjectPtr<class USoundAttenuation> HitAttenuation;
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category="Death") TSubclassOf<class AERCorpse> CorpseClass;
    UFUNCTION(Client, Reliable) void ClientDamageFeedback(float Amount, FVector_NetQuantizeNormal Direction, const FERAimPunchState& Snapshot, bool bBullet = false);
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category="Shot feedback") TObjectPtr<class UAnimSequence> FirstPersonFireAnimation;
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category="Shot feedback") TObjectPtr<class USoundBase> FireSound;
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category="Shot feedback") TObjectPtr<class USoundAttenuation> FireAttenuation;
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category="Shot feedback") TSubclassOf<class AERSpentCasing> CasingClass;
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category="Shot feedback") TObjectPtr<class UNiagaraSystem> TracerSystem;
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category="Shot feedback") TObjectPtr<class UStaticMesh> MuzzleFlashMesh;
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category="Shot feedback") TObjectPtr<class UMaterialInterface> MuzzleFlashMaterial;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) TObjectPtr<class UStaticMeshComponent> FirstPersonMuzzleFlash;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) TObjectPtr<class UStaticMeshComponent> WorldMuzzleFlash;
    // Local counter for the opt-in integration probe; never drives gameplay or replication.
    uint32 ShotFeedbackCount = 0;
    virtual void OnStartCrouch(float HalfHeightAdjust, float ScaledHalfHeightAdjust) override;
    virtual void OnEndCrouch(float HalfHeightAdjust, float ScaledHalfHeightAdjust) override;
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaTime) override;
    virtual void CalcCamera(float DeltaTime, struct FMinimalViewInfo& OutResult) override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    virtual float TakeDamage(float Damage, const FDamageEvent& Event, AController* Killer, AActor* Causer) override;
    virtual bool CanJumpInternal_Implementation() const override;
    virtual void Landed(const FHitResult& Hit) override;
    static float FallDamage(float DownSpeed,float Gravity);
    // Weapon remains the selected inventory item while automatically stowed.
    bool CanHoldWeaponOnLadder() const;
    bool IsWeaponStowed() const;
    virtual void FellOutOfWorld(const class UDamageType& Type) override;
    UFUNCTION(Server, Reliable) void ServerStartFire();
    void ApplyStartFire();
    void ApplyQueuedFire();
    bool bQueuedFireStart = false, bQueuedFireStop = false;
    void SetGrenadeInputHeld(bool bHeld);
    float GrenadeThrowPoseBlend = 0;
    UFUNCTION(Server, Reliable) void ServerStopFire();
    UFUNCTION(Server, Reliable) void ServerReload();
    UFUNCTION(Server, Reliable) void ServerSelectWeapon(int32 Slot);
    void InitializeLoadout(int32 Primary);
    FVector GetVisualMuzzle(bool bFirstPerson) const;
    FVector GetIronSightLocation() const;
    float GetIronAimBlend() const { return IronAimBlend*IronAimBlend*(3.f-2.f*IronAimBlend); }
    FVector GetRenderedMuzzle(bool bFirstPerson) const;
    UFUNCTION(NetMulticast, Unreliable) void MulticastShot(FVector_NetQuantize Start, FVector_NetQuantize End, bool bTracer, class UERWeaponData* FiredData, const TArray<FVector_NetQuantize>& PelletEnds);
    void ApplyDeathVisuals();
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
private:
    bool bDeathVisualsApplied = false;
    UPROPERTY(Transient) TArray<FERProtectionSurfaceState> ProtectionSurfaces;
    bool bProtectionVisualsApplied = false;
    bool bProtectionMaterialsDirty = true;
    UPROPERTY() TObjectPtr<class UERWeaponData> AppliedWeaponData;
    bool bAppliedReloading = false;
    FVector FirstPersonBodyBaseLocation = FVector::ZeroVector;
    FRotator FirstPersonBodyBaseRotation = FRotator::ZeroRotator;
    float IronAimBlend = 0;
    void UpdateWeaponPresentation();
    FVector LastHitDirection = FVector::ForwardVector;
    FVector DamageViewKick = FVector::ZeroVector;
    FVector CosmeticDamagePitch = FVector::ZeroVector;
    FERAimPunchState AuthorityPunch, OwnerPunch;
    double OwnerPunchReceiptTime = 0, OwnerPunchReceiptServerTime = 0;
    mutable double OwnerPunchLastSampleTime = 0;
    TWeakObjectPtr<class AERWeapon> PunchPelletWeapon;
    uint32 PunchPelletShot = MAX_uint32;
    TArray<FERDamageVisual> DamageVisuals;
    float DeathViewDrop = 0;
    bool bDeathCameraInitialized = false;
    FVector DeathCameraLocation = FVector::ZeroVector;
    FVector DeathCameraAway = FVector::BackwardVector;
    FVector DeathCameraLastFocus = FVector::ZeroVector;
    float DeathCameraFOV = 90.f;
    bool bFPSPoseAudited = false;
    uint8 AppliedBodyTeam = 0;
    int32 AppliedSkin = INDEX_NONE;
    bool bLeftArmHidden = false;
    bool bRightArmHidden = false;
    bool bLocalGrenadeInputHeld = false;
    double LocalGrenadeHideUntil = 0;
    double LocalGrenadeCookDeadline = 0;
    float StepPhase = 0;
    FVector StepOffset = FVector::ZeroVector;
    TMap<const UEROneShotAudio*, FERAudioShuffle> ShotShuffles;
    float CameraKick = 0;
    double MuzzleFlashUntil = 0;
};
