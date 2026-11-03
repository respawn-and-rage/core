#pragma once
#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "ERBlockWorld.h"
#include "ERMapTemplate.generated.h"

UENUM(BlueprintType)
enum class EERTemplateSymmetry : uint8 { None, HalfTurn, Mirror };

UENUM(BlueprintType)
enum class EERMapArchetype : uint8 { LegacyAssembly, CentralBuilding, MiddleEasternAlleys, FireTraining, Rooms, Village };

// Shoots-inspired timber village; dimensions and decoration have separate RNGs.
USTRUCT(BlueprintType)
struct FERVillageRecipe
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FVector2D ArenaMin=FVector2D(3400,4400);
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FVector2D ArenaMax=FVector2D(5600,13200);
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 MinCentralBlocks=1;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 MaxCentralBlocks=5;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FVector2D BlockWidth=FVector2D(2000,3000);
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FVector2D BlockDepth=FVector2D(600,800);
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 MinStoreys=1;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 MaxStoreys=3;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 StoreyBudget=10;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float DeckHeight=240;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float PerimeterHeight=1100;
    // Legacy serialized field; palette selection now belongs to ThemeLibrary.
    UPROPERTY() FName Theme=TEXT("Jungle");
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TArray<TObjectPtr<class UStaticMesh>> ExteriorTrees;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TObjectPtr<class UMaterialInterface> FoliageSurface;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TObjectPtr<class UMaterialInterface> TrunkSurface;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TObjectPtr<class UERDestructibleMesh> StorageStructure;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TObjectPtr<class UERDestructibleMesh> BridgeStructure;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TArray<TObjectPtr<class UStaticMesh>> ExteriorHouses;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TObjectPtr<class UStaticMesh> PalisadePoleMesh;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TObjectPtr<class UStaticMesh> SkyDomeMesh;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TObjectPtr<class UMaterialInterface> CoverSurface;
};

// Existing art only. A complete theme is sampled independently of the room graph.
USTRUCT(BlueprintType)
struct FERRoomsTheme
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FName Name;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TObjectPtr<class UMaterialInterface> Wall;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TObjectPtr<class UMaterialInterface> Floor;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TObjectPtr<class UMaterialInterface> Ceiling;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TObjectPtr<class UMaterialInterface> Wood;
};

// One connection plan baked by PCG_RoomsMacroGraph, in stable slots (ERRoomsMacro.h).
USTRUCT(BlueprintType)
struct FERRoomsMacroVariant
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 LaneRooms=0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 BakeSeed=0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TArray<FIntPoint> Links;
};

// Plain data: matches never run PCG, so no peer depends on graph execution.
UCLASS(BlueprintType)
class FDM_API UERRoomsMacroTable : public UDataAsset
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 Revision=1;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TArray<FERRoomsMacroVariant> Variants;
};

USTRUCT(BlueprintType)
struct FERRoomsRecipe
{
    GENERATED_BODY()
    // Two bases plus two lanes of 1..4 rooms: 4, 6, 8 or 10 rooms in all.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="1",ClampMax="4")) int32 MinLaneRooms=2;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="1",ClampMax="4")) int32 MaxLaneRooms=4;
    // Zero keeps the authored legacy dimensions. Runtime sizing changes rooms, never door/capsule scale.
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float NominalRoomSize=0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float LaneSpacing=2400;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float LaneOffset=1700;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float PlanMargin=1100;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float CorridorWidth=360;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float CorridorHeight=560;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float CornerCut=55;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TArray<FERRoomsTheme> Themes;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TObjectPtr<class UMaterialInterface> LampSurface;
    // Read only when the replicated descriptor enables the macro graph.
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TObjectPtr<UERRoomsMacroTable> MacroTable;
};

UENUM(BlueprintType)
enum class EERCrossingRule : uint8 { Seeded, OpenYard, ClimbableWall, FencedCrossings };

