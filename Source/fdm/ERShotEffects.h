#pragma once
#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "GameFramework/Actor.h"
#include "ERShotEffects.generated.h"

UCLASS()
class FDM_API UERShotEffects : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    static void Prewarm(UWorld* World, class UNiagaraSystem* System, const FVector& Position);
    static void SpawnTracer(UWorld* World, class UNiagaraSystem* System, const FVector& Start, const FVector& End, float Lifetime = .02f);
    static void SpawnImpact(UWorld* World, class UNiagaraSystem* System, const FVector& Position, const FVector& Normal);
    static void SpawnWaterSplash(UWorld* World, class UNiagaraSystem* System, const FVector& Surface);
    static void SpawnWaterExplosion(UWorld* World, class UNiagaraSystem* System, const FVector& Surface);
    UFUNCTION(BlueprintCallable, Category="Development")
    static bool ConfigureImpact(class UNiagaraSystem* System, class UMaterialInterface* Material);
    UFUNCTION(BlueprintCallable, Category="Development")
    static bool ConfigureTracer(class UNiagaraSystem* System, class UMaterialInterface* Material);
};

// Local presentation only: never a projectile, damage source or gameplay obstacle.
UCLASS()
class FDM_API AERSpentCasing : public AActor
{
    GENERATED_BODY()
public:
    AERSpentCasing();
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) TObjectPtr<class UStaticMeshComponent> Mesh;
    virtual void BeginPlay() override;
    static AERSpentCasing* Eject(UWorld* World, TSubclassOf<AERSpentCasing> Class,
        const FVector& Position, const FVector& Right, const FVector& Forward, float VisualScale = 1.f);
};

// Local tracer with a fixed world-space path.
UCLASS()
class FDM_API AERAttachedTracer : public AActor
{
    GENERATED_BODY()
public:
    AERAttachedTracer();
    UPROPERTY(VisibleAnywhere) TObjectPtr<class UNiagaraComponent> Beam;
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;
    static AERAttachedTracer* Spawn(class AERCharacter* Character, class UNiagaraSystem* System,
        const FVector& End, bool bFirstPerson, float Duration = .016f, const FVector* PredictedMuzzle = nullptr);
    static void SpawnMuzzleStreak(class AERCharacter* Character, class UNiagaraSystem* System, uint32 ShotKey);
    FVector LastStart = FVector::ZeroVector;
    FVector LastEnd = FVector::ZeroVector;
    uint32 UpdatedFrames = 0;
private:
    static AERAttachedTracer* SpawnSegment(class AERCharacter* Character, class UNiagaraSystem* System,
        const FVector& Start, const FVector& End, bool bFirstPerson, float Duration, bool bTravel,
        TOptional<uint32> PredictedShot = {});
    void BeginMuzzleStreak(AERCharacter& Character);
    void SetVisibleSegment(const FVector& Start, const FVector& End);
    TWeakObjectPtr<class AERCharacter> Source;
    TWeakObjectPtr<class UERWeaponData> ShotData;
    bool bFirstPerson = false;
    bool bTravel = false;
    TOptional<uint32> PredictedShotKey;
    double VisibleSince = -1;
    float DurationSeconds = .016f;
};
