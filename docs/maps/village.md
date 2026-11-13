# Village

Shared rules are in [MAPS.md](../MAPS.md).

**Code:** `ERVillage.cpp`, `ERVillageTerrain.cpp`, `ERVillageWatercourses.cpp`, `ERVillageRing.cpp`, `EROutdoorBackdrop.cpp`.

Inspired by the owner's Shoots references; no Shoots geometry imported. Recipe in `Village` struct:
1–5 central blocks, paired or one self-symmetric origin block, 1–3 platform storeys from
2.4m, budget of 10 floors. Standard blocks are 22–32m × 7–9m. The compact 1v1 and
two-block 2v2 layouts use 21–23m wide blocks and retain their recipe depths. Timber huts
have flat sheet-metal roofs.

- Seeded terrain is flat, raised in the centre or lower in the centre. Away from streams, elevation changes only along the base-to-base axis; both bases have equal elevation. Level foundations carry central blocks, spawn structures and stairs; gentle ramps and level side-wall landings connect them. Ground shelters stay on level terrain.
- The Village recipe's `Watercourses` option lets suitable seeds place streams from one
  side of the arena to the other, bending between bridges and continuing beneath the
  palisade at each outlet, or one continuous ring enclosing the central building
  blocks, with rounded corners and bends between the crossings. The channels use sloping earth banks and the shared pool water surface 10cm
  below grade. The water is about 1.6–2m wide and 50–60cm deep at the bed. Fixed timber
  bridges rise 22cm above grade and have a ramp on both ends.
- Ground blocks enclose three timber bays around two dogleg passages; bays are breachable.
  Passages are 3m wide with a 4m diagonal bend; one straight line across a block stays blocked.
- Huts have 220cm doors and keep 1.8m of deck in front of each long wall; balcony cover has no
  side returns. Block stairs are 2m wide, gallery and lookout stairs 1.8m.
- Floors are boards: plank runs clear of ladder wells are one panel each. Ladders are fixed
  instanced timber.
- Each base has a variant (gallery, covered porch or barracks), a corner lookout, a side shack
  with roof access. Lateral walls 240cm, with standing bypasses recorded in `VillageSideWalls`
  and `VillageSideBypasses`.
- Spawn galleries are 5.6–6.6m deep with starts 2.4m apart in two rows 1.4m apart. Bases have
  a 12m wide, 3m timber screen in front of their spawn bank, with both gallery stairs
  accessible around its ends; the barracks variant leaves both stair landings beside it and 1.4m
  in front of its door.
- Nothing may stand unsupported on a fresh map: decks drop plank and beam stubs beside ladder
  wells, and imported storage shells hold their lowest 30cm through `AnchorHeight`.
  Imported timber shells use two-sided RoughPlanks so their walls are visible from both sides.
- 3v3+ uses at least two blocks; paired 4.8×2.4m L shelters meet a ground-cover quota that upper
  floors cannot satisfy.
- Perimeter: tapered 3.8–4.4m logs with real shot gaps, plus a hidden locomotion-only wall.
- The horizon uses the shared grass finish. No exterior trees. Village uses the shared
  outdoor lighting draw.

## Verify

`test-village.py` (`--offscreen`, `--network`). Native: `EclipseRecoil.Destruction.Village*`.
Run standalone with `-ERVillageProbe -ERVillageLightingGallery` for first-person Metal captures
on the deck and under cover in `Saved/Testing`. In development, use
`-EROutdoorLightingSeed=1` for sunset and `-EROutdoorLightingSeed=2` for standard lighting.
