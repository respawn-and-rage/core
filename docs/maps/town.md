# Town

**Code:** `ERUrbanTown.cpp`, `FERUrbanTownRecipe` in `ERMapTemplate.h`,
`scripts/setup_town.py`, `ERUrbanTownProbe.cpp`.

`DA_Town` / `L_Town` is an original urban layout inspired by the supplied Chinatown
overhead reference. The open-air ring bends around angled facades, recesses and
projecting closed lots. Compact layouts have one broad, stepped building mass per
half, with paired closed service buildings that split the corner courts into
staggered lanes. Larger layouts add more parcels. A bent covered passage joins the wings,
with outdoor courts in their recesses. Doors sit on different axes and deeper
wings add a third room, creating successive turns through the complex. Screened
spawn pockets sit at opposite ends of the ring.

All initial circulation stays on the ground, with 12cm pavement thresholds. One to
three storeys give the blocks height and overhead shelter; upper facades and concrete
closures are fixed, inaccessible architecture. Ground-floor brick/concrete walls and
drywall partitions use calibrated destructible cells. Roofs remain visible in play
and use the normal minimap roof tag. There are no roof routes, stairs or ladders.
Fixed window frames and glazed panels mark the upper facades; metal awnings mark
the ground-floor portals. This version uses flat asphalt terrain and warm daylight.

Closed central wings, projecting perimeter lots and triangular corner recesses are
permanently inaccessible. Their clipped polygon/rectangle union is excluded from the measured ground area; sizing
fits the net playable footprint, rather than the outer rectangle. The plan exporter
shows these exclusions in black.

The seed varies bounds, parcel widths, setbacks, courtyard cuts, wing orientation,
masonry and storeys; the area budget adds
blocks and screens for larger teams. Every sampled building and route has a half-turn
mate. The optional mirror mode uses the shared fold. Team sizing changes dimensions
and counts rather than resizing meshes, doors, materials or cells.

The dedicated Town palette reuses existing asphalt, brick, concrete, wood, stone,
metal and glass. The setup owns only `DA_Town`, `DA_Theme_Town` and `L_Town`, keeps a
verified rollback and hashes all other map packages. No reference-game geometry or
new third-party assets are imported.

## Verify

Compile the editor, then run `scripts/setup_town.py` through Unreal Python.
`python3 scripts/test-town.py` checks 516 seeds, every team size, cell budgets,
physical standing routes, spawn occlusion, damage and regeneration.
`--game` checks a real pawn through a shop and outside street; `--network` adds
initial and late localhost clients; `--render --offscreen` captures three Metal views.
These are local regression probes, not physical-LAN tests or balance playtests.
