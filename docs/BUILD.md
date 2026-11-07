# Build

Daily compile and run. Packaging: [PACKAGE_MAC.md](PACKAGE_MAC.md), [PACKAGE_DESKTOP.md](PACKAGE_DESKTOP.md).

**Code:** `Source/RespawnAndRage*.Target.cs`, `scripts/build-editor.command`,
`scripts/build-server-linux.py`.

## Prerequisites

- Unreal Engine **5.8.3** (`/Users/Shared/Epic Games/UE_5.8` on Mac).
- Python 3.10+ on Mac (3.9+ on Windows, as `python.exe`), Git, Git LFS.
- Access to the three private asset repositories:

  ```sh
  git lfs install
  python3 scripts/setup-assets.py --init
  python3 scripts/setup-assets.py --check
  ```

- PCGExtendedToolkit 0.79.2 with source. Packaging copies enabled Marketplace code
  plugins into the build project as source-only and compiles them there.
- EOS guest play uses a generated, ignored plugin (`Plugins/OnlineSubsystemEOS`).
  Build and open helpers create it; before a manual IDE/UBT build run
  `python3 scripts/prepare-eos-guest.py --engine "/path/to/UE_5.8"`.

## Daily development (Mac)

```sh
scripts/build-editor.command   # compile the Editor module
scripts/open-editor.command    # open the project
scripts/play.command           # play in a separate window on the Editor runtime
```

Equivalent compile command:

```sh
"/Users/Shared/Epic Games/UE_5.8/Engine/Build/BatchFiles/Mac/Build.sh" \
  RespawnAndRageEditor Mac Development -Project="$PWD/RespawnAndRage.uproject" \
  -architecture=arm64 -NoHotReloadFromIDE
```

Agents launching the editor from a shell: a recent project in `~/Downloads` can
block the home screen on a macOS privacy prompt until the user answers it.

**A Mac compile does not validate the other platforms.** Before pushing C++, follow
the cross-platform rules in [AGENTS.md](../AGENTS.md#cross-platform-c-rules).

## Dedicated server target

`RespawnAndRageServer` uses `TargetType.Server`, the same module and build settings as
the game. The Linux helper builds the Linux Editor for cooking, then a **Development
Linux x86_64 server**. It requires a prepared source engine or an Installed Build that
declares both Editor and Server support; Linux Game support alone is insufficient.
It never overrides Installed Build restrictions or changes the selected engine.

The target preserves the game's asset references and authority animation/hitbox code.
Headless execution is not permission to remove skeletal meshes, animation or collision
assets. Server-specific asset reductions require cooked authority hitbox verification.
See [local server packaging and verification](PACKAGE_DESKTOP.md#local-linux-dedicated-server).

## Channels and versions

Uploads go to `aaggeeaa/respawn-and-rage`: `mac-universal` (or `mac-arm64`, `mac-x86_64`),
`windows-x86_64`, `linux-x86_64`. Existing page access settings are retained. Butler
success does not prove the game starts: test launch, menus, maps and LAN on each OS.

Release builds require a clean Git tree, including untracked files and dirty submodules.

Each native build stamps `YY.MM.DD.<8-char commit>` through
`scripts/stamp-build-version.py` into the ignored `Build/GameVersion.txt`, shown at the
bottom right of the main menu. The date is the HEAD committer date. Isolated workspaces
and source exports without `.git` carry `Build/SourceCommit.txt` and
`Build/SourceCommitDate.txt`.

## Verify

Use `scripts/build-editor.command` for Mac Editor compilation. Dedicated-server helper
tests run with `python3 -m unittest discover -s scripts/tests -p test_build_server_linux.py`.
They verify tooling contracts, not a Linux compile, cook, server tick rate or connection.
