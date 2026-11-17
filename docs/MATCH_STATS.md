# Match statistics

**Code:** `ERMatchStats`, `UERHealthComponent::ReceiveDamage`, `AERGameMode::FinishRound`,
`ERMatchGraph`, `scripts/export-match-stats.py`.

The authority records each loss of health or armor during a match. Damage blocked by
spawn protection or server rules is excluded. Each damaging pellet or target of a blast
gets a row. A lethal hit is the same row with `is_death=true`. Fall, drowning and
out-of-world damage get explicit causes and no attacker. A blast has no anatomical zone.
Rows contain the final scores, winner, map, optional procedural seed, match mode, match
ID and UTC timestamps. Player IDs are scoped to the match. Names and teams reflect the
event time. If a projectile's attacker disconnects, their launch-time identity is used.
Impact coordinates are Unreal world centimeters.

Recording and automatic export are enabled in packaged games and the dedicated Docker
image. Only the local, listen or dedicated server writes files. The default directory is
`Saved/MatchStats` in Unreal's writable Saved directory. Set `RR_MATCH_STATS_DIR` to an
absolute directory to use another disk or a mounted container volume. The server option
`-ERMatchStatsDir="/absolute/path"` takes precedence. `-ERNoMatchStats` disables recording.

The desktop and standalone server packagers include a private Python and PyArrow runtime
for each target architecture. The Docker image installs its own PyArrow environment.
For editor runs from a source checkout, prepare the same runtime once:

```sh
python3 scripts/stage-match-stats.py --archive . --target mac-arm64
```

`RR_MATCH_STATS_PYTHON` can still override the interpreter with an absolute path to an
environment that contains PyArrow. Standard game packages need neither installation
nor environment overrides.

At the end of each match the background worker saves the pending data under `.pending`
inside the archive directory and runs the exporter. It writes Zstd level 3 Parquet to
`day=YYYY-MM-DD/events.parquet`. The date is the match end date in the server's local
time zone. The exporter writes a new file and replaces the old one after closing the
writer. It skips match IDs already present so retrying a completed write is safe.
The worker leaves pending data for retry after an export error. To export manually,
run the same interpreter with `scripts/export-match-stats.py /path/to/MatchStats`.
Only completed matches are saved.

Open **Combat Graph** from the main or pause menu or press **F2** from the scoreboard.
Choose an archive day and a completed match. Players are grouped by team around an
orbit. Circle radius is the absolute kill-minus-death balance, green for
nonnegative and red for negative. The best balance has a gold outline and the
worst a black outline. A zero balance has a label and no circle. Faint hit strokes
and darker kill strokes appear together; their widths are the corresponding event
counts. Each stroke goes two thirds of the way from attacker to victim. Hover a
player's circle or label to reveal both counts on their lethal links, or click to
keep them visible. Team bars show scores for the selected archive day, split by match.
The host reads its Parquet archive and pending matches off the game thread. Remote clients receive
only the selected match's aggregate and the archive menu, without positions or raw
events. Main menu and Play Offline read the local machine's archive.

In Athena, point a Parquet table at the archive and partition by `day string`. Include
that column in the query predicate to prune days. The physical `match_date` column
also keeps standalone files self-contained. For shared storage, configure one writer
for the archive. Exporter invocations on a single filesystem use a file lock.

## Verify

Run the Mac Editor build and `scripts/test-network.py`. Finish two matches with damage
on the same day, then read `events.parquet` with `pyarrow.parquet.ParquetFile`. Check
that there is one file, both match IDs appear, final scores repeat on their rows, and
a lethal hit has only one row. Replay a pending match and confirm the count stays
constant. Use a separate `RR_MATCH_STATS_DIR` when running tests.
In Development, `-ERMatchGraphProbe` opens the menu and logs the received counts.
With rendering enabled, add `-ERMatchGraphCapture=/absolute/path.png` to capture it.
