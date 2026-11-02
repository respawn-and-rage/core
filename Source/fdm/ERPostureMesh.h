#pragma once
#include "CoreMinimal.h"
#include "Components/SkeletalMeshComponent.h"
#include "ERTeamAppearance.h"
#include "ERShotHistory.h"
#include "ERPostureMesh.generated.h"

// Procedural prone and weapon support-hand fitting over the team locomotion.
UCLASS()
class FDM_API UERPostureMesh : public USkeletalMeshComponent
{
    GENERATED_BODY()
public:
    virtual void TickComponent(float DeltaTime,ELevelTick TickType,FActorComponentTickFunction* ThisTickFunction) override;
    virtual void FinalizeBoneTransform() override;
    // Optional local dressing-room inputs; gameplay characters always use their own state.
    UPROPERTY(Transient) TObjectPtr<class UERWeaponData> PreviewWeapon;
    UPROPERTY(Transient) FERTeamAppearance PreviewAppearance;
    FERShotHistory ShotHistory;
    float GetWorldAimBlend() const {return WorldAimBlend;}
private:
    float WorldAimBlend=0;
};
