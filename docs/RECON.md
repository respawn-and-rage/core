# Recon

Team vision, fog of war, drones and graffiti.

**Code:** `ERRecon.h/.cpp`.

## Recon

Owner-only `UERReconComponent` snapshots carry each team's 64×64 explored mask and currently
visible contacts: never derive enemy markers by enumerating client pawns.

- Server sight at 4Hz: living players and bots (120°, map extent), active drone (360°, 42m
  horizontal footprint), through real geometry, two head/torso samples.
- Contacts expire on lost sight (1s HUD stale timeout); terrain persists within a round. Late
  join and respawn keep fog and stock; round reset or generation epoch clears them.
- Smoke hides sightings and new terrain by integrating 48 density samples along the ray, with
  optical depth 1 as threshold. Keep shader and `ReconDensity` coefficients aligned. Bot AI
  ignores this.
- Rooms disables launch cases and drones; player vision remains active.
- Each team has one indestructible Pelican case and five drones in total, including the first.
  An authorized bullet hit destroys a drone; replacement after 30s. Flight evaluates replicated
  parameters against server time. Case lid plays original FBX keys from `LaunchAt`.
- Five source fragments on each hit, capped at 24, WorldStatic only, cleared on epoch and exit.
- Graffiti: sparse team wall decals fade toward a 30% central exclusion, 36 per team.
  Drone underside carries the team emblem as an attached decal.

Sources: S9 drone (blockv_lb), Pelican Case (polbrainstorm), CC BY 4.0, private in
`SourceAssets/ReconDrones`, runtime under `Art/Recon`.

## Verify

`test-recon.py` (`--automation`, default, `--render`), `test-team-graffiti.py`. Native: `EclipseRecoil.Recon.*`.
