"""Add observers to an exported copy of 4079ce5. Never modify the baseline timing binary."""
from pathlib import Path
import argparse
import shutil
import subprocess
import tarfile


def insert(path, old, new):
    text=path.read_text()
    if text.count(old)!=1:
        raise RuntimeError(f"observer site changed in {path}")
    path.write_text(text.replace(old,new))


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("empty_directory",type=Path)
    target=parser.parse_args().empty_directory.resolve()
    target.mkdir(parents=True,exist_ok=True)
    if any(target.iterdir()): raise ValueError("export directory must be empty")
    repo=Path(__file__).resolve().parents[1]
    with subprocess.Popen(["git","archive","4079ce57544c1d72338c6af02ba9ecbc8b19f8b6"],
                          cwd=repo,stdout=subprocess.PIPE) as proc:
        with tarfile.open(fileobj=proc.stdout,mode="r|") as archive:
            archive.extractall(target)
        if proc.wait(): raise RuntimeError("baseline export failed")
    for f in ["system/util/query_stats.h","app_gmatch/GraphCensus.h","app_gmatch/QueryExchange.h"]:
        shutil.copyfile(repo/f,target/f)
    app=target/"app_gmatch"
    insert(target/"system/util/global.h","#include <mpi.h>",'#include <mpi.h>\n#include "query_stats.h"')
    insert(app/"GMatchComper.h",'#include "graph_matching.h"','#include "graph_matching.h"\n#include "GraphCensus.h"')
    site='    graph_matching_root(order.find(root)->second, graph, binding, counts[root], states.find(root)->second);'
    insert(app/"GMatchComper.h",site,site+'''
    edge_run::add(edge_run::Seeds, graph.roots.size());
    edge_run::add(edge_run::Groups);
    edge_run::add(edge_run::JoinedGroups, graph.roots.size() > 1);
    edge_run::record(graph);
    edge_run::flush();''')
    insert(app/"graph_matching.h",'    for (const AdjItem &edge : source.adj)',
           '    edge_run::add(edge_run::ListBuilds);\n    edge_run::add(edge_run::ScanItems,source.adj.size());\n    for (const AdjItem &edge : source.adj)')
    insert(app/"graph_matching.h",'  else if (!lists.empty())\n    leapfrog_join(lists, result);','  else if (!lists.empty()) { edge_run::add(edge_run::Intersections); leapfrog_join(lists, result); }')
    for m in ('m0','m1'):
        site='          MPI_Send('+m+'->get_buf()'
        insert(target/"system/subgraph/RespQueue.h",site,'          edge_run::totals[edge_run::ResponseBytes] += '+m+'->size();\n'+site)
    insert(app/"run.cpp",'    WorkerParams param;', '''    edge_run::mode = edge_run::Root;
    edge_run::stats = true;
    edge_run::footprints = true;
    WorkerParams param;''')
    insert(app/"run.cpp",'    worker_finalize();','    edge_run::finish_footprints();\n    edge_run::report(_my_rank);\n    worker_finalize();')


if __name__=="__main__": main()
