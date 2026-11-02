# macOS packaging and release

**Code:** `scripts/package-macos.py`, `release-macos.py`, `notarize-macos.py`.

## macOS package

```sh
python3 scripts/package-macos.py --check
python3 scripts/package-macos.py
```

Default is a **Development universal** app (arm64 + x86_64), cooking both Metal SM6
and SM5. Output: `../builds/macos/<timestamp>/dist/RespawnAndRage.app`, plus `build.log`
and `receipt.json`. The app contains licensed assets: keep it out of public commits.

| Option | Effect |
| --- | --- |
| `--architecture arm64\|x64` | One architecture, smaller footprint. |
| `--configuration Shipping` | Shipping build. |
| `--engine`, `--workspace` | Other engine or build disk (must be outside the checkout). |
| `--resume <dir>` | Reuse an attempt with its existing snapshot; newer edits are not included. |
| `--resume <dir> --recook-only` | Recook with linked binaries. |
| `--xcode-stdin-workaround` | Force the `ApplePostBuildSync` route, automatic on macOS 27 / Xcode 27. |

Free space guards: 25 GiB universal, 20 GiB one architecture with APFS clones, 40 GiB
ordinary copies; a watchdog stops the build under 5 GiB. Follow progress with
`tail -f ../builds/macos/<timestamp>/build.log`.

Development packages boot every configured map offline and as a listen server with six
bots, then render SM5 and SM6 offscreen; receipts are in `runtime-smoke/`. This needs
an SM6-capable Mac (M2+, macOS 15+). A menu-only smoke test misses real failures.

The macOS runtime smoke helper passes `-llm` to keep Unreal's low-level memory
tracker enabled. UE 5.8 can otherwise clear the main thread's tracker state while
AppKit holds an allocator scope during startup, causing `FLLMTracker::PopTag` to
assert before map loading. Use `-llm` for direct Development app launches too;
this is a launch workaround, not an engine patch. Shipping disables this tracker
at compile time.

```sh
python3 scripts/test-packaged-runtime.py --executable <App>/Contents/MacOS/RespawnAndRage --all-maps
python3 scripts/test-packaged-runtime.py --executable <App>/Contents/MacOS/RespawnAndRage --render --shader-model sm5
```

FPS clips use compressed playback with normal retargeting.
`scripts/bake_cooked_first_person.py` migrates the owned tracks and runs in the isolated
snapshot before cooking; `-ERFPSCookVerify` requires its saved marker. The pistol clips
use `BC_ER_PistolPrecise`.

## macOS release

`release-macos.py` runs cleanup, checks, isolated build, Developer ID signing, Apple
notarization, stapling, Gatekeeper assessment and Butler upload, in that order.

```sh
python3 scripts/release-macos.py --check    # read-only preflight
python3 scripts/release-macos.py
```

Needs a **Developer ID Application** certificate with private key in Keychain
(`security find-identity -v -p codesigning`), and an Apple account configured in Xcode
or `xcrun notarytool store-credentials RespawnAndRage`. Keep credentials in Keychain,
never in files or chat, and never bypass Gatekeeper.

| Option | Effect |
| --- | --- |
| `--identity '...'` | Required when several identities exist. |
| `--notarization-method auto\|notarytool\|xcode`, `--keychain-profile NAME` | Notarization route. |
| `--init-assets` | Fetch pinned assets first. |
| `--no-upload` | Sign and notarize only. |
| `--resume <release dir>` | Retry signing/notarization of a packaged build. |
| `--upload-only <release dir>` | Retry upload; all checks repeat. |
| `--butler`, `--butler-identity` | Override discovery. |

Space: 30 GiB universal, 25 GiB one architecture on APFS, 45 GiB ordinary copies, plus
5 GiB on the source and home volumes. Xcode's archive route needs twice the app size
plus 1 GiB. Failed runs keep their directory; a successful upload removes the whole
release directory. `scripts/notarize-macos.py` runs signing and notarization alone
(`--check`, `--sign-only`, `--method xcode`). If `xcodebuild` reports `No Accounts`, sign
in again in Xcode Settings → Accounts and use `--resume`; never extract its tokens.

## Not yet verified

Intel execution, a Shipping package, and native Windows runtime behaviour need real
machines. Automated helper tests use mocked tools.
