# Assets and repository

## Layout

| Repository | Checkout | Contents |
| --- | --- | --- |
| respawn-and-rage/core (public) | here | Code, config, scripts, docs |
| respawn-and-rage/assets-characters | `Assets/characters` | Skins, FPS rig and poses, character animation |
| respawn-and-rage/assets-weapons | `Assets/weapons` | Arsenal, data, audio, effects, import sources |
| respawn-and-rage/assets-maps | `Assets/maps` | Maps, Core Blueprints, input, template UI, map art, sources |

The bundles work together: Core Blueprint defaults reference characters and weapons.
`scripts/setup-assets.py` follows `assets.json` to create directory links below the ignored
`Content/` and `SourceAssets/` (symlinks on Mac/Linux, junctions on Windows), preserving every
`/Game` path. It never overwrites existing data and rejects unresolved LFS pointers.

```sh
git lfs install
python3 scripts/setup-assets.py --init
python3 scripts/setup-assets.py --check
```

A public clone alone cannot run. LFS objects live in the sibling `_cache/lfs-*`.

## Editing assets

Edit through Unreal at the original `/Game` paths. Links write into the submodule, so commit
and push the private asset repository first, then its gitlink:

```sh
git -C Assets/characters add Content
git -C Assets/characters commit -m "Update Alpha appearances"
git -C Assets/characters push origin main
git add Assets/characters
git commit -m "Use updated character assets"
```

Update collaborators with `git pull` then `python3 scripts/setup-assets.py --init`. Close Unreal
before switching pinned revisions, and avoid `submodule update --remote` unless upgrading.

## Publication limits

- `scripts/verify-public-tree.py` allowlists paths and checks all reachable history for assets,
  binaries and LFS pointers; the `pre-push` hook runs it. Binary exceptions (branding, fonts) are
  pinned by SHA-256 in that script. New exceptions need review.
- `Config` allows only exact approved JSON/Markdown paths.
- `scripts/export-code.py --apply` creates a code-only tree in `BuildArtifacts/code-only`.
- No open-source licence has been chosen for the code; never pick one implicitly.

## Licences

Distribution assessment, not legal clearance. Rights for acquired packs depend on terms accepted
at acquisition. Keep per-asset provenance in each private repository and in-game attribution in
`Branding/Credits.txt`.

| Content | Source | Treatment |
| --- | --- | --- |
| Paragon characters | Epic, Fab | Private; derivatives included. Fab forbids standalone redistribution. |
| Fitted FPS rig | Derived from Mercenary (Waveforce Studio) | Private; keeps provenance. |
| Factory Environment | Owner's FactoryEnvironmentCollect | Private; terms not inspected. |
| Interactive Smoke, Low Poly Weapons | Fab | Private; fixture and legacy sources. |
| Content Examples effects, UE templates | Epic | Private; retain Epic notices. |
| Sketchfab weapons | Nine listings | Mostly CC BY 4.0; **Milkor CC BY-NC 4.0**, grenades Sketchfab free standard. Check before commercial use. |
| Sketchfab textures, sand, houses, fence, Village, drones | Listed in Credits | CC BY 4.0, private sources. |
| CC0 textures | Poly Haven, ambientCG | CC0, private sources. |
| Audio | Free Firearm Library, Freesound, congusbongus pack | CC0 or CC BY 3.0 with credits. Sonniss GDC 2024: no receipt verified. Tabasco pack not imported: its page and included licence conflict. |
| Menu music, graffiti, decals, HUD art, branding | Owner | Supplied for this game; no separate redistribution licence. |

## Maintenance

- `python3 scripts/clean-project.py` previews log cleanup; `--apply` runs it, with Unreal closed.
  Profiles: `logs`, `screenshots`, `cache`, `build`. It protects Content, SourceAssets, settings,
  autosaves and Git/LFS.
- `scripts/audit-assets.py` reads package dependencies without changes. An Asset Registry
  candidate is not proof that something is disposable.
- Delete assets only through Unreal, after an outside-reference check and a verified rollback
  outside the project. Never prune maps, weapons or LFS implicitly. Keep unrelated work.
- Removed families stay removed: Mercenary, Phase, Murdock, retired levels, `ParagonStaging`, the Fire Training warehouse kit and wet concrete.
  Do not regenerate deleted sources during routine setup; old scripts in `scripts/legacy` need
  archived inputs.
