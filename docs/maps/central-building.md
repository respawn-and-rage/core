# Central Building

Shared rules are in [MAPS.md](../MAPS.md).

**Code:** `ERCentralBuilding.cpp`, `ERIndustrialFence`.

Reference circulation from Lake, Safehouse and Bank, adapted: no Valve assets or outlines.
Crossing is always sampled from the seed: open yard, 3m destructible brick wall with paired timber
bridges and stairs, or fixed fence crossed by the roof or side buildings. `MapCrossing` works only
under the non-shipping `-ERIndustrialMapProbe`.

- 1–3 storey core with 3m clear ceilings and 20cm slabs, windowed facades,
  paired room wings off a central hall, and accessible upper-floor masonry balconies. Circulation uses stairwells (20cm risers, 30cm treads) and paired
  exterior ladders. OpenYard may shift the core ±8m on X, about its own `SymmetryCenter`, and
  step upper floors back 5m / 2m with terrace ladders.
- Secondary buildings and towers vary 1–3 floors, materials and yaw; compact ones use
  ladders through floor hatches. Their timber floors and supports use the cell graph.
- Containers are hollow reinforced steel cells. Tanks and barrels are fixed.
- Barriers: 100cm concrete plinth plus one 127cm original steel course, from the single
  module of Nullified's Aluminium Fences (CC BY 4.0). Collision ends at the visible top; shots
  pass through apertures. Roof jumps may cross it, by design.
- Multiple bands of destructible L/U pockets, screens, crates and containers. Entrances and
  hide spaces are validated with real capsules and eye rays.

Compact plans drop secondary buildings first. PCG and PCGEx 0.79.2 are for route
analysis and an authoring preview only: runtime collision stays synchronous.

## Verify

`test-industrial-map.py` (`--game`, `--render --gallery`, `--network`). Native: `EclipseRecoil.Destruction.CentralBuilding*`.
