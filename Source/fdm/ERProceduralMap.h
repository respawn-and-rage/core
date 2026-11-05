#pragma once
#include "CoreMinimal.h"
#include "ERBlockRoom.h"
#include "ERMapTemplate.h"
#include "ERProceduralMap.generated.h"

struct FERLayoutRect { FVector2D Min, Max; int32 Room = INDEX_NONE; float Z=0; int32 Level=0; };
struct FERLayoutWall { FVector Start; FVector Direction; float Length = 0; EERBlockMaterial Material; };
struct FERLayoutOpening { int32 Wall = INDEX_NONE; float Along = 0, Width = 0, Sill = 0, Height = 0; bool bWindow = false; };
struct FERLayoutFixedPiece { FTransform Transform; EERBlockMaterial Material; class UStaticMesh* Mesh=nullptr; FRotator MeshRotation=FRotator::ZeroRotator; bool Roof=false; bool VisualOnly=false; class UMaterialInterface* Surface=nullptr; bool PawnBarrier=false; bool MeshCollision=false; bool CollisionOnly=false; bool ReflectedGeometry=false; };
struct FERLayoutHouse
{
    FTransform Frame; class UStaticMesh* Mesh=nullptr; bool RoofAccess=false;
    FName StructureName;
    TArray<TArray<FVector>> Entrances;
    TArray<int32> EntranceFaces;
    TArray<FVector> RoofMouths;
    float RimHeight=0;
};
struct FERLayoutTownGate { FTransform Frame; float Gap=110,Width=300,Angle=25; TArray<FVector> Passage; };
struct FERLayoutLadder { FTransform Frame; float Height=430,DeckHeight=320; };
struct FERAlleyRamp { FBox Bounds; FVector Entry,Exit; };
// Convex interior polygons and graph edges, in map-local centimetres.
struct FERRoomsNode
{
    FVector Center;
    FVector2D Size;
    TArray<FVector2D> Polygon;
    float Height=340;
    bool Base=false,Junction=false,Upper=false;
};
struct FERRoomsLink { int32 A=0,B=0; FVector Start,End; float Width=360,Height=340,Cut=55; };
struct FERRoomsScreen { FTransform Frame; FVector Size; int32 Link=INDEX_NONE,Room=INDEX_NONE; };
// Solid prism behind a visible quad. Front normal points into the room.
struct FERRoomsFace { FVector A,B,C,D; class UMaterialInterface* Surface=nullptr; bool Roof=false; bool CorridorFloor=false; };
struct FERLayoutCover
{
    FTransform Frame;
    FVector Size,Hide,Entry,SightFrom,SightTo;
    EERBlockMaterial Material=EERBlockMaterial::Brick;
    float HalfHeight=92.5f;
    int32 FiringStair=INDEX_NONE;
    FVector FiringPosition=FVector::ZeroVector;
};
struct FERLayoutContainer { FTransform Frame; FVector Size; };
struct FERLayoutSecondary { FTransform Frame; FVector Size; int32 Floors=1; EERBlockMaterial Material=EERBlockMaterial::Brick; bool Tower=false; bool LadderAccess=false; };
struct FERTemplateBase
{
    FTransform Frame;
    FVector Size;
    float StairX=0,StairStartY=0,RoofZ=0;
    int32 StepCount=0;
    FVector Point(FVector Local) const { return Frame.TransformPosition(Local); }
};
struct FERVillageBlock
{
    FTransform Frame;
    FVector2D Size;
    int32 Storeys=1;
    bool SelfSymmetric=false;
};
// Contiguous ranges refer to the existing per-team start arrays.
struct FERLayoutSpawnZone
{
    int32 Team=0,FirstStart=0,StartCount=0;
    FVector Center=FVector::ZeroVector;
    TArray<FVector> ExitRoute;
};
struct FERProceduralLayout
{
    TArray<FERLayoutSpawnZone> SpawnZones;
    TArray<FERLayoutRect> Rooms, Corridors;
    TArray<FERLayoutWall> Walls;
    TArray<FERLayoutOpening> Openings;
    TArray<TArray<FVector>> Routes; // Room centre -> branch -> common hall centre.
    // Destructible 5cm-cell panels (wall constructions, openings as holes).
    TArray<FERCellPanelDesc> Panels;
    TArray<FTransform> Steps,StairSupports;
    TArray<TArray<FVector>> StairRoutes;
    TArray<FERLayoutFixedPiece> FixedPieces;
    TArray<FTransform> AlphaSpawns, OmegaSpawns;
    FVector ArenaSize=FVector::ZeroVector;
    class UMaterialInterface* GroundSurface=nullptr;
    TArray<FERTemplateBase> Bases;
    TArray<EERTemplateModule> ModuleKinds;
    TArray<TArray<FVector>> RoomAccessRoutes;
    TArray<TArray<FVector>> SpawnExitRoutes; // Optional screened-base doglegs, alpha then omega.
    TArray<FBox> TownBuildings,TownCourts;
    // Initial inaccessible footprints, including closed lots and perimeter
    // recesses. Their clipped XY union is excluded from primary playable area.
    TArray<FBox> InaccessibleGround;
    TArray<TArray<FVector>> InaccessibleGroundPolygons,TownFootprints;
    TArray<FName> TownStreetNames;
    TArray<FERLayoutTownGate> TownGates;
    TArray<FVector> UpperRoute,LowerRoute;
    TArray<FERLayoutHouse> Houses;
    TArray<TArray<FVector>> RooftopRoutes,GroundRoutes;
    TArray<TArray<FVector>> RoofTransfers;
    TArray<FBox> TerrainPads;
    TArray<FBox> PoolWater;
    bool GradedTerrain=false;
    EERTemplateSymmetry LayoutSymmetry=EERTemplateSymmetry::None;
    bool CircularArena=false;
    bool CentralHill=false;
    FVector HillCenter=FVector::ZeroVector;
    float HillHeight=0,HillPlateau=0,HillFalloff=0;
    bool SeededDistrict=false;
    int32 DistrictAccesses=0,DistrictBends=0;
    int32 DistrictWallCount=0;
    float DistrictWallOffset=0,DistrictOpenHalfDepth=0;
    int32 UnderpassAxis=0,CentralLowCovers=0; // 1 base-to-base; 2 west-to-east.
    TArray<FERAlleyRamp> UnderpassRamps;
    TArray<FBox> UnderpassBreaches,LowCovers;
    TArray<FVector> UnderpassRoute,UnderpassApproach;
    TArray<FTransform> DistrictTowers;
    TArray<float> DistrictTowerHeights;
    TArray<TArray<FVector>> DistrictRoutes;
    FVector TownCrossing=FVector::ZeroVector;
    EERCrossingRule CrossingRule=EERCrossingRule::OpenYard;
    int32 BuildingFloors=0;
    int32 CentralShape=0;
    TArray<FBox> CentralFootprints;
    TArray<FTransform> CentralEntrances;
    FVector CentralBuildingSize=FVector::ZeroVector;
    FVector CentralBuildingCenter=FVector::ZeroVector;
    bool TerracedBuilding=false;
    TArray<TArray<FVector>> CrossingRoutes;
    TArray<FVector> ClimbPoints;
    TArray<FERLayoutContainer> Containers;
    TArray<FERLayoutCover> CoverSites;
    TArray<FERLayoutLadder> Ladders;
    TArray<FERLayoutSecondary> SecondaryStructures;
    TArray<int32> WallAccessFlights;
    TArray<float> CoverAngles;
    TArray<FVector> WallBridges;
    // Fire Training warehouse: narrow connected walkways, protected base courts.
    int32 WarehouseBays=0,WarehouseBands=0,WarehouseNaves=0,LayoutAttempt=0;
    float WarehouseWidth=0,WarehouseLength=0,WarehouseNaveWidth=0,IndoorCeiling=0;
    int32 WarehouseCatwalkVariant=0; // Procedural footprint and flat roof underside.
    float ElevatedRatio=0,OverheadRatio=0,OpenRegionRatio=0,SpawnCoverRatio=0;
    TArray<FBox> ElevatedDecks;      // Walkable surfaces above the ground (both halves).
    TArray<FBox> OverheadClosures;   // Every horizontal closure above the ground.
    TArray<FBox> Plazas;             // Reserved empty squares (both halves).
    TArray<int32> ElevatedAccesses;  // Independent ladders/stairs per elevated deck.
    TArray<FName> FeatureNames;
    // Village ground floor: paired enclosed bay outlines and the only initial
    // standing routes through every central block. Upper huts stay separate.
    TArray<TArray<FVector2D>> VillageClosedGround;
    TArray<TArray<FVector>> VillagePassages;
    TArray<FERVillageBlock> VillageBlocks;
    TArray<FBox> VillageSideWalls;
    TArray<TArray<FVector>> VillageSideBypasses;
    TArray<FBox> VillageFloorHoles;
    TArray<FERLayoutLadder> VillageUpperLadders;
    TArray<int32> VillageAccessFlights;
    int32 VillageBaseVariant=0;
    TArray<FVector2D> VillageTerrainProfile; // Y/elevation knots; constant across X.
    TArray<FBox> WarehouseBaseCourts,WarehouseCatwalks;
    int32 RoomsTheme=0;
    int32 RoomsMacroVariant=INDEX_NONE; // Baked plan in use: lanes*1000+sample, or legacy.
    int32 ThemeIndex=INDEX_NONE,ThemeRevision=0;
    FName ThemeName;
    TArray<class UMaterialInterface*> ThemeMaterials;
    class UMaterialInterface* ThemeCeiling=nullptr;
    class UMaterialInterface* ThemeMetalDetails=nullptr;
    TArray<FERRoomsNode> RoomNodes;
    TArray<FERRoomsLink> RoomLinks;
    TArray<FERRoomsScreen> RoomScreens;
    TArray<FERRoomsFace> RoomShell;
    TArray<FVector> RoomLightPoints;
    FVector Spawn = FVector::ZeroVector;
};
namespace ERProcedural
{
    bool SpawnZonesClearInWorld(class UWorld* World,const FTransform& Frame,const FERProceduralLayout& Layout);
    bool BuildSpawnZones(int32 Seed,FERProceduralLayout& Layout,const FERCellSettings& Cells);
    bool ConfigureTeamSize(class UERMapTemplate& Template,int32 ExpectedTeamSize,int32 Seed=42);
    inline float TargetAreaM2(int32 ExpectedTeamSize) { return ExpectedTeamSize*1500.f; }
    inline bool ValidTeamArea(FVector Size,int32 ExpectedTeamSize)
    { const double Area=Size.X*Size.Y/10000.,Target=TargetAreaM2(ExpectedTeamSize);return FMath::IsFinite(Area) && Area>=Target*.9-.01 && Area<=Target*1.1+.01; }
    bool FoldLayoutToMirror(FERProceduralLayout& Layout,const FERCellSettings& Cells);
    double PlayableAreaM2(const FERProceduralLayout& Layout);
    bool BuildCircularCentralBuilding(const UERMapTemplate& Template,int32 Seed,TArray<FERMapTemplatePiece>& Pieces,FERProceduralLayout& Layout,TArray<FVector>& Patrol,int32 CrossingOverride=0);
    float CentralGroundHeight(const FERProceduralLayout& Layout,FVector2D Position);
    void BuildCentralTerrain(class UDynamicMeshComponent* Component,const FERProceduralLayout& Layout);
    bool ApplyMapTheme(const FERProceduralConfig& Config,FERProceduralLayout& Layout);
    bool Build(const FERProceduralConfig& Config, const FERBlockRoomConfig& Blocks, FERProceduralLayout& Out, const FERCellSettings& Cells = FERCellSettings());
    // Axis-aligned destructible box as cell panels: massive slabs, boards in thin
    // slots, 2cm shells (four sides and a top) for volumetric thin-material pieces.
    void AddPieceCells(int32 Id, EERBlockMaterial Material, const FBox& Box, bool bAnchoredBase, const FERCellSettings& Cells, TArray<FERCellPanelDesc>& Out);
    bool BuildTemplateVariants(const class UERMapTemplate& Template,int32 Seed,TArray<struct FERMapTemplatePiece>& Pieces,FERProceduralLayout& Layout,TArray<FVector>& Patrol);
    bool BuildDustTown(const class UERMapTemplate& Template,int32 Seed,TArray<struct FERMapTemplatePiece>& Pieces,FERProceduralLayout& Layout,TArray<FVector>& Patrol);
    bool BuildAlleyDistrict(const class UERMapTemplate& Template,int32 Seed,TArray<struct FERMapTemplatePiece>& Pieces,FERProceduralLayout& Layout,TArray<FVector>& Patrol);
    void AddAlleyHouseModule(const UERMapTemplate& Template,const FTransform& Frame,FVector Size,bool RoofAccess,const TArray<int32>& StreetFaces,FERProceduralLayout& Layout);
    void FinishAlleyHouseModules(const UERMapTemplate& Template,const TArray<FERMapTemplatePiece>& Pieces,FERProceduralLayout& Layout);
    bool PlanAlleyUnderpass(const UERMapTemplate& Template,int32 Seed,FVector Base,TArray<FERMapTemplatePiece>& Pieces,FERProceduralLayout& Layout);
    bool BuildAlleyLowCover(const UERMapTemplate& Template,int32 Seed,TArray<FERMapTemplatePiece>& Pieces,FERProceduralLayout& Layout);
    float AlleyRampHeight(const FERAlleyRamp& Ramp,FVector2D Position);
    float VillageGroundHeight(const FERProceduralLayout& Layout,FVector2D Position);
    void PlanVillageTerrain(FERProceduralLayout& Layout,int32 Seed,float Envelope,const TArray<float>& WallYs);
    void ApplyVillageTerrain(FERProceduralLayout& Layout);
    void BuildVillageTerrain(class UDynamicMeshComponent* Component,const FERProceduralLayout& Layout);
    float AlleyGroundHeight(const FERProceduralLayout& Layout,FVector2D Position);
    float AlleyBaseHeight(FVector Arena,FVector2D Position,bool Balanced=false);
    void BuildAlleyTerrain(class UDynamicMeshComponent* Component,const FERProceduralLayout& Layout);
    bool FindAlleyInteriorRoute(class UWorld* World,const FERProceduralLayout& Layout,TArray<FVector>& Route);
    bool FindAlleyRoofRimRoute(class UWorld* World,const FERProceduralLayout& Layout,TArray<FVector>& Route);
    bool BuildCentralBuilding(const class UERMapTemplate& Template,int32 Seed,TArray<struct FERMapTemplatePiece>& Pieces,FERProceduralLayout& Layout,TArray<FVector>& Patrol,int32 CrossingOverride=0);
    bool BuildRooms(const FERProceduralConfig& Config,const FERBlockRoomConfig& Blocks,FERProceduralLayout& Out,const FERCellSettings& Cells);
    bool BuildVillage(const FERProceduralConfig& Config,const FERBlockRoomConfig& Blocks,FERProceduralLayout& Out,const FERCellSettings& Cells);
    bool BuildUrbanTown(const FERProceduralConfig& Config,const FERBlockRoomConfig& Blocks,FERProceduralLayout& Out,const FERCellSettings& Cells);
    float RoomsFloorHeight(const FERProceduralLayout& Layout,FVector2D Position);
    void BuildRoomsShell(class AERProceduralMap* Map,const FERProceduralLayout& Layout,TArray<TObjectPtr<class UDynamicMeshComponent>>& Components);
    // Attempts 0..FireTrainingAttempts-1 sample the seed; the last is the verified reference layout.
    constexpr int32 FireTrainingAttempts=8;
    bool BuildFireTraining(const class UERMapTemplate& Template,int32 Seed,int32 Attempt,TArray<struct FERMapTemplatePiece>& Pieces,FERProceduralLayout& Layout,TArray<FVector>& Patrol);

}

