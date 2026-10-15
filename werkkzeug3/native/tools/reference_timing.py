#!/usr/bin/env python3
"""Snapshot timing for the native player's 16 ms, integer-sample timeline."""
import json
import math


def snapshot_sample(seconds, rate=44100):
    """First regular simulation step whose sample time reaches the request.

    This predicts uninterrupted playback before its endpoint. A player log is
    required for a duration-clamped endpoint or an interrupted run.
    """
    if not math.isfinite(seconds) or not 0 <= seconds <= 1800 or not 8000 <= rate <= 192000:
        raise ValueError("invalid snapshot time or sample rate")
    tick = math.floor(seconds * 1000 / 16)
    while tick and ((tick - 1) * 16 * rate // 1000) / rate >= seconds:
        tick -= 1
    while (tick * 16 * rate // 1000) / rate < seconds:
        tick += 1
    return tick * 16 * rate // 1000


def snapshot_records(path, requests):
    """Read actual captured samples; requests must retain CLI argument order."""
    records = {}
    for line in path.read_text().splitlines():
        if not line.startswith("Snapshot: "):
            continue
        record = json.loads(line.removeprefix("Snapshot: "))
        fields = ("version", "index", "sample", "sample_rate", "frame_index", "milliseconds", "beat_fixed")
        if (not isinstance(record, dict) or any(type(record.get(k)) is not int for k in fields) or
            record["version"] != 1 or record["index"] in records or
            not 0 <= record["index"] < len(requests) or not 8000 <= record["sample_rate"] <= 192000):
            raise ValueError("invalid or duplicate snapshot record")
        requested, sample, rate = record.get("requested_seconds"), record["sample"], record["sample_rate"]
        if (type(requested) not in (int, float) or not math.isfinite(requested) or
            requested != requests[record["index"]] or not 0 <= requested <= 1800 or
            not 0 <= sample <= 1800 * rate or sample / rate < requested or
            record["milliseconds"] != sample * 1000 // rate or record["frame_index"] < 0):
            raise ValueError("snapshot log does not match the requested captures")
        records[record["index"]] = record
    if len(records) != len(requests):
        raise ValueError("snapshot log is incomplete; retain every --snapshot in its original order")
    return [records[i] for i in range(len(requests))]
