// Build with the same headers and flags as app_gmatch/run.cpp.
#include "comm-struct.h"
#include <cassert>
#include <cstring>
#include <cstdio>

static void check(const GMatchTask &task, const GMatchTask &copy) {
  assert(copy.context == task.context && copy.group_id == task.group_id);
  assert(copy.to_pull == task.to_pull);
  assert(copy.subG.edge_seeds == task.subG.edge_seeds);
  assert(copy.subG.projections == task.subG.projections);
}

int main(int argc, char **argv) {
  if (argc != 2)
    return 2; // path in a test temp directory
  edge_run::mode = edge_run::Full;
  GMatchTask work;
  work.context = 2;
  work.group_id = -1;
  work.subG.max_step = 4;
  work.subG.query_vertexs = {-1};
  work.subG.edge_seeds = {1, 2, 1, 3};
  work.to_pull = {5, 6};
  work.subG.projections.resize(4);
  work.subG.projections[0][1] = {{5, 6}, {}};
  work.subG.projections[1][2] = {{6}};
  work.subG.projections[1][3] = {{5}};
  ibinstream bytes;
  bytes << work;
  char *buf = new char[bytes.size()];
  memcpy(buf, bytes.get_buf(), bytes.size());
  obinstream in(buf, bytes.size());
  GMatchTask receiver;
  in >> receiver;
  check(work, receiver);
  ifbinstream file(argv[1]);
  file << receiver;
  file.close();
  ofbinstream saved(argv[1]);
  GMatchTask resumed;
  saved >> resumed;
  check(work, resumed);
  puts("edge task wire and spill round trips passed");
}
