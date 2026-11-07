#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ERNetworkReadinessProbe.generated.h"

// Opt-in two-process regression; never created in ordinary games.
UCLASS()
class AERNetworkReadinessProbe : public AActor
{
    GENERATED_BODY()
public:
    AERNetworkReadinessProbe();
    virtual void Tick(float DeltaSeconds) override;
private:
    double BeganWaiting=0;
    int32 Phase=0;
    bool Failed=false;
    void Check(bool Passed,const TCHAR* Label);
};
