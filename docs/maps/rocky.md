# Rocky

**Code** — `ERRockyGround.cpp`, `ERBoulders.cpp`, `ERTerrainField.cpp`,
`ERTraversal.cpp`, `scripts/setup_rocky.py`.

`DA_Rocky` and `L_Rocky` generate a circular arena inspired by the rocky cover and
elevation changes in the supplied Lunacy reference. The divide runs from rim to rim
in two offset rock tongues, each rising from a tunnel shoulder. The climbing
route traverses the flanks of the tongues to reach covered overlooks beneath
the ten-metre rock crowns. The rock mesh forms the walkable terraces, with a
lower terrain surface supporting the central formation. Slabs
break up the faces and form ledges on the divide and near the bases. The two side
tunnels turn sharply within the same formation. A tall, continuous rock ring
blocks escape even from the high ridge.

Larger arenas select their terrain from the seed. A high saddle joins the ridge
across the middle. In the gorge, two banks surround a winding ford. Offset Hills
places two climbable summits on opposite diagonals, with a low saddle between
them. Terraced Basin drops roughly six metres below the spawn courts through concentric rock
terraces, with routes to raised flanks. Diagonal Ridge joins opposite sides of
the arena along an oblique crest with low cuts. Central Massif has a traversable
rock cavity with two lateral mouths. Broken Plateaus places four mesas in pairs,
with two elevated stone connections. Rock ribs interrupt the direct centre line.
The two side tunnels and the perimeter remain available across these layouts.
Hold Space and move into an inclined rock face to climb it with hands free.
The enclosing rock wall remains unclimbable.

A small angled lunar outpost overlooks each team's approach. It has a firing
aperture and a ladder to a lookout with cover on three sides. The walls and parapets use
destructible cells. The shared open-ground pass adds boulders to the natural cover.

The height field and perimeter stay fixed. Interior crags and boulders retain their
faceted collision and use the shared Stone damage rules. Hits remove surface cells
and leave the remaining faces attached. Layout queries use an opaque inner solid for sight and an outer
bound for standing clearance. The builder pairs starts, rock, routes and outposts with
the recipe's half-turn symmetry. Staggered spawn positions face overlapping rocks
with angled paths around either end. Closed rock is deducted from the disk's area.

Rocky uses fractured stone and matte metal from the shared surface catalog.
It is selectable in Play Offline and enabled by default for the server lobby vote
and round rotation. At night, six floodlight banks rise above the rock
ring and illuminate sectors inside the arena.

**Verify** — Run `python3 scripts/test-rocky.py` and `python3 scripts/test-rocky.py --network`.
Use `python3 scripts/test-rocky.py --render --team-size 3` for Metal captures of the
arena, tunnel, trail and summit.
Export dimensioned plans with
`python3 scripts/preview-map-sizing.py --templates Rocky --team-size 3 --seeds 3 5 7919 4 42 1 0`.
Plans show the measured ground elevations and rock cores. The native tests check the
area, pairing, route access, spawn screening, physical collision and regeneration.
The game probe walks a tunnel, the crest trail and the rock shelves before climbing
the outpost ladder. It also checks wading speed when the arena has a ford.
