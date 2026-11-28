// Benchmark control. Keep the routing, destination rank, number of tasks and
// every task size fixed. Permute only which edge seeds share each task.
#pragma once
#include "QueryExchange.h"
#include <random>

namespace edge_run {
template <class Emit> class MatchedShuffle {
  Emit &output;
  std::mt19937_64 rng;
  vector<size_t> sizes;
  vector<Seed> seeds;

public:
  explicit MatchedShuffle(Emit &emit) : output(emit), rng(71 + _my_rank) {}
  void operator()(const vector<int> &ids) {
    sizes.push_back(ids.size() / 2);
    for (size_t i = 0; i < ids.size(); i += 2) seeds.emplace_back(ids[i], ids[i + 1]);
    if (seeds.size() >= 32768) flush();
  }
  void flush() {
    shuffle(seeds.begin(), seeds.end(), rng);
    size_t pos = 0;
    for (size_t size : sizes) {
      vector<int> batch;
      batch.reserve(2 * size);
      for (size_t i = 0; i < size; ++i) {
        batch.push_back(seeds[pos].first);
        batch.push_back(seeds[pos++].second);
      }
      output(batch);
    }
    sizes.clear();
    seeds.clear();
  }
};
} // namespace edge_run
