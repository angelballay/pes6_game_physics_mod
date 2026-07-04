#!/usr/bin/env python3
"""
Analyze PES6 AI behavior blackbox logs from fix8/fix9.

VERSION: fix9_motion_funnel_analyzer

Default batch mode:
  python analyze_behavior_log.py

This scans every *.txt file located next to this .py and creates one output
folder per log file.

Explicit batch folder:
  python analyze_behavior_log.py --batch D:/pes/IA/tests

Single file mode:
  python analyze_behavior_log.py D:/pes/IA/logs.txt out_dir
"""
from __future__ import annotations

import csv
import math
import re
import sys
from collections import Counter, defaultdict
from pathlib import Path
from typing import Dict, Iterable, List, Optional, Tuple

PAIR_RE = re.compile(r"(\w+)=([^\s]+)")
EVENT_KINDS = ("AI_DECISION", "AI_STATE_CHANGE", "AI_FIELD_CHANGE", "AI_11C253_FOCUS", "AI_COMMIT_POSITIVE", "AI_PROBE")
VERSION = "fix9_motion_funnel_analyzer"

DIST_BINS = [0, 250, 500, 1000, 2000, 4000]


def parse_line(line: str) -> Optional[Dict[str, str]]:
    kind = None
    for k in EVENT_KINDS:
        if line.startswith(f"[{k}]"):
            kind = k
            break
    if not kind:
        return None
    d: Dict[str, str] = {"kind": kind}
    for m in PAIR_RE.finditer(line):
        d[m.group(1)] = m.group(2)
    return d


def get_float(rec: Dict[str, str], key: str) -> Optional[float]:
    value = rec.get(key)
    if value is None or value in ("?", "nan", "none", "NONE", ""):
        return None
    try:
        v = float(value)
    except ValueError:
        return None
    if math.isnan(v) or math.isinf(v) or v < 0:
        return None
    return v


def get_int(rec: Dict[str, str], key: str) -> Optional[int]:
    value = rec.get(key)
    if value is None or value in ("?", ""):
        return None
    try:
        if value.lower().startswith("0x"):
            return int(value, 16)
        return int(value)
    except Exception:
        return None


def state_hex(state: str) -> str:
    # input example: 22/0x16:3/0x0003
    if not state or state == "?":
        return "?"
    m = re.search(r"0x([0-9A-Fa-f]{2})", state)
    if not m:
        return "?"
    sub = "?"
    sm = re.search(r":(\d+)/0x([0-9A-Fa-f]{4})", state)
    if sm:
        sub = sm.group(2).upper()
    return f"0x{m.group(1).upper()}:{sub}"


def is_state(rec: Dict[str, str], hex_state: str, sub_hex: Optional[str] = None, key: str = "to") -> bool:
    value = rec.get(key, "")
    if f"0x{hex_state.upper()}" not in value.upper():
        return False
    if sub_hex is not None and f"0x{sub_hex.upper()}" not in value.upper():
        return False
    return True


def dist_bin(v: Optional[float]) -> str:
    if v is None:
        return "missing"
    prev = 0
    for b in DIST_BINS[1:]:
        if v < b:
            return f"{prev}-{b}"
        prev = b
    return f">={DIST_BINS[-1]}"


def write_counter(path: Path, counter: Counter, headers=("key", "count")) -> None:
    with path.open("w", newline="", encoding="utf-8") as f:
        w = csv.writer(f)
        w.writerow(headers)
        for k, v in counter.most_common():
            if isinstance(k, tuple):
                w.writerow([*k, v])
            else:
                w.writerow([k, v])


def write_rows(path: Path, rows: Iterable[Dict[str, object]], fields: List[str]) -> None:
    with path.open("w", newline="", encoding="utf-8") as f:
        w = csv.DictWriter(f, fieldnames=fields, extrasaction="ignore")
        w.writeheader()
        for row in rows:
            w.writerow(row)


def sanitize_folder_name(name: str) -> str:
    cleaned = re.sub(r"[<>:\"/\\|?*\x00-\x1F]", "_", name).strip(" .")
    return cleaned or "log_analysis"


