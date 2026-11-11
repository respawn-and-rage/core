#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameFramework/Actor.h"
#include "ERMatch.h"
#include "ERMinimapGeometry.h"
#include "ERRecon.generated.h"

namespace ERRecon
{
    constexpr int32 Grid = 64;
    constexpr int32 FogBytes = Grid * Grid / 8;
    constexpr int32 Stock = 5;
    constexpr double ReplacementSeconds = 30;
    FLinearColor TeamColor(EERTeam Team);
    bool InSightCone(const FVector& Eye, const FVector& Forward, const FVector& Target, float Range, bool Panoramic, float FullAngle=120);
    bool IsExplored(const TArray<uint8>& Bits, int32 Cell);
    void Explore(TArray<uint8>& Bits, int32 Cell);
}

USTRUCT()
struct FERReconContact
{
    GENERATED_BODY()
    UPROPERTY() FVector_NetQuantize Position;
    UPROPERTY() float Yaw = 0;
    UPROPERTY() EERTeam Team = EERTeam::None;
    UPROPERTY() bool Self = false;
    UPROPERTY() bool Drone = false;
};

// One atomic owner-only snapshot: enemies never enter another team's radar feed
// unless currently seen. Fog is permanent within this team/round/map epoch.
USTRUCT()
struct FERReconSnapshot
{
    GENERATED_BODY()
    UPROPERTY() uint32 Epoch = 0;
    UPROPERTY() EERTeam Team = EERTeam::None;
    UPROPERTY() TArray<uint8> Explored;
    UPROPERTY() TArray<FERReconContact> Contacts;
    UPROPERTY() int32 Remaining = 5;
    UPROPERTY() bool Airborne = false;
    UPROPERTY() double NextLaunch = 0;
    UPROPERTY() double SampleTime = 0;
};

UCLASS()
class FDM_API UERReconComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    UERReconComponent();
    UPROPERTY(Replicated) FERReconSnapshot Snapshot;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
};

UCLASS(Blueprintable)
class FDM_API AERReconBase : public AActor
{
    GENERATED_BODY()
public:
    AERReconBase();
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) TObjectPtr<class UStaticMeshComponent> Case;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) TObjectPtr<class UStaticMeshComponent> Lid;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) TObjectPtr<class UStaticMeshComponent> Lock;
    UPROPERTY(VisibleAnywhere) TObjectPtr<class UDecalComponent> Emblem;
    UPROPERTY(EditDefaultsOnly) TArray<float> LidOpening;
    UPROPERTY(EditDefaultsOnly) TArray<float> LockOpening;
    UPROPERTY(EditDefaultsOnly) TObjectPtr<class UMaterialInterface> AlphaGraffiti;
    UPROPERTY(EditDefaultsOnly) TObjectPtr<class UMaterialInterface> OmegaGraffiti;
    UPROPERTY(Replicated) double LaunchAt = -100;
    UPROPERTY(Replicated) EERTeam Team = EERTeam::None;
    virtual void Tick(float DeltaSeconds) override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
};

USTRUCT()
struct FERDroneFlight
{
    GENERATED_BODY()
    UPROPERTY() FVector Launch = FVector::ZeroVector;
    UPROPERTY() FVector Center = FVector::ZeroVector;
    UPROPERTY() FVector2D Radius = FVector2D(2000);
    UPROPERTY() double StartedAt = 0;
    UPROPERTY() EERTeam Team = EERTeam::None;
};

UCLASS(Blueprintable)
class FDM_API AERReconDrone : public AActor
{
    GENERATED_BODY()
public:
    AERReconDrone();
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) TObjectPtr<class USphereComponent> ShotTarget;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) TObjectPtr<class UStaticMeshComponent> Body;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) TObjectPtr<class UDecalComponent> UndersideSymbol;
    UPROPERTY(EditDefaultsOnly) TObjectPtr<class UMaterialInterface> AlphaSymbolMaterial;
    UPROPERTY(EditDefaultsOnly) TObjectPtr<class UMaterialInterface> OmegaSymbolMaterial;
    UPROPERTY(Replicated) FERDroneFlight Flight;
    virtual void Tick(float DeltaSeconds) override;
    virtual float TakeDamage(float Amount, const FDamageEvent& Event, AController* DamageInstigator, AActor* Causer) override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
    static FVector FlightPosition(const FERDroneFlight& Route, double ServerTime);
    TWeakObjectPtr<class AERReconDirector> Director;
private:
    bool bShotDown = false;
};

