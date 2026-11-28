"""Deterministic single-query fixtures. Write the same integer graph for every engine."""
import argparse
from collections import defaultdict
from pathlib import Path


def write_adj(path, rows):
    graph = defaultdict(list)
    for a, p, b in rows:
        graph[a].append((b, p))
    with path.open("w") as stream:
        for a, edges in graph.items():
            print(f"{a} {a}\t" + " ".join(f"{b} {b} {p}" for b, p in edges), file=stream)


def make(directory, shape, classes, repeats, width, fanout):
    directory.mkdir(parents=True, exist_ok=True)
    seeds = classes * repeats
    if shape == "cast":
        # Each cohort has repeats films with the same width actors. Names may have
        # multiple values. Additional unrelated names keep actedIn the first edge.
        rows = []
        actor_base = 1 + seeds
        name_base = actor_base + classes*width
        for i in range(seeds):
            rows.extend((actor_base + (i%classes)*width+j, 1, i+1) for j in range(width))
        for j in range(classes*width):
            rows.extend((actor_base+j,2,name_base+j*fanout+k) for k in range(fanout))
        extra = name_base + classes*width*fanout
        for i in range(seeds*width): rows.append((extra+2*i,2,extra+2*i+1))
        write_adj(directory/"data",rows)
        write_adj(directory/"query",[(-1,2,-2),(-3,2,-4),(-1,1,-5),(-3,1,-5)])
        expected=seeds*(width*fanout)**2
        (directory/"expected").write_text(str(expected)+"\n")
        print(shape, "triples",len(rows),"anchor edges",seeds*width,"rows",expected)
        return
    pool = classes if shape == "shared" else seeds
    # Non-overlapping ID ranges; consumers should not depend on edge order.
    xbase = 2 * seeds + 1
    ybase = xbase + 2 * width * pool
    rows = []
    for i in range(seeds):
        a, b = 2*i+1, 2*i+2
        group = i % classes if shape == "shared" else i
        x = xbase + 2*width*group
        rows.append((a, 1, b))
        rows.extend((a, 2, x+j) for j in range(width+width//2))
        rows.extend((b, 3, x+j) for j in range(width//2, 2*width))
    for j in range(2*width*pool):
        rows.extend((xbase+j, 4, ybase+fanout*j+k) for k in range(fanout))
    write_adj(directory / "data", rows)
    write_adj(directory / "query", [(-1,1,-2),(-1,2,-3),(-2,3,-3),(-3,4,-4)])
    (directory / "expected").write_text(str(seeds*width*fanout) + "\n")
    print(shape, "triples", len(rows), "anchor edges", seeds, "rows", seeds*width*fanout)


if __name__ == "__main__":
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("directory", type=Path)
    p.add_argument("--shape", choices=["shared","disjoint","cast"], default="shared")
    p.add_argument("--classes", type=int, default=32)
    p.add_argument("--repeats", type=int, default=32)
    p.add_argument("--width", type=int, default=32)
    p.add_argument("--fanout", type=int, default=4)
    a = p.parse_args()
    if min(a.classes,a.repeats,a.width,a.fanout)<1 or a.width%2: p.error("positive dimensions and even width required")
    make(a.directory, a.shape, a.classes, a.repeats, a.width, a.fanout)
