#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ERLobbyProbe.generated.h"
UCLASS(Transient)
class AERLobbyProbe : public AActor
{
    GENERATED_BODY()
public:
    AERLobbyProbe();
    virtual void Tick(float DeltaTime) override;
private:
    int32 Phase = 0;
    double Deadline = 0;
    bool Failed = false;
    void Check(bool Value, const TCHAR* Label);
    bool TickSkullHunt(class AERPlayerController& PC, const class AERGameState& State, double Now);
};
