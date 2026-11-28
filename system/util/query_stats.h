#ifndef PSPARQL_QUERY_STATS_H
#define PSPARQL_QUERY_STATS_H

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mpi.h>

namespace edge_run {
enum Mode { Root, Solo, Endpoint, Random, Plain, Full, Residual, Reuse, Auto };
static const char *const mode_names[] = {"root", "edge",     "endpoint", "random", "plain",
                                         "full", "residual", "reuse",    "auto"};
static Mode mode = Auto;
static bool auto_root = false;
static int batch = 4096;
static bool stats = false;
static bool footprints = false;
enum Metric {
  Seeds,
  Groups,
  JoinedGroups,
  ScanItems,
  ListBuilds,
  Intersections,
  SummaryItems,
  SummaryBytes,
  RouteBytes,
  ResponseBytes,
  MemoHits,
  VertexCopies,
  TripleCopies,
  UniqueVertices,
  UniqueTriples,
  Metrics
};
static const char *const names[] = {
    "seeds",         "groups",        "multi_seed_groups", "scan_items",      "list_builds",
    "intersections", "summary_items", "summary_bytes",     "route_bytes",     "response_bytes",
    "memo_hits",     "vertex_copies", "triple_copies",     "unique_vertices", "unique_triples"};
static std::atomic<unsigned long long> totals[Metrics]{};
static thread_local unsigned long long local[Metrics]{};
inline void add(Metric m, size_t n = 1) {
  if (stats)
    local[m] += n;
}
inline void flush() {
  if (!stats)
    return;
  for (int i = 0; i < Metrics; ++i)
    if (local[i]) {
      totals[i].fetch_add(local[i], std::memory_order_relaxed);
      local[i] = 0;
    }
}
inline void report(int rank) {
  if (!stats)
    return;
  flush();
  unsigned long long src[Metrics], sum[Metrics];
  for (int i = 0; i < Metrics; ++i)
    src[i] = totals[i].load();
  MPI_Reduce(src, sum, Metrics, MPI_UNSIGNED_LONG_LONG, MPI_SUM, 0, MPI_COMM_WORLD);
  if (rank == 0) {
    printf("PSPARQL_METRICS mode=%s selected=%s", auto_root ? "auto" : mode_names[mode],
           mode == Auto ? "reuse" : mode_names[mode]);
    for (int i = 0; i < Metrics; ++i)
      printf(" %s=%llu", names[i], sum[i]);
    puts("");
  }
}
} // namespace edge_run
#endif
