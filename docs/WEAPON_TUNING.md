# Weapon tuning

Accuracy, recoil and aim punch. Read this only when changing how firing feels.

**Code:** `ERWeaponAccuracy.h`, `ERWeaponRecoil.h`, `ERAimPunch.h`, `ERDamageHUD.cpp`, plus `GetEffectiveAimRotation` in `ERCharacter`.

## Accuracy

`bUseWorksheetAccuracy` with `HipAccuracy` and `ScopedAccuracy` (Alt values, inherited when
missing; AK iron sights use hip). Raw offsets are tangent units × 1000:

`half-angle = degrees(atan((Spread + Inaccuracy) / 1000))`

Add offsets before converting. Spread and inaccuracy are sampled independently on the
server, and the HUD projects the same envelope.

- Capsule half height interpolates crouch and stand; prone uses crouch.
- Motion scales to `InaccuracyMove` at `MaxPlayerSpeed`; 1 Source unit = 2.54cm.
- Ladder base is `2 * InaccuracyLadder`. Moving pistols also add 0.6° at full ladder speed.
- Air adds `Jump`, interpolating `JumpApex..JumpInitial` by vertical speed. Landing adds
  `Jump + Land * downward_speed`, measured on authority.
- Each authorized shot adds `InaccuracyFire`; recovery is `error*0.1^(t/RecoveryTime)`.
  AK interpolates initial to final recovery over bullets four to six.
- Server snapshots carry state across posture changes; the owner extrapolates them on
  the synchronized clock.

The server applies global firearm dispersion and recoil scales. The pistol has additional
scales in its own settings category, both set to 0.5 in Standard.

## Recoil

`bUseWorksheetRecoil` with `HipRecoil` and `ScopedRecoil`:

| Slot | Row | Authored hip ± var | Angle var | Optical | Pattern |
| --- | --- | ---: | ---: | ---: | --- |
| Pistol | deagle | 48.2 ±18 | ±60° | same | Random |
| SMG | mp9 | 21 ±1 | ±70° | same | Set |
| Shotgun | xm1014 | 80 ±20 | ±20° | same | Set |
| Assault | ak47 | 30 ±0 | ±70° | same | Set |
| Carbine | scar20 | 31 ±4 | ±30° | 31 ±4 | Set |
| Sniper | awp | 78 ±15 | ±20° | 25 ±2 | Random |

Project adaptation: each magnitude unit is one deg/s impulse, with angle around upward pitch.
Between shots, `dA/dt=V-8A`, `dV/dt=-4.5V`, solved analytically. Set patterns are repeatable from
CRC32 of the row name, resetting after .55s without fire. One impulse per accepted
cartridge, added after its rays are sampled. State is one character-level snapshot replicated to
the owner; weapon switching keeps the residual. Rejected shots add nothing.

## Aim punch and damage feedback

Positive nonlethal server-accepted bullet damage adds pitch/yaw to
`GetEffectiveAimRotation`, shared by camera, FPS rig and authoritative fire, before spread.
`FERAimPunchTuning`: .40° + .012°/HP, head ×1.5, armor-mitigated ×.6, lateral .35, caps
2.5°/1.5°, half-life .20s. Pellets of one shot share the base term.

`AuthorityPunch` is gameplay. Reliable owner-only `ClientDamageFeedback` carries absolute
post-hit snapshots, sequence and server time; `OwnerPunch` ages them on a dedicated
latest-sample clock plus half RTT, rejecting duplicates. Clients never send offsets. An
unavoidable interval exists before the owner hears about a hit: never claim zero latency.
Reset on death and possession. `ERDamageHUD.cpp` draws directional arcs (.65s) and edge
flashes (.45s). Non-bullet damage keeps its cosmetic response.

**Lethal hit:** the server marks the final `ClientDamageFeedback` as lethal. The owner then gets
a cosmetic camera punch (`LethalPunch`, 50ms attack, .14s half-life) that follows the hit: back
from the front, forward from behind, turned and rolled toward the travel side; blasts punch
harder and falls pitch down. It plays during the .8s death beat (`AERCharacter::DeathBeatSeconds`),
which starts from the lethal feedback or the replicated death, whichever arrives first. The
view stays in first person and the impact sound plays at full volume. The audio listener
stays at the body, so the shot report and any explosion are heard in place before the kill
camera starts (`IsDeathCameraActive`). It never touches `AuthorityPunch` or gameplay aim.

## Verify

`test-network.py --accuracy`, `test-network.py --recoil`, `test-aim-punch.py --automation`
(also default, `--lag 100`, `--render`). Native: `EclipseRecoil.Weapons.WorksheetAccuracy`,
`WorksheetRecoil`, `AimPunch`.
