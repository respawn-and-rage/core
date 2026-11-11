# Destruction

**Code:** `ERCellWorld`, `ERCellMesher`, `ERCellSupport`, `ERCellNetShard`, `ERBlockRoom`, `ERBlockCombat`, `ERDestructibleMesh`.

Every destructible surface is made of logical **5cm cells** (wood, drywall, sheet and glass
use their real thickness). There is one path for all maps: `AERBlockRoom`/`AERProceduralMap`
own a `FERCellWorld`. `UERBlockWorld` survives only as an empty subobject for saved maps.

## Storage and mesh

Per-chunk alive bitsets plus sparse float HP. Faces and aggregate boxes are cached per 32×32-cell
chunk; one `UDynamicMeshComponent` (`Cells_P<id>_G<group>`) draws and collides for a tile of up
to 8×8 chunks of one panel, so most panels are a single component, intact or damaged. Dirty
chunks and their face neighbours rebuild their caches; each touched group then uploads mesh and
collision together, once per batch. Group names are deterministic movement bases.
A group's body merges chunk boxes that adjoin with the same cross-section, so an intact panel
is one box: a world raycast lying exactly in the plane between two boxes of one body misses
both (sweeps hit). Probe floors with a sweep, or off the chunk grid, on damaged panels.
Cell and skin components use the static draw path (`EDynamicMeshDrawPath::StaticDraw`), so
update them with `SetMesh` only: fast vertex or index updates are not compatible with it.

- Layouts emit `FERCellPanelDesc`. Door and window openings are holes (absent cells, never
  "destroyed"). Each axis holds `round(L/5cm)` equal cells (`FERCellWorld::FitCell`): pieces
  keep their exact volume, because truncating leaves see-through slits.
- Skinned structures use `UERDestructibleMesh`: original triangles clipped at destroyed
  cells, hollow triangle collision, one body per building. `ERMeshCells::FCache` indexes
  source triangles by chunk and retains clipped triangles, cuts and fracture faces.
  Intact skins allocate no clipping cache; the first damage creates their index lazily.
  Only changed chunks and their face neighbours invalidate cached work. Triangle order,
  UVs and collision remain identical to a full rebuild; cache budgets fall back to that
  full rebuild. Mesh upload and collision commit remain synchronous and atomic.
- `FDynamicMesh3` conventions: front faces satisfy `GetTriNormal == normal`, one overlay
  element per vertex, right-handed UV frames. `nullrhi` cannot see errors here.
- Resolve cells from the impact point, never `Hit.Item`.

## Materials

Gameplay preset per cell, not physical measurement, in `DA_ER_BlockMaterials`:

| ID | Type | HP | Bullet × | Explosion × | Transmission |
| ---: | --- | ---: | ---: | ---: | ---: |
| 0 | Drywall | 13 | 1.00 | 2.00 | .50 |
| 1 | Brick | 65 | .75 | 1.50 | .25 |
| 2 | Concrete | 390 | .10 | .50 | .05 |
| 3 | Wood | 26 | 1.00 | 1.50 | .50 |
| 4 | Stone | 650 | .05 | .20 | .02 |
| 5 | Metal | 65 | .50 | 1.00 | .50 |
| 6 | Glass | 5.2 | 1.00 | 3.00 | .90 |

`FERCellCalibration` stamps the tuned resolution. A mismatch rejects generation with
`ER_BLOCK_CALIBRATION_MISMATCH`. Reinforcement (`Strength`, containers use 8) scales HP,
penetration and shielding without changing the shared catalog. Thin steel uses 2mm sheets.
Server settings can override material rules per session.

## Bullets

Voxel DDA through the hit panel. Cover subtracts crossed cells' current HP from the shared
bullet damage budget (see [WEAPONS.md](WEAPONS.md)); structural damage, character damage and
penetration energy stay separate concepts. Hit zones never apply to cells. Trace continuation
uses the real exit, never ignoring a whole room or actor.

## Explosions

`UERWeaponData.BlockExplosion`: 400 base, inner 25cm, outer radius, exponent 2, on cell
centres, × Explosion multiplier × exposure. Rockets use `FERBlockExplosionProfile::Linear`.
Exposure comes from a pre-blast snapshot with 9 traces per candidate chunk. Intact
material attenuates by path length per 20cm (`TransmissionReferenceCm`, `MaxShieldingCm` 40).
Damage is one aggregated batch: excess never chains to neighbours. Only Glass breaks as a
connected pane. Under the preset, one grenade opens drywall, wood or sheet; 20cm brick needs
several; concrete and stone resist by design. Never claim a fixed grenade count per door.