USTRUCT()
struct FERReconMapFrame
{
    GENERATED_BODY()
    UPROPERTY() FVector Center = FVector::ZeroVector;
    UPROPERTY() float Span = 6400;
    UPROPERTY() float Top = 1400;
    // Roofed arenas: absolute eave height and orbit radii keep the drone indoors.
    UPROPERTY() float Indoor = 0;
    UPROPERTY() FVector2D IndoorOrbit = FVector2D::ZeroVector;
    UPROPERTY() uint32 Epoch = 0;
    UPROPERTY() uint32 GeometryEpoch = 0;
};

UCLASS(Blueprintable)
class FDM_API AERReconDirector : public AActor
{
    GENERATED_BODY()
public:
    AERReconDirector();
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite) TSubclassOf<AERReconDrone> DroneClass;
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite) TSubclassOf<AERReconBase> BaseClass;
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category="Recon|Effects") TArray<TObjectPtr<class UStaticMesh>> DebrisMeshes;
    UPROPERTY(EditDefaultsOnly) TObjectPtr<class UMaterialInterface> AlphaGraffiti;
    UPROPERTY(EditDefaultsOnly) TObjectPtr<class UMaterialInterface> OmegaGraffiti;
    UPROPERTY(Replicated) FERReconMapFrame Frame;
    UPROPERTY(VisibleAnywhere) TObjectPtr<class USceneCaptureComponent2D> BirdCamera;
    UPROPERTY(Transient) TObjectPtr<class UTextureRenderTarget2D> BirdView;
    virtual void Tick(float DeltaSeconds) override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
    void ResetRecon();
    void DroneDestroyed(AERReconDrone* Drone);
    FVector CellPosition(int32 Cell) const;
    int32 WorldCell(const FVector& World) const;
    bool CanSee(const FVector& Eye, const FVector& Forward, const FVector& Target, bool Drone) const;
    void DrawMinimap(class AERHUD* HUD, class AERPlayerController* PC);
    AERReconDrone* ActiveDrone(EERTeam Team) const;
    int32 Remaining(EERTeam Team) const;
    double NextLaunch(EERTeam Team) const;
    UFUNCTION(NetMulticast, Reliable) void MulticastDroneBreak(FVector_NetQuantize Position, FRotator Rotation, FVector_NetQuantizeNormal Direction, int32 Seed);
private:
    friend class AERReconProbe;
    struct FTeamRecon
    {
        TArray<uint8> Explored;
        TWeakObjectPtr<AERReconBase> Base;
        TWeakObjectPtr<AERReconDrone> Drone;
        int32 Launched = 0;
        double NextLaunch = 0;
        int32 FogCursor = 0;
    };
    FTeamRecon Teams[2];
    TArray<float> SurfaceHeights;
    uint32 SourceMapEpoch = MAX_uint32;
    uint32 CapturedEpoch = 0;
    double NextUpdate = 0;
    // Fraction of one fog grid pass owed to the next server ticks.
    float FogPass = 0;
    double CaptureAfter = 0;
    bool bCapturePending = false;
    UPROPERTY(Transient) TArray<TObjectPtr<class AERBlockDebris>> LocalDebris;
    uint32 DebrisEpoch = 0;
    uint32 BreakEvents = 0;
    FVector LastBreakPosition = FVector::ZeroVector;
    int32 LastDrawnEnemies = 0;
    double LastHUDSample = -1;
    FERMinimapFog MinimapFog;
    UPROPERTY(Transient) TArray<TObjectPtr<class UDecalComponent>> Graffiti;
    uint32 GraffitiEpoch = 0;
    void DecorateBases();
    void TickGraffitiGallery();
    double GraffitiGalleryStart = -1;
    int32 GraffitiGalleryStep = 0;
    UPROPERTY(Transient) TObjectPtr<class ACameraActor> GraffitiCamera;
    void ConfigureMap();
    void LaunchDrone(int32 Team);
    void ExploreFog(float DeltaSeconds);
    void UpdateVisibility();
    void CaptureBirdView();
    void ClearFleet();
    void ClearDebris();
    bool SightClear(const FVector& Eye, const FVector& Target, const FCollisionQueryParams& Query, const TArray<class AERSmokeCloud*>& Clouds) const;
    float SightRange(bool Drone) const { return Drone ? ERServer::Get(GetWorld(),"Recon.Range")*100 : FMath::Max(Frame.Span,3000.f); }
    FCollisionQueryParams SightQuery() const;
};