def unique_output_dir(base_dir: Path, desired_name: str) -> Path:
    candidate = base_dir / sanitize_folder_name(desired_name)
    if not candidate.exists() or candidate.is_dir():
        return candidate
    suffix = 2
    while True:
        candidate = base_dir / f"{sanitize_folder_name(desired_name)}_{suffix}"
        if not candidate.exists() or candidate.is_dir():
            return candidate
        suffix += 1


def avg(vals: List[float]) -> Optional[float]:
    return sum(vals) / len(vals) if vals else None


def pct(a: int, b: int) -> float:
    return (100.0 * a / b) if b else 0.0


def analyze_log(log_path: Path, out_dir: Path) -> Dict[str, object]:
    out_dir.mkdir(parents=True, exist_ok=True)

    counters = {
        "kind": Counter(),
        "branch": Counter(),
        "branch_diff": Counter(),
        "transitions": Counter(),
        "transitions_diff": Counter(),
        "phase": Counter(),
        "spatial_intent": Counter(),
        "motion_intent": Counter(),
        "active_source": Counter(),
        "positive": Counter(),
        "state_to": Counter(),
        "state_from": Counter(),
        "ball84": Counter(),
        "decision_diff": Counter(),
        "distance_bins": Counter(),
        "distance_bins_11c253": Counter(),
        "distance_bins_positive": Counter(),
    }

    decisions: List[Dict[str, str]] = []
    positive_decisions: List[Dict[str, str]] = []
    positive_unique: Dict[Tuple[str, str, str, str, str], Dict[str, str]] = {}
    negative_11c253: List[Dict[str, str]] = []
    total_lines = parsed_lines = 0

    with log_path.open("r", encoding="utf-8", errors="ignore") as f:
        for line in f:
            total_lines += 1
            rec = parse_line(line.strip())
            if not rec:
                continue
            parsed_lines += 1
            kind = rec.get("kind", "?")
            branch = rec.get("branch", "?")
            diff = rec.get("diff", "?")
            counters["kind"][kind] += 1
            counters["branch"][branch] += 1
            counters["branch_diff"][(branch, diff)] += 1
            counters["phase"][rec.get("phase", "?")] += 1
            counters["spatial_intent"][rec.get("spatial_intent", "?")] += 1
            counters["motion_intent"][rec.get("motion_intent", "?")] += 1
            counters["active_source"][rec.get("active_source", "?")] += 1
            counters["ball84"][rec.get("ball84", "?")] += 1

            b_real = dist_bin(get_float(rec, "dist_actor_ball"))
            b_pred = dist_bin(get_float(rec, "dist_actor_ball_pred"))
            counters["distance_bins"][("real", b_real)] += 1
            counters["distance_bins"][("pred", b_pred)] += 1
            if "11C253" in branch:
                counters["distance_bins_11c253"][("real", b_real, diff)] += 1
                counters["distance_bins_11c253"][("pred", b_pred, diff)] += 1

            if rec.get("positive_candidate") == "1":
                counters["positive"][(branch, diff)] += 1
                counters["distance_bins_positive"][("real", b_real, diff)] += 1
                counters["distance_bins_positive"][("pred", b_pred, diff)] += 1

            if kind in ("AI_DECISION", "AI_STATE_CHANGE"):
                frm = rec.get("from", "?")
                to = rec.get("to", "?")
                counters["transitions"][(frm, to, branch)] += 1
                counters["transitions_diff"][(frm, to, diff, branch)] += 1
                counters["state_from"][frm] += 1
                counters["state_to"][to] += 1

            if kind == "AI_DECISION":
                decisions.append(rec)
                counters["decision_diff"][diff] += 1
                if rec.get("positive_candidate") == "1":
                    positive_decisions.append(rec)
                    key = (rec.get("tick", "?"), rec.get("actor", "?"), branch, rec.get("from", "?"), rec.get("to", "?"))
                    positive_unique.setdefault(key, rec)
                elif "11C253" in branch:
                    negative_11c253.append(rec)

    # Basic counters.
    write_counter(out_dir / "kind_counts.csv", counters["kind"])
    write_counter(out_dir / "branch_counts.csv", counters["branch"])
    write_counter(out_dir / "branch_by_diff.csv", counters["branch_diff"], ("branch", "diff", "count"))
    write_counter(out_dir / "transitions.csv", counters["transitions"], ("from", "to", "branch", "count"))
    write_counter(out_dir / "transitions_by_diff.csv", counters["transitions_diff"], ("from", "to", "diff", "branch", "count"))
    write_counter(out_dir / "phase_counts.csv", counters["phase"])
    write_counter(out_dir / "spatial_intent_counts.csv", counters["spatial_intent"])
    write_counter(out_dir / "motion_intent_counts.csv", counters["motion_intent"])
    write_counter(out_dir / "active_source_counts.csv", counters["active_source"])
    write_counter(out_dir / "positive_by_branch_diff.csv", counters["positive"], ("branch", "diff", "count"))
    write_counter(out_dir / "state_to_counts.csv", counters["state_to"], ("state_to", "count"))
    write_counter(out_dir / "state_from_counts.csv", counters["state_from"], ("state_from", "count"))
    write_counter(out_dir / "ball84_counts.csv", counters["ball84"], ("ball84", "count"))
    write_counter(out_dir / "decision_diff_counts.csv", counters["decision_diff"], ("diff", "count"))
    write_counter(out_dir / "distance_bins_all.csv", counters["distance_bins"], ("metric", "bin", "count"))
    write_counter(out_dir / "distance_bins_11C253.csv", counters["distance_bins_11c253"], ("metric", "bin", "diff", "count"))
    write_counter(out_dir / "distance_bins_positive.csv", counters["distance_bins_positive"], ("metric", "bin", "diff", "count"))

    decision_fields = [
        "tick", "diff", "branch", "actor", "team12", "index11", "from", "to",
        "state_changed", "field_changed", "positive_candidate", "phase", "spatial_intent", "motion_intent",
        "active", "active_valid", "active_source", "ball_owner", "ball_owner_valid", "ball_owner_source",
        "ball84", "ball88", "ball_actor_id", "actor_best", "active_best",
        "dist_actor_ball", "dist_actor_active", "dist_active_ball", "dist_actor_ball_pred",
        "prev_dist_actor_ball", "prev_dist_actor_active", "prev_dist_active_ball", "prev_dist_actor_ball_pred",
        "delta_actor_ball", "delta_actor_active", "delta_active_ball", "delta_actor_ball_pred",
        "actor_speed", "ball_speed", "active_speed", "pred_speed", "moving_to_ball", "moving_to_active", "moving_to_pred",
        "actor_b0", "actor_dir", "actor_r1", "actor_r2", "actor_l2", "actor_p114", "actor_anim30",
        "active_b0", "active_dir", "active_r1", "active_r2", "active_l2", "active_p114", "active_anim30",
        "samples_in_state", "state_age_ms", "timer114", "f98", "ball50", "targets",
    ]
    write_rows(out_dir / "ai_decisions_sample.csv", decisions[:10000], decision_fields)
    write_rows(out_dir / "positive_decisions.csv", positive_decisions, decision_fields)
    write_rows(out_dir / "positive_unique.csv", positive_unique.values(), decision_fields)
    write_rows(out_dir / "negative_11C253_sample.csv", negative_11c253[:10000], decision_fields)

    # Funnel by diff.
    by_diff: Dict[str, Dict[str, object]] = defaultdict(lambda: defaultdict(int))
    dist_pos_by_diff: Dict[str, Dict[str, List[float]]] = defaultdict(lambda: defaultdict(list))
    for rec in decisions:
        d = rec.get("diff", "?")
        row = by_diff[d]
        row["total_decisions"] += 1
        if "11C253" in rec.get("branch", ""):
            row["decisions_11C253"] += 1
        if is_state(rec, "16", "0003"):
            row["state_16_3_to"] += 1
        if is_state(rec, "0F", "0001"):
            row["state_0F_1_to"] += 1
        if is_state(rec, "0F", "0004"):
            row["state_0F_4_to"] += 1
        if rec.get("positive_candidate") == "1":
            row["positive_events"] += 1
            for key in ("dist_actor_ball", "dist_actor_ball_pred", "dist_actor_active", "actor_speed"):
                v = get_float(rec, key)
                if v is not None:
                    dist_pos_by_diff[d][key].append(v)
        frm = rec.get("from", "")
        to = rec.get("to", "")
        if "0x16" in frm and "0x0F" in to and "0x0004" in to:
            row["transitions_16_to_0F4"] += 1

    funnel_rows = []
    for d in sorted(by_diff.keys(), key=lambda x: (len(x), x)):
        row = dict(by_diff[d])
        total = int(row.get("total_decisions", 0))
        out = {
            "diff": d,
            "total_decisions": total,
            "decisions_11C253": row.get("decisions_11C253", 0),
            "pct_11C253": pct(int(row.get("decisions_11C253", 0)), total),
            "state_16_3_to": row.get("state_16_3_to", 0),
            "pct_state_16_3": pct(int(row.get("state_16_3_to", 0)), total),
            "state_0F_1_to": row.get("state_0F_1_to", 0),
            "state_0F_4_to": row.get("state_0F_4_to", 0),
            "positive_events": row.get("positive_events", 0),
            "transitions_16_to_0F4": row.get("transitions_16_to_0F4", 0),
            "avg_pos_dist_actor_ball": avg(dist_pos_by_diff[d]["dist_actor_ball"]),
            "avg_pos_dist_actor_ball_pred": avg(dist_pos_by_diff[d]["dist_actor_ball_pred"]),
            "avg_pos_dist_actor_active": avg(dist_pos_by_diff[d]["dist_actor_active"]),
            "avg_pos_actor_speed": avg(dist_pos_by_diff[d]["actor_speed"]),
        }
        funnel_rows.append(out)
    funnel_fields = [
        "diff", "total_decisions", "decisions_11C253", "pct_11C253", "state_16_3_to", "pct_state_16_3",
        "state_0F_1_to", "state_0F_4_to", "positive_events", "transitions_16_to_0F4",
        "avg_pos_dist_actor_ball", "avg_pos_dist_actor_ball_pred", "avg_pos_dist_actor_active", "avg_pos_actor_speed",
    ]
    write_rows(out_dir / "decision_funnel_by_diff.csv", funnel_rows, funnel_fields)

    # Positive vs negative context groups around 11C253.
    twins = Counter()
    for rec in decisions:
        if "11C253" not in rec.get("branch", ""):
            continue
        d = rec.get("diff", "?")
        to = state_hex(rec.get("to", "?"))
        real_bin = dist_bin(get_float(rec, "dist_actor_ball"))
        pred_bin = dist_bin(get_float(rec, "dist_actor_ball_pred"))
        spatial = rec.get("spatial_intent", "?")
        motion = rec.get("motion_intent", "?")
        pos = "positive" if rec.get("positive_candidate") == "1" else "negative"
        twins[(d, to, spatial, motion, real_bin, pred_bin, pos)] += 1
    write_counter(out_dir / "positive_negative_context_11C253.csv", twins, ("diff", "to_state", "spatial_intent", "motion_intent", "real_bin", "pred_bin", "result", "count"))

    with (out_dir / "analysis_report.md").open("w", encoding="utf-8") as f:
        f.write(f"# PES6 AI behavior log analysis ({VERSION})\n\n")
        f.write(f"Input log: `{log_path}`\n\n")
        f.write(f"Total raw lines: {total_lines}\n\n")
        f.write(f"Total parsed events: {parsed_lines}\n\n")
        f.write("## Event kinds\n")
        for k, v in counters["kind"].most_common():
            f.write(f"- {k}: {v}\n")
        f.write("\n## Decision funnel by diff\n")
        for row in funnel_rows:
            f.write(
                f"- diff {row['diff']}: decisions={row['total_decisions']}, "
                f"11C253={row['decisions_11C253']} ({row['pct_11C253']:.1f}%), "
                f"0x16:3={row['state_16_3_to']} ({row['pct_state_16_3']:.1f}%), "
                f"positives={row['positive_events']}, 16→0F4={row['transitions_16_to_0F4']}\n"
            )
        f.write("\n## Top branches\n")
        for k, v in counters["branch"].most_common(20):
            f.write(f"- {k}: {v}\n")
        f.write("\n## Positive candidates by branch/diff\n")
        for (branch, diff), v in counters["positive"].most_common(20):
            f.write(f"- {branch} diff={diff}: {v}\n")
        f.write("\n## Top transitions\n")
        for (frm, to, branch), v in counters["transitions"].most_common(30):
            f.write(f"- {frm} -> {to} via {branch}: {v}\n")
        f.write("\n## Spatial intent\n")
        for k, v in counters["spatial_intent"].most_common(20):
            f.write(f"- {k}: {v}\n")
        f.write("\n## Motion intent\n")
        for k, v in counters["motion_intent"].most_common(20):
            f.write(f"- {k}: {v}\n")

    return {
        "log": str(log_path),
        "out_dir": str(out_dir),
        "total_lines": total_lines,
        "parsed_events": parsed_lines,
        "ai_decisions": counters["kind"].get("AI_DECISION", 0),
        "state_changes": counters["kind"].get("AI_STATE_CHANGE", 0),
        "positive_events": sum(counters["positive"].values()),
        "top_diff": counters["decision_diff"].most_common(1)[0][0] if counters["decision_diff"] else "?",
        "top_branch": counters["branch"].most_common(1)[0][0] if counters["branch"] else "?",
    }


