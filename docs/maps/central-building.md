# Central Building

Shared rules are in [MAPS.md](../MAPS.md).

**Code:** `ERCircularCentralBuilding.cpp`, `ERCentralTerrain.cpp`, `ERMapSymmetry.cpp`,
`ERCentralBuilding.cpp`, `ERIndustrialFence`.

The playable ground is circular, with mirrored teams across Y=0. The seed chooses:

- A central building on a raised, flat hilltop, with sloping approaches on every side.
- A building on the west side and a separate hill on the east side. Both teams have
  equivalent approaches to both features. Compact arenas reduce the lateral offset
  to keep the complete building inside the circle.

The seed retains its hill configuration, one of four building profiles (rectangle,
H, cross or stepped wings), and one of three crossing rules across geometry retries.
Width and depth vary independently within team-sized bounds; floors, 20cm risers,
30cm treads and 240cm door openings keep their physical dimensions. The main entrances
sit on the west-facing side walls of each half, so they do not face one another.
Windowed outer walls, room wings, balconies and roof parapets follow the profile.

- Open: connected ground routes, with no central separator.
- Climbable wall: 120cm destructible Brick separator, with paired steps inside the
  building, an open headroom well in the first deck, and mantle access elsewhere.
- Fixed division: permanent 3m opaque barrier; ground crossing is blocked, while
  the main stairs and first upper landing join the teams above it.

Lateral-hill layouts include 2–4 enterable secondary Brick buildings, each with
an open doorway, destructible roof and a level terrain foundation. Flank routes
pass beside them. The shared 1–3 spawn-zone generator retains screened, paired
starts; every opposing start pair is occluded on the intact map.

Outdoor cover combines straight walls, L corners, U pockets, staggered block pairs
and deep timber blocks. Seeds vary dimensions, standing/crouching heights, quarter-turn
orientations, material and placement within two exterior bands. Every group has
paired geometry, a level foundation, a clear rear approach and a checked hiding site.
Some straight, L and U covers have mirrored rear firing steps: two 30cm treads and
a 90cm landing with three 20cm rises, reaching a 60cm firing platform. Ground hiding
sites remain clear. Standing capsule support and firing clearance are checked; the
pawn probe walks these flights and fires over their cover.

Terrain is a synchronous collision mesh with a 256-sided circular edge and radial
spacing at most 75cm. Spawn and route heights use the same triangles as collision.
The perimeter uses visible concrete plinths and original-height steel fence courses;
there is no rectangular floor underneath or taller invisible perimeter wall.
Team sizing budgets the circular area, excluding upper storeys and roofs.

`CircularTerrain=false` retains rectangular authored studies and their crossing rules
in `ERCentralBuilding.cpp`: open yard, destructible wall with timber crossings, or
fixed fence crossed through elevated routes. `MapCrossing` can force the three rules for non-shipping authoring probes.

## Verify

Native: `EclipseRecoil.Maps.CircularCentralBuilding`, `EclipseRecoil.Maps.TemplateMirror`,
`EclipseRecoil.Destruction.CentralBuilding*` (rectangular studies).
`preview-map-sizing.py --verify-seeds 512` exports measured plans.
`test-industrial-map.py` (`--game`, `--render --gallery`, `--network`) exercises real
pawn stairs, rifle and blast damage, cover breaches, round regeneration, and initial
plus late client snapshots. `--finish-gallery --offscreen` captures intact Metal rendering.
