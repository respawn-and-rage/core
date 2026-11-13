#include "ERDepotNight.h"
#include "Components/StaticMeshComponent.h"

bool AERDepotNight::GenerateLogicalWorld()
{
    TArray<FERCellPanelDesc> Panels;
    TArray<FERMapLamp> NextLamps;
    TArray<FERMapLightZone> NextZones;
    ERMapLighting::BuildDepot(CellSettings.CellSize, MapLampSurface, Panels, NextLamps, NextZones);
    if (!BuildCellWorld(Panels)) return false;
    MapLamps = MoveTemp(NextLamps);
    MapLightZones = MoveTemp(NextZones);
    return true;
}

void AERDepotNight::ConfigureFloors()
{
    Floor->SetStaticMesh(nullptr);
    Floor->ComponentTags.AddUnique(TEXT("ERTemplateVisual"));
}
