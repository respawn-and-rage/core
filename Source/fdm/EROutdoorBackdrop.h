#pragma once

#include "CoreMinimal.h"

class AActor;
class UMaterialInterface;
class UWorld;
class UTexture2D;

namespace EROutdoorBackdrop
{
bool IsOutdoorMap(const UWorld& World);
bool IsOutdoorMap(const FString& MapName);
bool IsNight(const UWorld& World);
TArray<FString> OfflineSkyLabels();
UTexture2D* LoadOfflineSkyPreview(int32 Choice, FVector2D& Size);
uint32 RollLightingSeed();
bool Apply(AActor& Host, uint32 Seed, const FLinearColor& GroundTint, UMaterialInterface* GroundSurface=nullptr, uint32 LightingSeed=0);
}
