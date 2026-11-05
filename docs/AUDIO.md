# Audio

**Code:** `ERAudio`, `ERFootsteps`, `MulticastShot` in `ERWeapon`, `ReleaseVolume` in `ERSmokeCloud`.

`UERWeaponData.ShotAudio` references a `UEROneShotAudio` palette. The server-authorized
`MulticastShot` carries the accepted weapon data; no variant index is replicated. Each emitter
keeps a shuffle bag with its own `FRandomStream`, excluding immediate repeats across bags.
Saved cues use Modulator → Switch → WavePlayers; virtualization off. Pitch .98–1.02, level ±1dB.
Owner plays centred 2D; others spatial (300cm inner, 3700cm falloff). 24 per owner / 96 global.

| Slot | Source |
| --- | --- |
| Pistol, SMG, Assault | Free Firearm Sound Library (CC0): 1911, M45, AK-47 |
| Shotgun | Freesound 660299, hyperix6 (CC0) |
| Marksman | Freesound 855843, serøutōnin//deprivəd (CC0) |
| Sniper | Freesound 398348, EvanBoyerman (CC BY 3.0), attack aligned by 70ms |
| Launcher | One existing Epic sample: never claim more takes |
| Empty trigger | Freesound 387482, CosmicEmbers (CC BY 3.0): owner-only, server-checked, 400ms limit, firearms only |

**Footsteps:** `UERFootsteps` emits authority-driven transient multicasts from real travel:
stride 95/130/150cm at 5/10/18 km/h, 80 low, 65 prone, 220ms safety interval, no landing sound.
`DA_ER_Footsteps` maps physical surfaces and cell materials to Hard, Wood, Metal, Grass, Gravel,
LightHard; unknown falls back to Hard. Never change HP or collision for audio.

**Glass break:** When a pane is destroyed by damage or collapse, the authority emits a
spatial one-shot at its centre. Each listener randomly selects Freesound 221528 by unfa
(CC0, first 330ms trimmed) or 41348 by datasoundsample (CC BY 4.0).
Import with `scripts/setup_glass_break_audio.py`.

**Explosions and smoke:** HE and smoke use Sonniss GDC 2024 WAVs. HE: spatial 5m inner, 75m falloff.
Smoke `ReleaseVolume` defaults to about .2055, a Blueprint-editable local gain.
Menu music: owner WAV, looping Bink SoundWave on `BP_ERPlayerController`, saved 0–100% slider,
half gain in the Esc menu.

Regenerate with `scripts/prepare_audio_pass.py` then `setup_audio_pass.py`; per-sound scripts
(`setup_shotgun_audio.py`, ...) are reapplied by it. Headless runs never prove listening quality.

## Verify

`test-audio.py` (`--network --render`), `test-network.py --shot-feedback`. Native: `EclipseRecoil.Audio.*`.