## Support and collapse

`FERCellSupport`: nodes are face-connected components within a chunk, edges per chunk pair
(neighbours plus precomputed panel contacts), refreshed only for changed chunks. Union-find
from anchors (`bAnchorBottom`, also up to `AnchorHeight`; `bAnchored`, indestructible, ground
rows). This is connectivity, not load simulation. Ordinary upper decks are never permanently anchored.

Evaluation runs on the 50ms timer, never inside a blast snapshot. Relabelling, contact
refresh, union-find, collection and canonical sorting resume under `WorkPerStep` and
`GraphTimeMs`; time checks are cooperative, so one small operation or allocation may
cross the time budget. A check retains one immutable alive-mask snapshot using copy-on-write
chunks, without copying art or HP. New damage is included in the next check; regeneration
cancels pending work. Only complete results reach the bounded removal step (32768 cells).
Damage starts it, so a fresh map is never checked: anything built
without support stands until the first damage anywhere, then falls. Each detached component
spawns one cosmetic debris cube (8 per step, 64 global) with no player, projectile or query
collision. Regeneration and EndPlay clear all owned effects: 128 decals and bounded Niagara.

## Replication

Only the server mutates (`CanMutateBlocks`). No client cell-damage RPC exists.

`ERCellProtocol::Version` is the shared descriptor/validator protocol, bumped for any geometry policy change. A descriptor carries cell settings, layout digest, material rules and shard manifest.
`AERCellNetShard` fast arrays hold RLE alive masks for modified chunks only; shell masks may
use bounded zlib frames. HP never leaves the server. Shard headers carry payload count and
digest; clients apply a transaction only when the whole manifest matches, and fail closed.
Initial movement waits for complete collision through an owned acknowledgment.
The server finalizes each dirty shard's digest once per damage batch. Client mask changes
use wordwise differences and one dirty-chunk set/sort for the whole transaction.
Successful payload verification is cached by header and FastArray revision; incomplete
payloads never become valid merely because their header arrived.

`ERCellRecovery` delivers an immutable checkpoint to one owner when live deltas cannot
catch up. Its descriptor and per-page witnesses stay fixed while the live map advances.
Only modified chunks travel; empty pages are proved by the checkpoint manifest. A complete
checkpoint can advance the last applied sequence without waiting for the newest live
manifest, then normal deltas resume. Older checkpoints never roll a client back, and
same-epoch masks cannot resurrect removed cells. Payload errors close both readiness and
collision until a valid checkpoint repairs the state. A checkpoint overtaken by live state
is acknowledged and discarded; it never clears a subsequent payload error, allowing a
bounded retry to request a newer repair.

Recovery allows one checkpoint per player, up to 32 MiB per checkpoint, 128 MiB per map
and 64 concurrent players; allocation failures and budget exhaustion reject the request
with a reason. Owner-only actors have lower network priority than ordinary gameplay and
expire after 180s, or are destroyed by an owner-matching acknowledgment or map regeneration.
Progress counts verified payload versions and commits, not merely newer live sequence
numbers. `GetCellSyncStatus` exposes completion age, progress and apply CPU time to the
controller; entry gates and timeout policy are in [GAMEPLAY.md](GAMEPLAY.md).

Layout digests include template identity, fixed geometry, mesh collision, spawns, surfaces
and attempt index. Guards: 8M cells, or 96M occupied / 512M slots for skinned layouts.

## Rebuild

`setup_block_damage_preset.py` owns HP, multipliers and transmission. Saved binaries go
through Unreal into the private maps mount.

## Verify

Native: `EclipseRecoil.Destruction.*`. `Cells.BudgetedSupport` compares sliced checks
against the synchronous reference, including damage during a retained snapshot;
`Cells.IncrementalSkin` compares every vertex, triangle, material, normal and UV against
full reconstruction; `Cells.BatchedDirtyMasks` covers border and partial-word deltas.
`Cells.RecoveryCheckpoint` covers immutable owner-only snapshots while live state advances,
header-before-payload and checkpoint-before-descriptor delivery, failure/recovery, no rollback, byte limits and acknowledgment
ownership/expiry. Physical network latency and bandwidth still require multi-process runs.
The development-only `-ERCellRecoveryProbe` client flag retains the initial live baseline
and requires the first update through owner-only checkpoint channels; normal live applies
resume after that checkpoint. Recovery logs distinguish a committed payload from an
already-current or obsolete checkpoint acknowledgment.
