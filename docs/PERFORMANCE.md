# Performance settings

**Code:** `ERPerformanceSettings`, `ERResolutionPolicy`.

Setup → Performance, local and immediate, saved under `EclipseRecoil.Performance`. Native startup
applies the base profile for local and packaged runs; do not restore launcher overrides.

| Preset | Resolution | AA | GI | Reflections | Shadows | Effects | Post | Textures | Cap |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Competitive | manual 67% | TAAU | off | off | off | minimum | low | medium | 144 |
| Performance | auto | TAAU | off | off | low | low | medium | medium | 90 |
| **Balanced** (default) | auto | TSR | Lumen medium | SSR | medium | medium | high | high | 90 |
| Quality | auto | TSR | Lumen high + HW RT | Lumen | high | high | ultra | ultra | 90 |

Fresh profiles, `SetDefaults` and legacy `Defaults=1` select Balanced; saved custom choices stay.
Sun shadows use the engine's top virtual shadow map resolution only at high shadows; medium
keeps the standard resolution.
`r.MaterialQualityLevel` stays High when effects are minimum, a workaround for orange Paragon
skin patches whose root cause was never isolated. Do not claim it was.

Automatic resolution uses `ERResolutionPolicy` on real RHI frame history: 60 FPS target, band
56–66, 3s sustain, 4s hold, with a 1080p scene cap and UI at viewport resolution.
`-ERPerformanceAudit` logs it. Lower effect levels bound local debris, decals and block effects and
disable cloth at minimum. Competitive stays manual because the governor would spend headroom on
resolution. No FPS figures are guarantees.

## Verify

Native: `EclipseRecoil.Menu.PerformancePreferences`, `EclipseRecoil.Player.ResolutionHysteresis`.
