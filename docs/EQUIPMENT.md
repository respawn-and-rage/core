# Equipment

Loadouts and utility: grenades, launcher, smoke, Claymores.

**Code:** `ERLoadout`, `ERGrenadeBallistics.h`, `ERGrenadeExplosion`, `ERSmokeCloud`, `ERClaymore`; quantities in `Config/DefaultGame.ini`.

## Loadouts

Five stable presets, each with `gunner`, `offense`, `defense` variants, resolved by
`ERLoadouts`. `Config/DefaultGame.ini` holds base quantities and skin names; native
fallbacks must match. Rifleman/Offense is the safe default.

| Preset | Alpha skin | Omega skin |
| --- | --- | --- |
| breacher | Wraith · Assault (1) | Drongo · Jackal (1) |
| rifleman | Belica · Arctic Patrol (6) | Drongo · Raider (0) |
| marksman | Wraith · Patrol (2) | Gadget · Mechanic (6) |
| sniper | Wraith · Officer (0) | Gadget · Patrol (7) |
| grenadier | Belica · Armored (8) | Gadget · Field Engineer (8) |

The resolver doubles carried totals: firearm loaded counts stay, extra rounds go to
reserve; utility stacks double directly (offense 4 HE + 2 smoke, defense 4 smoke +
2 Claymores, gunner none). Config values are base quantities, not doubled ones.
Inventory and skin resolve together at spawn; the menu reads the replicated server
catalog, and Apply affects the next spawn only. `?ERLegacyArsenal=1` and
`ERFullArsenalTest` are explicit test fixtures, not play modes.

## Grenades, launcher and smoke

- HE and launched HE: 140 peak, full through 2m, quadratic to zero at 6m, three
  WorldStatic exposure samples against intact cover. Armor absorbs per `ArmorRatio`.
- Fuse 3.5s from press for all hand grenades and launcher rounds; they bounce and
  tumble (server bounce .18–.25, friction .4). Cooking can detonate in hand; death drops
  live ordnance. Launcher fires `AERWeapon::LauncherLobDegrees` (12°) above aim, 1750cm/s; scope multiplies horizontal
  speed ×1.5. Contact with a pawn: launched HE deals 25% base damage once per target.
- Hand throw (`ERGrenadeBallistics`): `normalize(aim+Z*.35)*ProjectileSpeed` (880cm/s)
  with horizontal ×1.2, scaled by `1+.2*clamp(pitch/45,-1,1)`, then character velocity
  added. Origin is yaw-relative `(-14,24,-4)` from the eye, swept against cover.
- Armed fuse projectiles have a query-only `ShotTarget` (6cm thrown, 12cm held). Only
  positive server bullet damage detonates them. Blasts chain through an explicit
  server path with each hop's intact-cover snapshot.
- Structural blast is separate: radius 350cm, damage ×3 over the base profile.
- Smoke (`AERSmokeCloud`): server-owned, 24s lifetime, 2.2s growth, 4s fade, 4.4m
  radius, 4m height, 16×16×8 connected containment grid, four clouds. Team-tinted from a
  snapshot taken at arming. Bullets cut temporary wakes (2s recovery), moving ordnance
  smaller ones, blasts clear 400cm until expiry. Never changes aim, damage or bots.
  Containment watches collision chunk revisions intersecting the cloud bounds, including a
  conservative sweep margin. Distant damage does not rebuild the grid; local destruction,
  regeneration and room transforms invalidate it.

## Claymore

Fire starts a server 2.2s placement (deposit at 1.3s, rise at 1.6s), 65cm ahead of the
feet, blocking movement and selection. It is consumed once, on valid deposit.
Mines belong to the session PlayerState, survive death, arm after 2s, expire 60s
after arming, with no count cap. Trigger radius is `damage radius × TriggerPercent`
(8m × 25% = 2m), 60° sector, capped by the 3D blast radius, with collision-free
team-colored rays stopping at cover. Damage is 140 peak with ordinary quadratic falloff,
110° forward cone, cover and armor: no guaranteed kill, zero rear damage. Surface loss
disarms; logout, team change, round end and regeneration clear mines.

## Verify

`test-loadouts.py` (`--network`, `--render`, `--network --mines`), `test-grenades.py` (`--combat`,
`--render --audio`). Native: `EclipseRecoil.Loadout.*`, `EclipseRecoil.Weapons.HandThrow`.
