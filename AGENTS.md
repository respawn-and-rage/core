# Respawn & Rage — agent guide

Current rules and current state only. History lives in Git: the long chronological
docs were removed after commit `d292554`, so `git show d292554:AGENTS.md` (or
`:docs/DEVELOPMENT.md`, `:docs/TEST_RESULTS.md`) recovers old reasoning when needed.

## Project

- Tactical multiplayer FPS, tagline **Serious guns. Questionable players**.
  Unreal Engine **5.8.3**, `RespawnAndRage.uproject`, targets `RespawnAndRage` and
  `RespawnAndRageEditor`, build settings V7, include order `Unreal5_8`.
- Sibling repositories under `/Users/agea/git/respawn-and-rage/`: `core` (this
  public repo), `unreal-builder` (Fargate/EC2 build infrastructure), `website`.
  `_cache` holds private LFS caches and `builds/` packaging output.
- Licensed content lives in three private submodules (`Assets/characters`,
  `Assets/weapons`, `Assets/maps`), mounted into the ignored `Content/` and
  `SourceAssets/` by `scripts/setup-assets.py`.
- Keep the internal identifiers `fdm`, `/Script/fdm`, `/Game/EclipseRecoil` and the
  `EclipseRecoil.*` settings/test names: serialized assets and saved preferences
  depend on them. Never show them to players.
- Native code is one module, `Source/fdm` (flat directory, `ER*` prefix).

## What to read

Read **only** the row for the task, not `docs/` as a whole and not the README. Open another doc
only when the task really crosses into it. Each doc starts with a **Code** line and ends with
**Verify**: start from those files, grep `Source/fdm` (flat, 230 files), and read large files such
as `ERCharacter.cpp` by range.

| Task | Read |
| --- | --- |
| Add or change a weapon, damage, scope, weapon pose | [WEAPONS](docs/WEAPONS.md) |
| Accuracy, recoil, aim punch | [WEAPON_TUNING](docs/WEAPON_TUNING.md) |
| Loadouts, grenades, launcher, smoke, Claymore | [EQUIPMENT](docs/EQUIPMENT.md) |
| Match rules, movement, bots, controls, server settings, messages | [GAMEPLAY](docs/GAMEPLAY.md) |
| One map | [MAPS](docs/MAPS.md) + `docs/maps/<map>.md` |
| Map sizing, themes, textures, new map | [MAPS](docs/MAPS.md) |
| Map playability analysis, tactical graph, reports | [PROCEDURAL_MAP_PLAYABILITY](docs/PROCEDURAL_MAP_PLAYABILITY.md) |
| Cells, material HP, collapse, cell replication | [DESTRUCTION](docs/DESTRUCTION.md) |
| Skins, FPS rig, clothing | [CHARACTERS](docs/CHARACTERS.md) |
| HUD, minimap | [HUD](docs/HUD.md) |
| Fog of war, drones, graffiti | [RECON](docs/RECON.md) |
| Sound | [AUDIO](docs/AUDIO.md) |
| Graphics settings, resolution | [PERFORMANCE](docs/PERFORMANCE.md) |
| LAN, EOS, capacity | [ONLINE](docs/ONLINE.md) |
| Setup, compile, versions | [BUILD](docs/BUILD.md) |
| macOS package or release | [PACKAGE_MAC](docs/PACKAGE_MAC.md) |
| Windows or Linux package | [PACKAGE_DESKTOP](docs/PACKAGE_DESKTOP.md) |
| Submodules, licences, cleanup, publication | [ASSETS](docs/ASSETS.md) |
| Unsure what a test proves | [TESTING](docs/TESTING.md) |

## Cross-platform C++ rules

**A green Mac build proves nothing about Linux or Windows.** UE's Apple toolchain
adds `-Wno-range-loop-analysis` and `-Wno-shorten-64-to-32`, and Apple Clang does
not report the MSVC-only cases below. All four caused real failed builds (commits
`d292554` Linux, `839452c` Windows). Follow them in all new code, tests and probes:

1. **Range-for never copies through `const auto`.** Write `const auto& X:` or
   `auto* X:`. `for(const auto X:Array)` over `TObjectPtr` (and any non-trivially
   copyable or large element) fails on Linux (`-Wrange-loop-construct`).
2. **No local may hide a class member, including private inherited ones.** In any
   `UObject`/`AActor` member function or its lambdas, avoid `Role`, `RemoteRole`,
   `Owner`, `Instigator`, `Tags`, `Children`, `Layers`, `Controller`, `Mesh`...
   MSVC raises C4458 as an error even for inaccessible members such as
   `AActor::Role`; Clang stays silent. Rename the local (`LoadoutRole`).
3. **Convert to `bool` explicitly.** `const bool Odd=(I%2)!=0;`, never
   `const bool Odd=I%2;`: C4800 is an error on MSVC.
