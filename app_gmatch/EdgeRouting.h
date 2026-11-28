#ifndef PSPARQL_EDGE_ROUTING_H
#define PSPARQL_EDGE_ROUTING_H

#include "EdgeFootprint.h"
#ifdef PSPARQL_MATCHED_SHUFFLE
#include "MatchedShuffle.h"
#endif
#include <climits>
#include <memory>

namespace edge_run {
using Table = hash_map<VertexID, GMatchVertex *>;

struct Channel {
  EdgeDirect d;
  int pred;
  Word salt;
  size_t weight;
};
class EdgeRouter {
  const Program &p;
  const Table &query, &local;
  DefaultHash<VertexID> owner;
  vector<vector<Channel>> channels;
  using EndpointKey = pair<int, int>; // role, data ID
  map<EndpointKey, vector<Word>> summaries;
  size_t cursor = 0, edge_cursor = 0;
  int subject_role, object_role;

  vector<Word> describe(EndpointKey key) {
    auto hit = summaries.find(key);
    if (hit != summaries.end())
      return hit->second;
    int role = key.first;
    const auto *v = local.find(key.second)->second;
    const auto &slices = channels[role];
    const bool cost_key = mode == Reuse || mode == Auto;
    vector<Word> result(1 + (cost_key ? 4 : slices.size()), ULLONG_MAX);
    result[0] = nlf_filter(v, *query.find(p.roles[role].id)->second);
    if (cost_key)
      result[1] = 0;
    if (result[0] && !slices.empty()) {
      vector<Word> low(slices.size(), ULLONG_MAX), second(slices.size(), ULLONG_MAX);
      vector<Word> costs(slices.size(), 0);
      add(SummaryItems, v->value.adj.size());
      for (const auto &edge : v->value.adj) {
        for (size_t i = 0; i < slices.size(); ++i) {
          const auto &c = slices[i];
          if (mode != Plain && (edge.d != c.d || edge.el != c.pred))
            continue;
          Word token = c.salt ^ (uint32_t)edge.id;
          low[i] = std::min(low[i], mix(token));
          if (cost_key) {
            second[i] = std::min(second[i], mix(token ^ 0xa0761d6478bd642fULL));
            costs[i] += c.weight;
          }
        }
      }
      if (cost_key) {
        size_t i = max_element(costs.begin(), costs.end()) - costs.begin();
        result = {1, costs[i], slices[i].salt, low[i], second[i]};
      } else {
        copy(low.begin(), low.end(), result.begin() + 1);
      }
    }
    if (summaries.size() >= 65536)
      summaries.clear();
    summaries.emplace(key, result);
    return result;
  }

  vector<Seed> next_seeds(const vector<GMatchVertex *> &vertices) {
    vector<Seed> seeds;
    while (cursor < vertices.size() && seeds.size() < 4096) {
      const auto &v = *vertices[cursor];
      const auto &subject = p.roles[subject_role], &object = p.roles[object_role];
      if (subject.l < 0 || subject.l == v.value.l) {
        while (edge_cursor < v.value.adj.size() && seeds.size() < 4096) {
          const auto &edge = v.value.adj[edge_cursor++];
          if (edge.d != EdgeDirect::OUT || edge.el != p.predicate ||
              (object.l >= 0 && edge.l != object.l) || (p.arity == 1 && v.id != edge.id))
            continue;
          bool valid = true;
          for (const auto &need : query.find(subject.id)->second->value.adj) {
            if (need.id != object.id)
              continue;
            AdjItem actual{edge.id, edge.l, need.d, need.el};
            if (!binary_search(v.value.adj.begin(), v.value.adj.end(), actual, edge_less)) {
              valid = false;
              break;
            }
          }
          if (valid)
            seeds.push_back(subject_role == 0 ? Seed(v.id, edge.id) : Seed(edge.id, v.id));
        }
        if (edge_cursor < v.value.adj.size())
          break;
      }
      ++cursor;
      edge_cursor = 0;
    }
    return seeds;
  }

public:
  EdgeRouter(const Program &program, const Table &q, const Table &l)
      : p(program), query(q), local(l), channels(p.arity) {
    subject_role = p.arity == 1 || p.first_dir == EdgeDirect::OUT ? 0 : 1;
    object_role = p.arity == 1 ? 0 : 1 - subject_role;
    for (int r = 0; r < p.arity; ++r) {
      set<pair<EdgeDirect, int>> required;
      if (mode == Plain)
        required.emplace(EdgeDirect::OUT, 0);
      if (mode == Full)
        for (const auto &e : q.find(p.roles[r].id)->second->value.adj)
          required.emplace(e.d, e.el);
      if (mode == Residual || mode == Reuse || mode == Auto)
        for (const auto &op : p.outputs[r])
          required.emplace(op.dir, op.predicate);
      for (const auto &c : required) {
        Word tag = ((Word)(uint32_t)c.second << 2) | (r << 1) | (c.first == EdgeDirect::OUT);
        size_t weight = 1;
        for (size_t t = p.arity; t < p.roles.size(); ++t)
          for (auto in : p.inputs[t]) {
            const auto &op = p.outputs[in.source][in.slot];
            if (in.source == r && c.first == op.dir && c.second == op.predicate)
              // A constrained intersection saves work on every bound input;
              // a terminal name/label lookup has no further expansion.
              weight = std::max(weight, 1 + p.outputs[t].size() + 2 * (p.inputs[t].size() - 1));
          }
        channels[r].push_back({c.first, c.second, mix(tag), weight});
      }
    }
  }

