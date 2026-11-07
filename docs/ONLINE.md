# Online

**Code:** `EROnline`, `ERNetDriverEOS`, `ERPlayerNetwork`, `ERNetworkQuality`,
`ERCellRecovery`, `scripts/prepare-eos-guest.py`, `scripts/eos-guest/`.

## Sessions

Offline, LAN and Internet share the same teams and gameplay. Listen hosting keeps the
host's game open. A separate Linux server target and direct-IP launch workflow are available
in [PACKAGE_DESKTOP.md](PACKAGE_DESKTOP.md); there is no dedicated matchmaking service.

- **LAN:** UDP discovery or direct IP, port 7777, bound to `0.0.0.0` through MULTIHOME. Needs
  no EOS configuration. Exercised on one Mac only: a physical two-PC LAN needs its own test.
- **Internet:** Start Server → Connection: Internet (Epic Online Services), or Play Online →
  Search Servers.
- **Capacity:** Start Server saves a positive human capacity (default 16, host included), with
  no fixed cap. GameSession, admission, LAN availability, lobby status and travel all use it.
  Test bots are additional. Occupied starts overflow to safe positions near the base, waiting
  when no space exists.

All peers must match protocol and build. `RR_VERSION` includes the wire-protocol suffix and
filters incompatible results. Unreal's handshake checks the same build signature and game
protocol for direct-IP connections, including maps without cell descriptors.

## EOS guest identity

Internet play signs in with **EOS Connect Device ID**: `bUseEAS=false`, no Epic account,
launcher, browser or password. The credential is stored per OS user, and the same process
reuses the same hashed Product User ID. Never delete or reset a Device ID for a test.

Configuration: non-secret IDs in `Config/EOS.example.ini`; private `Config/EOS.local.ini` holds the
**untrusted Peer2Peer client** secret (ignored, mode 0600, never printed). It is staged as NonUFS
when present at compile time, so rebuild after adding or removing it. Builds without it keep LAN
and explain that Internet is not configured. Never ship a trusted server credential. The portal
policy `p2p` includes `matchmaking:managePlayers`.

## Plugin override

UE 5.8.3's stock OSS EOS cannot create a Device ID or supply desktop `UserLoginInfo`, and its public
slot mapping hides results from the browser. `scripts/prepare-eos-guest.py` generates an ignored
project plugin from the **locally licensed** engine, with version and source-hash guards, plus
extensions from `scripts/eos-guest`. The installed engine stays untouched. Never commit
`Plugins/OnlineSubsystemEOS`. Build, open and packaging helpers run it automatically.

## Networking

`UERNetDriverEOS` extends `NetDriverEOS`: IP passthrough for ordinary launches, P2P only with
`EROnline=1`, including lobby travel. Do not restore `bIsUsingP2PSockets`. EOS **Sessions** (not
Lobbies) provide the public browser, with a shared `RespawnAndRage` bucket, so capacity follows the
setting. Server names are trimmed to 48 characters. Exit Game waits up to 5s for session destruction.
`EROnline` releases its world-specific OSS instance while EOSShared is still loaded, after net
drivers shut down. Late async completions after cancel destroy their session.

## Slow clients and diagnostics

A remote pawn enters combat only after acknowledging a complete collision snapshot for its
current room, epoch and pawn. Pending pawns are hidden, noncolliding, immobile and cannot deal
or receive damage. Spawn protection starts at actual entry; duplicate acknowledgments never
renew it. One slow connection does not delay the match for ready players.

Progress extends a 60-second stall deadline, with a five-minute absolute admission limit.
Repeated acknowledgments of unchanged progress do not extend it. A stalled connection receives
a synchronization timeout and can reconnect. Live destruction can continue while one owner
downloads an immutable recovery checkpoint; see [DESTRUCTION.md](DESTRUCTION.md).
Recovery after admission retains the player's combat vulnerability and never renews protection.

The online HUD shows measured round-trip latency, smoothed variation between RTT samples,
the larger of inbound/outbound packet loss, and the server's average frame period. A map-age
indicator appears while newer destruction is pending. These diagnostics never authorize shots
or extend rewind. Two small unreliable probes per second measure RTT without client wall-clock
synchronization. `-ERNetworkAudit` logs the same data in development builds.

## Not verified

Real distinct-device `JoinSession`, two-network NAT or relay, and natural token expiry. Two
processes on one OS profile share a Device ID and do not replace remote players. Keep
same-Mac service results separate from cross-network claims.

## Verify

`test-online-service.py` (real EOS, two launches, must exit 0), `test-online-unconfigured.py` in an isolated checkout,
`test-lobby.py`, `test-capacity.py`, `test-network-readiness.py --require-checkpoint` (two local peers; defaults to
15 FPS client, 75ms outgoing delay per peer, 20ms variance and 2% outgoing loss).
Native: `EclipseRecoil.Network.*`, `EclipseRecoil.Destruction.Cells.RecoveryCheckpoint`.
