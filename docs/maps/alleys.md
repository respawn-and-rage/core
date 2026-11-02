# Middle Eastern Alleys

Shared rules are in [MAPS.md](../MAPS.md).

**Code:** `ERAlleyDistrict.cpp`, `ERAlleyHouseModule.cpp`, `ERAlleyCover.cpp`, `ERAlleyUnderpass.cpp`, `ERAlleyDestruction`, `ERDustTown.cpp`.

Original seeded districts informed by a user minimap and *Making of Dust 2*. Generates 2–4 main
approaches, winding streets, plazas, and 0–2 central walls with 2–4 arches. Separators have narrow firing slits below a walkable crown. Access is paired at the lateral
ends through 6m annex roofs, with separate ground-to-roof and roof-to-crown ladders;
aprons are reserved before lot partitioning. Market courts stay open at ground level.
Short elevated decks span actual gaps between nearby roofs; some use destructible timber. Teams use sand terraces up to +1.8m.

- `Town.ProceduralHouses`: native masonry, alternating Stone and weaker pale Brick pairs, 160×240cm doors, flat 15cm slabs, windows, flat
  roofs flush or with 25cm-high, 40cm-wide rims and 180cm gaps. Exterior switchback stairs. One hollow
  skin per house; floor and roof skins are separate bounded panels, not merged into wall panels.
  Keep parent structure tags and support contacts.
- Lower passage at -3.6m with paired ramps and two breachable brick road sections, over real
  terrain holes, with clear approaches through the market courts. Low cover: masonry 65–90cm, timber crates 110cm.
- Watchtowers 700–900cm with ladders. Perimeter, terrain and stair foundations stay permanent.

## Verify

`test-dust-template.py` (`--architecture`, `--render`, `--network`, `--plans`, `--passage`, `--modules`). Native: `EclipseRecoil.Destruction.MiddleEastern*`.