  template <class Emit> bool route(const vector<GMatchVertex *> &vertices, Emit output) {
#ifdef PSPARQL_MATCHED_SHUFFLE
    MatchedShuffle<Emit> emit(output);
#else
    Emit &emit = output;
#endif
    bool first_window = true;
    map<vector<Word>, vector<int>> pending;
    size_t pending_seeds = 0;
    auto flush_pending = [&] {
      for (const auto &entry : pending)
        emit(entry.second);
      pending.clear();
      pending_seeds = 0;
    };
    while (true) {
      auto seeds = next_seeds(vertices);
      int active = !seeds.empty(), any = 0;
      MPI_Allreduce(&active, &any, 1, MPI_INT, MPI_MAX, MPI_COMM_WORLD);
      if (!any)
        break;
      map<EndpointKey, vector<Word>> descriptions;
      Packets requests(_num_workers);
      for (const Seed &s : seeds)
        for (int r = 0; r < p.arity; ++r) {
          EndpointKey key(r, r ? s.second : s.first);
          if (!descriptions.emplace(key, vector<Word>()).second)
            continue;
          int rank = owner(key.second);
          if (rank == _my_rank)
            descriptions[key] = describe(key);
          else {
            requests[rank].push_back(pack_ids(r, key.second));
          }
        }
      auto asks = exchange(requests, SummaryBytes);
      Packets replies(_num_workers);
      for (int rank = 0; rank < _num_workers; ++rank)
        for (Word endpoint : asks[rank]) {
          auto desc = describe(unpack_ids(endpoint));
          replies[rank].insert(replies[rank].end(), desc.begin(), desc.end());
        }
      auto received = exchange(replies, SummaryBytes);
      for (int rank = 0; rank < _num_workers; ++rank) {
        size_t j = 0;
        for (Word endpoint : requests[rank]) {
          auto key = unpack_ids(endpoint);
          int r = key.first;
          size_t width = (mode == Reuse || mode == Auto) ? 5 : 1 + channels[r].size();
          descriptions[key].assign(received[rank].begin() + j, received[rank].begin() + j + width);
          j += width;
        }
      }

      // Encode a sketch once per local bucket; every seed then costs one word.
      map<vector<Word>, vector<Word>> local_buckets;
      Packets packets(_num_workers);
      size_t width = 0;
      for (const auto &c : channels)
        width += c.size();
      if (mode == Solo || mode == Random || mode == Endpoint)
        width = 1;
      const bool cost_key = mode == Reuse || mode == Auto;
      if (cost_key)
        width = 3;
      for (const Seed &s : seeds) {
        vector<Word> key;
        Word highest_cost = 0;
        bool valid = true;
        for (int r = 0; r < p.arity; ++r) {
          const auto &desc = descriptions.at({r, r ? s.second : s.first});
          valid = valid && desc.front();
          if (cost_key) {
            if (key.empty() || desc[1] > highest_cost) {
              highest_cost = desc[1];
              key.assign(desc.begin() + 2, desc.end());
            }
          } else
            key.insert(key.end(), desc.begin() + 1, desc.end());
        }
        if (!valid)
          continue;
        Word pair_id = pack_ids(s.first, s.second);
        if (mode == Solo || mode == Random)
          key.push_back(mix(pair_id));
        if (mode == Endpoint)
          key.push_back(mix((uint32_t)s.first));
        local_buckets[std::move(key)].push_back(pair_id);
      }
      for (const auto &bucket : local_buckets) {
        Word hash = 0;
        for (Word word : bucket.first)
          hash = mix(hash ^ word);
        int dest = bucket.first.empty() ? _my_rank : hash % _num_workers;
        auto &packet = packets[dest];
        packet.insert(packet.end(), bucket.first.begin(), bucket.first.end());
        packet.push_back(bucket.second.size());
        packet.insert(packet.end(), bucket.second.begin(), bucket.second.end());
      }
      auto routed = exchange(packets, RouteBytes);
      map<vector<Word>, vector<Seed>> groups;
      for (const auto &packet : routed) {
        for (size_t i = 0; i < packet.size();) {
          vector<Word> key(packet.begin() + i, packet.begin() + i + width);
          i += width;
          size_t size = packet[i++];
          auto &group = groups[std::move(key)];
          for (size_t j = 0; j < size; ++j)
            group.push_back(unpack_ids(packet[i++]));
        }
      }
      if (mode == Auto && first_window) {
        // Same cheap decision on every rank, before any tasks are published.
        // Fall back when this window has little opportunity for edge reuse.
        unsigned long long local_counts[2] = {0, 0}, global_counts[2];
        for (const auto &entry : groups) {
          local_counts[0] += entry.second.size();
          local_counts[1] += (entry.second.size() + batch - 1) / batch;
        }
        MPI_Allreduce(local_counts, global_counts, 2, MPI_UNSIGNED_LONG_LONG, MPI_SUM,
                      MPI_COMM_WORLD);
        if (global_counts[0] < 64 || 2 * global_counts[1] >= global_counts[0])
          return false;
        first_window = false;
      }
      // Keep partial matching buckets across scan windows. Cap resident seed
      // descriptors even when every sketch is different (e.g. functional p).
      for (const auto &group : groups) {
        vector<Word> key = mode == Random ? vector<Word>() : group.first;
        for (auto seed : group.second) {
          auto &ids = pending[key];
          ids.push_back(seed.first);
          ids.push_back(seed.second);
          ++pending_seeds;
          if (mode == Solo || ids.size() == 2u * batch) {
            emit(ids);
            pending_seeds -= ids.size() / 2;
            pending.erase(key);
          }
          if (pending_seeds >= 32768)
            flush_pending();
        }
      }
    }
    flush_pending();
#ifdef PSPARQL_MATCHED_SHUFFLE
    emit.flush();
#endif
    return true;
  }
};
} // namespace edge_run
#endif
