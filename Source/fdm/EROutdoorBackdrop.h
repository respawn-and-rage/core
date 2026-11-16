#pragma once

#include "CoreMinimal.h"
#include "EROutdoorBackdrop.generated.h"

USTRUCT()
struct FEROutdoorLighting
{
    GENERATED_BODY()

    UPROPERTY() uint32 Seed = 0;
    UPROPERTY() int32 SkyChoice = 0;
};

class AActor;
class UMaterialInterface;
class UWorld;
class UTexture2D;

namespace EROutdoorBackdrop
{
bool IsOutdoorMap(const UWorld& World);
bool IsOutdoorMap(const FString& MapName);
bool IsNight(int32 SkyChoice);
FEROutdoorLighting ChooseLighting(const UWorld& World);
TArray<FString> OfflineSkyLabels();
UTexture2D* LoadOfflineSkyPreview(int32 Choice, FVector2D& Size);
bool Apply(AActor& Host, uint32 Seed, const FLinearColor& GroundTint, UMaterialInterface* GroundSurface, const FEROutdoorLighting& Lighting);
}
