# Maps

`ERMapCatalog` is the single menu, travel, rotation and rotation-settings catalog. Only production
entries expose rotation toggles; a Town-only pool remains Town-only. All maps cook through
`MapsToCook` in `Config/DefaultGame.ini`.

| ID | Level | Recipe | Rotation |
| --- | --- | --- | --- |
| `central_building` | `L_CentralBuilding` | `DA_CentralBuilding` | yes |
| `middle_eastern_alleys` | `L_MiddleEasternAlleys` | `DA_MiddleEasternAlleys` | yes |
| `fire_training` | `L_FireTraining` | `DA_FireTraining` | yes |
| `rooms` | `L_Rooms` | `DA_Rooms` | offline only |
| `village` | `L_Village` | `DA_Village` | yes |
| `town` | `L_Town` | `DA_Town` | yes |
| `depot` | `L_Depot` | fixed reference | offline only |

Recipes live in `Art/MapTemplates`. Reproduce a layout with
`/Game/EclipseRecoil/Maps/<Level>?MapSeed=42`, or `ERMapGenerate <seed>` on authority.

**Code:** `ERSpawnZones.cpp`, `ERLayoutSolids.h`, `ERMapCatalog`, `ERMapTemplate.h`, `ERProceduralConfig.h`, `ERProceduralMap`, `ERMapSizing.cpp`, `ERMapTheme.h`.

## Shared rules

- `ERProcedural::Build` is deterministic from the replicated seed and validates before
  committing; invalid layouts are rejected atomically. Each peer repeats the same bounded
  attempts (16, or 1024 for alleys), and the attempt index enters the digest.
- **Paired symmetry:** sample one half, then apply the recipe’s half-turn or reflection
  across Y=0 to geometry, spawns and collision. `EERTemplateSymmetry::Mirror` is available
  to every template. Specialized generators fold their canonical construction at the axis,
  reflecting cell grids with positive unit scale and reversing skin winding and openings.
  Oblique cell seams are rejected atomically; imported members stay whole and are
  paired by reflection without being resized. Teams never
  roll independently. Symmetry is not a claim of balance.
- **No initial spawn exposure:** every opposing start pair must have blocked visibility and
  direct projectile lines on the intact map, at every supported size and seed. The common
  gameplay builder requires opaque known-solid cover, then authority checks real collision
  before publishing an epoch or allowing players in. Candidate selection is optional; this
  rule is not. Exhausted retries fail closed. Destruction may open lines later.
- Walkable decks, roofs and treads use solid surfaces with matching visible and collision
  geometry. Detailed art never provides a walkable surface.
- All peers rebuild the same layout behind the loading gate. See [DESTRUCTION.md](DESTRUCTION.md).
- Gameplay generation distributes the existing player slots over 1–3 paired spawn zones.
  The seed requests the count; bounded fitting may keep fewer where the map has no safe
  space. Secondary zones sit at least 10m apart, in screened brick pockets beside existing
  standing routes, with checked rear exits. The primary zone retains its original start.
  Existing standing routes and cover entry paths remain clear of the new screens.
  Every opposing pair stays screened. Real standing, support and exit collision checks
  run before readiness. Zone count is independent of player slots and capacity.
- Rounds advance the seed before respawn. Menu sessions use fresh seeds.
- Cosmetic rolls use a separate stream and never consume geometry RNG.

## Outdoor backdrop

Central Building, Alleys, Village and Town pick one of the 15 Atmospheric Skybox
panoramas from the replicated map seed using a separate cosmetic random stream.
Depot rolls a cosmetic seed at level start. The authored directional and ambient
lights retain their settings. Each panorama rotates toward the strongest directional light using an approximate sun azimuth; overcast images use their brightest opening. The backdrop uses
one unlit 2048px texture and a flat ground plane below the playable terrain.
Village and variants with a grass ground use a dedicated horizon finish with the shared grass texture. The sky is visible in real-time Skylight captures; the horizon is excluded. The existing ambient
lighting stays in use. Fire Training and Rooms keep their indoor lighting.

