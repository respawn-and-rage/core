#!/usr/bin/env python3
"""Merge the Unreal server's pending matches into one Parquet file per day."""

import argparse
from collections import defaultdict
from contextlib import contextmanager
from datetime import date, datetime, timezone
import json
import os
from pathlib import Path
import sys
import tempfile
from uuid import UUID

import pyarrow as pa
import pyarrow.compute as pc
import pyarrow.parquet as pq


TIMESTAMPS = ("event_at", "match_started_at", "match_ended_at")
SCHEMA = pa.schema([
    ("schema_version", pa.int32()),
    ("match_id", pa.string()),
    ("match_date", pa.date32()),
    *((name, pa.timestamp("ms")) for name in TIMESTAMPS),
    ("map", pa.string()),
    ("map_seed", pa.int32()),
    ("mode", pa.string()),
    ("alpha_score", pa.int32()),
    ("omega_score", pa.int32()),
    ("winner", pa.string()),
    ("event_index", pa.int32()),
    ("cause", pa.string()),
    ("weapon", pa.string()),
    ("bone", pa.string()),
    ("body_zone", pa.string()),
    *((f"{player}_{field}", kind)
      for player in ("attacker", "victim")
      for field, kind in (("id", pa.int32()), ("name", pa.string()),
                          ("team", pa.string()), ("is_bot", pa.bool_()))),
    *((f"impact_{axis}_cm", pa.float32()) for axis in "xyz"),
    ("hp_damage", pa.float32()),
    ("armor_damage", pa.float32()),
    ("is_death", pa.bool_()),
    ("is_headshot", pa.bool_()),
])


@contextmanager
def archive_lock(root):
    # Keep the lock file in place so waiting processes always lock the same file.
    with (root / ".archive.lock").open("a+b") as lock:
        if os.name == "nt":
            import msvcrt
            lock.seek(0)
            msvcrt.locking(lock.fileno(), msvcrt.LK_LOCK, 1)
        else:
            import fcntl
            fcntl.flock(lock, fcntl.LOCK_EX)
        yield


def read_match(path):
    data = json.loads(path.read_text(encoding="utf-8"))
    if data["schema_version"] != 1 or str(UUID(data["match_id"])) != path.stem:
        raise ValueError(f"Unsupported match file: {path}")
    events = data.pop("events")
    data["match_date"] = date.fromisoformat(data["match_date"])
    rows = []
    for event in events:
        row = {**data, **event}
        for name in TIMESTAMPS:
            iso = row[name].replace("Z", "+00:00")
            row[name] = datetime.fromisoformat(iso).astimezone(timezone.utc).replace(tzinfo=None)
        rows.append(row)
    return data["match_date"], data["match_id"], rows


def merge_day(destination, matches):
    previous = pq.ParquetFile(destination) if destination.exists() else None
    try:
        if previous and previous.schema_arrow != SCHEMA:
            raise ValueError(f"Unexpected schema in {destination}")
        existing = set()
        if previous:
            for batch in previous.iter_batches(columns=["match_id"]):
                existing.update(batch.column(0).to_pylist())
        added = [row for match_id, rows, _ in matches if match_id not in existing for row in rows]
        if not added:
            return

        destination.parent.mkdir(parents=True, exist_ok=True)
        with tempfile.NamedTemporaryFile(prefix=".events-", suffix=".tmp",
                                         dir=destination.parent, delete=False) as temporary:
            temp_path = Path(temporary.name)
        try:
            with pq.ParquetWriter(temp_path, SCHEMA, compression="zstd",
                                  compression_level=3) as writer:
                if previous:
                    for batch in previous.iter_batches():
                        writer.write_batch(batch)
                writer.write_table(pa.Table.from_pylist(added, schema=SCHEMA))
            if previous:
                previous.close()  # Windows requires the destination to be closed for replacement.
                previous = None
            with temp_path.open("r+b") as file:
                os.fsync(file.fileno())
            os.replace(temp_path, destination)
        finally:
            temp_path.unlink(missing_ok=True)
    finally:
        if previous:
            previous.close()


def export(root):
    failed = False
    with archive_lock(root):
        days = defaultdict(list)
        for path in sorted((root / ".pending").glob("*.json")):
            try:
                day, match_id, rows = read_match(path)
                days[day].append((match_id, rows, path))
            except (ValueError, TypeError, KeyError, OSError) as error:
                print(f"{path}: {error}", file=sys.stderr)
                failed = True

        for day, matches in sorted(days.items()):
            destination = root / f"day={day.isoformat()}" / "events.parquet"
            try:
                # Group replay by match time so rows land in chronological order.
                matches.sort(key=lambda match: match[1][0]["match_started_at"])
                merge_day(destination, matches)
                for _, _, path in matches:
                    path.unlink()
            except (ValueError, OSError, IndexError, pa.ArrowException) as error:
                print(f"{destination}: {error}", file=sys.stderr)
                failed = True
    return int(failed)


