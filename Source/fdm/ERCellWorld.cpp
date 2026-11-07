#include "ERCellWorld.h"
#include "Materials/MaterialInterface.h"
#include "Algo/BinarySearch.h"
#include "ERMeshCells.h"
#include "Misc/Compression.h"

namespace
{
constexpr int64 MaxCells = 8000000;
constexpr int32 MaxChunks = 65536;
int32 FindDamaged(const FERCellChunk& C, uint32 Index)
{
    const int32 I = Algo::LowerBoundBy(C.Damaged, Index, [](const FERDamagedCell& D) { return D.Index; });
    return C.Damaged.IsValidIndex(I) && C.Damaged[I].Index == Index ? I : INDEX_NONE;
}
bool TestBit(const TArray<uint64>& Bits, int32 I) { return (Bits[I >> 6] >> (I & 63)) & 1; }
void ClearBit(FERCellAliveBits& Bits, int32 I) { Bits[I >> 6] &= ~(1ull << (I & 63)); }
}

void FERCellWorld::AddConstruction(int32 WallId, EERBlockMaterial Material, const FTransform& Slot, const FVector& S,
    const FERCellSettings& Set, TArray<FERCellPanelDesc>& Out)
{
    // Lengths that are not whole cells (template pieces) keep their exact volume:
    // round(L / CellSize) equal cells per axis, never a gap or a dropped strip.
    const FVector Cell(FitCell(S.X, Set.CellSize), 0, FitCell(S.Z, Set.CellSize));
    auto Add = [&](int32 Layer, float Y, float Depth, float CellDepth, bool bPane)
    {
        auto& P = Out.AddDefaulted_GetRef();
        P.Id = WallId * 8 + Layer; P.Material = Material; P.Transform = FTransform(FVector(0, Y, 0)) * Slot;
        P.Size = FVector(S.X, Depth, S.Z); P.Cell = bPane ? P.Size : FVector(Cell.X, CellDepth, Cell.Z); P.bPane = bPane;
    };
    const float T = S.Y;
    switch (Material)
    {
    case EERBlockMaterial::Drywall:
    {
        // Two boards around an empty cavity; never a slot full of plaster.
        const float B = FMath::Min(Set.DrywallBoard, T);
        if (T >= 2 * B + .01f) { Add(0, 0, B, B, false); Add(1, T - B, B, B, false); }
        else Add(0, (T - B) * .5f, B, B, false);
        break;
    }
    case EERBlockMaterial::Wood: { const float W = FMath::Min(Set.WoodDepth, T); Add(0, (T - W) * .5f, W, W, false); break; }
    case EERBlockMaterial::Metal: { const float W = FMath::Min(Set.SheetThickness, T); Add(0, (T - W) * .5f, W, W, false); break; }
    case EERBlockMaterial::Glass: { const float W = FMath::Min(Set.GlassThickness, T); Add(0, (T - W) * .5f, W, W, true); break; }
    default: Add(0, 0, T, FitCell(T, Set.CellSize), false);
    }
}

