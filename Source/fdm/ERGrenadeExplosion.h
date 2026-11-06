#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ERGrenadeExplosion.generated.h"

// Local cosmetic burst spawned by an authoritative detonation multicast.
UCLASS()
class FDM_API AERGrenadeExplosion : public AActor
{
    GENERATED_BODY()
public:
    AERGrenadeExplosion();
    UPROPERTY(VisibleAnywhere) TObjectPtr<class USkeletalMeshComponent> Debris;
    UPROPERTY(VisibleAnywhere) TObjectPtr<class UStaticMeshComponent> Flash;
    UPROPERTY(VisibleAnywhere) TObjectPtr<class UPointLightComponent> Light;
    UPROPERTY(VisibleAnywhere) TObjectPtr<class UParticleSystemComponent> Burst;
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite) TObjectPtr<class UParticleSystem> BurstTemplate;
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite) TObjectPtr<class USkeletalMesh> DebrisMesh;
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite) TObjectPtr<class UAnimSequence> DebrisAnimation;
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite) TObjectPtr<class UStaticMesh> FlashMesh;
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite) TObjectPtr<class UMaterialInterface> FlashMaterial;
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite) TObjectPtr<class UStaticMesh> GroundFragmentMesh;
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite) TObjectPtr<class USoundBase> ExplosionSound;
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite) TObjectPtr<class USoundAttenuation> ExplosionAttenuation;
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite) TObjectPtr<class USoundConcurrency> ExplosionConcurrency;
    int32 GroundFragmentCount = 0;
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite) float BurstScale = .55f;
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite) float PlayRate = 1.5f;
    UFUNCTION(BlueprintCallable, Category="Respawn & Rage|Editor")
    static bool AssignDebrisMaterials(USkeletalMesh* Mesh, UMaterialInterface* Surface, UMaterialInterface* Side);
    UFUNCTION(BlueprintCallable, Category="Respawn & Rage|Editor")
    static bool ConfigureCompactBurst(UParticleSystem* System);
    static void PrecacheEffect(TSubclassOf<AERGrenadeExplosion> EffectClass);
    static USoundBase* SelectSound(UWorld* World, const FVector& Position, USoundBase* DefaultSound,
        class UNiagaraSystem*& WaterSystem, FVector& WaterSurface);
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;
private:
    struct FGroundChip { UStaticMeshComponent* Mesh; FVector Velocity, Spin, Scale; bool bSettled=false; };
    TArray<FGroundChip> GroundChips;
    void SpawnGroundChips();
    float Elapsed = 0, Duration = 3;
};
