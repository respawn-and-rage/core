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

**Code:** `ERSpawnZones.cpp`, `ERLayoutSolids.h`, `ERMapCatalog`, `ERMapTemplate.h`, `ERProceduralConfig.h`, `ERProceduralMap`, `ERMapSizing.cpp`, `ERMapTheme.h`, `ERInteriors.cpp`.

## Design goals

A template earns its geometry: every piece should give a route, a sight line to break or cover to
fight from. Fill is not a goal.

- **Readable places.** A room reads as a shop, a kitchen or a depot, a street as a street. Places
  give callouts, and their furniture is the cover.
- **Several routes, a contested middle.** At least two independent ways between bases, a lateral
  route, and a centre worth holding. See [PROCEDURAL_MAP_PLAYABILITY.md](PROCEDURAL_MAP_PLAYABILITY.md).
- **Mixed fighting distances.** Long lines are broken by tall pieces; low pieces give cover to
  crouch behind and shoot over. No route crosses open ground without a way to cover.
- **Comparable time to contact.** Size follows the walk between bases, not the floor area alone.
- **Substantial, not perfect, symmetry.** Structure, routes and starts are paired. Furniture may
  differ between mates, within the same room type and budget.
- **Variety between seeds.** A seed should change the plan, not only its dimensions.

References: Central Building follows compact house maps (Lake, Safehouse, Bank), Town a dense
shopping district (Chinatown), Village timber compounds (Shoots, St. Marc), Alleys three-lane
towns (Dust II, Mirage). No reference geometry is imported.

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
- **Few, large pieces.** Every destructible panel is at least one draw call and one collision
  body. Build floors, walls and covers from the largest panels the design allows (a run of
  planks is one board; split only around openings and wells). Small repeated details such as
  ladder rails and rungs are fixed instanced pieces, never cell panels. Check `panels=` and
  `chunks=` in `ER_BLOCK_GENERATED` when a template changes.
- All peers rebuild the same layout behind the loading gate. See [DESTRUCTION.md](DESTRUCTION.md).
- Gameplay generation distributes the existing player slots over 1–3 paired spawn zones.
  The seed requests the count; bounded fitting may keep fewer where the map has no safe
  space. Secondary zones sit at least 10m apart, in screened brick pockets beside existing
  standing routes, with checked rear exits. A pocket is 4×3m with starts 130cm apart where the
  site has room, otherwise 2.8×2.4m with starts 90cm apart. Pre-physics pocket and exit checks
  apply the standing-capsule margin in world axes, so decks and beams overhead count.
  The primary zone retains its original start.
  Existing standing routes and cover entry paths remain clear of the new screens.
  Every opposing pair stays screened. Real standing, support and exit collision checks
  run before readiness. Zone count is independent of player slots and capacity.
- **Interiors.** A builder declares its rooms, courts and street edges as `FERLayoutInterior`
  (`AddInteriorPair`). `ERProcedural::FurnishInteriors` runs once on the accepted layout, after
  spawn zones and before the palette, and fills each with a room program from the shared kit:
  backed pieces along the usable walls, free-standing pieces on a row lattice or scattered.
  It only adds solids. Every recorded route keeps a 90cm half-width, doors 130cm, starts 120cm,
  and free-standing pieces a 110cm aisle on every side, so a furnished room stays connected.
  Timber pieces are destructible cells; lockers, fridges, dumpsters and stall canopies are fixed
  instanced metal. Mates share a room type and budget; half of the pairs repeat the arrangement
  and one in four swaps the second room's type within its family. Furnishing stops on a pair
  boundary inside the panel and cell budgets, and `Interior.<Program>` lands in `FeatureNames`.
- Rounds advance the seed before respawn. Menu sessions use fresh seeds.
- Cosmetic rolls use a separate stream and never consume geometry RNG.

## Outdoor backdrop

Central Building, Alleys, Village, Town and Depot roll a cosmetic lighting seed
on the authority at the start of each match. The seed gives standard lighting and
sunset equal chances. It is replicated to peers and persists through arena
regeneration. Standard lighting uses the authored lights and the full Atmospheric
Skybox palette. Sunset picks a FreeStylized panorama, with warm direct light at
19.25° elevation, cool skylight and a dedicated fill for the first-person rig.
Checkouts without those textures fall back to the Atmospheric Skybox sunset palette.
Play Offline offers a named sky picker with previews for outdoor maps. It has 15
standard panoramas, six sunset panoramas and four night panoramas. Every sunset uses the same 19.25°
lighting and rotates the panorama to align its sun or brightest opening in azimuth.
Central Building also exposes its three crossing variants.
Each panorama rotates toward the strongest directional light using an approximate sun azimuth.
Overcast images use their brightest opening. The backdrop uses
one unlit texture (2048px for standard skies, 4096px for sunset and night) and a flat ground
plane below the playable terrain.
Village and variants with a grass ground use a dedicated horizon finish with the shared grass texture.
The sky is visible in real-time Skylight captures. Night disables the level sun,
reduces skylight and adds six perimeter steel poles. Each square panel has four
breakable lenses powering a broad, shadowless fill in its own sector. Each broken
lens removes a quarter of that fill. Ceiling fixtures on outdoor maps also use
local fill at night. A severed pole
collapses through the cell support graph and its bank goes dark. Night uses reduced
exposure and a dim fill that remains on presets without global illumination.

Rooms groups breakable ceiling panels by room and corridor. Fire Training groups
them in sectors of up to 2×2 panels. Each group uses broad, shadowless fill whose
intensity follows the fraction of panels still working. The fill fades at sector
edges. Fire Training and Rooms retain a low ambient minimum
(`er.InteriorKeyLux`, `er.InteriorFillLux`, `er.InteriorBounceLux`).
`er.InteriorLights 0` disables the ambient minimum. Only cell masks replicate damage to the
fixtures. Dedicated servers build the same collision without render emitters.

`setup_outdoor_backdrops.py` creates the private art derivatives in Unreal and
`setup_outdoor_sunsets.py` imports the sunset and night textures and menu previews from the maps bundle.
`setup_outdoor_sky_capture.py` updates the sky material flag and `setup_outdoor_sky_yaw.py` adds the yaw parameter in existing installations. Verify
with the Metal galleries for Central Building and Village, and compare the
`ER_OUTDOOR_BACKDROP` log across seeds and peers.

## Team sizing

`Map.ExpectedTeamSize` (1..16) budgets **750m² per expected player on open ground**:
`target area = team size × 1500m² × AreaDensity`, never below 1500m², **±10% of area**, not per
axis. `ERProcedural::AreaDensity` is 1 for Central Building, Village, Fire Training and Alleys, 0.7 for
Town and 0.6 for Rooms: closed streets and sealed rooms bend every walk, so they shrink until
bases sit a comparable walk apart. Alleys keeps the full budget until a smaller district passes
real-collision spawn occlusion on every tested seed. `TargetAreaM2` is the only place that knows the budget.
Upper floors, roofs and
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
and the water used by Central Building’s pools and Village’s streams are exceptions to the shared
finish catalog. Characters, weapons and combat effects are separate systems.

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
