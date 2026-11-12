#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ERArsenalProbe.generated.h"

// Explicit local development flags only; never spawned in ordinary matches.
UCLASS(NotBlueprintable)
class AERArsenalProbe : public AActor
{
    GENERATED_BODY()
public:
    AERArsenalProbe();
    virtual void Tick(float DeltaTime) override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Props) const override;
private:
    UPROPERTY(Replicated) int32 Phase = 0;
    int32 ClientPhase = -1, GalleryIndex = 0;
    double Deadline = 0;
    double WheelBurstEnd = 0, NextWheelPulse = 0;
    double BurstFrameTotal = 0, BurstFrameMax = 0;
    int32 BurstFrames = 0, BurstSlowFrames = 0;
    bool Failed = false;
    bool bRemoteSmokeCutLogged = false;
    TWeakObjectPtr<class AERSpentCasing> GalleryCasing;
    FVector CasingOrigin = FVector::ZeroVector;
    int32 MotionAmmoBefore = 0;
    UPROPERTY() TObjectPtr<class AERPlayerController> Host;
    UPROPERTY() TObjectPtr<class AERPlayerController> Client;
    void Check(bool Value, const TCHAR* Label);
    void Advance(int32 Next, float Delay);
    void TickMuzzleGallery();
    FVector FirstFlashScale = FVector::ZeroVector;
};
