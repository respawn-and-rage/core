# Windows, Linux and dedicated server packaging

Isolated build projects are written under `../builds/`.

**Code:** `scripts/package-windows.ps1`, `scripts/server-image.py`, `scripts/archive-linux-project.py`,
`scripts/build-server-linux.py`, `scripts/run-server-linux.py`,
`Source/RespawnAndRageServer.Target.cs`.

## Windows, Linux and server image (from Windows)

Run from `core` in 64-bit PowerShell, with Unreal closed. The default builds the Windows
x64 and Linux x86_64 clients and uploads them with Butler, then builds the Linux x64
dedicated server as a Docker image and pushes it to Docker Hub. `-InitAssets` is only
needed the first time.

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\package-windows.ps1 -InitAssets
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\package-windows.ps1
```

| Option | Effect |
| --- | --- |
| `-Check` | Preflight only (includes a Linux compiler smoke and Butler status). Not with `-InitAssets`. |
| `-TargetPlatform Windows,Linux,Server` | Any subset. `All` (default) is all three, `Both` the two clients. Linux still builds the Windows Editor for cooking. |
| `-NoUpload` | Keep packages local; the server image is still built into the local Docker daemon. Skips Butler, Docker Hub and their authentication. |
| `-Configuration Shipping` | Clients only; default is Development. The server is always Development. |
| `-Engine`, `-LinuxToolchain`, `-OutputRoot`, `-Butler`, `-ButlerIdentity` | Override discovered paths. |
| `-ServerEngine <UE 5.8.3 root>` | Engine for the server; discovered automatically when omitted (see below). |
| `-Docker`, `-DockerCredentials`, `-DockerRepository` | Docker CLI, credentials file (default `.secrets\dockerhub.json`), `namespace/name` override. |
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
- Server only: a UE 5.8.3 engine that can build **Linux Development Server** targets,
  Docker Desktop in Linux-container mode with buildx, and Docker Hub credentials.

Every selected target is checked before Unreal starts: tools, asset mounts and LFS
pointers, a closed Editor, 10 GiB free on the source drive and 60 GiB on the workspace
drive. Software is never installed automatically.

Content is shared through a junction, so do not edit assets during a run. Output goes
to `../builds/desktop/<timestamp>-<id>/{windows,linux}/` with `build.log`, `receipt.json`
and `upload.log`. Linux must produce a real ELF x86_64 executable and pushes with
`--fix-permissions`. A failed platform is recorded separately while the other can
finish; any failure returns exit code 1. Successful uploads delete their attempt
directory, and the whole run once all platforms have uploaded.

### Dedicated server image

Epic Launcher engines cannot build `Server` targets. The server needs a UE 5.8.3 source
engine (a clone of the `5.8.3-release` tag after `Setup.bat`) or an Installed Build made
with server support (`BuildGraph ... -set:WithServer=true -set:WithLinux=true`).
`server-image.py check-engine` reads its `InstalledPlatformConfigurations` and never
overrides them. The engine is chosen in this order:

1. `-ServerEngine <root>`, else the `UE_SERVER_ENGINE_ROOT` environment variable. An
   explicit choice that fails the check stops the preflight; there is no fallback.
2. Otherwise the first UE 5.8.3 that passes the check among the client engine and the
   engines registered by UnrealVersionSelector (`HKCU\SOFTWARE\Epic Games\Unreal Engine\Builds`,
   where a source engine's `Setup.bat` or *Switch Unreal Engine version* registers it). When it differs from `-Engine`, the server gets its own workspace
(`<run>\server-project`, Fab plugins still copied from `-Engine` as source) and its own
Editor build, so the first source-engine run compiles the engine. The Linux toolchain
is the same v26 one.

UAT runs `BuildCookRun -target=RespawnAndRageServer -server -noclient` for Linux x64
Development with `MapsToCook`. `server-image.py stage` then applies the same package
validation as the native Linux helper below (ELF, `.pak`, Server target receipt), adds
`run-server-linux.py`, `server-settings.json` and a generated `Dockerfile`, and checks
that the staged `GameVersion.txt` matches. The image is `debian:bookworm-slim` with
Python and tini, runs as uid/gid 10001 (Unreal refuses root), exposes `7777/udp`, omits
`*.debug` symbols and uses `run-server-linux.py` as entry point. The Server target never
stages `EOS.local.ini`, and `stage`/`validate` refuse a package that contains one: the
image is public. It is built for
`linux/amd64` and tagged `<repository>:<GameVersion>` and `:latest`.

Credentials live in the ignored `core/.secrets/dockerhub.json` (never commit it):

```json
{"username": "dockerhub-user", "token": "dckr_pat_...", "repository": "dockerhub-user/respawn-and-rage-server"}
```

Use a Docker Hub personal access token with Read & Write scope. Preflight logs in to the
Docker Hub API and requires the repository to **exist and be public**: the server image
is published publicly, and a push to a missing repository would take the account's
default visibility. The push logs in through stdin
inside a temporary Docker config, so the token never reaches arguments, logs, receipts
or your own `docker login`. Built images stay in the local Docker daemon; prune them there.

By default the container advertises itself in the Internet server browser as a
**Community** server, using the public Community EOS client built into the launcher (see
[ONLINE](ONLINE.md#dedicated-server)). No parameter is required:

```sh
docker run --rm -p 7777:7777/udp -e RR_SERVER_NAME='My server' <repository>:latest
docker run --rm -p 7777:7777/udp <repository>:latest --name 'My server' --map '/Game/EclipseRecoil/Maps/L_Town?Seed=73' -- -log
docker run --rm -p 7777:7777/udp <repository>:latest --direct-ip
```

To use another EOS client, set all five `RR_EOS_*` variables (for example from a private
`--env-file`); a partial set stops the container at once and names the missing ones.
`--direct-ip` runs without EOS: no browser entry, players connect by IP. Either way
`7777/udp` must be reachable from the Internet (host firewall, security group, router
forwarding). Arguments are those of `run-server-linux.py` (see below); keep `--port` and
the published port equal.

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

Map URL options and arguments after `--` pass through as individual arguments. The
launcher defaults to EOS mode (`-EROnlineServer`, the Community client unless all five
`RR_EOS_*` are set, browser name from `--name` or `RR_SERVER_NAME`); `--direct-ip` disables it. The dedicated server always
listens on IP through `ERNetDriverEOS` (see [ONLINE](ONLINE.md)). `--check` prints the
command, mode and credential source, never the secret.
The 60 Hz setting is a cap, not a measured guarantee under load.

## Verify

```sh
python3 -m unittest discover -s scripts/tests -p test_build_server_linux.py
python3 -m unittest discover -s scripts/tests -p test_server_image.py
```

These tests use structural fixtures and prove CLI, capability rejection, map-list,
ELF validation, launcher, Dockerfile, tag and credential-handling contracts only. They
call neither Unreal, Docker nor Docker Hub. No dedicated Linux cook or runtime is
established by them. On a supported Linux host, complete the real package, start it,
connect compatible clients by IP, check the `ER_TRANSPORT LAN/IP` log, and exercise
spawn, movement, hits across skins/stances, destruction and map travel. Record measured
server frame/tick behaviour under target player load. A successful package alone does
not validate these gameplay or performance checks.
