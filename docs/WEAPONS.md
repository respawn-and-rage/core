# Weapons

**Code:** `ERWeapon.h/.cpp` (`UERWeaponData`, `AERWeapon`, `AERProjectile`), `ERWeaponBallistics.h`, `ERShotTrace`, `ERShotHistory`, `ERShotEffects`, `ERWounds`, `ERWorldWeaponPose`, `ERLoadout`; fire entry point in `ERCharacter`.

## Add or change a weapon

**Number change only:** edit the owning value, then run its numeric setup script from
Rebuild. Worksheet rows are in [WEAPON_TUNING.md](WEAPON_TUNING.md); kit quantities in
[EQUIPMENT.md](EQUIPMENT.md). Nothing else below applies.

**New weapon:** slots are positional and hardcoded, so update all of these together.

1. **Source:** original and licence in private `Assets/weapons/SourceAssets`, credit in
   `Branding/Credits.txt`.
2. **Art and data:** add a profile in `scripts/import_sketchfab_weapons.py` and
   `bake_sketchfab_weapons.py`, which create `DA_<Key>` in `Weapons/Sketchfab`. Add it to
   `ReferenceArsenal` on `BP_ERCharacter`, through Unreal. Stats come from the worksheet JSON
   or from project values in that import script.
3. **Slot catalog:** `ERLoadouts::WeaponAssetName` and `WeaponName` in `ERLoadout.cpp` (arrays
   and the `I<11` bound); `Resolve` there if a kit carries it.
4. **Input:** `AERPlayerController::SetupInputComponent` and `SelectSlot`.
5. **HUD:** `ERWeaponHUD.cpp`: icon rects, `IconScales`, the `I<11` loop, key labels; art through
   `scripts/setup_hud_weapons.py`.
6. **Server settings:** the weapon ID list in `ERServerSettings.cpp`, with its `Group` label and
   the category branches below it.
7. **Key lists in scripts:** `presentation_labels.py`, `scope_profiles.py` when scoped,
   `setup_world_weapon_pose.py` for firearms, `setup_empty_trigger_audio.py`,
   `prepare_audio_pass.py` plus a palette ([AUDIO.md](AUDIO.md)).
8. **Grips:** `retarget_weapon_hands.py`, `fix_weapon_muzzles.py`.
9. **Checks that count slots:** `verify_sketchfab_weapons.py` (`complete_ten_slot_catalog`),
   `ERLoadoutTests.cpp`, `ERServerSettingsTests.cpp`.
10. **Docs:** the Arsenal table below.

Slot order is serialized in saved preferences: append, never insert.

## Arsenal

| Key | Label | Data key | Stat source | Damage | Cycle |
| --- | --- | --- | --- | ---: | ---: |
| 1 | Knife | Knife | project | 40 base | 0.5s |
| 2 | Pistol | Pistol | `deagle` | 53 | .225s |
| 3 | SMG | SMG | `mp9` | 26 | source |
| 4 | Shotgun | Shotgun | `xm1014` + override | 22 × 8 pellets | .35s |
| 5 | Assault Rifle | Assault | `ak47` | 36 | source |
| 6 | Marksman Rifle | Carbine | `scar20` | 80 | source |
| 7 | Sniper Rifle | Sniper | `awp` | 115 | 1.455s |
| 8 | Grenade Launcher | Launcher | project | 140 peak | .5s |
| F / G | HE / Smoke | HE / Smoke | project | [EQUIPMENT](EQUIPMENT.md) | .8s |
| H | Claymore | Claymore | project | [EQUIPMENT](EQUIPMENT.md) | |

Labels come from `scripts/presentation_labels.py`; data keys, asset paths and saved IDs
stay stable. Model names belong only in source credits. Each slot keeps independent ammo
and cooldowns; holstering cancels fire and reload. Ammo, zoom, melee and explosive values are
project choices in `scripts/import_sketchfab_weapons.py`, and
`weapon_gameplay_overrides.py` applies the Shotgun override after reference data.

Worksheet source: owner-supplied CS2 workbook dated 2026-03-18. The original XLSX is
private in `Assets/weapons/SourceAssets/References`; public exact values and cells are in
`docs/references/cs2-weapon-ballistics-2026-03-18.json`, extracted read-only by
`scripts/extract-weapon-ballistics.py`. The workbook defines values and envelopes, not
CS2 seeds or spray paths: never claim CS2 parity.

## Damage

- Bullets: server anatomical traces with a 2cm edge tolerance, used only if no exact body
  intersection exists on that target. Exact hits retain their anatomical zone. Bodies remain
  smaller than some clothing and boots; the tolerance trades fewer visible misses for a small
  margin outside the figure. Melee retains exact current-time body traces.
  `UERPostureMesh` pushes its procedural pose to the physics bodies and records server poses.
  Remote bullet queries interpolate this history using a server estimate of RTT/2, capped at 200ms.
  Local shots use current bodies. No client timestamp, aim direction or hit result is trusted.
  This conservative estimate does not reconstruct replication/interpolation delay, jitter,
  animation phase or movement-packet timing exactly.
  Wounds map the historical contact back onto the current bone.
  Queries transform rays into each body's current frame without moving live bodies. History
  resets across mesh changes, death and large teleports; missing history falls back to current
  bodies. Cover is tested in its current state, including the actual edge contact point, and
  penetration retains its shared HP budget. A remote trigger runs after that frame's moves and
  the owner sends its move at once, so the shot uses the received view. Range attenuation
  `damage * RangeModifier^(distance_cm/1270)`. Hit zones on skeletal bodies: head 4
  (deagle 3.9), stomach 1.25, chest/arms 1, legs .75. Fractional damage is preserved.