4. **`FVector` is double, so `FMath::RoundToInt` is `int64`.** Round/Floor/Ceil/Trunc
   of a double return `int64`. Use `RoundToInt32`, `FloorToInt32`, `CeilToInt32` or
   `TruncToInt32` wherever the result reaches `int32`, and always inside brace
   initialization (`FIntPoint`, `FIntVector`, `{...}` arguments), where narrowing is
   a hard MSVC error.

Run this over changed files before finishing C++ work. Hits for rules 1 and 2 need
review, not automatic rewriting; value types such as `FVector` are accepted today:

```sh
git diff --name-only HEAD -- 'Source/*.cpp' 'Source/*.h' | xargs -r grep -nE \
  'for\( *const auto +[A-Za-z_]+ *:|\b(const )?(auto|F[A-Z][A-Za-z]+|int32|float) *[&*]? *(Role|RemoteRole|Instigator)\b *[=;(]|bool +[A-Za-z_]+ *= *[^;=!<>]*%[^;=!<>]*;|\{[^}]*FMath::(Round|Floor|Ceil|Trunc)ToInt\('
```

Say which platforms were really compiled. Windows and Linux are only verified by
`scripts/package-windows.ps1` or the `unreal-builder` Linux task.

## Working rules

- Never claim a test, compile, licence check or visual result without evidence.
  `nullrhi` runs do not prove rendering, audio or subjective balance; geometry
  checks are not playtests; local two-process runs are not a physical LAN test.
- Edit or move `.uasset`/`.umap` only through Unreal, never by binary patching.
  Asset migrations are versioned `scripts/setup_*.py` scripts run through Unreal
  Python: back up, save only the owned packages, verify protected hashes, and
  stay idempotent. Never rerun an art import for a numeric tuning change.
- In Unreal Python, build rotations with named `pitch`, `yaw`, `roll` arguments.
- Keep art references in Blueprint defaults and original supplied art immutable;
  generate owned derivatives. Never load from Downloads at runtime.
- Commit asset submodules first, then their gitlinks here. Never publish licensed
  files or change repository visibility implicitly. See docs/ASSETS.md.
- `Config/EOS.local.ini` is private and ignored (mode 0600); never print it.
- Do not modify `/Users/agea/git/csgopen/eclipse-recoil` (the original game).
- Run the topic doc's **Verify** tests for what changed, not the whole suite.
- Respect the user's stated scope; ask before packaging or uploading.

## Non-negotiable invariants

- **Authority:** combat, ammo, teams, scoring, protection, respawn, recoil, spread and
  damage are server authoritative. No client-selected shot direction, damage, cell
  mutation or unrestricted position RPC. Movement goes through `UERMovement` and
  saved moves. Cosmetic RNG never consumes gameplay or geometry RNG.
- **Aim:** `GetEffectiveAimRotation` / `GetViewRotation` combine input, worksheet
  recoil and aim punch once. `ControlRotation`, capsule and hitboxes stay untouched.
- **Maps:** templates are deterministic from a seed, paired by half-turn or reflection, validated
  atomically, 1500m² per team-size unit ±10% of area. Never scale meshes, doors,
  stairs, materials or cells to fit. Sample one half, then transform it using the template symmetry.
  Every gameplay layout must block visibility AND direct projectile lines between every opposing
  spawn pair on the intact map, regardless of seed or team size. Validate before readiness;
  no exposed fallback. Destruction may open these lines later.
- **Destruction:** one path, logical 5cm cells. Server owns HP; clients reconstruct
  from the protocol descriptor and apply complete shard manifests only, failing
  closed. HP values are calibrated to cell size: changing resolution or thickness
  needs recalibration.
- **Characters:** 20 selectable skins (10 per team) with stable indices. The FPS rig
  and poses are independent of world skins. Never call
  `FBoneContainer::SetDisableRetargeting` in gameplay: it crashes cooked builds.
- **Packaging:** the only map list is `MapsToCook` in `Config/DefaultGame.ini`. Add
  every new playable map there; never pass a separate `-map` list.

## Documentation rules

- Docs describe **what is true now**. Edit the relevant section in place; delete
  what stopped being true. Add a new file only for a new subsystem, then add a row
  to the table above.
- One topic per file, so a task costs few tokens: keep files small, with **Code** and
  **Verify** sections, and link across topics instead of repeating content.
- No dated entries, revision narratives, "latest", "supersedes", "previously" or
  execution logs. Test evidence and reasoning go in commit messages and
  `Saved/Testing`, not here.
- Prefer pointing to code over copying numbers: `ERServer::Settings()` owns tuning
  defaults, `ERMapCatalog` the map list, `character_catalog.py` the skin labels.
- Keep this file short: only rules an agent would otherwise get wrong. README and
  docs are in English.