`setup_outdoor_backdrops.py` creates the private art derivatives in Unreal; `setup_outdoor_sky_capture.py` updates the sky material flag and `setup_outdoor_sky_yaw.py` adds the yaw parameter in existing installations. Verify
with the Metal galleries for Central Building and Village, and compare the
`ER_OUTDOOR_BACKDROP` log across seeds and peers.

## Team sizing

`Map.ExpectedTeamSize` (1..16) budgets **750m² per expected player**:
`target area = team size × 1500m²`, **±10% of area**, not per axis. Upper floors, roofs and
lower passages do not count; Rooms counts primary room polygons and corridor floors, excluding galleries and upper crossings. Central Building counts
the circular footprint (`PlayableAreaM2`), rather than its bounding square. Town subtracts
the exact union of inaccessible central wings and polygonal perimeter recesses from its envelope. No saturation at 8.

`ERMapSizing.cpp` configures transient recipe copies: structural counts, spacing and bounds.
It never scales characters, portals, stairs, ladders, kit meshes, material thickness or cells.
Zero keeps geometry-only authored editor studies, not gameplay approval. Hosted games always
apply the server’s positive team-size rule and mandatory spawn validation. The size travels
in the protocol descriptor.
Admission, capacity and bot counts are independent of it.

```sh
python3 scripts/preview-map-sizing.py --team-size 2 --seeds 42 73 7919 --verify-seeds 512
```

Plans land in `Saved/Testing/MapPlans/<N>v<N>/`, with metres, actual area, target and
deviation. They are technical plans, not renders. `--playability` overlays the tactical graph:
see [PROCEDURAL_MAP_PLAYABILITY.md](PROCEDURAL_MAP_PLAYABILITY.md).

## Palettes

All production templates share `Art/SharedMaterials/DA_SharedMapSurfaces`: the 72 reviewed
Twinmotion finishes in `scripts/shared_material_catalog.py`. Each template binds its own
`DA_Theme_<Name>` (`ThemeLibrary`) with a selection suited to the place. **A finish always shows the physical material:** every
`EERBlockMaterial` row has its own
recognizable surface, so what a block looks like is how it behaves. Palettes never change
geometry, physics, calibration or damage, and their identity enters the digest.

| Template | Palette | Character | Ground |
| --- | --- | --- | --- |
| Central Building | Urban | light concrete villa with clear glazing and steel upper decks | asphalt, grass or snow, from the seed |
| Alleys | Stone | lime-coloured brick houses and stone perimeter | desert sand |
| Fire Training | Warehouse | concrete courts and through-rooms inside a closed procedural hall | dry light concrete |
| Rooms | Tunnels | cast concrete shell, dry floor, timber cover | dry light concrete |
| Village | Jungle | timber and corrugated sheet cladding | jungle ground |
| [Town](maps/town.md) | Town | urban street loop, ground-floor through-shops, closed upper storeys | asphalt or pavers |

Roles stay separate so structures read at distance: shell sheets, structural steel, galvanized
ladders, decks and painted cargo. `CargoSteel` replaces supplied container paint; the Jungle
palette replaces imported building skins and cladding too. Alley house walls follow their
physical material and slabs use `Floor`. `DeckSteel` uses matte 150 cm metal panels on thin Metal slabs.
Fire Training cargo uses matte ribbed `AluminiumSiding02`. Other cargo uses `LightMetalFlakePaint`; `CortenSteel01` and `AluminiumSiding04` remain available
in the shared catalog for oxidized and ribbed finishes.
Village has no exterior trees. Rooms and Fire Training use an owned unlit white
emissive finish on square 60cm ceiling panels. The outdoor backdrop, light panels
and Central Building’s native pool water are exceptions to the shared finish catalog. Characters,
weapons and combat effects are separate systems.