bool FERCellWorld::Build(const TArray<FERCellPanelDesc>& Desc, const FERCellSettings& S, const UERBlockMaterialCatalog* Catalog, FString& Error)
{
    if (!Catalog || !Catalog->IsValidCatalog()) { Error = TEXT("invalid material catalog"); return false; }
    const auto& K = Catalog->CellCalibration;
    auto Same = [](float A, float B) { return FMath::IsNearlyEqual(A, B, 1.e-3f); };
    if (!Same(K.CellSize, S.CellSize) || !Same(K.WoodDepth, S.WoodDepth) || !Same(K.DrywallBoard, S.DrywallBoard) || !Same(K.SheetThickness, S.SheetThickness))
    {
        Error = FString::Printf(TEXT("ER_BLOCK_CALIBRATION_MISMATCH HP calibrated for cell=%.2f wood=%.2f drywall=%.2f sheet=%.2f, settings cell=%.2f wood=%.2f drywall=%.2f sheet=%.2f; retune HP and update CellCalibration"),
            K.CellSize, K.WoodDepth, K.DrywallBoard, K.SheetThickness, S.CellSize, S.WoodDepth, S.DrywallBoard, S.SheetThickness);
        return false;
    }
    if (S.ChunkCells < 4 || S.ChunkCells > 64 || Desc.IsEmpty() || Desc.Num() > 4096 || !(S.MinCollisionThickness > 0)) { Error = TEXT("invalid cell settings or panel count"); return false; }
    TArray<FERCellPanel> NewPanels; TArray<FERCellChunk> NewChunks; TSet<int32> Ids;
    int64 Cells = 0, Voids = 0;
    const bool HasSkins=Desc.ContainsByPredicate([](const auto& P){return !P.Skin.IsEmpty();});
    uint32 H = HashCombineFast(7, ::GetTypeHash(S.ChunkCells));
    auto Mix = [&H](double V) { H = HashCombineFast(H, ::GetTypeHash(FMath::RoundToInt64(V * 1000))); };
    for (const auto& D : Desc)
    {
        if (Ids.Contains(D.Id) || !FMath::IsFinite(D.Strength) || D.Strength<1 || D.Strength>16 || D.Transform.ContainsNaN() || !D.Transform.GetScale3D().Equals(FVector::OneVector, 1.e-4) ||
            D.Transform.GetLocation().GetAbs().GetMax() > 1000000 || !Catalog->Find(D.Material)) { Error = FString::Printf(TEXT("invalid panel %d"), D.Id); return false; }
        Ids.Add(D.Id);
        FIntVector N;
        for (int32 A = 0; A < 3; ++A)
        {
            const double R = D.Cell[A] > 0 ? D.Size[A] / D.Cell[A] : 0;
            N[A] = FMath::RoundToInt(R);
            if (N[A] < 1 || N[A] > 100000 || FMath::Abs(R - N[A]) > 1.e-3) { Error = FString::Printf(TEXT("panel %d size is not a whole number of cells"), D.Id); return false; }
        }
        if ((D.bPane && N != FIntVector(1)) || N.Y > (D.Skin.IsEmpty() ? 256:1024)) { Error = FString::Printf(TEXT("invalid panel %d depth"), D.Id); return false; }
        Cells += int64(N.X) * N.Y * N.Z;
        if (Cells > (HasSkins ? 512000000:MaxCells)) { Error = TEXT("cell storage budget exceeded"); return false; }
        if(!D.Skin.IsEmpty() && (D.Skin.Num()%3 || !D.SkinCRC || D.Skin.Num()>30000)){Error=TEXT("invalid mesh skin");return false;}
        auto& P = NewPanels.AddDefaulted_GetRef();
        static_cast<FERCellPanelDesc&>(P) = D; P.Dims = N;P.bSkinned=!D.Skin.IsEmpty();
        P.Bounds = FBox(FVector::ZeroVector, D.Size).TransformBy(D.Transform);
        P.ChunksX = FMath::DivideAndRoundUp(N.X, S.ChunkCells); P.ChunksZ = FMath::DivideAndRoundUp(N.Z, S.ChunkCells);
        P.FirstChunk = NewChunks.Num();
        if (NewChunks.Num() + P.ChunksX * P.ChunksZ > MaxChunks) { Error = TEXT("chunk budget exceeded"); return false; }
        for (int32 CZ = 0; CZ < P.ChunksZ; ++CZ)
            for (int32 CX = 0; CX < P.ChunksX; ++CX)
            {
                auto& C = NewChunks.AddDefaulted_GetRef();
                C.Panel = NewPanels.Num() - 1; C.X0 = CX * S.ChunkCells; C.Z0 = CZ * S.ChunkCells;
                C.SX = FMath::Min(S.ChunkCells, N.X - C.X0); C.SZ = FMath::Min(S.ChunkCells, N.Z - C.Z0);
                const int32 Bits = C.SX * C.SZ * N.Y;
                C.Alive.Init(D.Skin.IsEmpty() || D.bSolidSkin ? ~0ull:0ull, FMath::DivideAndRoundUp(Bits, 64));
                if ((D.Skin.IsEmpty() || D.bSolidSkin) && (Bits & 63)) C.Alive.Last() = (1ull << (Bits & 63)) - 1;
                C.AliveCount = D.Skin.IsEmpty() || D.bSolidSkin ? Bits:0;
                for (const FBox2D& Hole : D.Holes)
                {
                    // Cells whose centre lies inside the opening never exist.
                    const int32 HX0 = FMath::Max(C.X0, FMath::CeilToInt(Hole.Min.X / D.Cell.X - .5)), HX1 = FMath::Min(C.X0 + C.SX - 1, FMath::FloorToInt(Hole.Max.X / D.Cell.X - .5));
                    const int32 HZ0 = FMath::Max(C.Z0, FMath::CeilToInt(Hole.Min.Y / D.Cell.Z - .5)), HZ1 = FMath::Min(C.Z0 + C.SZ - 1, FMath::FloorToInt(Hole.Max.Y / D.Cell.Z - .5));
                    for (int32 Z = HZ0; Z <= HZ1; ++Z) for (int32 Y = 0; Y < N.Y; ++Y) for (int32 X = HX0; X <= HX1; ++X)
                    {
                        const int32 I = CellIndex(C, X - C.X0, Y, Z - C.Z0);
                        if (TestBit(C.Alive, I)) { ClearBit(C.Alive, I); --C.AliveCount; ++Voids; }
                    }
                }
                C.Bounds = FBox(FVector(C.X0 * D.Cell.X, 0, C.Z0 * D.Cell.Z), FVector((C.X0 + C.SX) * D.Cell.X, D.Size.Y, (C.Z0 + C.SZ) * D.Cell.Z)).TransformBy(D.Transform);
            }
        if(!D.Skin.IsEmpty())
        {
            if(!D.bSolidSkin)ERMeshCells::Rasterize(P,[&](FIntVector V)
            {
                auto& C=NewChunks[P.FirstChunk+(V.Z/S.ChunkCells)*P.ChunksX+V.X/S.ChunkCells];
                const int32 I=CellIndex(C,V.X-C.X0,V.Y,V.Z-C.Z0);
                if(!TestBit(C.Alive,I)){C.Alive[I>>6]|=1ull<<(I&63);++C.AliveCount;}
            });
            for(int32 CI=P.FirstChunk;CI<NewChunks.Num();++CI)
            {auto& C=NewChunks[CI];C.InitialAlive=C.Alive;Voids+=int64(C.SX)*C.SZ*N.Y-C.AliveCount;}
            H=HashCombineFast(H,D.SkinCRC);H=HashCombineFast(H,uint32(D.bSolidSkin));H=HashCombineFast(H,FCrc::StrCrc32(*D.SkinName.ToString()));
        }
        if(Cells-Voids>(HasSkins ? 96000000:MaxCells)){Error=TEXT("occupied cell budget exceeded");return false;}
        H = HashCombineFast(H, ::GetTypeHash(D.Id)); H = HashCombineFast(H, ::GetTypeHash(uint8(D.Material)));Mix(D.Strength);
        if(D.Surface)H=HashCombineFast(H,FCrc::StrCrc32(*D.Surface->GetPathName()));
        if(D.InteriorSurface)H=HashCombineFast(H,FCrc::StrCrc32(*D.InteriorSurface->GetPathName()));
        H = HashCombineFast(H, ::GetTypeHash(uint32(D.bPane) | (uint32(D.bIndestructible) << 1) | (uint32(D.bAnchorBottom) << 2) | (uint32(D.bAnchored) << 3)));Mix(D.AnchorHeight);
        for (const FBox2D& Hole : D.Holes) { Mix(Hole.Min.X); Mix(Hole.Min.Y); Mix(Hole.Max.X); Mix(Hole.Max.Y); }
        for (const FVector& V : {D.Transform.GetLocation(), D.Transform.GetRotation().GetAxisX(), D.Transform.GetRotation().GetAxisZ(), D.Size, D.Cell}) { Mix(V.X); Mix(V.Y); Mix(V.Z); }
    }
    for (int32 I = 0; I < 7; ++I)
    {
        const auto* P = Catalog->Find(EERBlockMaterial(I));
        MaxHealth[I] = P->MaxHealth;
        for (float V : {P->MaxHealth, P->BulletDamageScale, P->EntryEnergyCost, P->EnergyCostPerCm, P->ExplosionDamageScale, P->ExplosionTransmission}) Mix(V);
    }
    for (float V : {K.CellSize, K.WoodDepth, K.DrywallBoard, K.SheetThickness, K.TransmissionReferenceCm, S.MinCollisionThickness}) Mix(V);
    Panels = MoveTemp(NewPanels); Chunks = MoveTemp(NewChunks); ChunkCells = S.ChunkCells; CellCount = Cells - Voids; VoidCount = Voids; RemovedCount = DetachedCount = 0; LayoutDigest = H;
    return true;
}

