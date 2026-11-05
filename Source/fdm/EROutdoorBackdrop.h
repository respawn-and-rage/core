#pragma once

#include "CoreMinimal.h"

class AActor;
class UMaterialInterface;

namespace EROutdoorBackdrop
{
bool Apply(AActor& Host, uint32 Seed, const FLinearColor& GroundTint, UMaterialInterface* GroundSurface=nullptr);
}
