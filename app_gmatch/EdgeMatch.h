#ifndef PSPARQL_EDGE_MATCH_H
#define PSPARQL_EDGE_MATCH_H

#include "graph_matching.h"
#include <cstdint>
#include <iterator>

namespace edge_run {
struct Slice {
  EdgeDirect dir;
  int predicate, label;
};
struct Input {
  int source, slot;
};
struct Program {
  vector<QueryPlanVertex> roles;
  vector<vector<Slice>> outputs;
  vector<vector<Input>> inputs;
  vector<vector<int>> live;
  vector<bool> independent;
  vector<int> factors;
  vector<AdjItem> prefix_edges;
  int arity;
  EdgeDirect first_dir;
  int predicate;

  Program(const QueryPlanVertexVecList &order, const hash_map<VertexID, GMatchVertex *> &query,
          int second, const AdjItem &anchor)
      : arity(order.front().id == second ? 1 : 2), first_dir(anchor.d), predicate(anchor.el) {
    roles.push_back(order.front());
    if (arity == 2)
      for (const auto &r : order)
        if (r.id == second)
          roles.push_back(r);
    for (const auto &r : order)
      if (r.id != second && r.id != roles.front().id)
        roles.push_back(r);
    for (const auto &e : query.find(roles.front().id)->second->value.adj)
      if (e.id == second)
        prefix_edges.push_back(e);
    outputs.resize(roles.size());
    inputs.resize(roles.size());
    live.resize(roles.size());
    // Keep every predicate between two roles. The ordered seed already satisfies
    // the constraints internal to the prefix (checked by the owner at scan time).
    for (int t = arity; t < (int)roles.size(); ++t) {
      for (int s = 0; s < t; ++s) {
        for (const auto &e : query.find(roles[s].id)->second->value.adj) {
          if (e.id != roles[t].id)
            continue;
          inputs[t].push_back({s, (int)outputs[s].size()});
          outputs[s].push_back({e.d, e.el, roles[t].l});
        }
      }
      if (inputs[t].empty())
        throw logic_error("disconnected edge query plan");
    }
    independent.assign(roles.size(), false);
    for (int t = arity; t < (int)roles.size(); ++t) {
      independent[t] = !needs_row(t) && all_of(inputs[t].begin(), inputs[t].end(),
                                               [&](Input i) { return i.source < arity; });
      if (independent[t])
        factors.push_back(t);
    }
    for (int depth = arity; depth < (int)roles.size(); ++depth) {
      set<int> prefix;
      for (size_t t = depth; t < roles.size(); ++t) {
        if (independent[t])
          continue;
        for (auto input : inputs[t])
          if (input.source < depth)
            prefix.insert(input.source);
      }
      live[depth].assign(prefix.begin(), prefix.end());
    }
  }