def graph_archive(root, day, match_id):
    """Read only the selected day and return aggregates suitable for game clients."""
    files = {p.parent.name[4:]: p for p in root.glob("day=*/events.parquet")}
    pending = defaultdict(list)
    for path in (root / ".pending").glob("*.json"):
        try:
            match_day, _, rows = read_match(path)
        except (ValueError, TypeError, KeyError, OSError):
            continue
        if rows:
            pending[match_day.isoformat()].append(rows)
    days = sorted(files.keys() | pending.keys(), reverse=True)
    day = day or next(iter(days), None)
    if not day:
        return {"days": [], "matches": []}
    if day not in days:
        raise ValueError("No archived matches for this date.")

    meta = ["match_id", "map", "mode", "match_ended_at", "alpha_score", "omega_score"]
    matches = {}
    if day in files:
        archive = pq.ParquetFile(files[day])
    else:
        archive = None
    try:
        if archive:
            for batch in archive.iter_batches(columns=meta):
                for row in batch.to_pylist():
                    matches.setdefault(row["match_id"], row)
        pending_only = {rows[0]["match_id"]: rows for rows in pending[day]
                        if rows[0]["match_id"] not in matches}
        for guid, rows in pending_only.items():
            matches[guid] = {key: rows[0][key] for key in meta}

        recent = sorted(matches.values(), key=lambda row: (row["match_ended_at"], row["match_id"]),
                        reverse=True)
        match_id = match_id or (recent[0]["match_id"] if recent else "")
        if match_id not in matches:
            return {"days": days, "day": day, "matches": recent}

        columns = ["match_id", "cause", "hp_damage", "armor_damage", "is_death"]
        columns += [f"{player}_{field}" for player in ("attacker", "victim")
                    for field in ("id", "name", "team", "is_bot")]

        def selected_events():
            if match_id in pending_only:
                yield from pending_only[match_id]
            elif archive:
                for batch in archive.iter_batches(columns=columns):
                    yield from batch.filter(pc.equal(batch.column(0), match_id)).to_pylist()

        players = {}
        edges = {}
        for row in selected_events():
            for role in ("attacker", "victim"):
                player_id = row.get(f"{role}_id")
                if player_id is None:
                    continue
                player = players.setdefault(player_id, {"id": player_id, "kills": 0, "deaths": 0,
                                                          "team_kills": 0, "environment": {}})
                player.update(name=row[f"{role}_name"], team=row[f"{role}_team"],
                              bot=row[f"{role}_is_bot"])
            attacker, victim = row.get("attacker_id"), row.get("victim_id")
            if row["is_death"] and victim is not None:
                players[victim]["deaths"] += 1
                if attacker is None or attacker == victim:
                    cause = row["cause"]
                    counts = players[victim]["environment"]
                    counts[cause] = counts.get(cause, 0) + 1
                elif row["attacker_team"] == row["victim_team"]:
                    players[attacker]["team_kills"] += 1
                else:
                    players[attacker]["kills"] += 1
            if attacker is None or victim is None or attacker == victim:
                continue
            edge = edges.setdefault((attacker, victim), {"from": attacker, "to": victim, "hits": 0,
                                                         "kills": 0, "hp": 0.0, "armor": 0.0})
            edge["hits"] += 1
            edge["kills"] += int(row["is_death"])
            edge["hp"] += row["hp_damage"]
            edge["armor"] += row["armor_damage"]
        for edge in edges.values():
            edge["hp"] = round(edge["hp"], 2)
            edge["armor"] = round(edge["armor"], 2)
        return {"days": days, "day": day, "matches": recent, "selected": match_id,
                "players": list(players.values()), "edges": list(edges.values())}
    finally:
        if archive:
            archive.close()


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("archive", type=Path, help="MatchStats directory containing .pending")
    parser.add_argument("--graph", action="store_true", help="Read the combat graph as JSON")
    parser.add_argument("--day", type=date.fromisoformat)
    parser.add_argument("--match", type=UUID)
    arguments = parser.parse_args()
    if arguments.graph:
        try:
            graph = graph_archive(arguments.archive, arguments.day.isoformat() if arguments.day else None,
                                  str(arguments.match) if arguments.match else None)
            print(json.dumps(graph, default=lambda value: value.isoformat(), ensure_ascii=True,
                             separators=(",", ":")))
        except (ValueError, OSError, pa.ArrowException) as error:
            print(str(error), file=sys.stderr)
            sys.exit(1)
        sys.exit(0)
    sys.exit(export(arguments.archive))
