# Destruction

**Code:** `ERCellWorld`, `ERCellMesher`, `ERCellSupport`, `ERCellNetShard`, `ERBlockRoom`, `ERBlockCombat`, `ERDestructibleMesh`.

Every destructible surface is made of logical **5cm cells** (wood, drywall, sheet and glass
use their real thickness). There is one path for all maps: `AERBlockRoom`/`AERProceduralMap`
own a `FERCellWorld`. `UERBlockWorld` survives only as an empty subobject for saved maps.

## Storage and mesh

Per-chunk alive bitsets plus sparse float HP. One `UDynamicMeshComponent` per 32×32-cell
chunk, with visible faces only and aggregate-box collision. Dirty chunks and their
face neighbours rebuild mesh and collision together.

- Layouts emit `FERCellPanelDesc`. Door and window openings are holes (absent cells, never
  "destroyed"). Each axis holds `round(L/5cm)` equal cells (`FERCellWorld::FitCell`): pieces
  keep their exact volume, because truncating leaves see-through slits.
- Skinned structures use `UERDestructibleMesh`: original triangles clipped at destroyed
  cells, hollow triangle collision, one body per building.
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

Evaluation runs on the 50ms timer, never inside a blast snapshot, with bounded removals
per step (32768 cells). Damage starts it, so a fresh map is never checked: anything built
without support stands until the first damage anywhere, then falls. Each detached component
spawns one cosmetic debris cube (8 per step, 64 global) with no player, projectile or query
collision. Regeneration and EndPlay clear all owned effects: 128 decals and bounded Niagara.

## Replication

Only the server mutates (`CanMutateBlocks`). No client cell-damage RPC exists.

Protocol is `AERBlockRoom::Protocol`, bumped for any geometry policy change. A descriptor carries cell settings, layout digest, material rules and shard manifest.
`AERCellNetShard` fast arrays hold RLE alive masks for modified chunks only; shell masks may
use bounded zlib frames. HP never leaves the server. Shard headers carry payload count and
digest; clients apply a transaction only when the whole manifest matches, and fail closed.
Initial movement waits for complete collision through an owned acknowledgment.

Layout digests include template identity, fixed geometry, mesh collision, spawns, surfaces
and attempt index. Guards: 8M cells, or 96M occupied / 512M slots for skinned layouts.

## Rebuild

`setup_block_damage_preset.py` owns HP, multipliers and transmission. Saved binaries go
through Unreal into the private maps mount.

## Verify

Native: `EclipseRecoil.Destruction.*`.