  bool needs_row(int role) const {
    return !outputs[role].empty() || !roles[role].self_edges.empty();
  }
};
static map<int, Program> programs;

inline void project(const Program &p, int role, const GMatchVertex &v, GMatchSubgraph &graph) {
  if (p.roles[role].l >= 0 && p.roles[role].l != v.value.l)
    return;
  for (auto pred : p.roles[role].self_edges) {
    AdjItem loop{v.id, v.value.l, EdgeDirect::OUT, pred};
    if (!binary_search(v.value.adj.begin(), v.value.adj.end(), loop, edge_less))
      return;
  }
  auto &row = graph.projections[role][v.id];
  row.resize(p.outputs[role].size());
  if (row.empty())
    return;
  add(ListBuilds, row.size());
  add(ScanItems, v.value.adj.size());
  for (const auto &e : v.value.adj) {
    for (size_t k = 0; k < row.size(); ++k) {
      const auto &op = p.outputs[role][k];
      if (op.dir == e.d && op.predicate == e.el && (op.label < 0 || op.label == e.l))
        row[k].push_back(e.id);
    }
  }
}

// Semijoin the union domains before pulling adjacency. Exact tuple correlations
// are enforced again with the actual bound source IDs during enumeration.
inline vector<int> domain(const Program &p, int role, const GMatchSubgraph &g) {
  vector<int> result;
  bool first = true;
  for (auto input : p.inputs[role]) {
    vector<int> ids;
    for (const auto &entry : g.projections[input.source]) {
      const auto &v = entry.second[input.slot];
      ids.insert(ids.end(), v.begin(), v.end());
    }
    sort(ids.begin(), ids.end());
    ids.erase(unique(ids.begin(), ids.end()), ids.end());
    if (first) {
      result.swap(ids);
      first = false;
    } else {
      vector<int> common;
      set_intersection(result.begin(), result.end(), ids.begin(), ids.end(), back_inserter(common));
      result.swap(common);
    }
    if (result.empty())
      break;
  }
  return result;
}

class Enumerator {
  const Program &p;
  const GMatchSubgraph &g;
  vector<int> binding;
  // Only the prefix variables referenced by the suffix can affect its count.
  vector<map<vector<int>, size_t>> memo;
  size_t cached = 0;
  vector<map<int, int>> canonical;
  using Row = vector<vector<int>>;
  struct RowLess {
    bool operator()(const Row *a, const Row *b) const { return *a < *b; }
  };
  // The suffix can observe a bound anchor only through its exact projections.
  // Compare full sorted rows; the sketch never establishes equality.
  void index_anchors() {
    for (int r = 0; r < p.arity; ++r) {
      map<const Row *, int, RowLess> index;
      for (const auto &row : g.projections[r]) {
        auto result = index.emplace(&row.second, row.first);
        canonical[r].emplace(row.first, result.first->second);
      }
    }
  }
  using Lists = vector<const vector<int> *>;
  Lists operands(size_t depth) const {
    vector<const vector<int> *> lists;
    for (auto input : p.inputs[depth]) {
      const auto &table = g.projections[input.source];
      auto row = table.find(binding[input.source]);
      if (row == table.end() || row->second[input.slot].empty())
        return {};
      lists.push_back(&row->second[input.slot]);
    }
    if (lists.size() > 1)
      add(Intersections);
    sort(lists.begin(), lists.end(),
         [](const vector<int> *a, const vector<int> *b) { return a->size() < b->size(); });
    return lists;
  }
  static bool matches(const Lists &lists, int id) {
    return all_of(lists.begin() + 1, lists.end(),
                  [id](const vector<int> *v) { return binary_search(v->begin(), v->end(), id); });
  }
  int identity(int role) const {
    int id = binding[role];
    return !canonical.empty() && role < p.arity ? canonical[role].at(id) : id;
  }
  // An unbound leaf constrained solely by the seed has no equality or edge
  // to the remaining roles. Its row count is an independent multiplicity.
  size_t factor(int role) {
    if (p.inputs[role].size() == 1) {
      auto in = p.inputs[role][0];
      return g.projections[in.source].at(binding[in.source])[in.slot].size();
    }
    vector<int> key;
    for (auto in : p.inputs[role])
      key.push_back(identity(in.source));
    auto hit = memo[role].find(key);
    if (hit != memo[role].end()) {
      add(MemoHits);
      return hit->second;
    }
    auto lists = operands(role);
    size_t count = 0;
    if (!lists.empty())
      for (int id : *lists.front())
        if (matches(lists, id))
          ++count;
    if (cached < 8192) {
      memo[role].emplace(std::move(key), count);
      ++cached;
    }
    return count;
  }
  size_t extend(size_t depth) {
#ifndef PSPARQL_TRACE_BINDINGS
    while (depth < p.roles.size() && p.independent[depth])
      ++depth;
#endif
    if (depth == p.roles.size()) {
#ifdef PSPARQL_TRACE_BINDINGS
      ostringstream row;
      row << "BIND";
      for (size_t i = 0; i < depth; ++i)
        row << ' ' << p.roles[i].id << '=' << binding[i];
      row << '\n';
      fputs(row.str().c_str(), stdout);
#endif
      return 1;
    }
    vector<int> key;
#ifndef PSPARQL_TRACE_BINDINGS
    if (p.live[depth].size() < depth || (depth == (size_t)p.arity && !canonical.empty())) {
      for (int i : p.live[depth]) {
        key.push_back(identity(i));
      }
      auto hit = memo[depth].find(key);
      if (hit != memo[depth].end()) {
        add(MemoHits);
        return hit->second;
      }
    }
#endif
    auto lists = operands(depth);
    if (lists.empty())
      return 0;
    size_t count = 0;
#ifndef PSPARQL_TRACE_BINDINGS
    if (depth + 1 == p.roles.size() && lists.size() == 1 && !p.needs_row(depth))
      count = lists.front()->size();
    else
#endif
      for (int id : *lists.front()) {
        if (p.needs_row(depth) && !g.projections[depth].count(id))
          continue;
        if (!matches(lists, id))
          continue;
        binding[depth] = id;
        count += extend(depth + 1);
      }
    if (!key.empty() && cached < 8192) {
      memo[depth].emplace(std::move(key), count);
      ++cached;
    }
    return count;
  }

public:
  Enumerator(const Program &program, const GMatchSubgraph &graph)
      : p(program), g(graph), binding(p.roles.size()), memo(p.roles.size()) {
#ifndef PSPARQL_TRACE_BINDINGS
    if (g.edge_seeds.size() >= 8 && p.roles.size() > (size_t)p.arity) {
      canonical.resize(p.arity);
      index_anchors();
    }
#endif
  }
  size_t count() {
    size_t total = 0;
    for (size_t i = 0; i < g.edge_seeds.size(); i += 2) {
      binding[0] = g.edge_seeds[i];
      if (p.arity == 2)
        binding[1] = g.edge_seeds[i + 1];
      size_t multiplicity = 1;
#ifndef PSPARQL_TRACE_BINDINGS
      for (int role : p.factors) {
        multiplicity *= factor(role);
        if (!multiplicity)
          break;
      }
#endif
      if (multiplicity)
        total += multiplicity * extend(p.arity);
    }
    return total;
  }
};

// Called with the ordinary pull/cache frontier, so local, remote and stolen
// tasks all execute the same program.
inline bool advance(GMatchTask &task, const vector<GMatchVertex *> &frontier, size_t &count) {
  auto &g = task.subG;
  const auto &p = programs.at(g.query_vertexs.front());
  int &role = task.context;
  if (!role) {
    g.projections.resize(p.roles.size());
    for (int r = 0; r < p.arity; ++r) {
      if (!p.needs_row(r))
        continue;
      set<int> ids;
      for (size_t i = r; i < g.edge_seeds.size(); i += 2)
        ids.insert(g.edge_seeds[i]);
      for (const auto *v : frontier)
        if (ids.count(v->id))
          project(p, r, *v, g);
    }
    role = p.arity;
  } else {
    for (const auto *v : frontier)
      project(p, role, *v, g);
    ++role;
  }
  while (role < (int)p.roles.size()) {
    if (p.needs_row(role)) {
      task.to_pull = domain(p, role, g);
      if (task.to_pull.empty()) {
        count = 0;
        return false;
      }
      return true;
    }
    ++role;
  }
  count = Enumerator(p, g).count();
  return false;
}
} // namespace edge_run
#endif