void FERCellWorld::Reset()
{
    Panels.Empty(); Chunks.Empty(); CellCount = RemovedCount = DetachedCount = VoidCount = 0; LayoutDigest = 0;
}

bool FERCellWorld::IsValid(const FERCellId& Id) const
{
    if (!Panels.IsValidIndex(Id.Panel)) return false;
    const FIntVector& N = Panels[Id.Panel].Dims;
    return Id.X >= 0 && Id.X < N.X && Id.Y >= 0 && Id.Y < N.Y && Id.Z >= 0 && Id.Z < N.Z;
}
FERCellWorld FERCellWorld::MakeSupportSnapshot() const
{
    FERCellWorld Snapshot;
    Snapshot.ChunkCells=ChunkCells;Snapshot.CellCount=CellCount;Snapshot.RemovedCount=RemovedCount;
    Snapshot.DetachedCount=DetachedCount;Snapshot.VoidCount=VoidCount;Snapshot.LayoutDigest=LayoutDigest;
    Snapshot.Panels.Reserve(Panels.Num());Snapshot.Chunks.Reserve(Chunks.Num());
    for(const auto& P:Panels)
    {
        auto& Copy=Snapshot.Panels.AddDefaulted_GetRef();
        Copy.Id=P.Id;Copy.Dims=P.Dims;Copy.Cell=P.Cell;Copy.Size=P.Size;Copy.Transform=P.Transform;Copy.Bounds=P.Bounds;
        Copy.FirstChunk=P.FirstChunk;Copy.ChunksX=P.ChunksX;Copy.ChunksZ=P.ChunksZ;Copy.bSkinned=P.bSkinned;
        Copy.bAnchored=P.bAnchored;Copy.bIndestructible=P.bIndestructible;Copy.bAnchorBottom=P.bAnchorBottom;Copy.AnchorHeight=P.AnchorHeight;
    }
    for(const auto& C:Chunks)
    {
        auto& Copy=Snapshot.Chunks.AddDefaulted_GetRef();
        Copy.Panel=C.Panel;Copy.X0=C.X0;Copy.Z0=C.Z0;Copy.SX=C.SX;Copy.SZ=C.SZ;Copy.Bounds=C.Bounds;
        Copy.AliveCount=C.AliveCount;Copy.Revision=C.Revision;Copy.Alive=C.Alive;
    }
    return Snapshot;
}
int32 FERCellWorld::GetChunkIndex(int32 Panel, int32 X, int32 Z) const
{
    const auto& P = Panels[Panel];
    return P.FirstChunk + (Z / ChunkCells) * P.ChunksX + X / ChunkCells;
}
bool FERCellWorld::WasInitiallyAlive(const FERCellId& Id) const
{
    if(!IsValid(Id))return false;
    const auto& C=Chunks[GetChunkIndex(Id.Panel,Id.X,Id.Z)];
    return C.InitialAlive.IsEmpty() ? true:TestBit(C.InitialAlive,CellIndex(C,Id.X-C.X0,Id.Y,Id.Z-C.Z0));
}
bool FERCellWorld::IsAlive(int32 Panel, int32 X, int32 Y, int32 Z) const
{
    if (!IsValid({Panel, X, Y, Z})) return false;
    const auto& C = Chunks[GetChunkIndex(Panel, X, Z)];
    return TestBit(C.Alive, CellIndex(C, X - C.X0, Y, Z - C.Z0));
}
float FERCellWorld::GetHealth(const FERCellId& Id) const
{
    if (!IsAlive(Id)) return 0;
    const auto& C = Chunks[GetChunkIndex(Id.Panel, Id.X, Id.Z)];
    const int32 D = FindDamaged(C, CellIndex(C, Id.X - C.X0, Id.Y, Id.Z - C.Z0));
    return D == INDEX_NONE ? GetMaxHealth(Id.Panel) : C.Damaged[D].Health;
}
FBox FERCellWorld::GetCellBox(const FERCellId& Id) const
{
    const FVector& S = Panels[Id.Panel].Cell;
    return FBox(FVector(Id.X, Id.Y, Id.Z) * S, FVector(Id.X + 1, Id.Y + 1, Id.Z + 1) * S);
}
FVector FERCellWorld::GetCellCenter(const FERCellId& Id) const
{
    return Panels[Id.Panel].Transform.TransformPosition(GetCellBox(Id).GetCenter());
}
bool FERCellWorld::IsAnchored(const FERCellId& Id) const
{
    if (!IsValid(Id)) return false;
    const auto& P = Panels[Id.Panel];
    return P.bAnchored || P.bIndestructible || (P.bAnchorBottom && (Id.Z == 0 || Id.Z * P.Cell.Z < P.AnchorHeight));
}
FERCellBatchResult FERCellWorld::DetachCells(const TArray<FERCellId>& Ids)
{
    FERCellBatchResult Result;
    TSet<int32> Dirty, Changed;
    for (const auto& Id : Ids)
    {
        if (!IsAlive(Id) || Panels[Id.Panel].bIndestructible) continue;
        const int32 ChunkIndex = GetChunkIndex(Id.Panel, Id.X, Id.Z);
        auto& C = Chunks[ChunkIndex];
        const int32 Index = CellIndex(C, Id.X - C.X0, Id.Y, Id.Z - C.Z0);
        ClearBit(C.Alive, Index); --C.AliveCount; ++C.Revision; ++RemovedCount; ++DetachedCount; ++Result.Destroyed; Result.Last = Id;
        if (const int32 D = FindDamaged(C, Index); D != INDEX_NONE) C.Damaged.RemoveAt(D);
        if (Panels[Id.Panel].bPane && Panels[Id.Panel].Material == EERBlockMaterial::Glass) Result.BrokenGlassPanes.Add(Id);
        Changed.Add(ChunkIndex); MarkDirty(Id.Panel, Id.X, Id.Z, Dirty);
    }
    Result.DirtyChunks = Dirty.Array(); Result.DirtyChunks.Sort();
    Result.ChangedChunks = Changed.Array(); Result.ChangedChunks.Sort();
    return Result;
}
void FERCellWorld::MarkDirty(int32 Panel, int32 X, int32 Z, TSet<int32>& Dirty) const
{
    const FIntVector& N = Panels[Panel].Dims;
    const int32 Own = GetChunkIndex(Panel, X, Z);
    Dirty.Add(Own);
    // A removed border cell exposes a face that belongs to the neighbouring chunk.
    for (const FIntPoint& D : {FIntPoint(-1, 0), FIntPoint(1, 0), FIntPoint(0, -1), FIntPoint(0, 1)})
    {
        const int32 NX = X + D.X, NZ = Z + D.Y;
        if (NX >= 0 && NX < N.X && NZ >= 0 && NZ < N.Z) Dirty.Add(GetChunkIndex(Panel, NX, NZ));
    }
}
FERCellBatchResult FERCellWorld::ApplyDamage(const TMap<FERCellId, float>& Damage)
{
    FERCellBatchResult Result;
    TArray<FERCellId> Order; Damage.GetKeys(Order); Order.Sort();
    TSet<int32> Dirty, Changed;
    for (const auto& Id : Order)
    {
        const float Amount = Damage[Id];
        if (!IsAlive(Id) || Panels[Id.Panel].bIndestructible || !(Amount > 0)) continue;
        const int32 ChunkIndex = GetChunkIndex(Id.Panel, Id.X, Id.Z);
        auto& C = Chunks[ChunkIndex];
        const int32 Index = CellIndex(C, Id.X - C.X0, Id.Y, Id.Z - C.Z0);
        const int32 D = FindDamaged(C, Index);
        const double Current = D == INDEX_NONE ? GetMaxHealth(Id.Panel) : C.Damaged[D].Health;
        // Fractional damage is kept; excess never propagates to neighbours.
        const double Next = Amount >= TNumericLimits<float>::Max() ? 0 : FMath::Max(0., Current - Amount);
        ++Result.Damaged; Result.Last = Id;
        if (Next <= 0)
        {
            ClearBit(C.Alive, Index); --C.AliveCount; ++C.Revision; ++RemovedCount; ++Result.Destroyed;
            if (D != INDEX_NONE) C.Damaged.RemoveAt(D);
            if (Panels[Id.Panel].bPane && Panels[Id.Panel].Material == EERBlockMaterial::Glass) Result.BrokenGlassPanes.Add(Id);
            Changed.Add(ChunkIndex); MarkDirty(Id.Panel, Id.X, Id.Z, Dirty);
        }
        else if (D != INDEX_NONE) C.Damaged[D].Health = float(Next);
        else C.Damaged.Insert({uint32(Index), float(Next)}, Algo::LowerBoundBy(C.Damaged, uint32(Index), [](const FERDamagedCell& E) { return E.Index; }));
    }
    Result.DirtyChunks = Dirty.Array(); Result.DirtyChunks.Sort();
    Result.ChangedChunks = Changed.Array(); Result.ChangedChunks.Sort();
    return Result;
}
void FERCellWorld::Traverse(int32 Panel, const FVector& A, const FVector& B, TFunctionRef<bool(const FIntVector&, double, double)> Visit) const
{
    if (!Panels.IsValidIndex(Panel)) return;
    const auto& P = Panels[Panel];
    const FVector Origin = P.Transform.InverseTransformPosition(A) / P.Cell;
    const FVector Delta = P.Transform.InverseTransformPosition(B) / P.Cell - Origin;
    double T = 0, TEnd = 1;
    for (int32 Axis = 0; Axis < 3; ++Axis)
    {
        if (FMath::Abs(Delta[Axis]) < 1.e-12)
        {
            if (Origin[Axis] < 0 || Origin[Axis] > P.Dims[Axis]) return;
            continue;
        }
        double T0 = -Origin[Axis] / Delta[Axis], T1 = (P.Dims[Axis] - Origin[Axis]) / Delta[Axis];
        if (T0 > T1) Swap(T0, T1);
        T = FMath::Max(T, T0); TEnd = FMath::Min(TEnd, T1);
        if (TEnd <= T) return;
    }
    // Amanatides-Woo stepping from the clipped entry.
    FIntVector Cell, Step;
    FVector Next, Inc;
    const FVector Entry = Origin + Delta * T;
    for (int32 Axis = 0; Axis < 3; ++Axis)
    {
        Cell[Axis] = FMath::Clamp(FMath::FloorToInt(Entry[Axis] + (Delta[Axis] < 0 ? -1.e-9 : 1.e-9)), 0, P.Dims[Axis] - 1);
        Step[Axis] = Delta[Axis] > 0 ? 1 : Delta[Axis] < 0 ? -1 : 0;
        Inc[Axis] = Step[Axis] ? FMath::Abs(1 / Delta[Axis]) : TNumericLimits<double>::Max();
        Next[Axis] = Step[Axis] ? ((Cell[Axis] + (Step[Axis] > 0 ? 1 : 0)) - Origin[Axis]) / Delta[Axis] : TNumericLimits<double>::Max();
    }
    for (int32 Guard = 0; Guard < 1000000; ++Guard)
    {
        const int32 Axis = Next.X <= Next.Y && Next.X <= Next.Z ? 0 : Next.Y <= Next.Z ? 1 : 2;
        const double T1 = FMath::Min(Next[Axis], TEnd);
        if (T1 - T > 1.e-9 && !Visit(Cell, T, T1)) return;
        if (T1 >= TEnd) return;
        T = T1; Cell[Axis] += Step[Axis]; Next[Axis] += Inc[Axis];
        if (Cell[Axis] < 0 || Cell[Axis] >= P.Dims[Axis]) return;
    }
}
bool FERCellWorld::LocateCell(int32 Panel, const FVector& Point, const FVector& Normal, FERCellId& Out) const
{
    if (!Panels.IsValidIndex(Panel) || Point.ContainsNaN()) return false;
    const auto& P = Panels[Panel];
    const FVector Local = P.Transform.InverseTransformPosition(Point - Normal.GetSafeNormal() * FMath::Min(.05, P.Cell.GetMin() * .25)) / P.Cell;
    FIntVector Cell;
    for (int32 A = 0; A < 3; ++A) Cell[A] = FMath::Clamp(FMath::FloorToInt(Local[A]), 0, P.Dims[A] - 1);
    if (IsAlive(Panel, Cell.X, Cell.Y, Cell.Z)) { Out = {Panel, Cell.X, Cell.Y, Cell.Z}; return true; }
    // Shared faces and inflated thin collision: take the nearest alive neighbour.
    double Best = TNumericLimits<double>::Max();
    for (int32 Z = Cell.Z - 1; Z <= Cell.Z + 1; ++Z)
        for (int32 Y = P.Skin.IsEmpty() ? 0:Cell.Y-1; Y < (P.Skin.IsEmpty() ? P.Dims.Y:Cell.Y+2); ++Y)
            for (int32 X = Cell.X - 1; X <= Cell.X + 1; ++X)
                if (IsAlive(Panel, X, Y, Z))
                {
                    const double D = FVector::DistSquared(Local, FVector(X + .5, Y + .5, Z + .5));
                    if (D < Best) { Best = D; Out = {Panel, X, Y, Z}; }
                }
    return Best < TNumericLimits<double>::Max();
}
void FERCellWorld::EncodeChunk(int32 Index, TArray<uint8>& Out) const
{
    Out.Reset();
    const auto& C = Chunks[Index];
    const int32 Bits = C.SX * C.SZ * Panels[C.Panel].Dims.Y;
    bool Value = Bits > 0 && TestBit(C.Alive, 0);
    Out.Add(Value);
    auto Put = [&Out](uint32 V) { do { uint8 B = V & 127; V >>= 7; Out.Add(B | (V ? 128 : 0)); } while (V); };
    int32 Run = 0;
    for (int32 I = 0; I < Bits; ++I)
    {
        if (TestBit(C.Alive, I) == Value) { ++Run; continue; }
        Put(Run); Value = !Value; Run = 1;
    }
    Put(Run);
    if(!Panels[C.Panel].Skin.IsEmpty() && Out.Num()>512)
    {
        const int32 Raw=Out.Num();int32 Size=FCompression::CompressMemoryBound(NAME_Zlib,Raw);
        TArray<uint8> Packed;Packed.SetNumUninitialized(Size+5);Packed[0]=2;
        for(int32 I=0;I<4;++I)Packed[1+I]=uint32(Raw)>>(8*I);
        if(FCompression::CompressMemory(NAME_Zlib,Packed.GetData()+5,Size,Out.GetData(),Raw) && Size+5<Raw)
        {Packed.SetNum(Size+5);Out=MoveTemp(Packed);}
    }
}
bool FERCellWorld::DecodeChunk(int32 Index, const TArray<uint8>& Data, TArray<uint64>& OutAlive) const
{
    if (!Chunks.IsValidIndex(Index) || Data.Num() < 2) return false;
    const auto& C = Chunks[Index];
    const int32 Bits = C.SX * C.SZ * Panels[C.Panel].Dims.Y;
    if(Data[0]==2)
    {
        if(Data.Num()<6)return false;
        uint32 Raw=0;for(int32 I=0;I<4;++I)Raw|=uint32(Data[1+I])<<(8*I);
        // A run occupies at most one byte per bit, plus the starting value.
        // Validate before allocation/decompression; nested frames are forbidden.
        if(Raw<2 || Raw>uint32(Bits)+1)return false;
        TArray<uint8> RLE;RLE.SetNumUninitialized(Raw);
        if(!FCompression::UncompressMemory(NAME_Zlib,RLE.GetData(),Raw,Data.GetData()+5,Data.Num()-5) || RLE[0]>1)return false;
        return DecodeChunk(Index,RLE,OutAlive);
    }
    if(Data[0]>1)return false;
    OutAlive.Init(0, C.Alive.Num());
    bool Value = Data[0] != 0;
    int32 Cursor = 1, Written = 0;
    while (Cursor < Data.Num())
    {
        uint32 Run = 0; int32 Shift = 0;
        for (;;)
        {
            if (Cursor >= Data.Num() || Shift > 28) return false;
            const uint8 B = Data[Cursor++]; Run |= uint32(B & 127) << Shift; Shift += 7;
            if (!(B & 128)) break;
        }
        if (Run == 0 || int64(Written) + Run > Bits) return false;
        if (Value) for (uint32 I = 0; I < Run; ++I) OutAlive[(Written + I) >> 6] |= 1ull << ((Written + I) & 63);
        Written += Run; Value = !Value;
    }
    if(!C.InitialAlive.IsEmpty())for(int32 I=0;I<OutAlive.Num();++I)if(OutAlive[I]&~C.InitialAlive[I])return false;
    return Written == Bits;
}
void FERCellWorld::SetChunkAlive(int32 Index, const TArray<uint64>& Alive, TArray<int32>& Dirty)
{
    TSet<int32> Marked(Dirty);
    SetChunkAlive(Index,Alive,Marked);
    Dirty=Marked.Array();Dirty.Sort();
}
void FERCellWorld::SetChunkAlive(int32 Index, const TArray<uint64>& Alive, TSet<int32>& Dirty)
{
    auto& C = Chunks[Index];
    if (C.Alive == Alive) return;
    check(C.Alive.Num()==Alive.Num());
    for(int32 Word=0;Word<Alive.Num();++Word)
    {
        uint64 Changed=C.Alive[Word]^Alive[Word];
        while(Changed)
        {
            const int32 Bit=Word*64+int32(FMath::CountTrailingZeros64(Changed));
            MarkDirty(C.Panel,C.X0+Bit%C.SX,C.Z0+(Bit/C.SX)%C.SZ,Dirty);
            Changed&=Changed-1;
        }
    }
    int32 Count = 0;
    for (uint64 W : Alive) Count += FMath::CountBits64(W);
    RemovedCount += C.AliveCount - Count;
    C.Alive = Alive; C.AliveCount = Count; C.Damaged.Reset(); ++C.Revision;
}
uint32 FERCellWorld::GetAliveDigest() const
{
    uint32 H = LayoutDigest;
    for (const auto& C : Chunks) for (uint64 W : C.Alive) H = HashCombineFast(H, ::GetTypeHash(W));
    return H;
}
SIZE_T FERCellWorld::GetAllocatedBytes() const
{
    SIZE_T Bytes = Panels.GetAllocatedSize() + Chunks.GetAllocatedSize();
    for (const auto& C : Chunks) Bytes += C.Alive.GetAllocatedSize() + C.InitialAlive.GetAllocatedSize() + C.Damaged.GetAllocatedSize();
    for(const auto& P:Panels)Bytes+=P.Skin.GetAllocatedSize()+P.Holes.GetAllocatedSize();
    return Bytes;
}
