#!/usr/bin/env python3
"""Summarize tracked rendering harnesses' FrameProfile CSVs without dependencies."""
import argparse
import csv
import math
from pathlib import Path
import re
import statistics


def summarize(directory):
    groups = {}
    for path in sorted(directory.glob("*-round-*.csv")):
        match = re.fullmatch(r"(.+)-round-(\d+)\.csv", path.name)
        if not match:
            continue
        with path.open(newline="") as stream:
            frames = list(csv.DictReader(stream))
        if not frames:
            raise ValueError(f"Empty round: {path}")
        if len({row["frame"] for row in frames}) != len(frames):
            raise ValueError(f"Duplicate frame IDs: {path}")
        groups.setdefault(match[1], []).append(frames)
    if not groups:
        raise ValueError(f"No round CSVs in {directory}")
    result = []
    for variant, rounds in sorted(groups.items()):
        row = {"variant": variant, "rounds": len(rounds), "frames": sum(map(len, rounds))}
        metrics = ["gpu_ms", "cpu_render_ms", "cpu_simulation_ms", "cpu_preparation_ms", "cpu_other_ms"]
        for frames in rounds:
            for frame in frames:
                frame["cpu_non_render_ms"] = sum(float(frame[key]) for key in metrics[2:])
        for metric in metrics + ["cpu_non_render_ms"]:
            samples = [float(frame[metric]) for frames in rounds for frame in frames]
            values = sorted(value for value in samples if math.isfinite(value))
            means = []
            for frames in rounds:
                finite = [float(frame[metric]) for frame in frames if math.isfinite(float(frame[metric]))]
                if finite:
                    means.append(statistics.mean(finite))
            row[f"{metric}_samples"] = len(values)
            row[f"{metric}_median"] = statistics.median(values) if values else math.nan
            row[f"{metric}_p95"] = values[math.ceil(.95 * len(values))-1] if values else math.nan
            row[f"{metric}_round_mean_median"] = statistics.median(means) if means else math.nan
        result.append(row)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path, help="Output directory from a rendering benchmark")
    args = parser.parse_args()
    rows = summarize(args.directory)
    with (args.directory / "summary.csv").open("w", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=rows[0])
        writer.writeheader()
        writer.writerows(rows)
    lines = ["Rendering benchmark summary (milliseconds; lower is better)",
             "GPU, CPU render and CPU non-render are separate; GPU and CPU overlap.",
             "Values: median of round means; p95 is across individual measured frames.",
             "CPU render includes driver stalls. See metadata.txt for the actual simulation workload.", "",
             "Variant | frames | GPU | GPU p95 | CPU render | CPU render p95 | CPU non-render"]
    for row in rows:
        lines.append(f"{row['variant']} | {row['frames']} | "
                     f"{row['gpu_ms_round_mean_median']:.3f} | {row['gpu_ms_p95']:.3f} | "
                     f"{row['cpu_render_ms_round_mean_median']:.3f} | {row['cpu_render_ms_p95']:.3f} | "
                     f"{row['cpu_non_render_ms_round_mean_median']:.3f}")
        if row["gpu_ms_samples"] != row["frames"]:
            lines.append(f"  GPU samples: {row['gpu_ms_samples']}/{row['frames']}; missing results excluded.")
    metadata = args.directory / "metadata.txt"
    if metadata.exists():
        lines += ["", "Scenario metadata:", metadata.read_text().rstrip()]
    report = "\n".join(lines) + "\n"
    (args.directory / "report.txt").write_text(report)
    print(report, end="")


if __name__ == "__main__":
    main()
