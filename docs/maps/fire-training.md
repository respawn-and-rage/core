# Fire Training

Shared rules are in [MAPS.md](../MAPS.md).

**Code:** `ERFireTraining.cpp`, `ERFireTrainingTests.cpp`, `ERFireTrainingProbe.cpp`,
`scripts/setup_fire_training_procedural.py`.

A closed procedural training hall: visible concrete walls and a flat concrete ceiling provide
matching collision. Width, depth and ceiling height are independent of the retired
warehouse modules; length is always at least 1.12 times width and at most 1.5 times width. Team sizing
retains the common floor-area budget. The shell stays closed during gameplay even
when the general ceiling display option is disabled.

- Two protected concrete courts with offset gates, enterable timber barracks,
  3m local roofs and two exterior ladders each. Shared 1–3 spawn zones remain paired
  and every opposing start pair is occluded.
- High steel routes vary in width, wall inset, end position and height. H crossings,
  paired inner bridges, an end-linked ring, double doglegs, offset connections and S routes provide six patterns. Rails follow the deck union so junctions remain open.
  Optional paired islands occupy a separate height plane with two ladder accesses.
- Ground timber rooms and corridors include paired interior through-rooms with offset doors, window openings and optional internal returns. Interiors can run along or
  across the hall. Some layouts include paired suites of two connected offices with a
  shared partition, interior window and a route between offset exterior doors. Destructible cargo uses matte ribbed sheets with smooth perimeter frames and seeded oxide red, green or ochre paint. Cargo and denser staggered groups of six cover types fill the ground; only movement and ladder volumes remain reserved.
  Clear routes, full climbing shafts and ladder exit sweeps are reserved before
  furnishing. Ladders step off at deck level instead of overshooting into overhead decks.
- Elevated projection ≤30%, overhead ≤40%, and a connected region open to the ceiling
  ≥45%, measured on the shared 50cm plan raster. Every elevated area has at least
  two independent accesses and checked standing headroom.
- Roof groups are omitted from minimap capture. Bounded attempts end with a safe
  procedural reference composition. White emissive 60cm square ceiling panels share Rooms'
  owned office finish and follow the existing grid of soft white point lights. Panels have
  no collision and are hidden from the minimap. A neutral
  specified ambient cubemap replaces the blue atmospheric capture; setup is scoped
  to Fire Training through `setup_fire_training_lighting.py`.

The warehouse kit, wet concrete and their import sources are retired. The hall uses the Rooms corridor palette for its walls, floor and ceiling. Shared steel derivatives live in the Industrial library.
`setup_remove_warehouse.py` audits references and verifies an external rollback before removal. Geometry uses generated solids, cell constructions
and procedural steel barriers. Legacy kit properties remain readable for serialized
asset compatibility and do not constrain generation.

## Verify

`verify_warehouse_removal.py` checks all production package dependencies and shared palette bindings.
Native: `EclipseRecoil.Destruction.FireTraining*`, `EclipseRecoil.Maps.TeamSizing`,
`EclipseRecoil.Maps.DensityBudgets`, `EclipseRecoil.Maps.SpawnZones`.
`preview-map-sizing.py --verify-seeds 512` exports measured plans.
`test-fire-training.py` (`--game`, `--render`, `--network`, `--team-size 3`) checks real ladder movement,
closed-shell collision, material rendering, regeneration and initial/late snapshots.
