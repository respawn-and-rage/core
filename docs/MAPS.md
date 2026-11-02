# Maps

`ERMapCatalog` is the single menu, travel and rotation catalog. All maps cook through
`MapsToCook` in `Config/DefaultGame.ini`.

| ID | Level | Recipe | Rotation |
| --- | --- | --- | --- |
| `central_building` | `L_CentralBuilding` | `DA_CentralBuilding` | yes |
| `middle_eastern_alleys` | `L_MiddleEasternAlleys` | `DA_MiddleEasternAlleys` | yes |
| `fire_training` | `L_FireTraining` | `DA_FireTraining` | yes |
| `rooms` | `L_Rooms` | `DA_Rooms` | offline only |
| `village` | `L_Village` | `DA_Village` | yes |
| `depot` | `L_Depot` | fixed reference | offline only |

Recipes live in `Art/MapTemplates`. Reproduce a layout with
`/Game/EclipseRecoil/Maps/<Level>?MapSeed=42`, or `ERMapGenerate <seed>` on authority.

**Code:** `ERMapCatalog`, `ERMapTemplate.h`, `ERProceduralConfig.h`, `ERProceduralMap`, `ERMapSizing.cpp`, `ERMapTheme.h`.

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
- Each template has ten supported starts per team, plus server-side overflow near the base.
- Rounds advance the seed before respawn. Menu sessions use fresh seeds.
- Cosmetic rolls use a separate stream and never consume geometry RNG.

## Team sizing

`Map.ExpectedTeamSize` (1..16) budgets **500m² per expected player**:
`target area = team size × 1000m²`, **±10% of area**, not per axis. Upper floors, roofs and
lower passages do not count; Rooms counts its enclosing rectangle. Central Building counts
the circular footprint (`PlayableAreaM2`), rather than its bounding square. No saturation at 8.

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

Each template binds its own `DA_Theme_<Name>` (`ThemeLibrary`) with one palette that suits the
place. **A finish always shows the physical material:** every `EERBlockMaterial` row has its own
recognizable surface, so what a block looks like is how it behaves. Palettes never change
geometry, physics, calibration or damage, and their identity enters the digest.

| Template | Palette | Character | Ground |
| --- | --- | --- | --- |
| Central Building | Urban | outdoor brick and concrete building, steel deck interiors | asphalt, grass or snow, from the seed |
| Alleys | Stone | sandstone houses and perimeter, some pale brick houses | three sands |
| Fire Training | Warehouse | concrete courts inside a steel shed | wet concrete |
| Rooms | Tunnels | cast concrete shell, wet floor, timber cover | wet concrete |
| Village | Jungle | timber, corrugated sheet, gray cladding | jungle ground |

Roles stay separate so structures read at distance: shell sheets, structural steel, galvanized
ladders, decks and painted cargo. `DeckSteel` applies to thin Metal slabs only. Alley walls follow
the panel's material and their slabs use `Floor`. Tints of the Alley finishes live in
`setup_map_palettes.py`. Alleys and Central Building keep shade readable with a warm sun, a less
blue sky and a lower hemisphere in the ground's colour (`setup_map_daylight.py`); keep the tint
mild, because a strong cast shifts team colours. `DA_MapThemes` is not bound to any template: it remains only as the input
of older migrations.

## Add a map

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

Production palettes: ten Sketchfab sources / twelve materials from the owner's RR collection
(original ZIPs and credits in private `SourceAssets/SketchfabTextures`), seven CC0 sets in
`SourceAssets/ProceduralTextures` from Poly Haven and ambientCG, plus Factory paint for
containers. Use DX normals, linear masks, sRGB base colour. Check packed ARM channels and
preserve 16-bit grayscale. Never execute supplied scripts. World-position tiling stays
continuous; no runtime displacement. Removed sets must not be reacquired by pipeline reruns.

## Rebuilding

Each template has one setup script (`setup_central_building.py`, `setup_middle_eastern_alleys.py`,
`setup_fire_training.py`, `setup_village.py`, ...) plus versioned migrations (`setup_alley_*`,
`setup_village_*`, `setup_map_variety.py`, `setup_map_palettes.py`). They validate 516 seeds, keep
a verified rollback and save only owned packages. Completed markers stop older
scripts from downgrading. Do not recreate retired levels (Killhouse, Dust, laboratories).

## Verify

`preview-map-sizing.py --verify-seeds 512`, `test-lobby.py --industrial --map-sizing`,
`test-map-playability.py`, `test-map-selection.py`. Native: `EclipseRecoil.Maps.*`, `EclipseRecoil.Destruction.SharedMapThemes`. Every template meets the same
bar: 516 seeds natively, several physical seeds with real capsules, a real pawn on stairs and doors,
damage, regeneration, and initial plus late client snapshots.
