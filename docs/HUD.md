# HUD and minimap

**Code:** `ERHUDLayout.h`, `ERInterfaceStyle.h`, `ERVitalsHUD.cpp`, `ERWeaponHUD.cpp`, `ERDamageHUD.cpp`, `ERMatchHUD.cpp`, `ERPlayerController`.

## HUD layout

All gameplay HUD and FPS sizes go through `ERHUDLayout`: reference 1280×720 coordinates as
viewport fractions, with `min(width/1280,height/720)` for isotropic text, circles and strokes,
without pixel caps, recomputed from the live viewport. Font: Teko SemiBold through
`ERInterfaceStyle.h`. Measure text before corner anchoring. Canvas only, no widget tree.

- **Vitals** (`ERVitalsHUD`): circular health, armor, stamina icons, 66 units with 4-unit gaps,
  16 sectors per icon (`round(fraction*16)`), 40-unit corner inset. Art in `Art/HUDVitals`.
- **Weapons** (`ERWeaponHUD`): replicated inventory only, bottom to top, with key labels,
  selected item in team tint, 33% opacity when spent. Count is `loaded / (loaded + reserve)`,
  utility total only, none for melee; amber while reloading.
- **Reticle**: ring of ammo dots that reads the real server cone plus replicated owner state.
  It indicates accuracy with a readability margin, not an exact boundary.
- **Top centre**: blue Alpha score, neutral target, orange Omega score, timer; FPS and scene size
  in Teko 9 above it. Online clients show ping, RTT variation, packet loss and server frame
  period below the timer; pending destruction adds snapshot age. Synchronization shows
  verified-page progress before entering combat.
- **Pre-match countdown** (`ERMatchHUD`): "MATCH STARTS IN" and the remaining seconds in the
  centre; the top timer holds the full match duration until the match starts.
- **Kill feed** (`ERMatchHUD`): top left above the kill confirmations, up to five rows for 8s
  with a .6s fade: killer name, cropped weapon icon (`AERHUD::DrawWeaponIcon`, same art and
  crops as the inventory), victim name, both in team colours. TK marks teamkills and a skull
  with a red reticle marks headshots; deaths without a catalog weapon show FELL, DIED or KILLED. Rows involving the local
  player have a darker backing and a white edge. Hidden on the results screen.
- **Kill streak** (`ERMatchHUD`): a centred row at the bottom, with one skull per enemy kill
  starting with the second in the same life. Headshot kills use the red reticle skull. The row
  clears on death or a new round and is hidden with the scoreboard. Both textures are assigned
  in `BP_ERMatchHUD` and imported by `scripts/setup_hud_kill_icons.py`.
- **Kill camera** (`ERMatchHUD`): one row above the killer's head with the name in team colour and
  the killer's replicated health and armor drawn with the vitals icons (`AERHUD::DrawVitalIcon`,
  same art and 16-sector rule, 30 units). It is kept below the top-centre score, timer and network
  status, and hidden if the killer dies.
- **Scoreboard** (Tab, and automatically after the match): two team panels side by side in the
  300–980 reference band, each with a team-coloured header (name, score), K/D/TK
  columns and rows sorted by kills then fewest deaths. Skull Hunt adds SKULLS and sorts by
  enemy skulls collected before the standard tie-breakers. The local player row is tinted, bots are
  tagged, and overflow collapses into "+N more".
- **Inflicted damage**: red HP and green AP text under the reticle, up to five rows, 1s hold,
  .2s fade, attacker only. Rejected, zero and self damage are not shown.
- Damage arcs and edge flashes: see [WEAPON_TUNING.md](WEAPON_TUNING.md).

## Minimap

Top right, 30-unit inset, heading-up with a fixed 40m radius, centred 26m ahead of the owner
(owner at 65% of radius below centre). Terrain, fog, contacts and ticks rotate together each
frame from local controller yaw. A static mipmapped 1024px bird capture is taken after generation,
gated on the local geometry epoch; it shows roofs and ignores later destruction. Five drone icons
sit below it, plus fixed-bearing triangles for enemy and home spawn sides.
Circle tessellation is cached. A summed-area fog mask merges fully explored interior squares
into larger quads; boundary cells retain clipping and feathering, and unknown cells stay hidden.

## Verify

`test-hud.py` (`--render`, `--render --weapons`); the render gallery seeds sample kill feed rows.
Running the gallery with `-ERMatchCountdown=12` also holds forward and jump through the countdown
and logs `ER_HUD_COUNTDOWN_FROZEN moved=<cm>`, which must be 0. Native: `EclipseRecoil.Player.HUDViewportLayout`.
