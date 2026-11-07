# Middle Eastern Alleys

Shared rules are in [MAPS.md](../MAPS.md).

**Code:** `ERAlleyDistrict.cpp`, `ERAlleyHouseModule.cpp`, `ERAlleyCover.cpp`, `ERAlleyUnderpass.cpp`, `ERAlleyDestruction`, `ERDustTown.cpp`.

Original seeded districts informed by a user minimap and *Making of Dust 2*. Generates 2–4 main
approaches, winding streets, plazas, and 0–2 central walls with 2–4 arches. Separators have narrow firing slits below a walkable crown. Access is paired at the lateral
ends through 6m annex roofs, with separate ground-to-roof and roof-to-crown ladders;
aprons are reserved before lot partitioning. The 3v3 profile extends house lots into the center,
uses smaller market courts, staggered central approaches and a paired 3m cross alley with
seeded elbows instead of the open central square. Unequal lot divisions vary house frontages.
Market courts stay open at ground level.
Short elevated decks span the gap between roof edges; exterior stair landings meet the roof slab at its edge. Flat covers use square paving with visible joints. Some elevated decks use destructible timber. Teams use sand terraces up to +1.8m. Gameplay profiles reserve opaque masonry spawn
screens inside the base courts, with the starts behind them and lateral dogleg exits.

- `Town.ProceduralHouses`: native lime-coloured brick masonry with Brick resistance, 160×240cm doors, flat 15cm slabs, windows, flat
  roofs flush or with 25cm-high, 40cm-wide rims and 180cm gaps. Exterior switchback stairs have solid rear landings to the lot edge so drops land on the stair route. One hollow
  skin per house; floor and roof skins are separate bounded panels, not merged into wall panels.
  Keep parent structure tags and support contacts. Stone is reserved for permanent
  structures and selected cover.
  `setup_alley_lime_brick.py` binds the existing shared lime-coloured finish.
- Lower passage at -3.6m with paired ramps and two breachable brick road sections, over real
  terrain holes, with clear approaches through the market courts. Low cover: masonry 65–90cm, timber crates 110cm.
- Watchtowers 700–900cm with ladders and notched sniper parapets. Their walls, decks and
  parapets are destructible 10cm Brick with ordinary material HP; only ground rows anchor.
  Removing the supporting walls can collapse the deck. Ladder metal remains independently
  grounded. Perimeter, terrain and stair foundations stay permanent. Stair treads retain
  fixed collision even when their finish is shared with destructible roof slabs.

## Verify

`test-dust-template.py` (`--architecture`, `--render`, `--network`, `--plans`, `--passage`, `--modules`). Native: `EclipseRecoil.Destruction.MiddleEastern*`.
