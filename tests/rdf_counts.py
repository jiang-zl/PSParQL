"""Check PSParQL bindings against a triple-pattern join over tiny RDF graphs."""
import argparse
from collections import Counter, defaultdict
from pathlib import Path
import re
import subprocess
from tempfile import TemporaryDirectory
from uuid import uuid4


def oracle(triples, query):
    rows = [{}]
    for subject, predicate, obj in query:
        extended = []
        for old in rows:
            for a, p, b in set(triples):
                if p != predicate:
                    continue
                row = old.copy()
                for term, value in ((subject, a), (obj, b)):
                    if term >= 0 and term != value:
                        break
                    if row.setdefault(term, value) != value:
                        break
                else:
                    extended.append(row)
        rows = extended
    return Counter(tuple(sorted(row.items())) for row in rows)


def adjacency(triples):
    nodes = defaultdict(list)
    for a, p, b in triples:
        nodes[a].append(f"{b} {b} {p}")
    return "".join(f"{a} {a}\t{' '.join(edges)}\n" for a, edges in nodes.items())


def cases():
    # YAGO Q1, Q2 and Q4: a non-birth relation to a second candidate city.
    family = [(1, 1, 2), (1, 2, 3), (1, 3, 4), (1, 4, 5), (5, 3, 4),
              (1, 5, 6), (6, 3, 4), (1, 6, 7), (6, 6, 8),
              (1, 7, 9), (5, 3, 9), (6, 3, 9)]
    q1 = [(-1, 1, -2), (-1, 2, -3), (-1, 3, -4), (-1, 4, -5), (-5, 3, -4)]
    yield "Q1", family, q1
    q2_data = family[:9] + [(1, 3, 9), (6, 3, 9), (10, 3, 9), (5, 7, 9),
                            (11, 1, 12), (11, 2, 13), (11, 4, 14)]
    yield "Q2", q2_data, q1 + [(-1, 5, -6), (-6, 3, -4)]
    yield "Q4", family, [(-1, 6, -2), (-3, 6, -4), (-1, 5, -3),
                         (-1, 3, -5), (-3, 3, -5)]

    q3 = [(-1, 1, -2), (-3, 1, -4), (-1, 2, -5), (-3, 2, -5)]
    cast = [(1, 1, 2), (3, 1, 4), (1, 2, 5), (3, 2, 5), (6, 1, 7)]
    yield "Q3-overlapping-roles", [(1, 2, 5), (2, 2, 5), (3, 2, 6), (4, 2, 6),
                                   (1, 1, 7), (2, 1, 8), (3, 1, 5), (4, 1, 9), (11, 1, 12)], q3
    yield "Q3-single-actor", cast + [(8, 1, 9), (8, 2, 10)], q3
    merged = cast + [(1, 2, 8), (3, 2, 8), (5, 9, 8)]
    merged += [(film, 9, tag) for tag in range(9, 24) for film in (5, 8)]
    merged += [(x, 1, x + 1) for x in (24, 26, 28)]
    yield "Q3-merged-seeds", merged, q3

    graph = [(1, 1, 2), (1, 2, 2), (2, 1, 1), (1, 1, 2),
             (3, 1, 4), (1, 3, 1), (2, 3, 2), (2, 1, 3)]
    for name, query in [
        ("predicate", [(-1, 1, -2)]),
        ("parallel", [(-1, 1, -2), (-1, 2, -2)]),
        ("reciprocal", [(-1, 1, -2), (-2, 1, -1)]),
        ("loop", [(-1, 3, -1)]),
        ("loop-plus-edge", [(-1, 1, -2), (-2, 3, -2)]),
        ("constant", [(1, 1, -2), (1, 2, -2)]),
        ("repeated-pattern", [(-1, 1, -2), (-1, 1, -2)]),
        ("missing", [(-1, 99, -2)]),
    ]:
        yield name, graph, query
        yield name + "-reverse-input", graph[::-1], query
    # The unique marker predicate makes vertex 5 the root. Both endpoints
    # of the parallel edge are at depth 1, including a decoy lacking p=2.
    yield "same-level-parallel", graph + [(6, 8, 5), (5, 4, 1), (5, 4, 2),
                                         (5, 4, 3), (8, 2, 9)], [
        (-4, 8, -1), (-1, 4, -2), (-1, 4, -3), (-2, 1, -3), (-2, 2, -3)]
    # Seed-only leaves multiply independently, but connecting the two leaves
    # constrains their bindings and they must no longer be factored.
    star = [(1,1,2), (1,2,3), (1,2,4), (2,3,5), (2,3,6),
            (3,4,5), (7,4,8)]
    leaves = [(-1,1,-2), (-1,2,-3), (-2,3,-4)]
    yield "independent-leaves", star, leaves
    yield "connected-leaves", star, leaves + [(-3,4,-4)]
    # Same BFS level, parallel predicates with opposite orientations.
    yield "same-level", graph + [(5, 4, 1), (5, 4, 2), (5, 4, 3)], [
        (-1, 4, -2), (-1, 4, -3), (-2, 2, -3), (-3, 1, -2)]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mode", choices=["root","edge","endpoint","random","plain","full","residual","reuse","auto"])
    parser.add_argument("--threads", type=int, default=1)
    parser.add_argument("--counts-only", action="store_true", help="use a build without binding traces")
    parser.add_argument("--hdfs-root", help="existing HDFS directory for temporary inputs")
    parser.add_argument("command", nargs="+", help="launcher and absolute path to run")
    args = parser.parse_args()
    hdfs_dir = f"{args.hdfs_root.rstrip('/')}/psparql-{uuid4().hex}" if args.hdfs_root else None
    if hdfs_dir:
        subprocess.run(["hdfs", "dfs", "-mkdir", hdfs_dir], check=True)
    try:
        with TemporaryDirectory() as tmp:
            for name, graph, query in cases():
                paths = []
                for filename, triples in (("data", graph), ("query", query)):
                    path = Path(tmp) / filename
                    path.write_text(adjacency(triples))
                    if hdfs_dir:
                        dest = hdfs_dir + "/" + filename
                        subprocess.run(["hdfs", "dfs", "-put", "-f", str(path), dest], check=True)
                        paths.append(dest)
                    else:
                        paths.append(str(path))
                result = subprocess.run([*args.command, *paths, str(args.threads), *([args.mode] if args.mode else [])], cwd=tmp,
                                        capture_output=True, text=True, timeout=60)
                if result.returncode:
                    raise RuntimeError(f"{name}: {result.stdout} {result.stderr}")
                expected = oracle(graph, query)
                counts = re.findall(r"number of matching results for query is: (\d+)", result.stdout)
                assert counts == [str(sum(expected.values()))], (name, counts, expected)
                if not args.counts_only:
                    actual = Counter()
                    for line in re.findall(r"^BIND (.+)$", result.stdout, re.MULTILINE):
                        actual[tuple(sorted(tuple(map(int, item.split("="))) for item in line.split()))] += 1
                    assert actual == expected, (name, actual - expected, expected - actual)
                print(f"{name}: {counts[0]}", flush=True)
    finally:
        if hdfs_dir:
            subprocess.run(["hdfs", "dfs", "-rm", "-r", hdfs_dir], check=True)


if __name__ == "__main__":
    main()
