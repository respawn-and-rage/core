# Procedural map playability

Reads a generated map as a tactical graph and says why it is weak. Reports never change a layout.
[Candidate selection](#candidate-selection), off by default, optimizes optional tactical metrics.
Initial spawn occlusion is a separate mandatory gameplay-generation rule, including Shipping.

**Code:** `ERMapPlayability.h/.cpp`, `ERMapPlayabilityTests.cpp`, `AERProceduralMap::GenerateLogicalWorld`
and `OnGenerationFinished`. Rooms macro graph: `ERRoomsMacro.h/.cpp`, `ERRoomsMacroTests.cpp`, `BuildRooms`.
Spawn scoring: `ERSpawnRisk.h/.cpp`, `ERSpawnRiskTests.cpp`, `AERGameMode::ChooseScoredStart`.
Selection and stress: `ERMapSelection.h/.cpp`, `ERMapSelectionTests.cpp`, `ERPlayability::Stress`,
`AERProceduralMap::ResolveMapCandidate`.

## Use

```sh
python3 scripts/test-map-playability.py
python3 scripts/test-map-playability.py --batch --reports
python3 scripts/preview-map-sizing.py --team-size 4 --seeds 42 --playability
```

In any non-Shipping run, `-ERMapPlayability=ReportOnly` analyses each map the authority generates.
Without that flag no analysis code runs, and in Shipping `ERPlayability::Mode()` is always `Off`.
Rollback is removing the flag. Output lands in `Saved/Testing/MapPlayability/`: one JSON per stage,
`<Archetype>-<N>v<N>-seed<Seed>-<stage>.json`, plus batch summaries `layout-summary.csv` and
`physical-summary.csv`. Each analysis logs one `ER_MAP_PLAYABILITY_LIVE` line.
[Example report](references/map-playability-report.example.json).

The overlay draws the graph in blue, chokes in magenta, spawn sight lines dashed in red.

## Two stages

| Stage | When | Works on | Decides |
| --- | --- | --- | --- |
| `layout` | After `ERProcedural::Build`, before the cell world commits | `FERProceduralLayout`, panels included | Topology, sight against known solids |
| `physical` | `OnGenerationFinished`, collision complete | The real world | Capsule sweep of every walking edge, real sight and shot traces |

The physical stage marks edges confirmed or blocked, then reevaluates the topology. Traces never run
at the layout stage: no geometry exists yet. Only the authority analyses, and clients are unaffected.

The layout stage knows boxes, cell panels with their holes, and Rooms shell faces. Hollow house
skins, mesh colliders and graded terrain are counted as `unknown_solids`. They can only hide more,
and known solids shrink by 1cm for sight, because rays that graze a cell edge can pass in real
collision. So a clear sight verdict stands, but a visible pair over unknown solids stays
`not_evaluated` until the physical stage.

## Initial spawn rule

Every gameplay template and positive team-size profile must block both visibility and direct
projectile lines between **all** opposing starts on the intact map. No pair downsampling is
allowed. Checks cover both directions, standing eye height before/after settling, and lateral
head, chest and leg samples. Glass, invisible collision, perforated pawn barriers and unknown
art cannot provide the opaque-cover proof.

`ERProcedural::Build` retries deterministic compositions until known visible opaque solids
prove occlusion. Authority repeats the check against real Visibility and Projectile collision
before publishing an epoch or becoming ready. Failure blocks match start and player placement;
there is no exposed-spawn fallback. Geometry-only editor studies (`ExpectedTeamSize=0`) do not
approve a layout for gameplay. Matches always set a positive server team-size profile.

Initial overflow and private bot starts pass the same current-collision test against every
opposing registered start. After destruction, these lines may open and normal respawn-risk
scoring applies; the rule does not make cover indestructible.

## Graph

| Archetype | Coverage | Source | Exits |
| --- | --- | --- | --- |
| Rooms | `exact` | `RoomNodes`, `RoomLinks`: the sealed shell allows nothing else | Evaluated |
| Fire Training | `routes` | Patrol, access, crossing, ladders; base zone is the court | Evaluated |
| Alleys | `routes` | Patrol, district and underpass routes; base zone around the starts | Evaluated |
| Central Building | `routes` | Patrol, crossing, stairs | `not_evaluated`: open yard |
| Village | `routes` | Reserved exits, passages, bypasses, stairs | `not_evaluated`: open field |

Routes that cross are cut at the crossing. Loose ends and starts join nearby routes when a standing
pawn fits against known solids. Those joins are unconfirmed until swept, and a join may never cross
a bounded base zone: bases are left through validated routes only. Stairs and ladders are not swept:
each template's pawn probes prove them.

Missing metadata reads `not_evaluated` with a reason, never `pass`. An `exact` graph that loses a
zone fails; a `routes` graph only warns, because open ground may join what metadata cannot. Fire
Training's isolated nodes are ladders onto cargo tops, dead ends far from any recorded route.

## Metrics

| Code | Reads | State |
| --- | --- | --- |
| `CONNECTED_ZONES` | Components, isolated nodes, shortest base-to-base walk, edges unconfirmed or blocked | Active |
| `BASE_EXIT_PATHS` | Edges leaving the base zone; vertex-disjoint paths between bases, so routes that merge count once; chokes every path from the starts crosses | Active |
| `INITIAL_SPAWN_LOS` | Opposing start pairs with a line, eye to head, chest and legs, both ways | Active |
| `ROUTE_DIVERSITY` | Cheapest walk between bases, then the cheapest avoiding its nodes, and their ratio | Active |
| `DOMINANT_VANTAGE` | Raised nodes against base exits: the largest share of one team's exits a single node sees | Active |
| `ROUTE_EXPOSURE` | Points along the direct walk seen from base exits and raised nodes, and the longest open run | Active |
| `COVER_ACCESS` | | Planned, `not_evaluated` |

Sight and shots stay separate: `visible_pairs` traces `ECC_Visibility`, `open_shot_pairs` the
Projectile channel. A blocked shot can still penetrate, so neither proves ballistic cover. Pawn
barriers stop movement but neither sight nor shots. Visual-only foliage has no collision, so it
never blocks a line here, although players cannot see through it.

Neighbouring common nodes group into one choke: both sides of a door are one. `alpha_chokes` also
counts chokes inside the base, which is how a spawn room with one door surfaces behind two gates.

`FERPlayabilityPolicy` carries only measuring parameters and the proposed minimum of two independent
paths. Initial spawn exposure is a hard generation failure on a fresh gameplay map. Other warnings
are findings to read; optional tactical preferences live in the selection profiles. Route sight
is bounded by `MaxVantages`, `MaxExits`, `MaxObservers` and `MaxRouteSamples`. With unknown solids it
overstates a lot at the layout stage: compare it at the physical stage, which uses `ECC_Visibility`.

## Units and determinism

Layout and analysis use centimetres; JSON uses metres (`_m`, `position_m`) and milliseconds.
Positions are map-local. Pawn-centre samples sit at 100cm above the floor, as builders record them.

Analysis takes a const layout and draws no random numbers, so it cannot move geometry. A seed and
policy give identical report bytes without `timings_ms`. Order follows metadata, never `TMap`
iteration. Node ids are stable only within one layout: compare `source` across code versions.

## Not covered

- Graph edges carry length and width, not time: gameplay speeds are not read yet.
- Routes through open ground can underestimate alternatives. Central Building and Village keep their
  counts in the report, without a verdict.
- Selection reads the layout stage: real collision does not exist yet for a candidate.

## Rooms macro graph

Prototype, off by default. A real PCGEx graph picks connections for `Rooms`, and the C++ builder
turns them into corridors. **Matches never run PCG:** only the baked table reaches them.

```sh
python3 scripts/test-rooms-macro.py
python3 scripts/test-rooms.py --network --macro
```

Enable it on the authority with `?RoomsMacroGraph=1` or `-ERRoomsMacroGraph` (non-Shipping).
`FERProceduralConfig::bEnablePCGExMacroGraph` travels in the descriptor, so clients build the same
plan and never read a local flag. Rollback: omit the option. Off, or with no usable plan, the builder
keeps the legacy spine. Selection hashes the seed and consumes no geometry RNG.

| Step | Where | Runs |
| --- | --- | --- |
| Slots from real plans, half-turn paired jitter | `UERRoomsMacroBake::FillInput`, `DA_RoomsMacroInput` | Authoring |
| `Connect Points` with `Probe: KNN`, then `Refine: Gabriel` | `PCG_RoomsMacroGraph` | Authoring |
| Links kept as slot pairs in `FERRoomsMacroVariant` | `UERRoomsMacroBake::Bake`, `DA_RoomsMacroTable` | Authoring |
| Variant choice, geometric checks, corridors | `BuildRooms` | Every peer, deterministic |

`scripts/setup_rooms_macro_graph.py` bakes through the commandlet, and `verify_rooms_macro_graph.py`
reruns the saved graph against the table. Graph and input are authoring assets, referenced by nothing
at runtime; `DA_Rooms` references only the table. PCGEx is 0.79.2 and no Pro component is used. The
only PCGEx contract read is `PCGEx/VData` and `PCGEx/EData`, inside the bake.

**Why not at runtime.** `ERProcedural::Build` is synchronous and every peer repeats it from the seed,
while PCG advances over ticks. The bake needs a bounded pump, and input assets must already be
loaded. Inside the automation tick the graph did not finish in 120s. Running it per peer would also
put the map's agreement on an unverified re-execution. No dedicated server target exists to test
PCG there, so graph execution on a server is unproven.

A variant may keep a base-to-centre bend, a central crossing and rungs to the lanes. `BuildRooms`
rejects a corridor that is steeper than 1:2 or that touches another volume. It then tries the plan
without rungs, then without the bend, and finally the next variant. Dropping the bend closes the
lower spine: the bases keep both lanes, so they drop from three paths to two. Plans with one room
per lane never use the table, and the graph yields no variant for four rooms per lane.

## Spawn scoring

`ERSpawnRisk` orders the starts that already passed every placement filter, once per respawn, on the
server, on procedural maps. **On by default;** the advanced rule `Health.SpawnScoring` set to 0 restores
the plain random choice, which Depot always keeps. Private bot starts and overflow placement keep
their own paths. No start stays no start.

| Risk | Reads | Weight |
| --- | --- | --- |
| Open shot on the standing capsule | Living enemy eye to head and chest, Projectile channel, current collision | Very high, per enemy |
| Live enemy Claymore | Inside its real damage radius and fan | High |
| Enemy close | Straight distance against `Move.Sprint` × `Health.Protection` | Medium |
| Recent team death in the sector | `RecordDeath`, fading over 15s | Low |
| Same start just used | Fading over 15s | Low |

Straight distance ignores walls, so it overstates closeness; graph distance is not used yet. Exits
held by enemies and live grenades are not read. Candidates within `Comparable` of the best are a
random draw, so spawns stay unpredictable. If every start is exposed the lowest risk wins, and no
protection is added. Each respawn spends at most `MaxTraces` traces, nearest enemies first, on at most
`MaxCandidates` starts; nothing runs per tick or is cached. Memory is bounded and resets with the arena.

`-ERSpawnAudit` logs `ER_SPAWN_CHOICE` per respawn in non-Shipping runs. `exposed/candidates` is the
chance a random choice would have been exposed, and `chosen_seen` is what scoring picked. A team whose
starts share one room gains nothing while enemies stand in it: every candidate is exposed together.

## Candidate selection

Off by default. Enable it on the authority with `?MapSelection=1` or `-ERMapSelection` (non-Shipping).
Rollback: omit the option, and candidate 0 is exactly the plain seed.

The authority builds and analyses a few layouts of the seed, before generating, and writes the
winner to `FERProceduralConfig::MapCandidate`. That index travels in the descriptor and shifts the
seed of the normal `ERProcedural::Build` attempts, so **clients rebuild the plan without analysing
anything**, and floating-point differences between platforms cannot split the map. Nothing runs
during play; each new seed or round selects once, sharing the result with the early start preparation.

`ERMapSelection::ProfileFor` holds one profile per archetype:

| Archetype | Candidates | Hard rules | Preferred ranges |
| --- | --- | --- | --- |
| Rooms | 6 | Whole exact graph, two independent paths, no spawn sight | Three paths |
| Central Building | 6 | No spawn sight at every size | Two paths, a lateral route, a partly open crossing |
| Alleys | 4 | No spawn sight | No spawn sight, three paths |
| Village | 6 | No spawn sight | No spawn sight, two paths, a lateral route, a partly open crossing |
| Fire Training | 1 | No spawn sight | None: it keeps the plain seed and pays nothing |

Spawn occlusion applies to all archetypes, even when selection is disabled or has one
candidate. The common generator rejects a plan that depends only on unknown art or terrain
for cover. Real-collision validation remains mandatory before gameplay readiness.

Each range met adds a point, and spawn sight outweighs the others together: no line on the layout
means no line in real collision. Ranges never rescue a broken rule. The plain seed keeps any tie, so a
map moves only for a reason, and the seed breaks other ties. With no tactically clean candidate, a built layout may be a `fallback=1` only if its initial
spawn occlusion passed. No built safe candidate means generation fails. No candidate is ever regenerated during a match.

`bPreferBreachSafe` then adds one point to the best clean candidates whose `central_breach` opens no
new spawn sight, read on the layout stage (`breach=` in the log). It only separates plans that tie, and runs only
when the budget was not hit, because a candidate left unread would make the choice depend on machine
speed. It always reads the layout, so unknown solids can overstate a breach and move a plan needlessly,
never the reverse.

`BudgetMs` is a safety valve well above the measured cost. It is the only way machine speed could
change a seed's outcome, so hitting it logs `budget_hit=1` and the batch fails on it. Every selection
logs one `ER_MAP_SELECTION` line with the winner and each candidate's rejection reasons, and
`ER_BLOCK_GENERATED` reports `candidate=`.

## Destruction stress

`ERPlayability::Stress` repeats the layout analysis on deterministic damaged states. Each policy
names its scenarios in `FERPlayabilityPolicy::Stress`, set by `PolicyFor`:

| Scenario | Damage | Applies when | Archetypes |
| --- | --- | --- | --- |
| `central_breach` | Door-sized hole through every layer of the wall nearest the middle of the line between bases | A breakable wall lies there | All |
| `cover_removed` | Up to three whole panels 60-170cm tall near the direct walk | Breakable low cover exists | All but Rooms |
| `high_sightline` | Same hole in a wall that hides a base exit from a raised node | Such a wall exists | All |
| `walkway_lost` | Walking edges of a timber crossing that really falls with its supports | The layout has `WallBridges` | Central Building |

Only breakable panels change: fixed pieces and hollow skins never do, and a scenario with nothing real
to damage reports not applicable with its reason. `walkway_lost` is therefore never applicable to
fixed catwalks. A breach adds no walking edge, so damaged paths can only understate new routes. Spawns
in play always read current collision.

The report's `stress` array holds one entry per scenario of the policy, read from the layout, and
each analysis under `-ERMapPlayability=ReportOnly` fills it. It never changes the layout it reads.
The physical stage keeps those readings unchanged. Candidate selection uses only `central_breach`.

`EclipseRecoil.Maps.Stress.Physics` makes each applicable scenario real in the cell world: it cuts the
same openings or the crossing's supports, settles support, removes debris and repeats the physical
reading, both on the plain seed and on the chosen plan. A cut crossing must fall while the opposite one
stands, and real sight may exceed the abstract state only when other cells fell too.

Cell support is evaluated only after damage, so anything unsupported on a fresh map stands until the
first cut anywhere on it. `EclipseRecoil.Maps.Stress.FreshSupport` lists those pieces in
`fresh-support.csv`, per team size and seed, and fails if a Village plan has any. The other templates
are only reported: Central Building’s rectangular authored studies include a fence plan with loose panels; the
circular builder also has a dedicated fresh-support regression test.

## Verify

`test-map-selection.py` (`--batch --physics`) runs `EclipseRecoil.Maps.Selection.*` and
`EclipseRecoil.Maps.Stress.*`, writing `selection-candidates.csv`, `selection-physical.csv`,
`stress-summary.csv`, `stress-physical.csv` and `fresh-support.csv`. `--stress-teams` picks the team
sizes of the real stress run. `test-rooms.py --network --selection --seed 3` checks host and late clients.
`test-spawn-risk.py` runs `EclipseRecoil.Maps.SpawnRisk.*` and timed bot matches.
`test-map-playability.py` runs `EclipseRecoil.Maps.Playability.*` and proves report-only neutrality:
identical layout digests, cell panels included, with and without the flag, on the same seeds. `--batch` covers 20 seeds
per template and size.
