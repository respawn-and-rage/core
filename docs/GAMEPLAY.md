# Gameplay

Tuning defaults live in `ERServer::Settings()`: that whitelist is the source of truth
for keys, units and ranges. Numbers below describe Standard values.

**Code:** `ERMatch`, `ERCharacter`, `ERMovement`, `ERLadder`, `ERPostureMesh`, `ERPlayerController`, `ERMenu`, `ERBotController`, `ERCorpse`, `ERServerSettings`, `ERLobbyRules`, `ERGameplayMessages`.

## Match

- Two teams, **Alpha** (CT role in reference maps, blue) and **Omega** (T role, orange).
- Team deathmatch: 600s, first to 50, 1 point per enemy kill, 10s intermission.
- Server voting and rotation use production maps only. Rounds consume every template
  once in random order before reshuffling, avoiding a repeat across the boundary.
- A lobby READY needs a strict majority of humans present (`Match.ReadyPercent`).
- Respawn is requested by the player after 3s (fire or jump; the press is consumed). On
  procedural maps the server avoids exposed starts: see
  [PROCEDURAL_MAP_PLAYABILITY.md](PROCEDURAL_MAP_PLAYABILITY.md#spawn-scoring).
  Spawn protection lasts 4s, renders the character opaque gray through one transient
  MID, and ends on expiry or attack. For remote map loads it starts when synchronization
  completes; pending pawns cannot participate in combat. See [ONLINE.md](ONLINE.md).
- Dead characters leave a replicated cosmetic ragdoll that touches only WorldStatic,
  capped at 12, removed by the server after 30s and cleared on round reset.
- No overhead player markers; `TeamMarker` stays hidden for serialized compatibility.
- Modes: Local, Create server, Connect as client. Session setup selects a map, 0–10 bots
  per team (additional to human capacity) and their behaviour.

## Bots

Opt-in only: ordinary hosting has none. Bots share combat, team, scoring and respawn
rules. Modes: stationary **Target**, **Movement** only (never fires), **Patrol/Combat**.
Movement bots run server-only staggered 22s posture cycles and use normal movement
intents, with bounded local obstacle searches (12 directions, 7m, every 2s). Smoke
does not affect bot vision. Stationary targets keep team, skin and start across respawns.

## Movement

| Action | Value |
| --- | --- |
| Capsule | 185cm standing, 68cm wide, eye 170cm; half height 92.5, crouched 62.5 |
| Walk (Shift) / run / sprint | 7.5 / 15 / 27 km/h |
| Crouch / crawl / posture transition | 150 / 82.5 / 67.5 cm/s |
| Ladder, light item / heavy weapon stowed | 216 / 270 cm/s |
| Stamina | 15s of sprint, 20s full recovery from empty, also while running |
| Auto step / vault / climb | 30 / 80 / 150 cm above current feet |
| Crouch / prone transition | 0.4s / 1.1s |
| Fall damage | safe ≤3m, lethal from 11m; bypasses armor, authority only |

Sprint: double-tap W within 300ms and hold, or hold E with W. Partial stamina can be
spent by a fresh gesture; a held key never auto-restarts. Hold Ctrl/C to crouch, double
tap to latch prone, press again to rise; prone entry passes through crouch before
lowering to the floor. Standing waits for ceiling clearance. Forward
input vaults; Space+forward climbs, also while airborne. `FindLedge` handles parapets
down to 20cm wide. When forward input targets a valid ledge within the ready
weapon's reach, long weapons lower for the approach. The body closes the gap
with normal collision before starting the mantle.

Prone reserves a horizontal body footprint before lowering. Movement sweeps that
footprint as well as the central capsule; turns sample intervening orientations
to stop the head and feet entering nearby cover. These checks run on the predicted
owner and authority paths. Weapon presentation checks are in [WEAPONS](WEAPONS.md).

Ladders use custom movement mode 2 (1 is mantle, 3 is the ladder-to-roof transition).
W/S climbs, Space detaches. Pistols, knives and hand grenades stay usable; other
weapons stow when their forward reach meets a nearby usable ladder, allowing
the approach before attachment, and return once clear of the ladder. Walls in front of
the ladder prevent this approach stow. Rungs must physically survive. On entry,
the capsule sweeps toward the rung line at a bounded speed before climbing.

Traversal uses `UERMovement` and saved moves. `IA_Look` scalar is 0.18. Camera bob is
cosmetic. FPS arms, weapon and muzzle flash render as FirstPerson primitives.
Stable capsule dimensions skip redundant collision and overlap updates; posture blending,
floor checks and ceiling clearance remain active.

## Controls

Mouse smoothing is disabled in the project input settings. Mouse acceleration defaults
to zero and can be adjusted in the menu. Saved mouse preferences are preserved.

WASD, mouse, left click fire, right click scope, Space jump/climb/respawn, Shift walk,
double-tap W or hold E sprint, Ctrl/C crouch and prone, R reload, 1–8 / F / G / H or
wheel select (the wheel has a 250ms quiet period), Tab scoreboard, Esc menu, P screenshots.
P saves the current viewport (including HUD) plus front, right, left, back and top
views of the possessed character as PNGs at the viewport resolution. The screen is
captured after rendering through Unreal/Slate. External views render over successive
frames with the main view's resolution scaling and post-processing. LDR capture
targets use linear storage with explicit display gamma to avoid a second sRGB
encoding when writing the PNG. External views use the character facing at P,
without moving the gameplay camera. Front, back and front-quarter cameras are 3m
above the ground; right/left sit at +/-60 degrees from the front (120 degrees apart).
Top is 10m above the ground, aimed vertically at the character center, with the
character facing from right to left in the image. Nearby geometry can obscure the
views. Each set has a unique folder under `Saved/Screenshots`;
a local on-screen message shows its absolute path and reports incomplete saves.
Menus, HUD and labels are English; the language is forced at GameInstance init.

## Server settings

**Start Server → Gameplay settings**: categories, basic/advanced fields, per-value
resets, Standard reset and named presets. Saved in GameUserSettings under
`RespawnAndRage.ServerRules`, `.ServerPresets`, `.ServerPreset.<name>`. Dedicated and
direct launches can set `[RespawnAndRage.ServerRules]` in Game.ini:

```ini
Move.Sprint=27
Smoke.Radius=6
Weapon.HE.ExplosionRadius=6
Kit.rifleman.offense.HE=3
```

Starting a session copies the draft to an immutable snapshot, which `AERGameState::
ServerRules` replicates. Weapons derive private effective copies through `Tuned()`;
authored assets stay untouched. Kit values are final carried quantities. Client saved
preferences never apply to another server. Configuration exposes existing mechanics
only; it adds no slots or systems. Material rules travel in the cell descriptor.

`Map.ExpectedTeamSize` (1–16, default 2) is fixed at creation and shown read-only in
the lobby. See [MAPS.md](MAPS.md).

## Gameplay messages

`Config/GameplayMessages/respawn-and-rage-gameplay-messages.en-US.json` is the owner's
catalog, preserved byte for byte (72 events, 720 variants). `UERGameplayMessages` loads
it and dispatches the 40 connected events; the rest need extra telemetry.

Every confirmed kill sends a private reliable RPC to both players, bypassing
cooldown and priority. Teamkills, self-inflicted and unattributed deaths are labelled.
Canvas shows up to three confirmations for 6s plus one bonus toast, top left, silent.
Runtime durations double the catalog values. Shuffle bags use a separate RNG, per
player/event/audience. Thresholds keep the catalog suggestions: 40m longshot, ≤10HP,
5/10/15/20/25 streaks, four unanswered kills, 4s rapid chain gap.

The death camera moves from above the body toward the killer, zooming 90→60° and
framing the body at about 65% height, with sphere sweeps. It is purely cosmetic.

## Original scope

Excluded unless requested: pickups, corpse loot, matchmaking, health regeneration,
automatic respawn, bomb or capture modes. Competitive balance needs real playtests.

## Verify

| Change | Run |
| --- | --- |
| Any gameplay | `test-network.py`; add `--accuracy`, `--skins` as relevant |
| Movement, posture, stamina | `test-movement.py` (`--posture-only --render`), `test-player-mechanics.py` |
| Server settings | `test-server-settings.py` (`--regressions`, `--network`, `--render`) |
| Lobby, rotation, capacity | `test-lobby.py`, `test-round-rotation.py`, `test-capacity.py`, `test-admission.py --bots`, `test-session.py` |
| Bots | `test-bots.py`, `test-bot-loadouts.py` |
| Messages | `test-gameplay-messages.py`, `test-network.py --render` |

Prone footprint and thin-wall regression: native
`EclipseRecoil.Movement.BodyClearance`; floor and weapon contacts across all skins:
`test-world-poses.py --prone --wall`.
