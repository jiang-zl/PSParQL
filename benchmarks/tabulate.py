"""Consolidate the four separately verified edge_bench.py output directories."""
import argparse
import csv
import json
from pathlib import Path
from statistics import median


def read(path):
    with path.open() as stream:
        return list(csv.DictReader(stream))


def write(path, rows):
    with path.open("w", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=rows[0], lineterminator="\n")
        writer.writeheader()
        writer.writerows(rows)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("runs", nargs=4, type=Path, metavar="Q")
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    timings, counters, summary = [], [], []
    for i, directory in enumerate(args.runs, 1):
        query = f"Q{i}"
        config = json.loads((directory / "config.json").read_text())
        if not config.get("baseline_observer") or config["baseline_observer"] == "None":
            raise ValueError("use an observer-only 4079ce5 binary for the primary comparison")
        rows = read(directory / "timings.csv")
        timings.extend(dict(query=query, **r) for r in rows)
        counts = {r["mode"]: r for r in read(directory / "counters.csv")}
        counts["baseline"] = dict(counts.pop("root"), mode="baseline")
        counters.extend(dict(query=query, **r) for r in counts.values())
        entry = dict(query=query, answers=config["expected"])
        for metric in ["task_s", "wall_s"]:
            def sample(mode):
                return [float(r[metric]) for r in rows if r["mode"] == mode]
            before, after = sample("baseline"), sample("auto")
            assert len(before) == len(after) == int(config["repeat"])
            for name, values in (("base", before), ("auto", after)):
                entry[f"{name}_{metric}"] = round(median(values), 6)
            entry[f"speedup_{metric}"] = round(median(before) / median(after), 4)
        for metric in ["seeds", "groups", "multi_seed_groups", "scan_items", "list_builds",
                       "intersections", "vertex_copies", "triple_copies"]:
            entry[f"base_{metric}"] = counts["baseline"][metric]
            entry[f"auto_{metric}"] = counts["auto"][metric]
        for item, unique, copies in [("V", "unique_vertices", "vertex_copies"),
                                     ("E", "unique_triples", "triple_copies")]:
            for name, mode in [("base", "baseline"), ("auto", "auto")]:
                entry[f"{name}_dup_{item}"] = int(counts[mode][copies]) - int(counts[mode][unique])
            entry[f"saved_dup_{item}"] = entry[f"base_dup_{item}"] - entry[f"auto_dup_{item}"]
        summary.append(entry)
    write(args.out / "timings.csv", timings)
    write(args.out / "counters.csv", counters)
    write(args.out / "summary.csv", summary)


if __name__ == "__main__":
    main()
