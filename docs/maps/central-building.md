# Central Building

Shared rules are in [MAPS.md](../MAPS.md).

**Code:** `ERCircularCentralBuilding.cpp`, `ERCentralTerrain.cpp`, `ERMapSymmetry.cpp`,
`ERCentralBuilding.cpp`, `ERIndustrialFence`.

The playable ground is circular, with mirrored teams across Y=0. The seed chooses:

- A central building on a raised, flat hilltop, with sloping approaches on every side.
- A building on the west side and a separate hill on the east side. Both teams have
  equivalent approaches to both features. Compact arenas reduce the lateral offset
  to keep the complete building inside the circle.

The building has 1–3 storeys, 3m clear ceilings, 20cm decks, windowed front facades,
room wings, a central hall and paired stairs with 20cm risers and 30cm treads.
Ground routes run through the building and along the hill flank. Cover has level
foundations that blend into the terrain. Ten starts per team sit behind mirrored 3m opaque,
destructible screens. Both sides remain open, with ground routes doglegging around their
ends. Every initial opposing pair is occluded; screens may be breached during play.

Terrain is a synchronous collision mesh with a 256-sided circular edge and radial
spacing at most 75cm. Spawn and route heights use the same triangles as collision.
The perimeter uses visible concrete plinths and original-height steel fence courses;
there is no rectangular floor underneath or taller invisible perimeter wall.
Team sizing budgets the circular area, excluding upper storeys and roofs.

`CircularTerrain=false` retains rectangular authored studies and their crossing rules
in `ERCentralBuilding.cpp`: open yard, destructible wall with timber crossings, or
fixed fence crossed through elevated routes. `MapCrossing` affects those studies only
under the non-shipping `-ERIndustrialMapProbe`.

## Verify

Native: `EclipseRecoil.Maps.CircularCentralBuilding`, `EclipseRecoil.Maps.TemplateMirror`,
`EclipseRecoil.Destruction.CentralBuilding*` (rectangular studies).
`preview-map-sizing.py --verify-seeds 512` exports measured plans.
`test-industrial-map.py` (`--game`, `--render --gallery`, `--network`) exercises real
pawn stairs, rifle and blast damage, cover breaches, round regeneration, and initial
plus late client snapshots. `--finish-gallery --offscreen` captures intact Metal rendering.
