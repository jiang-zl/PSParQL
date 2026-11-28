# Single-query QPMH measurements

The primary deployment baseline is **unmodified PSParQL revise at 4079ce5**.
The `edge` mode is a separate control for the change in task granularity.
The four query texts in `queries/yago2` come from the [ecrc/rdf-exp benchmark](https://github.com/ecrc/rdf-exp/tree/master/queries/individual/yago2/string).
Only trailing whitespace was normalized. All four are evaluated **on the same simulated graph**.

## Reproduction on one MPI host

Use actual MPICH. The benchmark-only HDFS adapter reads local files; Hadoop
and its storage/network behavior are outside these single-host measurements.
Both binaries must use the same compiler, optimization flags, MPI, adapter and
hardware. The production Makefile continues to link libhdfs by default.

```bash
repo=$(pwd)
bench=$(mktemp -d)
git worktree add --detach "$bench/baseline-src" 4079ce5
make -B -C "$bench/baseline-src/app_gmatch" CPPFLAGS="-I$repo/benchmarks/local_hdfs -I../system -Wno-deprecated -O2" LIB= LDFLAGS=
python3 benchmarks/profile_baseline.py "$bench/observed-src"
make -B -C "$bench/observed-src/app_gmatch" CPPFLAGS="-I$repo/benchmarks/local_hdfs -I../system -Wno-deprecated -O2" LIB= LDFLAGS=
make -B -C app_gmatch CPPFLAGS="-I$repo/benchmarks/local_hdfs -I../system -Wno-deprecated -O2" LIB= LDFLAGS=
c++ -O2 -std=c++11 data_proc/data_proc.cpp -o "$bench/convert" $(pkg-config --cflags --libs raptor2)
python3 benchmarks/yago_sim.py "$bench/data" "$bench/convert" --cohorts 8 --students 64 --cast 512 --films 8
for q in 1 2 3 4; do
  python3 benchmarks/edge_bench.py --baseline "$bench/baseline-src/app_gmatch/run" \
    --baseline-observer "$bench/observed-src/app_gmatch/run" --candidate "$repo/app_gmatch/run" --launcher 'mpiexec -n 4' --threads 2 \
    --data "$bench/data/data" --query "$bench/data/q$q" --expected "$(cat "$bench/data/expected$q")" \
    --batch 4096 --repeat 5 --footprint --out "$bench/q$q"
done
python3 benchmarks/tabulate.py --out "$bench/report" "$bench"/q{1,2,3,4}
```

For a cluster, build both native HDFS binaries, use HDFS input paths, and
supply the site's hosts, executable paths and a working directory present on
all ranks (e.g. MPICH `-wdir`). The runner shuffles mode order within paired
rounds using seed 71. Every timing includes validation of the answer count
after process exit. Collection runs are separate from timings.
`edge_workloads.py` generates a positive and a disjoint negative control for
a two-sided intersection followed by a dependent traversal.

## What the counters count

- `seeds`, `groups`, `multi_seed_groups` count completed seed bindings, execution tasks, and tasks containing two or more seeds. A root seed binds one role. An edge seed binds the ordered endpoints. Compare task counts across versions, but compare seed merge ratios only within the same seed type.
- `vertex_copies`, `triple_copies` sum the distinct vertices and RDF triples in **each** completed group. Root tasks contribute their retained task subgraph. Edge tasks contribute the union of their seed constraints and stored role projections. `unique_*` counts the distributed set union, including different compute threads and ranks. Their difference counts repeated logical graph items across groups. This is a logical census at completion, not a peak-memory or network measurement.
- `list_builds` counts filtered neighbor lists built for matching. `scan_items` counts adjacency entries visited to build these lists. The edge executor builds all outgoing role projections in one pass. These metrics exclude graph load, NLF, hashing and DFS probes. `summary_items` measures adjacency visits for sketches separately. `intersections` counts actual multi-input matching intersections; semijoin domain preparation is separate.
- `response_bytes` counts serialized vertex-response payloads at MPI send. `summary_bytes` and `route_bytes` count inter-rank summary traffic and seed payloads. These exclude collective framing, task stealing, baseline graph distribution, request control messages and the optional footprint census.
- `Task Time` is the existing application timer, including query planning, seed summaries, routing, matching and the status synchronization loop. `wall_s` is measured around mpiexec and additionally includes startup, graph loading and teardown. The fixed polling cadence is visible on short queries.

[Results and raw measurements](results/qpmh/README.md).

### Equal-size and equal-placement control

Build a separate candidate with `CXXFLAGS=-DPSPARQL_MATCHED_SHUFFLE` and
use the same `auto 4096` options. This diagnostic binary permutes complete seed
pairs after routing, within each rank. For every batch it retains its original
size, and every rank retains its original seed population. It bounds pending
descriptors and uses a fixed RNG seed. Compare footprints from separate runs
with this binary and the normal build. Shuffling overhead belongs to the
control; use the normal build for production timings.

The standalone `tests/edge_task_roundtrip.cpp` verifies both the in-memory MPI
serialization format and checkpoint-file format on a partially expanded task
containing parallel projection lists and an empty list. Compile with the same
project include paths and MPI compiler as the matcher and pass a temp file path.
