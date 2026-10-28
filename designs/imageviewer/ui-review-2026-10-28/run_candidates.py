#!/usr/bin/env python3
"""在独立X11显示中运行既有探针的Qt候选入口。"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import time
import statistics


def summarize(output):
    runs = json.loads((output / "runs.json").read_text())["runs"]
    summary = []
    for variant, label in [("baseline", "基线布局＋公共修正"), ("a", "A · 极简图库"), ("b", "B · 增强导航")]:
        selected = [run for run in runs if run["variant"] == variant and not run["warmup"]]
        records = [json.loads(Path(run["metrics"]).read_text()) for run in selected]
        measured = [[row for row in rows if "whole_window_render_median_ms" in row] for rows in records]
        peaks = [max(row.get("peak_rss_after_render_kib", row["peak_rss_kib"]) for row in rows) / 1024 for rows in records]
        all_rows = [row for rows in measured for row in rows]
        assert all(row["cache_budget_bytes"] == 64 * 1024 * 1024 and row["queue_tasks"] <= 64 for row in all_rows)
        result = {
            "variant": variant, "label": label, "runs": len(selected),
            "peak_rss_mib_median": statistics.median(peaks), "peak_rss_mib_range": [min(peaks), max(peaks)],
            "idle_cpu_percent_median": statistics.median(row["loaded_idle_cpu_ms_500ms"] / 5 for row in all_rows if row["phase"] != "empty"),
            "active_items_max": max(row["active_items"] for row in all_rows),
            "phases": {},
        }
        for phase in ["light", "dark", "narrow", "preview", "preview_narrow", "empty"]:
            rows = [row for row in all_rows if row["phase"] == phase]
            timings = [row["whole_window_render_median_ms"] for row in rows]
            result["phases"][phase] = {
                "render_median_ms": statistics.median(timings), "render_range_ms": [min(timings), max(timings)],
                "rss_mib_median": statistics.median(row["rss_kib"] / 1024 for row in rows),
                "pss_mib_median": statistics.median(row["pss_kib"] / 1024 for row in rows),
                "widgets": rows[0]["widget_count"], "filmstrip_cells": rows[0]["filmstrip_cells"],
            }
        result["dark_render_median_ms"] = result["phases"]["dark"]["render_median_ms"]
        result["preview_render_median_ms"] = result["phases"]["preview"]["render_median_ms"]
        summary.append(result)
    document = {"dataset_images": max(row["model_count"] for row in all_rows), "display": "独立Xvfb，xcb，100%",
                "baseline_includes_common_fixes": True, "render_frames_per_phase": 60,
                "rss_sampled_before_render_surface": True, "peak_includes_benchmark_and_screenshots": True,
                "summary": summary}
    (output / "measurements.json").write_text(json.dumps(document, ensure_ascii=False, indent=2) + "\n")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--directory", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--repeats", type=int, default=3)
    args = parser.parse_args()
    if not args.binary.is_file() or not args.directory.is_dir() or args.repeats < 1:
        parser.error("需要有效程序、图库目录和正的重复次数")
    args.output.mkdir(parents=True, exist_ok=True)
    results = []
    for repeat in range(-1, args.repeats):
        variants = ["baseline", "a", "b"]
        variants = ["baseline"] if repeat == -1 else variants[repeat % 3:] + variants[:repeat % 3]
        for variant in variants:
            output = args.output / f"{variant}-{repeat + 1}"
            env = os.environ.copy()
            env.update(QT_QPA_PLATFORM="xcb", QT_SCALE_FACTOR="1", IV_BENCH_WINDOW="1100x760",
                       IV_UI_VARIANT=variant, IV_UI_ROOT=str(args.directory),
                       XDG_CONFIG_HOME=str(args.output / f"config-{variant}-{repeat}"),
                       XDG_CACHE_HOME=str(args.output / "shared-cache"))
            command = ["xvfb-run", "-a", "-s", "-screen 0 2560x1600x24",
                       str(args.binary), "--ui", str(args.directory), str(output)]
            started = time.monotonic()
            process = subprocess.run(command, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
            log = process.stdout
            (args.output / f"{variant}-{repeat + 1}.txt").write_bytes(log[-65536:])
            row = {"variant": variant, "repeat": repeat + 1, "exit_code": process.returncode,
                   "warmup": repeat == -1,
                   "wall_seconds": time.monotonic() - started, "command": command,
                   "log_bytes": len(log), "metrics": str(output / "review-metrics.json")}
            results.append(row)
            (args.output / "runs.json").write_text(json.dumps({
                "binary_sha256": hashlib.sha256(args.binary.read_bytes()).hexdigest(),
                "variants_share_wheel_fix": True, "runs": results,
            }, ensure_ascii=False, indent=2) + "\n")
            print(variant, repeat + 1, process.returncode, flush=True)
            if process.returncode != 0:
                return process.returncode
    summarize(args.output)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
