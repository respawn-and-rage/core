# Central Building

Shared rules are in [MAPS.md](../MAPS.md).

**Code:** `ERCircularCentralBuilding.cpp`, `ERCentralTerrain.cpp`, `ERMapSymmetry.cpp`,
`ERCentralBuilding.cpp`, `ERIndustrialFence`.

The playable ground has mirrored teams across Y=0. Open and railing layouts are circular;
the wall layout is an ellipse stretched along the spawn axis, moving both starts farther from the centre while retaining the same area budget. The seed chooses:

- A central building on a raised, flat hilltop, with sloping approaches on every side.
- A building on the west side and a separate hill on the east side. Both teams have
  equivalent approaches to both features. Compact arenas reduce the lateral offset
  to keep the complete building inside the circle.

The seed retains its hill configuration, one of four building profiles (rectangle,
H, cross or stepped wings), and one of three crossing rules across geometry retries.
The light concrete villa stays within 18 × 24m even on large arenas. Width and
depth vary independently. A solid concrete floor follows the ground-floor plan
and sits 5cm above the outdoor terrain. Main entrances sit on the west-facing
side walls of each half. Every window has destructible clear glass. Wide front
bays have low sills and multiple panes separated by concrete mullions.

The villa has two storeys in 1v1 and three from 2v2 onward. Interior stairs
connect every storey, with doors onto recessed first-floor terraces. Stairs use
20cm risers and 30cm treads, and main doors are 240cm wide.

Some open central-hill layouts have one wide central pool; other central-hill layouts can have two mirrored recessed courtyard pools. Their solid concrete
floors are 20cm below the water at both ends and 90cm below it in the centre.
The clear water surface has no collision, so players walk on the basin floor.
Water detection stays within 95cm below the surface, excluding rooms beneath roof pools.
Wading reduces movement speed progressively from 90% near the surface to 50% at 100cm depth.
The wall separator extends below the local terrain grade so its underside stays hidden on slopes.
`setup_pool_water.py` creates the owned translucent water material in Unreal.
Open layouts on the central hill can have a compact roof pool even in 1v1. Its waterline is flush with the roof, its basin extends 80cm into the slab, and mirrored square corner treads lead from the rim to the floor. Paired exterior tread stairs reach the roof without an interior roof opening.

- Open: connected ground routes, with no central separator.
- Wall division: 340cm exterior Brick separator, above the default jump and climb
  reach. Inside the villa it meets the intact first-floor ceiling, with no crossing stairs or opening in the slab.
- Railing division: the 100cm concrete plinth and steel module match the outer fence height; the divider meets a small side building at its outer walls.
  A ground route crosses the seam through its two connected rooms.
- The wall division also has paired exterior stairs on the flank, away from
  the main building.

Lateral-hill layouts include 2–4 enterable secondary Brick or Concrete buildings, with seeded plain, parapet or entrance-canopy profiles. Each has
an open doorway, destructible roof and a level terrain foundation. Flank routes
pass beside them. The shared 1–3 spawn-zone generator retains screened, paired
starts; every opposing start pair is occluded on the intact map.

Outdoor cover combines straight walls, L corners, U pockets, staggered block pairs
and deep timber blocks. Seeds vary the number of exterior bands, dimensions, standing/crouching heights, quarter-turn
orientations, Brick/Concrete/Wood materials and placement. Groups within one generation use different materials. Every group has
paired geometry, a level foundation, a clear rear approach and a checked hiding site.
Some straight, L and U covers have mirrored rear firing steps: two 30cm treads and
a 90cm landing with three 20cm rises, reaching a 60cm firing platform. Ground hiding
sites remain clear. Standing capsule support and firing clearance are checked; the
pawn probe walks these flights and fires over their cover.

Terrain is a synchronous collision mesh with a 256-sided circular or elliptical edge and radial
spacing at most 75cm. Spawn and route heights use the same triangles as collision.
The perimeter uses visible concrete plinths and original-height steel fence courses;
there is no rectangular floor underneath or taller invisible perimeter wall.
Team sizing budgets the rounded ground area, excluding upper storeys and roofs.

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