// Reuses the lab's 5cm cells, hitscan, explosion, support and replication.
UCLASS(Blueprintable)
class FDM_API AERProceduralMap : public AERBlockRoom
{
    GENERATED_BODY()
public:
    AERProceduralMap();
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Procedural") FERProceduralConfig LayoutConfig;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Procedural") TObjectPtr<class UMaterialInterface> WoodFloorSurface;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Procedural") TObjectPtr<class UMaterialInterface> CeilingSurface;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Procedural") TObjectPtr<class UMaterialInterface> ExteriorFloorSurface;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Procedural") bool bCeilings = true;
    // Authority only, off by default. Clients follow LayoutConfig.MapCandidate.
    bool bSelectMapCandidates=false;
    void ResolveMapCandidate();
    uint32 GetTemplateGeometryDigest() const;
    const FERProceduralLayout& GetLayout() const { return Layout; }
    virtual FVector GetSpawnLocation() const override { return GetActorTransform().TransformPosition(Layout.Spawn); }
protected:
    virtual bool GenerateLogicalWorld() override;
    virtual void ConfigureFloors() override;
    virtual bool ValidateGeneratedCollision() const override;
    virtual void OnGenerationFinished() override;
    virtual FString GetLayoutStatistics() const override;
    virtual SIZE_T GetLayoutBytes() const override;
