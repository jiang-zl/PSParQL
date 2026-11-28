#pragma once
#include "EdgeMatch.h"
#include "GraphCensus.h"

namespace edge_run {
inline void record(const GMatchSubgraph &g, const Program &p) {
  if (!footprints)
    return;
  Footprint group;

  for (size_t i = 0; i < g.edge_seeds.size(); i += 2) {
    const int *ids = &g.edge_seeds[i];
    for (const auto &e : p.prefix_edges)
      group.fact(ids[0], e.d, e.el, ids[p.arity - 1]);
    for (int r = 0; r < p.arity; ++r)
      for (int pred : p.roles[r].self_edges)
        group.fact(ids[r], EdgeDirect::OUT, pred, ids[r]);
  }
  for (size_t r = 0; r < g.projections.size(); ++r)
    for (const auto &row : g.projections[r]) {
      group.vertices.insert(row.first);
      for (int pred : p.roles[r].self_edges)
        group.fact(row.first, EdgeDirect::OUT, pred, row.first);
      for (size_t k = 0; k < row.second.size(); ++k)
        for (int dst : row.second[k]) {
          const auto &op = p.outputs[r][k];
          group.fact(row.first, op.dir, op.predicate, dst);
        }
    }

  accumulate(group);
}
} // namespace edge_run