// Gameplay rules and industrial art are independent from any particular seed.
USTRUCT(BlueprintType)
struct FERCentralBuildingRecipe
{
    GENERATED_BODY()
    // Circular terrain and reflection across Y=0; false retains authored rectangular studies.
    UPROPERTY(EditAnywhere, BlueprintReadWrite) bool CircularTerrain=true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FVector2D ArenaMin=FVector2D(6400,8400);
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FVector2D ArenaMax=FVector2D(8000,10000);
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FVector2D BuildingWidth=FVector2D(1800,2400);
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FVector2D BuildingDepth=FVector2D(2200,3000);
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="1",ClampMax="3")) int32 MinFloors=1;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="1",ClampMax="3")) int32 MaxFloors=3;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TObjectPtr<class UStaticMesh> FenceMesh;
    // Optional original straight module, repeated inside the existing barrier bounds.
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TObjectPtr<class UStaticMesh> FenceModule;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TObjectPtr<class UStaticMesh> ContainerMesh;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TObjectPtr<class UStaticMesh> TankMesh;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TObjectPtr<class UStaticMesh> BarrelMesh;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TObjectPtr<class UMaterialInterface> DeckSurface;
    // Visual palette only: steel calibration and container reinforcement are unchanged.
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TObjectPtr<class UMaterialInterface> SteelSurface;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TArray<TObjectPtr<class UMaterialInterface>> ContainerSurfaces;
    // One coherent finish per construction type and round, independent of geometry RNG.
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TArray<TObjectPtr<class UMaterialInterface>> ConcreteSurfaces;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TArray<TObjectPtr<class UMaterialInterface>> BrickSurfaces;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TArray<TObjectPtr<class UMaterialInterface>> WoodSurfaces;
    // Asphalt, grass or snow are cosmetic; ground collision/friction stay identical.
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TArray<TObjectPtr<class UMaterialInterface>> GroundSurfaces;
};

// Legacy warehouse references retained solely for loading and migrating old recipes.
USTRUCT(BlueprintType)
struct FERWarehouseWidthKit
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 WidthCm=1600;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TObjectPtr<class UStaticMesh> RoofBay;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TObjectPtr<class UStaticMesh> RoofArch;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TObjectPtr<class UStaticMesh> RoofLamps;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TObjectPtr<class UStaticMesh> EndGround;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TObjectPtr<class UStaticMesh> EndBand;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TObjectPtr<class UStaticMesh> EndCrown;
};

// Closed procedural training hall with independent dimensions and local elevated routes.
// Legacy kit fields remain readable for asset migration; generation never uses them.
USTRUCT(BlueprintType)
struct FERFireTrainingRecipe
{
    GENERATED_BODY()
    // Square metres: length follows the sampled width within this floor range.
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FVector2D FloorArea=FVector2D(1300,7000);
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="6",ClampMax="36")) int32 MinBays=14;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="6",ClampMax="36")) int32 MaxBays=36;
    // Wider halls repeat the arched nave side by side on open column lines.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="1",ClampMax="4")) int32 MinNaves=1;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="1",ClampMax="4")) int32 MaxNaves=4;
    // Longest allowed length / total width, so narrow halls never become tubes.
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float MaxAspect=1.5f;
    // Largest halls reserve this many empty plazas per half, scaled by floor area.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="0",ClampMax="3")) int32 MaxPlazas=3;
    // 300cm wall bands only raise the envelope; they never add an upper floor.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="1",ClampMax="5")) int32 MinHeightBands=2;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="1",ClampMax="5")) int32 MaxHeightBands=4;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float MinCeilingHeight=800;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float MaxCeilingHeight=1400;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float ElevatedTargetRatio=.2f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float ElevatedMaxRatio=.3f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float OverheadMaxRatio=.4f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float OpenRegionMinRatio=.45f;
    // A mode constraint, never a menu option: validation rejects true.
    UPROPERTY(EditAnywhere, BlueprintReadWrite) bool ContinuousUpperFloors=false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TArray<FERWarehouseWidthKit> Widths;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TObjectPtr<class UStaticMesh> SideWallBay;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TObjectPtr<class UStaticMesh> SideWallLamps;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TObjectPtr<class UStaticMesh> Post;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TObjectPtr<class UStaticMesh> EndPortal;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TObjectPtr<class UMaterialInterface> GroundSurface;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TObjectPtr<class UMaterialInterface> SteelSurface;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TObjectPtr<class UMaterialInterface> DeckSurface;
    // Original industrial steel barrier, used at its 127cm height on walkways.
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TObjectPtr<class UStaticMesh> ParapetModule;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TArray<TObjectPtr<class UMaterialInterface>> WoodSurfaces;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TArray<TObjectPtr<class UMaterialInterface>> ConcreteSurfaces;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TArray<TObjectPtr<class UMaterialInterface>> ContainerSurfaces;
};

UENUM(BlueprintType)
enum class EERTemplateModule : uint8 { Room, LCover, UCover, StaggeredWalls, Cargo, Tanks, Crates };