- Armor: `ArmorRatio/2`, consuming half the prevented damage. Legs bypass armor; head uses
  replicated `Health.bHelmet`. Starting armor 100.
- Cover: each crossed cell subtracts its **current HP** from one shared bullet damage budget,
  stopping at zero or surviving/indestructible cover. Residual damage then gets distance,
  zone and armor once.
- Knife: server anatomical trace with cover, 160cm reach, 0.5s cooldown. Base 40; head/neck
  80, torso 40, limbs 25; +20 on head/neck/torso from the rear 120° ground sector.
  Unknown bones keep 40 with no rear bonus.
- Tracers: every authorized bullet shot, from the rendered muzzle to server endpoints.
  The shot multicast carries up to 16 pellet endpoints. Audio, recoil and flash stay one
  event per cartridge. Rockets and thrown utility stay physical.
- Decals: 128 marks, 20s, on surfaces only. Player wounds are replicated bone-local state
  (32 marks) copied to the corpse; health loss uses four wound PNGs, armor-only damage
  four fabric tears. Weapons and Claymores reject decals at runtime.

## Scopes

Only Assault (iron), Carbine and Sniper (optical) opt in through `ScopeMode`. Right click
toggles; optical cycles 40/10/off FOV. Launcher has iron-style ADS at 75 FOV.
Iron aim moves the complete FPS rig over .2s, smoothstepped, with integration capped at 50ms.
Optical rifles hide the centre reticle, keeping the ammo ring. Carbine keeps zoom after
shots; Sniper unscopes (`bUnscopeAfterShot`). `scope_profiles.py` preserves values through
setup scripts.

## World poses

Third-person firearms use `ERWorldWeaponPose`: per-weapon absolute scale and geometry-derived
palm contacts, independent of the FPS rig and of damage, muzzle and spread data.
Scales: Pistol .92, SMG .92, Shotgun .90, Assault 1.0, Carbine .95, Sniper .78, Launcher .95.

Long guns: stock passes under the upper arm at half shoulder span minus 1cm; chest 30°
neutral / 15° ADS plus clavicles 25° / 30°, neck counterturned; two-hand bounded IK on all
20 skins. Left palm on the guard underside, wrist yaw 60° toward the muzzle, palm normal +Z.
The left forearm must align with the palm's long axis. `FERTeamAppearance.
WorldSupportPalmOffsetCm` is a per-skin correction. Pistols use a two-hand cup contact.

`setup_world_weapon_pose.py` updates only the seven firearm presentation profiles.

Held world poses query full weapon bounds against pawn-blocking geometry and
try a bounded rearward/upward tuck while preserving aim direction. Movement sweeps
reserve space ahead for the ready firearm. Both palms
are fitted afterwards; traditional grips, including the knife, move through the
right wrist before fitting the support hand. The FPS rig uses the camera's separate FirstPerson scale. Prone carry
keeps the stock at shoulder height and changes the elbow bend pole. The prone torso
and per-mesh ground clearance are described in [CHARACTERS](CHARACTERS.md). These cosmetic
queries do not change shot direction or authoritative muzzle obstruction checks.

## FPS presentation

The rig lives in `Characters/FirstPerson`; skin forearms come from `Teams/{Alpha,Omega}/
FirstPerson`. Component transform `(-11.2263,1.4908,-159.9167)`, yaw -90, scale 1. Grip data lives
in project-owned weapon data and poses, never edited in source meshes.

The knife uses a user-set manual pose (`ERKnifeManualGripApproved` metadata,
`apply_manual_knife_grip.py`), FPS scale .65, and takes precedence over all automatic fitting.

## Rebuild

The SMG bake excludes the loose `Pmag_2` reload prop and helper cube, retaining
the inserted `Pmag`. After baking only SMG, `setup_smg_mesh.py` updates its V4
mesh with a backup and protected-package hash checks, preserving clips and data.

Numeric tuning: `setup_weapon_hit_zones.py`, `setup_weapon_accuracy.py`,
`setup_weapon_recoil.py`, `setup_grenade_combat.py`, `setup_scopes.py`. Each saves only owned
data. `import_sketchfab_weapons.py` is the art import; do not rerun it for tuning. Keep
each model's licence, including Milkor's CC BY-NC. Sources: private
`SourceAssets/SketchfabWeapons`, runtime `Weapons/Sketchfab`.

## Verify

Always `test-reference-weapons.py` (`--render` for viewmodels), then as relevant:
`test-arsenal.py`, `test-scopes.py --render`, `test-world-poses.py`, `test-network.py --shot-feedback`,
`test-hit-fidelity.py` (rendered figure against hit bodies; `--lag 0 25 50` for two-peer shot timing).
Native: `EclipseRecoil.Weapons.ReferenceBallistics`, `EclipseRecoil.Weapons.ShotHistory`. Saved assets: `verify_sketchfab_weapons.py`.
Use `test-world-poses.py --prone --wall` for elbow and full weapon bounds against
a collision floor and thin cover on both peers; `EclipseRecoil.Movement.BodyClearance` covers thin walls.
