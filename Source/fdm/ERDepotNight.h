#pragma once

#include "ERBlockRoom.h"
#include "ERDepotNight.generated.h"

// The fixed Depot has no procedural arena to own its night floodlights.
UCLASS()
class AERDepotNight : public AERBlockRoom
{
    GENERATED_BODY()

protected:
    bool GenerateLogicalWorld() override;
    void ConfigureFloors() override;
};
