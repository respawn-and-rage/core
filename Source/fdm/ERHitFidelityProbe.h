#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ERHitFidelityProbe.generated.h"
// Opt-in audit of what a shooter sees against what the server hits and where feet meet the floor.
UCLASS()
class FDM_API AERHitFidelityProbe : public AActor
{
    GENERATED_BODY()
public:
    AERHitFidelityProbe();
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaTime) override;
private:
    UPROPERTY() TObjectPtr<class AERCharacter> Subject;
    UPROPERTY() TObjectPtr<class AERCharacter> Shooter;
    float Age=0,Changed=0,NextShot=0,MoveSign=1;
    int32 Sample=-1,Shots=0;
    bool Ready=false;
    void TickFidelity();
    void TickLagHost();
    void TickLagClient();
    void Configure();
    void Measure();
    void MeasureView(const TArray<FVector>& World,const TArray<uint32>& Triangles,FVector Across,const TCHAR* ViewName,const FVector& Feet,bool bImage);
    bool Moving() const;
};