// Reference-plan coordinates are percentages, X east, Y south. Shared axis
// warping changes parcels/lanes without changing the named route graph.
USTRUCT(BlueprintType)
struct FERPlanStreet
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FName Name;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TArray<FVector2D> Points;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FVector2D Width=FVector2D(300,400);
    UPROPERTY(EditAnywhere, BlueprintReadWrite) bool Elevated=false;
};
USTRUCT(BlueprintType)
struct FERPlanCourt
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FName Name;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FVector2D Center=FVector2D::ZeroVector;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FVector2D Size=FVector2D(12,12);
};
USTRUCT(BlueprintType)
struct FERPlanGate
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FName Street;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FVector2D Position=FVector2D::ZeroVector;
};
USTRUCT(BlueprintType)
struct FERDustTownVariation
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite) bool Enabled=false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FVector2D ArenaMin=FVector2D(7600,8400);
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FVector2D ArenaMax=FVector2D(8600,9600);
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float AxisWarp=2;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FVector2D BuildingHeight=FVector2D(450,800);
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FVector2D DoorGap=FVector2D(110,130);
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TArray<FERPlanStreet> Streets;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TArray<FERPlanCourt> Courts;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TArray<FERPlanGate> Gates;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TArray<FVector2D> Patrol;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FVector2D Crossing=FVector2D(68,30);
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TObjectPtr<class UMaterialInterface> WallSurface;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TObjectPtr<class UMaterialInterface> RoofSurface;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TObjectPtr<class UMaterialInterface> RoadSurface;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TObjectPtr<class UMaterialInterface> WoodSurface;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TObjectPtr<class UMaterialInterface> IronSurface;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TObjectPtr<class UStaticMesh> ArchMesh;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TObjectPtr<class UStaticMesh> LeftDoorMesh;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TObjectPtr<class UStaticMesh> RightDoorMesh;
    // Optional alley palette. Cosmetic choices use a separate seeded stream.
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TArray<TObjectPtr<class UStaticMesh>> BuildingMeshes;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TObjectPtr<class UMaterialInterface> GroundSurface;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) bool CoveredTunnels=false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) bool TraversableTown=false;
    // Revision3: generate the street graph itself, with paired gameplay spaces.
    UPROPERTY(EditAnywhere, BlueprintReadWrite) bool SeededDistrict=false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="2",ClampMax="4")) int32 MinAccesses=2;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="2",ClampMax="4")) int32 MaxAccesses=4;
    // Revision4: open square, one city wall, or two walls around an open core.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="0",ClampMax="2")) int32 MinCentralWalls=1;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="0",ClampMax="2")) int32 MaxCentralWalls=1;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) bool LowCoverAndUnderpass=false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) bool DestructibleArchitecture=false;
    // Revision7: dimensioned masonry modules replace fitted imported houses.
    UPROPERTY(EditAnywhere, BlueprintReadWrite) bool ProceduralHouses=false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TArray<TObjectPtr<class UERDestructibleMesh>> BuildingStructures;
};

// Ranges and art palette belong to the template, never to runtime file paths.
USTRUCT(BlueprintType)
struct FERMapTemplateVariation
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite) bool Enabled=false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FVector2D ArenaMin=FVector2D(3600,7000);
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FVector2D ArenaMax=FVector2D(4800,9000);
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FVector2D BaseWidth=FVector2D(1000,1400);
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FVector2D BaseDepth=FVector2D(850,1050);
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 MinRows=2;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 MaxRows=3;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TArray<EERTemplateModule> Modules={EERTemplateModule::Room,EERTemplateModule::LCover,EERTemplateModule::UCover,EERTemplateModule::StaggeredWalls,EERTemplateModule::Cargo,EERTemplateModule::Tanks,EERTemplateModule::Crates};
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TObjectPtr<class UStaticMesh> CargoMesh;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TObjectPtr<class UStaticMesh> TankMesh;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TObjectPtr<class UStaticMesh> BarrelMesh;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TObjectPtr<class UStaticMesh> ColumnMesh;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TObjectPtr<class UStaticMesh> BeamMesh;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TObjectPtr<class UStaticMesh> GuardMesh;
};

