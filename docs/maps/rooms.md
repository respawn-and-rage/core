# Rooms

Shared rules are in [MAPS.md](../MAPS.md).

**Code:** `ERRooms.cpp`.

Sealed half-turn room graph with no arena floor outside it. 4, 6, 8 or 10 rooms.

- No recon drones or launch cases. Area sizing counts primary room and corridor floors only;
  galleries, switchbacks and the upper crossing do not add area.
- Corridors 3.6m wide, at least 560cm high (legacy saved 340 is raised at runtime), four clipped
  corners, slope ≤1:2. Closed shell prisms provide collision.
- Four-room plans: all rooms at rectangle corners, Alpha northwest, Omega southeast, two attack
  paths, optional central hall-to-hall link. Halls at least twice a base's area, 55%+ real floor
  coverage (60% for the large rectangle), 2m apart, 6m approaches.
- Larger graphs add an upper crossing with galleries at 620cm and 310cm switchbacks.
- No single corridor may disconnect bases. Starts and routes are reserved before furnishing.
- Gameplay bases have 3m opaque timber spawn baffles, separate from sparse furniture.
  Corner bases use an L shape with distinct west and south bypasses to their two mouths;
  larger graphs keep both lateral mouths open. The timber can be breached after spawning.
- Sparse 2.4m timber boards every 5m on runs over 10m; at most two furniture boards per room.
- White emissive 60cm square ceiling panels use the owned `M_OfficeCeilingWhite` finish;
  fixtures are visual only and do not block movement or shots.
- Prototype, off by default: `bEnablePCGExMacroGraph` swaps the straight lower spine for a baked
  central structure. See [PROCEDURAL_MAP_PLAYABILITY.md](../PROCEDURAL_MAP_PLAYABILITY.md#rooms-macro-graph).

## Verify

`test-rooms.py` (`--render --offscreen --network`), `test-rooms-macro.py`, `test-rooms.py --network
--macro`. Native: `EclipseRecoil.Destruction.Rooms*`. The probe walks only the routes a plan has and
logs `ER_ROOMS_WALK_SKIPPED` for the others, because a flat plan has no ramp or stairs. `--macro` runs
seed 3 and requires the pawn on all three walks.
