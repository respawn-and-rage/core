#pragma once

#include "CoreMinimal.h"

struct FERCellPanelDesc;
class FERCellWorld;
struct FERProceduralLayout;
class UMaterialInterface;
class UERMapTemplate;

// A light emitter may serve one lens or one four-lens floodlight bank.
// Cell indices are local to the replicated geometry for the current epoch.
struct FERMapLamp
{
    TArray<int32, TInlineAllocator<4>> Lenses;
    int32 Pole = INDEX_NONE;
    FVector Position = FVector::ZeroVector;
    FRotator Direction = FRotator(-90, 0, 0);
    float Radius = 750;
    float Candelas = 8;
    float InnerCone = 30;
    float OuterCone = 49;

    int32 WorkingLenses(const FERCellWorld& Cells) const;
};

struct FERMapLightZone
{
    TArray<int32> Lamps;
    TArray<FVector, TInlineAllocator<1>> FillPositions;
    float Radius = 0.f;
    float Intensity = 1.f;
    FLinearColor Color = FLinearColor::White;

    float PoweredFraction(const TArray<FERMapLamp>& MapLamps, const FERCellWorld& Cells) const;
};

namespace ERMapLighting
{
inline constexpr TCHAR LampMaterialPath[] = TEXT("/Game/EclipseRecoil/Art/Lighting/M_OfficeCeilingWhite");
void Build(FERProceduralLayout& Layout, const UERMapTemplate& Recipe, float CellSize,
    UMaterialInterface* LampSurface, bool Night, TArray<FERMapLamp>& Lamps,
    TArray<FERMapLightZone>& Zones);
void BuildDepot(float CellSize, UMaterialInterface* LampSurface,
    TArray<FERCellPanelDesc>& Panels, TArray<FERMapLamp>& Lamps, TArray<FERMapLightZone>& Zones);
}
