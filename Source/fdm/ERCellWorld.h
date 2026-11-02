#pragma once
#include "ERDestructibleMesh.h"
#include "CoreMinimal.h"
#include "ERBlockWorld.h"
#include "ERCellWorld.generated.h"

// 5cm logical cells. Thicknesses below are provisional construction settings,
// not agreed physical values; the catalog's FERCellCalibration must match them.
USTRUCT(BlueprintType)
struct FERCellSettings
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="1")) float CellSize = 5;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin=".5")) float WoodDepth = 2;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin=".1")) float DrywallBoard = 1.25f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin=".05")) float SheetThickness = .2f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin=".1")) float GlassThickness = 2;
    // Thin sheets keep their logical thickness; only the collision box grows.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin=".1")) float MinCollisionThickness = 1;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="4", ClampMax="64")) int32 ChunkCells = 32;
};

// One construction layer of one material: an oriented grid of cells. Origin at
// the minimum corner; X along the wall, Y through its depth, Z up.
struct FERCellPanelDesc
{
    int32 Id = 0; // stable: wall or piece id * 8 + construction layer
    EERBlockMaterial Material = EERBlockMaterial::Drywall;
    FTransform Transform; // room-local, rotation and translation only
    FVector Size = FVector::ZeroVector;
    FVector Cell = FVector::ZeroVector;
    // Reinforcement of this construction; base cell calibration stays unchanged.
    float Strength = 1;
    // Owned by the template palette; affects rendering/identity, never cell physics.
    class UMaterialInterface* Surface = nullptr;
    class UMaterialInterface* InteriorSurface = nullptr;
    bool bPane = false, bIndestructible = false;
    // Openings (doors, windows) as panel-local X/Z rectangles: cells whose centre
    // lies inside are absent from generation, never counted as destroyed.
    TArray<FBox2D> Holes;
    // Support: the bottom row rests on the ground or a fixed slab; or every cell is fixed.
    bool bAnchorBottom = false, bAnchored = false;
    float AnchorHeight = 0; // With bAnchorBottom, rows below this also hold: a skin's trim may sit clear of its bottom row.
    // A hollow imported shell. Original triangles drive visual AND collision
    // geometry; the ordinary calibrated cells carry damage, support and masks.
    TArray<FERMeshCorner> Skin;
    uint32 SkinCRC=0;
    FName SkinName;
    bool bSolidSkin=false; // authored masonry box, never an imported house
};
struct FERCellPanel : FERCellPanelDesc
{
    FIntVector Dims = FIntVector::ZeroValue;
    int32 FirstChunk = 0, ChunksX = 0, ChunksZ = 0;
    FBox Bounds = FBox(ForceInit); // room-local
};
// Stable within an epoch: panel index in the deterministic layout plus grid coordinates.
struct FERCellId
{
    int32 Panel = INDEX_NONE, X = 0, Y = 0, Z = 0;
    bool operator==(const FERCellId& O) const { return Panel==O.Panel && X==O.X && Y==O.Y && Z==O.Z; }
    bool operator<(const FERCellId& O) const { return Panel!=O.Panel ? Panel<O.Panel : Z!=O.Z ? Z<O.Z : Y!=O.Y ? Y<O.Y : X<O.X; }
    friend uint32 GetTypeHash(const FERCellId& Id) { return HashCombineFast(HashCombineFast(::GetTypeHash(Id.Panel),::GetTypeHash(Id.X)),HashCombineFast(::GetTypeHash(Id.Y),::GetTypeHash(Id.Z))); }
};
struct FERDamagedCell { uint32 Index = 0; float Health = 0; };
// Dense alive bits plus sparse HP of damaged survivors. Intact cells have no entry.
struct FERCellChunk
{
    int32 Panel = 0, X0 = 0, Z0 = 0, SX = 0, SZ = 0, AliveCount = 0;
    uint32 Revision = 0;
    TArray<uint64> Alive;
    TArray<uint64> InitialAlive; // only skins: distinguish source voids from cuts
    TArray<FERDamagedCell> Damaged;
    FBox Bounds = FBox(ForceInit); // room-local
};
struct FERCellBatchResult
{
    int32 Damaged = 0, Destroyed = 0;
    TArray<int32> DirtyChunks;   // geometry to rebuild, including face neighbours
    TArray<int32> ChangedChunks; // alive masks that changed
    FERCellId Last;
};

