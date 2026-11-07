# HUD and minimap

**Code:** `ERHUDLayout.h`, `ERInterfaceStyle.h`, `ERVitalsHUD.cpp`, `ERWeaponHUD.cpp`, `ERDamageHUD.cpp`, `ERPlayerController`.

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

`test-hud.py` (`--render`, `--render --weapons`). Native: `EclipseRecoil.Player.HUDViewportLayout`.
