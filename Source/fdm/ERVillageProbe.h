#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ERVillageProbe.generated.h"

UCLASS()
class FDM_API AERVillageProbe : public AActor
{
    GENERATED_BODY()
public:
    AERVillageProbe();
    virtual void Tick(float Delta) override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;
private:
    UPROPERTY(Replicated) int32 Phase=0;
    UPROPERTY(Replicated) uint32 ExpectedDigest=0;
    int32 Seen=0,Point=0,Shot=0,Flight=0;
    float Elapsed=0,Stage=0,FlightTime=0;
    bool Failed=false,Walking=false,SawUpperLadder=false,SawUpperExit=false,UpperLadderDone=false,SawFordDepth=false;
    void Check(bool Good,const TCHAR* Label);
    void Publish(int32 Next,class AERProceduralMap* Map);
};