private:
    FERProceduralLayout Layout;
    // Opt-in diagnostics: the layout stage needs Panels, which the map then drops.
    TSharedPtr<struct FERPlayabilityReport> PlayabilityDraft;
    TOptional<FIntPoint> SelectedFor; // Seed and team size: the early start preparation shares one selection.
    UPROPERTY() TObjectPtr<class UInstancedStaticMeshComponent> TileFloors;
    UPROPERTY() TObjectPtr<class UInstancedStaticMeshComponent> WoodFloors;
    UPROPERTY() TObjectPtr<class UInstancedStaticMeshComponent> Ceilings;
    UPROPERTY() TObjectPtr<class UInstancedStaticMeshComponent> Stairs;
    UPROPERTY() TArray<TObjectPtr<class UInstancedStaticMeshComponent>> FixedMeshes;
    UPROPERTY() TObjectPtr<class UMaterialInterface> PoolWaterSurface;
    UPROPERTY(Transient) TArray<TObjectPtr<class UStaticMeshComponent>> PoolSurfaces;
    UPROPERTY(Transient) TArray<TObjectPtr<class AERLadder>> LadderActors;
    UPROPERTY() TObjectPtr<class UInstancedStaticMeshComponent> FixedCollision;
    UPROPERTY() TObjectPtr<class UInstancedStaticMeshComponent> FenceCollision;
    UPROPERTY() TArray<TObjectPtr<class UDynamicMeshComponent>> FenceMetal;
    UPROPERTY() TObjectPtr<class UDynamicMeshComponent> AlleyTerrain;
    UPROPERTY() TArray<TObjectPtr<class UDynamicMeshComponent>> RoomsShell;
    // Cosmetic warehouse lighting under the roof lamps; never on a dedicated server.
    UPROPERTY(Transient) TArray<TObjectPtr<class UPointLightComponent>> WarehouseLights;
    void ConfigureTemplateFloors();
};
