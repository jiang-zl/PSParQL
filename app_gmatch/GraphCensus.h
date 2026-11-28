#pragma once
#include "QueryExchange.h"
#include <set>
#include <array>
#include <mutex>

namespace edge_run {
// Exact profiling only, on a separate run from latency measurements. An RDF
// triple is counted once per group even when represented by multiple roles.
using Triple = std::array<int, 3>;
struct Footprint {
  set<int> vertices;
  set<Triple> triples;
  void fact(int src, EdgeDirect d, int pred, int dst) {
    vertices.insert(src);
    vertices.insert(dst);
    if (d == EdgeDirect::IN)
      swap(src, dst);
    triples.insert({src, pred, dst});
  }
};
static Footprint rank_footprint;
static mutex footprint_lock;
inline void accumulate(const Footprint &group) {
  add(VertexCopies, group.vertices.size());
  add(TripleCopies, group.triples.size());
  lock_guard<mutex> lock(footprint_lock);
  rank_footprint.vertices.insert(group.vertices.begin(), group.vertices.end());
  rank_footprint.triples.insert(group.triples.begin(), group.triples.end());
}
inline void record(const GMatchSubgraph &g) {
  if (!footprints)
    return;
  Footprint group;
  for (const auto &v : g.vertexes) {
    group.vertices.insert(v.id);
    for (const auto &e : v.value.adj)
      if (e.d == EdgeDirect::OUT)
        group.fact(v.id, e.d, e.el, e.id);
  }
  accumulate(group);
}
// Distribute the distinct-union calculation itself. Diagnostic traffic never
// enters the query byte counters or Task Time.
inline void finish_footprints() {
  if (!footprints)
    return;
  Packets vertices(_num_workers), triples(_num_workers);
  for (int id : rank_footprint.vertices)
    vertices[mix((uint32_t)id) % _num_workers].push_back((uint32_t)id);
  for (const auto &t : rank_footprint.triples) {
    auto &out =
        triples[mix(mix((uint32_t)t[0]) ^ mix((uint32_t)t[1]) ^ (uint32_t)t[2]) % _num_workers];
    for (int x : t)
      out.push_back((uint32_t)x);
  }
  Footprint owned;
  for (const auto &part : exchange(vertices, Metrics))
    owned.vertices.insert(part.begin(), part.end());
  for (const auto &part : exchange(triples, Metrics))
    for (size_t i = 0; i < part.size(); i += 3)
      owned.triples.insert({(int)part[i], (int)part[i + 1], (int)part[i + 2]});
  add(UniqueVertices, owned.vertices.size());
  add(UniqueTriples, owned.triples.size());
}

} // namespace edge_run
