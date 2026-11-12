#pragma once

#include "CoreMinimal.h"

class AActor;
class UMaterialInterface;
class UWorld;

namespace EROutdoorBackdrop
{
bool IsOutdoorMap(const UWorld& World);
uint32 RollLightingSeed();
bool Apply(AActor& Host, uint32 Seed, const FLinearColor& GroundTint, UMaterialInterface* GroundSurface=nullptr, uint32 LightingSeed=0);
}
