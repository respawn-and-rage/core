#pragma once
#include "CoreMinimal.h"
class UERWeaponData;
class AActor;
struct FERTeamAppearance;
// Full-size world presentation, independent of the fitted first-person rig.
namespace ERWorldWeaponPose
{
    FTransform Attachment(const UERWeaponData& Data,float ParentScale);
    FVector SupportContact(const UERWeaponData& Data,const FERTeamAppearance& Appearance);
    FTransform ClearObstacles(const UERWeaponData& Data, const AActor& Character, const FTransform& Desired);
}
