#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ERMapTemplateProbe.generated.h"
UCLASS()
class FDM_API AERMapTemplateProbe : public AActor
{
    GENERATED_BODY()
public:
    AERMapTemplateProbe();
    virtual void Tick(float Delta) override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;
private:
    UPROPERTY(Replicated) int32 Phase=0;
    UPROPERTY(Replicated) uint32 ExpectedDigest=0;
    float Elapsed=0;
    int32 SeenPhase=0, RoofTest=0,GalleryIndex=0;
    bool RoofCapture=false,GalleryCapture=false;
    bool Failed=false;
    float ParapetElapsed=0;
    int32 ParapetLocalPhase=0;
    bool ParapetJump=false,ParapetTraversal=false,ParapetReported=false;
    void TickParapets(float Delta,class AERProceduralMap* Map);
    void TickTown(class AERProceduralMap* Map);
    float TownStage=0;
    int32 TownPath=0,TownPoint=0,TownCapture=0;
    bool TownWalking=false;
    void Check(bool Condition,const TCHAR* Label);
    void Publish(int32 Next,class AERProceduralMap* Map);
};