// Authored assembly boxes, in centimetres. A shared group moves a complete
// bunker/prop together; different groups vary independently on a 50cm grid.
USTRUCT(BlueprintType)
struct FERMapTemplatePiece
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FVector Center=FVector::ZeroVector;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FVector SymmetryCenter=FVector::ZeroVector;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FVector Size=FVector(100);
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FVector2D Jitter=FVector2D::ZeroVector;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 Group=0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) EERBlockMaterial Material=EERBlockMaterial::Wood;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) bool Destructible=true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) bool Mirrored=false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) bool Anchored=false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="1",ClampMax="16")) float Strength=1;
    // Fixed boxes or horizontal destructible construction panels, never fitted art.
    UPROPERTY(EditAnywhere, BlueprintReadWrite) bool WalkableSurface=false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FRotator MeshRotation=FRotator::ZeroRotator;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float RotationYaw=0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) bool VisualOnly=false;
    // Permanent movement barrier; visible metal has separate perforated collision.
    UPROPERTY(EditAnywhere, BlueprintReadWrite) bool PawnBarrier=false;
    // Collision follows the imported shell, including doors, rooms and roofs.
    UPROPERTY(EditAnywhere, BlueprintReadWrite) bool MeshCollision=false;
    // Hidden fixed box: an envelope backstop behind thin kit sheets.
    UPROPERTY(EditAnywhere, BlueprintReadWrite) bool CollisionOnly=false;
    // Fixed roof art, hidden from the overhead minimap capture.
    UPROPERTY(EditAnywhere, BlueprintReadWrite) bool Roof=false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TObjectPtr<class UMaterialInterface> FixedSurface;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TObjectPtr<class UMaterialInterface> CellSurface;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TObjectPtr<class UMaterialInterface> MirroredCellSurface;
    // Fixed industrial art is fitted to Size; the authored box owns collision.
    // Destructible geometry always uses the project's block mesh/catalog.
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TObjectPtr<class UStaticMesh> FixedMesh;
};

UCLASS(BlueprintType)
class FDM_API UERMapTemplate : public UDataAsset
{
    GENERATED_BODY()
public:
    UFUNCTION(BlueprintCallable, Category="Template") bool ValidateLayout(int32 Seed=42) const;
    UFUNCTION(BlueprintCallable, Category="Template|Maintenance") static bool PrepareAlleyHouseCollision(class UStaticMesh* Mesh);
    // Warehouse sheets collide exactly as drawn (two-sided, complex as simple).
    UFUNCTION(BlueprintCallable, Category="Template|Maintenance") static bool PrepareWarehouseCollision(class UStaticMesh* Mesh);
    // PCG/PCGEx authoring receives the same validated recipe as runtime collision.
    UFUNCTION(BlueprintCallable, Category="Template|PCG") class UPCGDataAsset* CreatePCGPreview(int32 Seed=42) const;
    UFUNCTION(BlueprintCallable, Category="Template|PCG") class UPCGDataAsset* SavePCGPreview(const FString& PackageName,int32 Seed=42) const;
    UFUNCTION(BlueprintCallable, Category="Template|Maintenance") static bool DeleteArchivedProceduralLab();
    UPROPERTY(EditAnywhere, BlueprintReadWrite) EERMapArchetype Archetype=EERMapArchetype::LegacyAssembly;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FERCentralBuildingRecipe CentralBuilding;
    // Only set on an ephemeral per-build copy, never saved into an authored asset.
    UPROPERTY(Transient) int32 SizingTeamSize=0;
    UPROPERTY(Transient) int32 CompositionSeed=42;
    UPROPERTY(Transient) bool SizingRoomsGroundOnly=false;
    UPROPERTY(Transient) bool ReflectionBuild=false;
    UPROPERTY(Transient) FVector2D SizingArena=FVector2D::ZeroVector;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FERFireTrainingRecipe FireTraining;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TObjectPtr<class UERMapThemeLibrary> ThemeLibrary;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FERRoomsRecipe Rooms;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FERVillageRecipe Village;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FString TemplateId;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FString SourceReference;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 Revision=1;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) EERTemplateSymmetry Symmetry=EERTemplateSymmetry::None;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TObjectPtr<class UStaticMesh> ShellWallMesh;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TObjectPtr<class UStaticMesh> RoofMesh;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FVector ArenaSize=FVector(4100,7800,1050);
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FERMapTemplateVariation Variation;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FERDustTownVariation Town;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TArray<FERMapTemplatePiece> Pieces;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TArray<FTransform> AlphaSpawns;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TArray<FTransform> OmegaSpawns;
    // Pawn-centre route waypoints; validation checks spawn-to-route access.
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TArray<FVector> PatrolRoute;
};