def find_batch_logs(folder: Path) -> List[Path]:
    return [p for p in sorted(folder.glob("*.txt")) if p.is_file()]


def run_batch(folder: Path) -> int:
    folder = folder.resolve()
    if not folder.exists():
        print(f"ERROR: batch folder does not exist: {folder}")
        print("Tip: if the folder is next to this .py, use: python analyze_behavior_log.py --batch test")
        return 2
    if not folder.is_dir():
        print(f"ERROR: batch path is not a folder: {folder}")
        return 2
    logs = find_batch_logs(folder)
    if not logs:
        print(f"No *.txt files found in {folder}")
        return 1

    print(f"Batch mode: found {len(logs)} txt file(s) in {folder}")
    summaries: List[Dict[str, object]] = []
    for log_path in logs:
        out_dir = unique_output_dir(folder, log_path.stem)
        print(f"- Analyzing {log_path.name} -> {out_dir.name}/")
        try:
            summaries.append(analyze_log(log_path, out_dir))
        except Exception as exc:
            print(f"  ERROR analyzing {log_path.name}: {exc}")
            summaries.append({"log": str(log_path), "out_dir": str(out_dir), "error": str(exc)})

    summary_path = folder / "_batch_summary.csv"
    fields = ["log", "out_dir", "total_lines", "parsed_events", "ai_decisions", "state_changes", "positive_events", "top_diff", "top_branch", "error"]
    write_rows(summary_path, summaries, fields)
    print(f"Wrote batch summary to {summary_path}")
    return 0


def main() -> int:
    print(f"PES6 AI behavior analyzer VERSION={VERSION}")
    script_dir = Path(__file__).resolve().parent

    if len(sys.argv) == 1:
        return run_batch(script_dir)

    if len(sys.argv) in (2, 3) and sys.argv[1] == "--batch":
        folder = Path(sys.argv[2]) if len(sys.argv) == 3 else script_dir
        if not folder.is_absolute():
            folder = Path.cwd() / folder
        return run_batch(folder)

    if len(sys.argv) >= 3:
        log_path = Path(sys.argv[1])
        out_dir = Path(sys.argv[2])
        summary = analyze_log(log_path, out_dir)
        print(f"Wrote analysis to {out_dir}")
        print(f"Parsed events: {summary.get('parsed_events', 0)}")
        return 0

    print(__doc__)
    return 2


if __name__ == "__main__":
    raise SystemExit(main())