Both recipes and theme libraries require the shared catalog. Missing catalog references,
foreign palette finishes and legacy cargo preservation reject generation before publishing
any layout. The shared catalog identity/revision enters the replicated layout digest.
`setup_shared_material_library.py` owns the derivatives, bindings and seven debris/cell
fallback surfaces. It verifies rollback and protected hashes and rejects dependency leaks
back to the unversioned pack download. Supplied originals remain immutable. Rebuilding older
palettes cannot silently introduce non-catalog finishes: the runtime rejects them.

The shared triplanar functions use world position and unit object scale, retaining the
source physical dimensions and tiling across generated cell meshes. Role assignments are
per-template; weighted choices per building and roof construction variation are not enabled.
`Walkway` is available in the catalog but is not assigned to solid walkable slabs.
`DA_MapThemes` is not bound to any template and remains only as input to older migrations.

## Add a map

Check drops from accessible roofs, stairs and landings. Any reachable landing must have a
route back to the playable area, including the ground behind stairs and raised platforms.

1. Recipe `DA_<Name>` and level `L_<Name>` through a `setup_<name>.py` that validates 516 seeds.
2. Register in `ERMapCatalog.cpp`, and add it to `MapsToCook` in `Config/DefaultGame.ini`.
3. Native builder `ER<Name>.cpp`, reached from `ERProcedural::Build`; team sizing in `ERMapSizing.cpp`.
4. Bind a `DA_Theme_<Name>` palette as `ThemeLibrary`, and add the map to the digest-affecting fields if it has new geometry data.
5. Bump `AERBlockRoom::Protocol` for any change in generated geometry.
6. Layout and physics automation, a `test-<name>.py`, and a page in `docs/maps/`.

## Depot

Fixed reference map with finite ammunition, used by offline play and baseline regressions.
Excluded from lobby buttons, votes and rotation through `InRotation`, so a listen server started
on it by URL restarts its rounds in place.

## Textures

On large walls and roofs, choose finishes whose joints or pattern remain visible at play distance.
Check connections between generated elements from both sides and at grazing angles. Roof links
and landings should meet the neighboring slab at its edge so their visible faces do not overlap.

Production construction palettes use owned Twinmotion derivatives under
`Art/SharedMaterials`; their textures and shader functions are in the private maps repository.
The public manifest contains source package identifiers only. `setup_retire_material_sources.py`
removes the raw Fab download, its vendor preview scene and unreferenced legacy finishes after
outside-reference checks. Git/LFS payloads are verified before tracked files are removed;
the raw pack can be acquired again from Fab. Old finishes still referenced by retained mesh
slots or authoring inputs stay on disk. Sky and owned emissive panels remain separate.
Use DX normals, linear masks and sRGB base colour; preserve packed channels and 16-bit data.
Never execute supplied scripts or reacquire removed sets through pipeline reruns.

## Rebuilding

Each template has one setup script (`setup_central_building.py`, `setup_middle_eastern_alleys.py`,
`setup_fire_training_procedural.py`, `setup_village.py`, ...) plus versioned migrations (`setup_alley_*`,
`setup_village_*`, `setup_map_variety.py`, `setup_map_palettes.py`). They validate 516 seeds, keep
a verified rollback and save only owned packages. Completed markers stop older
scripts from downgrading. Do not recreate retired levels (Killhouse, Dust, laboratories).

## Verify

`preview-map-sizing.py --verify-seeds 512`, `test-lobby.py --industrial --map-sizing`,
`test-map-playability.py`, `test-map-selection.py`. Native: `EclipseRecoil.Maps.*`, `EclipseRecoil.Destruction.SharedMapThemes`. Every template meets the same
bar: 516 seeds natively, several physical seeds with real capsules, a real pawn on stairs and doors,
damage, regeneration, and initial plus late client snapshots.
