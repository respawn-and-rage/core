#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ERNetworkProbe.generated.h"

// Opt-in development integration harness; never spawned in a normal game.
UCLASS(NotBlueprintable)
class AERNetworkProbe : public AActor
{
    GENERATED_BODY()
public:
    AERNetworkProbe();
    virtual void Tick(float DeltaTime) override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
private:
    UPROPERTY(Replicated) int32 Phase = 0;
    int32 ClientPhase = -1;
    double ClientDeathSeenAt = -1;
    bool bClientLethalBeatChecked = false;
    bool bClientReleasedRespawnSprint = false;
    double ClientRespawnSprintAt = -1;
    bool bSawRespawnSprint = false;
    double Deadline = 0;
    double EmptyClipTimeout = 0;
    UPROPERTY() TObjectPtr<class AERPlayerController> Host;
    UPROPERTY() TObjectPtr<class AERPlayerController> Client;
    bool bFailed = false;
    TWeakObjectPtr<class AERCorpse> FirstCorpse;
    uint32 ClientFeedbackBefore = 0;
    FRotator ClientAimBefore;
    void Advance(int32 Next, float Delay);
    void Check(bool Condition, const TCHAR* Label);
    void PlacePlayers(bool AcrossWall = false);
    void HostShot();
    void ClientStep();
};
