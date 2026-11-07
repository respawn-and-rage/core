# Windows and Linux packaging

Isolated build projects are written under `../builds/`.

**Code:** `scripts/package-windows.ps1`, `scripts/archive-linux-project.py`,
`scripts/build-server-linux.py`, `scripts/run-server-linux.py`,
`Source/RespawnAndRageServer.Target.cs`.

## Windows and Linux (from Windows)

Run from `core` in 64-bit PowerShell, with Unreal closed. The default builds Windows
x64 and Linux x86_64 and uploads both. `-InitAssets` is only needed the first time.

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\package-windows.ps1 -InitAssets
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\package-windows.ps1
```

| Option | Effect |
| --- | --- |
| `-Check` | Preflight only (includes a Linux compiler smoke and Butler status). Not with `-InitAssets`. |
| `-TargetPlatform Windows\|Linux` | One platform; Linux still builds the Windows Editor for cooking. |
| `-NoUpload` | Keep packages local, skip Butler and its authentication. |
| `-Configuration Shipping` | Default is Development. |
| `-Engine`, `-LinuxToolchain`, `-OutputRoot`, `-Butler`, `-ButlerIdentity` | Override discovered paths. |
| `-UploadOnly <run or platform dir>` | Retry pending uploads without compiling; filter with `-TargetPlatform`, validate with `-Check`. |

Requirements:

- UE 5.8.3 Installed Build with **Windows and Linux target support**. Found through
  `UE_ENGINE_ROOT`, Epic's registry or standard paths.
- Visual Studio 2022/2026 or Build Tools with C++ desktop/game tools, MSVC x64 and
  matching ATL. The helper reads `Engine/Config/Windows/Windows_SDK.json` for allowed
  compilers (for UE 5.8.3: 14.44.35211+ with VS 2022 or 14.50.35723+ with VS 2026).
- Windows SDK 10.0.22621.0+.
- Epic Linux cross toolchain **`v26_clang-20.1.8-rockylinux8`**.
  `LINUX_MULTIARCH_ROOT` must point to the version root that contains
  `ToolchainVersion.txt`, not its `bin` folder. No VM or WSL is needed.
- Butler on PATH with one `butler login`; `BUTLER_API_KEY` is also accepted.

Every selected target is checked before Unreal starts: tools, asset mounts and LFS
pointers, a closed Editor, 10 GiB free on the source drive and 60 GiB on the workspace
drive. Software is never installed automatically.

Content is shared through a junction, so do not edit assets during a run. Output goes
to `../builds/desktop/<timestamp>-<id>/{windows,linux}/` with `build.log`, `receipt.json`
and `upload.log`. Linux must produce a real ELF x86_64 executable and pushes with
`--fix-permissions`. A failed platform is recorded separately while the other can
finish; any failure returns exit code 1. Successful uploads delete their attempt
directory, and the whole run once all platforms have uploaded.

Common preflight problems:

- `Missing mount: ...Content/...`: run `python .\scripts\setup-assets.py`, or just the
  normal build, which mounts assets after the tool checks pass.
- `[OK] Unreal Linux target support` covers only the Launcher component; the v26
  toolchain is a separate installation.
- A Windows Store Python alias first on PATH must be moved behind real Python.

Helper tests:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\tests\test_package_windows.ps1
python -m unittest discover -s scripts/tests -p test_prepare_desktop_build.py
```

## Linux through Fargate

`unreal-builder` builds Linux on AWS. The cached engine lacks Fab plugins, so create
the private input with the helper, which adds them as source-only plugins:

```sh
python3 scripts/archive-linux-project.py /private/tmp/rr-linux-project.tar.gz --check
python3 scripts/archive-linux-project.py /private/tmp/rr-linux-project.tar.gz
```

Pass `-` as output to stream into `aws s3 cp - s3://<bucket>/inputs/<release>/project.tar.gz`
with `set -o pipefail`. Then run `unreal-builder/scripts/run-task.py` with `--project-key`
and `--publish-itch`. Never commit that archive. See `../unreal-builder/docs/LINUX_BUILD.md`.

## Local Linux dedicated server

Run on **native Linux x86_64**, Python 3.10+, from a clean checkout with the private
asset mounts prepared and the Editor closed. Select a UE 5.8.3 engine explicitly. It
must be a prepared source engine or an Installed Build with downloaded **Linux
Development Editor and Server** support. The native v26 toolchain from the engine's
`Linux_SDK.json` must already be installed; the helper also respects an explicit
`LINUX_MULTIARCH_ROOT` or `UE_SDKS_ROOT`. An official Linux game engine package is not
assumed to support servers. The helper does not install prerequisites, override engine
restrictions, launch cloud jobs, publish or upload.

```sh
python3 scripts/build-server-linux.py --engine /path/to/UE_5.8 --check
python3 scripts/build-server-linux.py --engine /path/to/UE_5.8 \
  --output /path/to/new-server-run
```

`--check` is read-only and prints prerequisite results and the command plan. It does
not compile or cook. Both commands require a Linux host; Mac or Windows cannot validate
the Linux toolchain through this helper. Output must be a new directory outside the
checkout, with at least 60 GiB free. The default is `../builds/server-linux/<run>/`.

The build copies an isolated code/config/plugin workspace with shared, complete Content,
builds `RespawnAndRageEditor Linux Development`, then runs `BuildCookRun` for
`RespawnAndRageServer` with `-server -noclient -serverconfig=Development`. The sole cook
map list remains `MapsToCook` in `Config/DefaultGame.ini`; no second `-map` list or reduced
asset tree is created. Keep source assets unchanged during the run.

The archive must contain one actual ELF64 x86_64 server executable with an executable
entry point, nonempty cooked `.pak` content, and a matching Linux Development Server
build receipt. `receipt.json` records the binary hash and `runtime_verified=false`;
`build.log` preserves Unreal output. Validation of these files does not establish that
every needed asset was cooked or that gameplay works. Failed output remains available
for diagnosis; rerun into a new directory.

Inside the resulting package, `server-settings.json` owns the default map, port and
tick rate. The generated map choices come from `MapsToCook`; the default map is Central
Building. The launcher uses direct IP by default and applies a 60 Hz server/replication
cap to this process's `ERNetDriverEOS` and fallback `IpNetDriver` only.

```sh
python3 run-server-linux.py --check
python3 run-server-linux.py --port 7777 --tick-rate 60
python3 run-server-linux.py --map '/Game/EclipseRecoil/Maps/L_Town?Seed=73' -- -log
```

Map URL options and arguments after `--` pass through as individual arguments. Explicit
`?EROnline=1` retains the existing EOS transport path; it does not provision an EOS
session or credentials, and dedicated EOS hosting still needs integration verification.
Direct IP uses the existing IP passthrough in `ERNetDriverEOS` (see [ONLINE](ONLINE.md)).
The 60 Hz setting is a cap, not a measured guarantee under load.

## Verify

```sh
python3 -m unittest discover -s scripts/tests -p test_build_server_linux.py
```

These tests use structural fixtures and prove CLI, capability rejection, map-list,
ELF validation and launcher contracts only. No dedicated Linux cook or runtime is
established by them. On a supported Linux host, complete the real package, start it,
connect compatible clients by IP, check the `ER_TRANSPORT LAN/IP` log, and exercise
spawn, movement, hits across skins/stances, destruction and map travel. Record measured
server frame/tick behaviour under target player load. A successful package alone does
not validate these gameplay or performance checks.
