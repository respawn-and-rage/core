#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "ERAudio.h"
#include "ERFootsteps.generated.h"

// Distance, not input or a free-running timer. No predicted client footstep RPCs.
struct FDM_API FERFootstepCadence
{
    FVector Previous=FVector::ZeroVector;
    float Distance=0;
    double LastStep=-1.e9;
    bool Grounded=false;
    static float Stride(float Speed, bool Low, bool Prone);
    static float Gain(float Speed, bool Low, bool Prone);
    bool Advance(const FVector& Position, bool Active, float Speed, bool Low, bool Prone, double Now);
};
UCLASS(ClassGroup=(Audio), meta=(BlueprintSpawnableComponent))
class FDM_API UERFootsteps : public UActorComponent
{
    GENERATED_BODY()
public:
    UERFootsteps();
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TObjectPtr<UERFootstepAudio> Audio;
    UPROPERTY(EditDefaultsOnly, Category="Water") TArray<TObjectPtr<class USoundWave>> WaterSteps;
    virtual void BeginPlay() override;
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
    UFUNCTION(NetMulticast, Unreliable) void MulticastStep(FVector_NetQuantize Position, EERFootstepSurface Surface, float Gain);
    uint32 EmittedCount=0, PlaybackCount=0;
    EERFootstepSurface LastSurface=EERFootstepSurface::Hard;
private:
    FERFootstepCadence Cadence;
    bool bWasInWater=false;
    TMap<const UEROneShotAudio*, FERAudioShuffle> Shuffles;
};