class FDM_API FERCellWorld
{
public:
    // Layers of one construction inside a wall slot (origin at the slot's minimum corner).
    static void AddConstruction(int32 WallId, EERBlockMaterial Material, const FTransform& Slot, const FVector& SlotSize,
        const FERCellSettings& Settings, TArray<FERCellPanelDesc>& Out);
    // Cell edge that divides Length into round(Length / CellSize) equal cells (at least one).
    static double FitCell(double Length, double CellSize) { return Length / FMath::Max(1, FMath::RoundToInt(Length / CellSize)); }
    // Validates everything before replacing live state.
    bool Build(const TArray<FERCellPanelDesc>& Desc, const FERCellSettings& Settings, const UERBlockMaterialCatalog* Catalog, FString& Error);
    void Reset();
    const TArray<FERCellPanel>& GetPanels() const { return Panels; }
    const TArray<FERCellChunk>& GetChunks() const { return Chunks; }
    int64 GetCellCount() const { return CellCount; }
    int64 GetRemovedCount() const { return RemovedCount; }
    int64 GetDetachedCount() const { return DetachedCount; }
    int64 GetVoidCount() const { return VoidCount; }
    // Fixed support: indestructible/anchored panels, anchored bottom rows.
    bool IsAnchored(const FERCellId& Id) const;
    bool IsAlive(int32 Panel, int32 X, int32 Y, int32 Z) const;
    bool IsAlive(const FERCellId& Id) const { return IsAlive(Id.Panel, Id.X, Id.Y, Id.Z); }
    bool IsValid(const FERCellId& Id) const;
    bool WasInitiallyAlive(const FERCellId& Id) const;
    float GetHealth(const FERCellId& Id) const;
    float GetMaxHealth(int32 Panel) const { return Panels.IsValidIndex(Panel) ? MaxHealth[uint8(Panels[Panel].Material)]*Panels[Panel].Strength : 0; }
    int32 GetChunkIndex(int32 Panel, int32 X, int32 Z) const;
    FVector GetCellCenter(const FERCellId& Id) const;
    FBox GetCellBox(const FERCellId& Id) const; // panel-local
    // Order-independent authority damage; values >= FLT_MAX destroy the cell.
    FERCellBatchResult ApplyDamage(const TMap<FERCellId, float>& Damage);
    // Structural collapse: removes alive cells regardless of HP, counted as detached.
    FERCellBatchResult DetachCells(const TArray<FERCellId>& Ids);
    // Grid cells crossed by the room-local segment A-B inside one panel, in order.
    // T0/T1 are segment parameters in [0,1]; return false from Visit to stop.
    void Traverse(int32 Panel, const FVector& A, const FVector& B, TFunctionRef<bool(const FIntVector&, double, double)> Visit) const;
    // Cell under a surface point (room-local), preferring the nearest alive cell.
    bool LocateCell(int32 Panel, const FVector& Point, const FVector& Normal, FERCellId& Out) const;
    // Network: run-length alive mask; complex skins optionally use a bounded zlib
    // frame around that same RLE stream. Decoding validates sizes before use.
    void EncodeChunk(int32 Chunk, TArray<uint8>& Out) const;
    bool DecodeChunk(int32 Chunk, const TArray<uint8>& Data, TArray<uint64>& OutAlive) const;
    // Client state: alive bits only, HP stay on the server.
    void SetChunkAlive(int32 Chunk, const TArray<uint64>& Alive, TArray<int32>& Dirty);
    uint32 GetLayoutDigest() const { return LayoutDigest; }
    uint32 GetAliveDigest() const;
    SIZE_T GetAllocatedBytes() const;
    static int32 CellIndex(const FERCellChunk& C, int32 X, int32 Y, int32 Z) { return X + C.SX * (Z + C.SZ * Y); }
private:
    TArray<FERCellPanel> Panels;
    TArray<FERCellChunk> Chunks;
    float MaxHealth[7] = {};
    int32 ChunkCells = 32;
    int64 CellCount = 0, RemovedCount = 0, DetachedCount = 0, VoidCount = 0;
    uint32 LayoutDigest = 0;
    void MarkDirty(int32 Panel, int32 X, int32 Z, TSet<int32>& Dirty) const;
};
