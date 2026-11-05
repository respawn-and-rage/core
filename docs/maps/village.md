# Village

Shared rules are in [MAPS.md](../MAPS.md).

**Code:** `ERVillage.cpp`, `ERVillageTerrain.cpp`.

Inspired by the owner's Shoots references; no Shoots geometry imported. Recipe in `Village` struct:
1–5 central blocks (20–30m × 6–8m), paired or one self-symmetric origin block, 1–3 platform
storeys from 2.4m, budget of 10 floors.

- Seeded terrain is flat, raised in the centre or lower in the centre. Elevation changes only along the base-to-base axis; both bases have equal elevation. Level foundations carry central blocks, spawn structures and stairs; gentle ramps and level side-wall landings connect them. Ground shelters stay on level terrain.
- Ground blocks enclose three timber bays around two dogleg passages; bays are breachable.
- Each base has a variant (gallery, covered porch or barracks), a corner lookout, a side shack
  with roof access. Lateral walls 240cm, with standing bypasses recorded in `VillageSideWalls`
  and `VillageSideBypasses`.
- Bases have a 3m timber screen in front of their spawn bank, with both
  gallery stairs accessible around its ends.
- Nothing may stand unsupported on a fresh map: decks drop plank and beam stubs beside ladder
  wells, and imported storage shells hold their lowest 30cm through `AnchorHeight`.
- 3v3+ uses at least two blocks; paired 4.8×2.4m L shelters meet a ground-cover quota that upper
  floors cannot satisfy.
- Perimeter: tapered 3.8–4.4m logs with real shot gaps, plus a hidden locomotion-only wall.
- Backdrop: J&E Gray Park panorama (Greg Zaal, CC0), skipped on dedicated. No exterior trees.

## Verify

`test-village.py` (`--offscreen`, `--network`). Native: `EclipseRecoil.Destruction.Village*`.
