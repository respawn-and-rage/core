#include "ERDepotNight.h"
#include "Components/StaticMeshComponent.h"

bool AERDepotNight::GenerateLogicalWorld()
{
    TArray<FERCellPanelDesc> Panels;
    MapLamps.Reset();
    ERMapLighting::BuildDepot(CellSettings.CellSize, MapLampSurface, Panels, MapLamps);
    return BuildCellWorld(Panels);
}

void AERDepotNight::ConfigureFloors()
{
    Floor->SetStaticMesh(nullptr);
    Floor->ComponentTags.AddUnique(TEXT("ERTemplateVisual"));
}
