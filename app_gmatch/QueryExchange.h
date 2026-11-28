#pragma once
#include "GMatchSubgraph.h"
#include <cstdint>

namespace edge_run {
using Word = unsigned long long;
using Packets = vector<vector<Word>>;
using Seed = pair<int, int>;

inline Word pack_ids(int first, int second) {
  return (static_cast<Word>(static_cast<uint32_t>(first)) << 32) | static_cast<uint32_t>(second);
}
inline Seed unpack_ids(Word pair) {
  return {static_cast<int32_t>(pair >> 32), static_cast<int32_t>(pair & 0xffffffffULL)};
}

inline Word mix(Word x) {
  x += 0x9e3779b97f4a7c15ULL;
  x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
  x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
  return x ^ (x >> 31);
}

// Bounded input windows. Split large all-to-all payloads to respect MPI's int
// displacements even for large communicators and wide query descriptors.
inline Packets exchange(const Packets &out, Metric metric) {
  const int n = _num_workers;
  Packets in(n);
  const size_t limit = std::max(1, (1 << 20) / n);
  int rounds = 0;
  for (int i = 0; i < n; ++i) {
    if (i == _my_rank) {
      in[i] = out[i];
      continue;
    }
    rounds = std::max(rounds, (int)((out[i].size() + limit - 1) / limit));
    if (metric != Metrics)
      add(metric, sizeof(Word) * out[i].size());
  }
  int all = 0;
  MPI_Allreduce(&rounds, &all, 1, MPI_INT, MPI_MAX, MPI_COMM_WORLD);
  for (int r = 0; r < all; ++r) {
    vector<int> sc(n), rc(n), sd(n), rd(n);
    vector<Word> send;
    size_t offset = r * limit;
    for (int i = 0; i < n; ++i) {
      sd[i] = send.size();
      if (i == _my_rank || offset >= out[i].size())
        continue;
      sc[i] = std::min(limit, out[i].size() - offset);
      send.insert(send.end(), out[i].begin() + offset, out[i].begin() + offset + sc[i]);
    }
    MPI_Alltoall(sc.data(), 1, MPI_INT, rc.data(), 1, MPI_INT, MPI_COMM_WORLD);
    int total = 0;
    for (int i = 0; i < n; ++i) {
      rd[i] = total;
      total += rc[i];
    }
    vector<Word> recv(total);
    MPI_Alltoallv(send.data(), sc.data(), sd.data(), MPI_UNSIGNED_LONG_LONG, recv.data(), rc.data(),
                  rd.data(), MPI_UNSIGNED_LONG_LONG, MPI_COMM_WORLD);
    for (int i = 0; i < n; ++i)
      in[i].insert(in[i].end(), recv.begin() + rd[i], recv.begin() + rd[i] + rc[i]);
  }
  return in;
}

} // namespace edge_run
