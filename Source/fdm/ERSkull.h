#pragma once
#include "CoreMinimal.h"
#include "ERMatch.h"
#include "GameFramework/Actor.h"
#include "ERSkull.generated.h"

UCLASS()
class FDM_API AERSkull : public AActor
{
    GENERATED_BODY()
public:
    AERSkull();
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

private:
    friend class AERGameMode;

    UPROPERTY(VisibleAnywhere) TObjectPtr<class UStaticMeshComponent> Coin;
    UPROPERTY(Replicated) EERTeam VictimTeam = EERTeam::None;
    UPROPERTY(Replicated) double SpawnTime = 0;
};
