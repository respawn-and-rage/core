#include "ERBlockCombat.h"
#include "ERBlockRoom.h"
#include "ERBlockTarget.h"
#include "ERCellMesher.h"
#include "ERCellNetShard.h"
#include "ERCellWorld.h"
#include "Engine/World.h"
#include "Components/DynamicMeshComponent.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace
{
UWorld* CreateCellWorld()
{
    UWorld::InitializationValues V;
    V.AllowAudioPlayback(false).CreateNavigation(false).CreateAISystem(false).CreateFXSystem(false).EnableTraceCollision(true);
    return UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &V);
}
AERBlockRoom* SpawnCellRoom(UWorld* World, FVector Location = FVector::ZeroVector, float Thickness = 20)
{
    FActorSpawnParameters Params; Params.bDeferConstruction = true;
    auto* Room = World->SpawnActor<AERBlockRoom>(Location, FRotator::ZeroRotator, Params);
    Room->Config.BlockSize.Y = Thickness;
    Room->FinishSpawning(FTransform(Location));
    Room->Regenerate();
    return Room;
}
int32 FindPanel(const FERCellWorld& W, int32 Id)
{
    return W.GetPanels().IndexOfByPredicate([Id](const auto& P) { return P.Id == Id; });
}
FERCellId CellAt(const FERCellWorld& W, int32 PanelId, int32 X, int32 Y, int32 Z) { return {FindPanel(W, PanelId), X, Y, Z}; }
// Capsule resting on the floor, or lifted by the 45cm step-up onto a remaining sill.
// Standing 34x92.5; crouched half-height 62.5 (centres given for standing).
bool CapsuleCrosses(UWorld* World, FVector Start, FVector End, float HalfHeight = 92.5f)
{
    FCollisionQueryParams Query(SCENE_QUERY_STAT(ERCellCapsule), false);
    const FVector Down(0, 0, 92.5f - HalfHeight);
    for (float Lift : {0.f, 45.f})
        if (!World->SweepTestByChannel(Start - Down + FVector(0, 0, Lift), End - Down + FVector(0, 0, Lift), FQuat::Identity, ECC_Pawn, FCollisionShape::MakeCapsule(34, HalfHeight), Query)) return true;
    return false;
}
bool RayBlocked(UWorld* World, FVector Start, FVector End)
{
    FCollisionQueryParams Query(SCENE_QUERY_STAT(ERCellRay), false);
    return World->LineTraceTestByChannel(Start, End, ECC_GameTraceChannel1, Query);
}
FERBlockShotResult Fire(UWorld* World, FVector Start, FVector Direction, float Structural)
{
    FCollisionQueryParams Query(SCENE_QUERY_STAT(ERCellShot), false);
    FHitResult Hit;
    World->LineTraceSingleByChannel(Hit, Start, Start + Direction * 1000, ECC_GameTraceChannel1, Query);
    FERBlockShotSettings Settings; Settings.bOverrideStructuralDamage = true; Settings.StructuralDamage = Structural;
    return ERBlockCombat::ContinueShot(World, Hit, Start, Direction, 1000, Structural, Settings, Query, nullptr, nullptr);
}
FString DescribeBreach(const FERCellWorld& W, int32 PanelId, const FERBlockBatchResult& R)
{
    const int32 P = FindPanel(W, PanelId); const FIntVector N = W.GetPanels()[P].Dims;
    TArray<int32> Layers; Layers.Init(0, N.Y);
    int32 Through = 0, MinX = N.X, MaxX = -1, MinZ = N.Z, MaxZ = -1;
    for (int32 Z = 0; Z < N.Z; ++Z) for (int32 X = 0; X < N.X; ++X)
    {
        bool bOpen = true;
        for (int32 Y = 0; Y < N.Y; ++Y) { const bool bDead = !W.IsAlive(P, X, Y, Z); Layers[Y] += bDead; bOpen &= bDead; }
        if (bOpen) { ++Through; MinX = FMath::Min(MinX, X); MaxX = FMath::Max(MaxX, X); MinZ = FMath::Min(MinZ, Z); MaxZ = FMath::Max(MaxZ, Z); }
    }
    FString Text = FString::Printf(TEXT("ER_CELL_BREACH panel=%d candidates=%d occluded=%d attenuated=%d removed=%d through_cells=%d x=[%d,%d] z=[%d,%d] layers="),
        PanelId, R.Candidates, R.Occluded, R.Attenuated, R.Destroyed, Through, MinX, MaxX, MinZ, MaxZ);
    for (int32 L : Layers) Text += FString::Printf(TEXT("%d,"), L);
    return Text;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FERCellStorageTest, "EclipseRecoil.Destruction.Cells.StorageAndCalibration", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FERCellStorageTest::RunTest(const FString& Parameters)
{
    const auto* Catalog = GetDefault<UERBlockMaterialCatalog>();
    const FERCellSettings Settings;
    TArray<FERCellPanelDesc> Desc;
    const FVector Slot(100, 20, 100);
    for (int32 M = 0; M < 7; ++M) FERCellWorld::AddConstruction(M, EERBlockMaterial(M), FTransform(FVector(0, M * 100, 0)), Slot, Settings, Desc);
    FERCellWorld W; FString Error;
    TestTrue(TEXT("constructions build"), W.Build(Desc, Settings, Catalog, Error));
    // Section 2: drywall boards around a cavity, 4 massive layers, 2cm wood, thin sheet, glass pane.
    TestEqual(TEXT("drywall is two boards"), W.GetPanels().FilterByPredicate([](const auto& P) { return P.Material == EERBlockMaterial::Drywall; }).Num(), 2);
    TestEqual(TEXT("brick 20cm has four 5cm layers"), W.GetPanels()[FindPanel(W, 1 * 8)].Dims, FIntVector(20, 4, 20));
    TestEqual(TEXT("wood cell is 5x5x2"), W.GetPanels()[FindPanel(W, 3 * 8)].Cell, FVector(5, 2, 5));
    TestTrue(TEXT("sheet thickness is separate from the 5cm cell"), FMath::IsNearlyEqual(W.GetPanels()[FindPanel(W, 5 * 8)].Cell.Y, .2f));
    TestEqual(TEXT("glass is one pane"), W.GetPanels()[FindPanel(W, 6 * 8)].Dims, FIntVector(1));
    // Calibration stamp: HP are never silently reused at another resolution.
    FERCellSettings Coarse; Coarse.CellSize = 10;
    TArray<FERCellPanelDesc> CoarseDesc; FERCellWorld::AddConstruction(0, EERBlockMaterial::Brick, FTransform::Identity, Slot, Coarse, CoarseDesc);
    TestFalse(TEXT("other cell size rejected"), W.Build(CoarseDesc, Coarse, Catalog, Error));
    TestTrue(TEXT("mismatch names the calibration"), Error.Contains(TEXT("ER_BLOCK_CALIBRATION_MISMATCH")));
    TestEqual(TEXT("rejected build preserves live state"), W.GetPanels().Num(), Desc.Num());
    TArray<FERCellPanelDesc> Odd = {Desc[0]}; Odd[0].Size.X = 102;
    TestFalse(TEXT("partial cells rejected"), FERCellWorld().Build(Odd, Settings, Catalog, Error));
    // Local damage and the 36-point table, fractions kept.
    auto Hits = [&](EERBlockMaterial M, float Structural)
    {
        const int32 Panel = FindPanel(W, int32(M) * 8);
        const FERCellId Id{Panel, 3, 0, 3};
        const float Damage = Structural * Catalog->Find(M)->BulletDamageScale;
        int32 Count = 0;
        while (W.IsAlive(Id) && Count < 1000) { TMap<FERCellId, float> D; D.Add(Id, Damage); W.ApplyDamage(D); ++Count; }
        return Count;
    };
    TestEqual(TEXT("drywall 1 hit"), Hits(EERBlockMaterial::Drywall, 36), 1);
    TestEqual(TEXT("brick 3 hits"), Hits(EERBlockMaterial::Brick, 36), 3);
    TestEqual(TEXT("wood 1 hit"), Hits(EERBlockMaterial::Wood, 36), 1);
    TestEqual(TEXT("sheet 4 hits"), Hits(EERBlockMaterial::Metal, 36), 4);
    const FERCellId Concrete{FindPanel(W, 2 * 8), 7, 0, 7};
    for (int32 I = 0; I < 10; ++I) { TMap<FERCellId, float> D; D.Add(Concrete, 36 * .1f); W.ApplyDamage(D); }
    TestTrue(TEXT("concrete keeps fractional 3.6 per hit"), FMath::IsNearlyEqual(W.GetHealth(Concrete), 354.f, .01f));
    TestEqual(TEXT("neighbour untouched"), W.GetHealth({Concrete.Panel, 8, 0, 7}), 390.f);
    TestEqual(TEXT("cell behind untouched"), W.GetHealth({Concrete.Panel, 7, 1, 7}), 390.f);
    // Network encoding: round trip and fail-closed decoding.
    TArray<uint8> Mask; TArray<uint64> Bits;
    W.EncodeChunk(W.GetPanels()[FindPanel(W, 1 * 8)].FirstChunk, Mask);
    TestTrue(TEXT("mask round trip"), W.DecodeChunk(W.GetPanels()[FindPanel(W, 1 * 8)].FirstChunk, Mask, Bits) && Bits == W.GetChunks()[W.GetPanels()[FindPanel(W, 1 * 8)].FirstChunk].Alive);
    TArray<uint8> Bad = Mask; Bad.Add(5);
    TestFalse(TEXT("oversized mask rejected"), W.DecodeChunk(W.GetPanels()[FindPanel(W, 1 * 8)].FirstChunk, Bad, Bits));
    TestFalse(TEXT("zero run rejected"), W.DecodeChunk(0, {1, 0}, Bits));
    // DDA: a perpendicular ray through 20cm brick crosses four 5cm cells.
    double Length = 0; int32 Crossed = 0;
    const auto& Brick = W.GetPanels()[FindPanel(W, 1 * 8)];
    W.Traverse(FindPanel(W, 1 * 8), Brick.Transform.TransformPosition(FVector(12, -10, 12)), Brick.Transform.TransformPosition(FVector(12, 30, 12)),
        [&](const FIntVector&, double T0, double T1) { ++Crossed; Length += (T1 - T0) * 40; return true; });
    TestEqual(TEXT("four cells crossed"), Crossed, 4);
    TestTrue(TEXT("20cm crossed"), FMath::IsNearlyEqual(Length, 20., 1.e-6));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FERCellMeshCollisionTest, "EclipseRecoil.Destruction.Cells.MeshAndCollision", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FERCellMeshCollisionTest::RunTest(const FString& Parameters)
{
    UWorld* World = CreateCellWorld(); if (!World) return false;
    auto* Room = SpawnCellRoom(World);
    const auto& W = Room->GetCellWorld();
    TestTrue(TEXT("cell room generated and validated"), W.GetCellCount() == 77580 && Room->ValidateCells());
    // Random removals, then compare the aggregates with brute force.
    FRandomStream Random(17);
    TMap<FERCellId, float> Remove;
    for (int32 I = 0; I < 3000; ++I)
    {
        const int32 P = Random.RandRange(0, W.GetPanels().Num() - 1); const FIntVector& N = W.GetPanels()[P].Dims;
        Remove.Add({P, Random.RandRange(0, N.X - 1), Random.RandRange(0, N.Y - 1), Random.RandRange(0, N.Z - 1)}, TNumericLimits<float>::Max());
    }
    Room->ApplyCellBatch(Remove);
    TestTrue(TEXT("dirty chunks rebuilt consistently"), Room->ValidateCells());
    bool bFaces = true, bBoxes = true;
    for (int32 C = 0; C < W.GetChunks().Num(); ++C)
    {
        const auto& Chunk = W.GetChunks()[C]; const auto& P = W.GetPanels()[Chunk.Panel];
        TArray<ERCellMesh::FQuad> Quads; TArray<FBox> Boxes;
        ERCellMesh::BuildFaces(W, C, Quads); ERCellMesh::BuildBoxes(W, C, Boxes);
        double Exposed = 0, Area = 0, Covered = 0;
        for (int32 Z = 0; Z < Chunk.SZ; ++Z) for (int32 Y = 0; Y < P.Dims.Y; ++Y) for (int32 X = 0; X < Chunk.SX; ++X)
        {
            const int32 GX = Chunk.X0 + X, GZ = Chunk.Z0 + Z;
            if (!W.IsAlive(Chunk.Panel, GX, Y, GZ)) continue;
            const FVector S = P.Cell;
            const FIntVector Offsets[6] = {{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}};
            for (const auto& O : Offsets) if (!W.IsAlive(Chunk.Panel, GX + O.X, Y + O.Y, GZ + O.Z)) Exposed += O.X ? S.Y * S.Z : O.Y ? S.X * S.Z : S.X * S.Y;
            Covered += S.X * S.Y * S.Z;
        }
        for (const auto& Q : Quads) Area += ((Q.Corner[1] - Q.Corner[0]) ^ (Q.Corner[3] - Q.Corner[0])).Size();
        double Volume = 0; for (const FBox& B : Boxes) Volume += B.GetVolume();
        bFaces &= FMath::IsNearlyEqual(Area, Exposed, Exposed * 1.e-5 + .01);
        bBoxes &= FMath::IsNearlyEqual(Volume, Covered, Covered * 1.e-5 + .01);
    }
    TestTrue(TEXT("faces cover exactly the exposed cell faces: no internal faces"), bFaces);
    // Rendered winding and shading normal agree with Unreal's front-face convention.
    bool bWinding = true; int32 Triangles = 0;
    TArray<UPrimitiveComponent*> Components; Room->GetCellComponents(Components);
    for (auto* C : Components)
        Cast<UDynamicMeshComponent>(C)->ProcessMesh([&](const UE::Geometry::FDynamicMesh3& Mesh)
        {
            const auto* Normals = Mesh.HasAttributes() ? Mesh.Attributes()->PrimaryNormals() : nullptr;
            if (!Normals) { bWinding = false; return; }
            for (int32 T : Mesh.TriangleIndicesItr())
            {
                // Every triangle must carry its normals (unset overlays render unlit garbage).
                if (!Normals->IsSetTriangle(T)) { bWinding = false; continue; }
                FVector3f N; Normals->GetTriElement(T, 0, N);
                bWinding &= FVector3f::DotProduct(FVector3f(Mesh.GetTriNormal(T)), N) > .99f; ++Triangles;
            }
        });
    TestTrue(TEXT("every triangle winds as an Unreal front face of its normal"), bWinding && Triangles > 0);
    TestTrue(TEXT("collision boxes cover exactly the alive cells"), bBoxes);
    Room->Regenerate();
    // A single 5cm hole lets a ray through but is not a passage for the capsule.
    const FERCellId Hole = CellAt(W, 4 * 8, 15, 0, 20);
    const FVector Center = W.GetCellCenter(Hole);
    TestTrue(TEXT("intact board blocks the ray"), RayBlocked(World, Center + FVector(40, 0, 0), Center - FVector(40, 0, 0)));
    { TMap<FERCellId, float> D; D.Add(Hole, TNumericLimits<float>::Max()); Room->ApplyCellBatch(D); }
    TestFalse(TEXT("ray passes the removed 5cm cell"), RayBlocked(World, Center + FVector(40, 0, 0), Center - FVector(40, 0, 0)));
    TestTrue(TEXT("neighbour cell still blocks"), RayBlocked(World, Center + FVector(40, 5, 0), Center - FVector(40, -5, 0)));
    TestFalse(TEXT("capsule cannot pass a 5cm hole"), CapsuleCrosses(World, FVector(-110, Center.Y, 95), FVector(-190, Center.Y, 95)));
    FHitResult Hit;
    FCollisionQueryParams Query(SCENE_QUERY_STAT(ERCellResolve), false);
    // Layer 3 of the east brick wall faces the room interior.
    const FERCellId Probe = CellAt(W, 1 * 8, 50, 3, 20);
    const FVector Front = W.GetCellCenter(Probe);
    World->LineTraceSingleByChannel(Hit, Front - FVector(50, 0, 0), Front + FVector(50, 0, 0), ECC_GameTraceChannel1, Query);
    FERCellId Resolved;
    TestTrue(TEXT("hit resolves the exact cell from the impact point"), Room->ResolveCell(Hit, Resolved) && Resolved == Probe);
    // Breaches: drywall opens with one grenade, 20cm brick does not.
    AERBlockRoom::ApplyWorldExplosion(World, FVector(-100, -240, 110), FERBlockExplosionProfile());
    TestTrue(TEXT("capsule crosses the drywall grenade breach"), CapsuleCrosses(World, FVector(-100, -200, 95), FVector(-100, -330, 95)));
    // Transmission equivalent to the block preset (0.933/cm): measure, do not assume, the count.
    int32 Crouched = 0, Standing = 0;
    for (int32 G = 1; G <= 6 && !Standing; ++G)
    {
        AddInfo(DescribeBreach(W, 1 * 8, AERBlockRoom::ApplyWorldExplosion(World, FVector(240, 0, 110), FERBlockExplosionProfile())));
        if (!Crouched && CapsuleCrosses(World, FVector(200, 0, 95), FVector(330, 0, 95), 62.5f)) Crouched = G;
        if (CapsuleCrosses(World, FVector(200, 0, 95), FVector(330, 0, 95))) Standing = G;
    }
    AddInfo(FString::Printf(TEXT("ER_CELL_BRICK20_GRENADES crouched=%d standing=%d"), Crouched, Standing));
    TestTrue(TEXT("one grenade does not open four-layer brick"), Crouched > 1 && Standing > 1);
    TestTrue(TEXT("repeated grenades open four-layer brick for a crouched pawn"), Crouched >= 2 && Crouched <= 3);
    TestTrue(TEXT("and then for a standing pawn"), Standing >= Crouched && Standing <= 5);
    TestTrue(TEXT("mappings after breaches"), Room->ValidateCells());
    World->DestroyWorld(false);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FERCellCombatTest, "EclipseRecoil.Destruction.Cells.BallisticsAndBlast", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FERCellCombatTest::RunTest(const FString& Parameters)
{
    UWorld* World = CreateCellWorld(); if (!World) return false;
    auto* Room = SpawnCellRoom(World);
    const auto& W = Room->GetCellWorld();
    // Each drywall board absorbs its 20 current HP, with one budget across the cavity.
    const FERCellId Front = CellAt(W, 0 * 8 + 1, 50, 0, 30), Back = CellAt(W, 0 * 8, 50, 0, 30);
    const FVector Aim = W.GetCellCenter(Front);
    auto Shot = Fire(World, FVector(Aim.X, 0, Aim.Z), FVector(0, -1, 0), 60);
    TestTrue(TEXT("bullet removes the inner board cell"), !W.IsAlive(Front));
    TestTrue(TEXT("remaining damage removes the outer board cell"), !W.IsAlive(Back));
    TestEqual(TEXT("only the two crossed cells"), W.GetRemovedCount(), int64(2));
    TestTrue(TEXT("two 13HP boards leave 34 damage from 60"), FMath::IsNearlyEqual(Shot.RemainingDamage, 34.f, .01f));
    // A weaker bullet exhausts itself in the second board.
    Room->Regenerate();
    Shot = Fire(World, FVector(Aim.X, 0, Aim.Z), FVector(0, -1, 0), 22);
    TestTrue(TEXT("22 breaks first board and leaves second at 4HP"), !W.IsAlive(Front) && FMath::IsNearlyEqual(W.GetHealth(Back),4.f,.01f) && Shot.RemainingDamage==0);
    // Brick stops the bullet in its first surviving layer.
    Room->Regenerate();
    const FERCellId B0 = CellAt(W, 1 * 8, 54, 3, 30), B1 = CellAt(W, 1 * 8, 54, 2, 30), B2 = CellAt(W, 1 * 8, 54, 1, 30), B3 = CellAt(W, 1 * 8, 54, 0, 30);
    const FVector BrickAim = W.GetCellCenter(B0);
    Shot = Fire(World, FVector(0, BrickAim.Y, BrickAim.Z), FVector(1, 0, 0), 36);
    TestTrue(TEXT("first 65HP layer absorbs all 36 damage"), FMath::IsNearlyEqual(W.GetHealth(B0), 29.f, .01f) && Shot.RemainingDamage==0);
    TestTrue(TEXT("intact first layer protects every deeper layer"), W.GetHealth(B1)==65 && W.GetHealth(B2)==65 && W.GetHealth(B3)==65);
    TestEqual(TEXT("penetration never removes brick at 36"), W.GetRemovedCount(), int64(0));
    // Owner's example, using a real wood cell and a physical target behind it.
    auto* Wood=SpawnCellRoom(World,FVector(0,5000,0));
    for(auto& Material:Wood->MaterialCatalog->Materials)
        if(Material.Type==EERBlockMaterial::Wood)Material.MaxHealth=60;
    Wood->Regenerate();
    const auto& WoodWorld=Wood->GetCellWorld();
    const FERCellId WoodCell=CellAt(WoodWorld,4*8,15,0,20);
    const FVector WoodCenter=Wood->GetActorTransform().TransformPosition(WoodWorld.GetCellCenter(WoodCell)),WoodStart=WoodCenter+FVector(60,0,0);
    auto* Rear=World->SpawnActor<AERBlockTarget>(WoodCenter-FVector(80,0,0),FRotator::ZeroRotator);
    Shot=Fire(World,WoodStart,FVector(-1,0,0),100);
    TestTrue(TEXT("100 minus 60HP wood delivers exactly 40 to rear target"), !WoodWorld.IsAlive(WoodCell) && Shot.TerminalHit.GetActor()==Rear && Shot.RemainingDamage==40 && Rear->Health==460);
    Wood->Regenerate(); Rear->ResetHealth();
    TMap<FERCellId,float> PriorDamage;PriorDamage.Add(WoodCell,45);Wood->ApplyCellBatch(PriorDamage);
    Shot=Fire(World,WoodStart,FVector(-1,0,0),100);
    TestTrue(TEXT("previously damaged wood absorbs only remaining 15HP"),Shot.RemainingDamage==85 && Rear->Health==415);
    Wood->Regenerate(); Rear->ResetHealth();
    Shot=Fire(World,WoodStart,FVector(-1,0,0),60);
    TestTrue(TEXT("exact HP destroys cover but delivers no damage behind it"),!WoodWorld.IsAlive(WoodCell) && Shot.RemainingDamage==0 && Rear->Health==500);
    Wood->Regenerate();
    Shot=Fire(World,WoodStart,FVector(-1,0,0),20);
    TestTrue(TEXT("insufficient bullet leaves cover alive and target untouched"),WoodWorld.GetHealth(WoodCell)==40 && Rear->Health==500 && Shot.RemainingDamage==0);
    Rear->Destroy(); Wood->Destroy();
    // Grenade: full local damage inside 25cm, nothing from 200cm, shielding by layers.
    Room->Regenerate();
    const FERCellId C0 = CellAt(W, 2 * 8, 50, 0, 22), C3 = CellAt(W, 2 * 8, 50, 3, 22);
    const FVector Face = W.GetCellCenter(C0) - FVector(0, 12.5, 0);
    const auto Blast = AERBlockRoom::ApplyWorldExplosion(World, Face, FERBlockExplosionProfile());
    TestTrue(TEXT("concrete front cell takes 400 x 0.5 = 200 at full exposure"), FMath::IsNearlyEqual(W.GetHealth(C0), 190.f, .05f));
    TestTrue(TEXT("rear layer shielded, not full damage"), W.GetHealth(C3) > 190.f && W.GetHealth(C3) < 390.f);
    TestTrue(TEXT("most candidates attenuated"), Blast.Attenuated > 0 && W.IsAlive(C0) && W.IsAlive(C3));
    bool bLocal = true;
    for (const auto& Chunk : W.GetChunks())
        for (int32 Z = 0; Z < Chunk.SZ; ++Z) for (int32 Y = 0; Y < W.GetPanels()[Chunk.Panel].Dims.Y; ++Y) for (int32 X = 0; X < Chunk.SX; ++X)
        {
            const FERCellId Id{Chunk.Panel, Chunk.X0 + X, Y, Chunk.Z0 + Z};
            if (W.GetHealth(Id) < W.GetMaxHealth(Chunk.Panel)) bLocal &= FVector::Distance(W.GetCellCenter(Id), Face) < 200;
        }
    TestTrue(TEXT("zero structural damage from 200cm"), bLocal);
    // Four layers are not one layer: the same grenade breaches a 5cm brick wall.
    {
        auto* Thin = SpawnCellRoom(World, FVector(0, 5000, 0), 5);
        TestEqual(TEXT("5cm brick is a single layer"), Thin->GetCellWorld().GetPanels()[FindPanel(Thin->GetCellWorld(), 1 * 8)].Dims.Y, 1);
        AERBlockRoom::ApplyWorldExplosion(World, FVector(240, 5000, 110), FERBlockExplosionProfile());
        TestTrue(TEXT("one grenade opens single-layer brick"), CapsuleCrosses(World, FVector(200, 5000, 95), FVector(320, 5000, 95)));
        Thin->Destroy();
    }
    // Order independence: the same snapshot applied in reverse insertion order.
    auto* RoomB = SpawnCellRoom(World, FVector(5000, 0, 0));
    TMap<FERCellId, float> Forward, Reverse; FERBlockBatchResult Ignored;
    FCollisionQueryParams Query(SCENE_QUERY_STAT(ERCellOrder), false);
    Room->Regenerate(); RoomB->Regenerate();
    // Regeneration replaces the chunk components: collect them afterwards.
    TArray<UPrimitiveComponent*> Components; Room->GetCellComponents(Components); RoomB->GetCellComponents(Components);
    for (auto* C : Components) Query.AddIgnoredComponent(C);
    Room->CollectCellExplosion(FVector(240, 0, 110), FERBlockExplosionProfile(), Query, Forward, Ignored);
    TArray<FERCellId> Keys; Forward.GetKeys(Keys); Keys.Sort();
    for (int32 I = Keys.Num() - 1; I >= 0; --I) Reverse.Add(Keys[I], Forward[Keys[I]]);
    TMap<FERCellId, float> Again;
    Room->CollectCellExplosion(FVector(240, 0, 110), FERBlockExplosionProfile(), Query, Again, Ignored);
    TestTrue(TEXT("snapshot exposure is repeatable"), Again.OrderIndependentCompareEqual(Forward));
    Room->ApplyCellBatch(Forward); RoomB->ApplyCellBatch(Reverse);
    TestEqual(TEXT("processing order never changes the result"), Room->GetStateDigest(), RoomB->GetStateDigest());
    TestTrue(TEXT("brick breach removes many small cells"), W.GetRemovedCount() > 500);
    // Glass breaks as a pane; drywall keeps local cells.
    {
        FERCellWorld G; FString Error; TArray<FERCellPanelDesc> D;
        FERCellWorld::AddConstruction(0, EERBlockMaterial::Glass, FTransform::Identity, FVector(100, 20, 100), FERCellSettings(), D);
        FERCellWorld::AddConstruction(1, EERBlockMaterial::Drywall, FTransform(FVector(0, 100, 0)), FVector(100, 20, 100), FERCellSettings(), D);
        TestTrue(TEXT("pane world"), G.Build(D, FERCellSettings(), Room->MaterialCatalog, Error));
        const FERCellId Pane{0, 0, 0, 0};
        TMap<FERCellId, float> Hit;
        Hit.Add(Pane, 1);
        TestTrue(TEXT("nonlethal glass damage does not report a break"), G.ApplyDamage(Hit).BrokenGlassPanes.IsEmpty());
        Hit.Add(Pane, 8);
        Hit.Add({1, 3, 0, 3}, 20);
        const auto R = G.ApplyDamage(Hit);
        TestEqual(TEXT("pane and one drywall cell removed"), R.Destroyed, 2);
        TestTrue(TEXT("only the destroyed glass pane is reported"), R.BrokenGlassPanes == TArray<FERCellId>{Pane});
        TestTrue(TEXT("repeated damage does not report the same pane"), G.ApplyDamage(Hit).BrokenGlassPanes.IsEmpty());
        TestEqual(TEXT("the whole pane is gone with one cell"), G.GetRemovedCount(), int64(2));
        TestTrue(TEXT("drywall neighbour remains"), G.IsAlive({1, 4, 0, 3}));
    }
    World->DestroyWorld(false);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FERCellNetTransactionTest, "EclipseRecoil.Destruction.Cells.NetworkTransactions", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FERCellNetTransactionTest::RunTest(const FString& Parameters)
{
    UWorld* World = CreateCellWorld(); if (!World) return false;
    auto* Server = SpawnCellRoom(World);
    auto* Client = World->SpawnActor<AERBlockRoom>(); Client->SetRole(ROLE_SimulatedProxy); Client->bNetworkBegunPlay = true;
    const int32 Shards = FMath::DivideAndRoundUp(Server->GetCellWorld().GetChunks().Num(), AERCellNetShard::Capacity);
    auto& N = Client->Network;
    N.Epoch = 1; N.Protocol = 55; N.Cells = true; N.CellSettings = Server->CellSettings; N.Blocks = Server->Config;
    N.BlockCount = int32(Server->GetCellWorld().GetCellCount()); N.LayoutDigest = Server->GetLayoutDigest(); N.Sequence = 1; N.PageVersions.Init(1, Shards);
    Client->OnRep_Network();
    TestTrue(TEXT("client built cells from the descriptor"), Client->GetCellWorld().GetCellCount() > 0 && !Client->bNetworkError);
    TestFalse(TEXT("collision gated before the snapshot"), Client->IsMapReady());
    TArray<AERCellNetShard*> Pages;
    for (int32 I = 0; I < Shards; ++I)
    {
        auto* S = World->SpawnActor<AERCellNetShard>(); S->Room = Client; S->Header.Epoch = 1; S->Header.Index = I; S->Header.Version = 1; Pages.Add(S);
    }
    Client->ReceiveCellShard(Pages[0]);
    TestTrue(TEXT("complete empty manifest: intact layout ready"), Shards != 1 || Client->IsMapReady());
    for (auto* S : Pages) Client->ReceiveCellShard(S);
    TestTrue(TEXT("initial snapshot ready"), Client->IsMapReady());
    TestEqual(TEXT("initial digests equal"), Client->GetStateDigest(), Server->GetStateDigest());
    // Authority grenade; only modified chunks travel.
    AERBlockRoom::ApplyWorldExplosion(World, FVector(-100, -240, 110), FERBlockExplosionProfile());
    const auto& SW = Server->GetCellWorld();
    TArray<int32> Changed; int32 Bytes = 0;
    for (int32 C = 0; C < SW.GetChunks().Num(); ++C) if (SW.GetChunks()[C].Revision) Changed.Add(C);
    TArray<int32> Versions; Versions.Init(1, Shards);
    for (int32 C : Changed)
    {
        TArray<uint8> Mask; SW.EncodeChunk(C, Mask); Bytes += Mask.Num() + 8;
        auto* S = Pages[C / AERCellNetShard::Capacity];
        S->SetChunk(C, SW.GetChunks()[C].Revision, MoveTemp(Mask)); Versions[C / AERCellNetShard::Capacity] = 2;
    }
    AddInfo(FString::Printf(TEXT("ER_CELL_NET_DELTA chunks=%d bytes=%d removed=%lld"), Changed.Num(), Bytes, SW.GetRemovedCount()));
    TestTrue(TEXT("grenade delta stays small"), Changed.Num() > 0 && Bytes < 4096);
    for (int32 I = 0; I < Shards; ++I) Pages[I]->Header.Version = Versions[I];
    Client->ReceiveCellShard(Pages[0]);
    TestEqual(TEXT("future shard waits for its manifest"), Client->GetCellWorld().GetRemovedCount(), int64(0));
    N.Sequence = 2; for (int32 I = 0; I < Shards; ++I) N.PageVersions[I] = Versions[I];
    Client->OnRep_Network();
    TestEqual(TEXT("coherent transaction applied"), Client->GetStateDigest(), Server->GetStateDigest());
    TestTrue(TEXT("client meshes and collision rebuilt"), Client->ValidateCells());
    // Reproduce a late join where the current header arrives before its fast
    // array. Previously an empty array could open the gate permanently intact.
    auto* Late=World->SpawnActor<AERBlockRoom>();Late->SetRole(ROLE_SimulatedProxy);Late->bNetworkBegunPlay=true;Late->Network=N;Late->OnRep_Network();
    TArray<AERCellNetShard*> LatePages;
    for(auto* Page:Pages)
    {
        auto* S=World->SpawnActor<AERCellNetShard>();S->Room=Late;S->Header=Page->Header;LatePages.Add(S);Late->ReceiveCellShard(S);
    }
    TestFalse(TEXT("header alone cannot open late-join collision"),Late->IsMapReady());
    for(int32 I=0;I<Pages.Num();++I){LatePages[I]->Chunks.Items=Pages[I]->Chunks.Items;Late->ReceiveCellShard(LatePages[I]);}
    TestTrue(TEXT("complete late-join payload opens gate"),Late->IsMapReady());
    TestEqual(TEXT("late join retains existing destruction"),Late->GetStateDigest(),Server->GetStateDigest());
    // Invalid payload fails closed before anything changes.
    const uint32 Before = Client->GetStateDigest();
    auto* Bad = Pages[Changed[0] / AERCellNetShard::Capacity];
    Bad->SetChunk(Changed[0], 999, {1, 0}); ++Bad->Header.Version; N.PageVersions[Changed[0] / AERCellNetShard::Capacity] = Bad->Header.Version; N.Sequence = 3;
    AddExpectedError(TEXT("ER_BLOCK_NET_INCOMPATIBLE state"), EAutomationExpectedErrorFlags::Contains, 1);
    Client->OnRep_Network();
    TestTrue(TEXT("invalid mask detected"), Client->bNetworkError);
    TestEqual(TEXT("invalid transaction preserves state"), Client->GetStateDigest(), Before);
    // New epoch: late join to a fresh layout clears previous removals.
    Client->bNetworkError = false; N.Epoch = 2; N.Sequence = 1; N.PageVersions.Init(1, Shards);
    Client->OnRep_Network();
    for (auto* S : Pages) { S->Chunks.Items.Reset(); S->Header.Epoch = 2; S->Header.Version = 1;S->Header.ChunkCount=0;S->Header.PayloadDigest=0; Client->ReceiveCellShard(S); }
    TestTrue(TEXT("new epoch ready"), Client->IsMapReady());
    TestEqual(TEXT("stale removals cleared"), Client->GetCellWorld().GetRemovedCount(), int64(0));
    N.Epoch = 3; N.Protocol = 109;
    AddExpectedError(TEXT("ER_BLOCK_NET_INCOMPATIBLE descriptor"), EAutomationExpectedErrorFlags::Contains, 1);
    Client->OnRep_Network();
    TestTrue(TEXT("protocol mismatch fails closed"), Client->bNetworkError);
    N.Epoch=4;N.Protocol=55;N.BlockCount=8000001;
    AddExpectedError(TEXT("ER_BLOCK_NET_INCOMPATIBLE descriptor"),EAutomationExpectedErrorFlags::Contains,1);
    Client->OnRep_Network();
    TestTrue(TEXT("ordinary rooms retain the 8M descriptor limit"),Client->bNetworkError);
    World->DestroyWorld(false);
    return true;
}
#endif
