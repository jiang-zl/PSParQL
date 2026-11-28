//########################################################################
//## Copyright 2018 Da Yan http://www.cs.uab.edu/yanda
//##
//## Licensed under the Apache License, Version 2.0 (the "License");
//## you may not use this file except in compliance with the License.
//## You may obtain a copy of the License at
//##
//## //http://www.apache.org/licenses/LICENSE-2.0
//##
//## Unless required by applicable law or agreed to in writing, software
//## distributed under the License is distributed on an "AS IS" BASIS,
//## WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
//## See the License for the specific language governing permissions and
//## limitations under the License.
//########################################################################


#include "comm-struct.h"
#include "subg-dev.h"
#include "GMatchTrimmer.h"
#include "GMatchAgg.h"
#include "GMatchWorker.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char *argv[])
{
    init_worker(&argc, &argv);
    if (argc < 4 || argc > 6 || atoi(argv[3]) < 1) {
        if (!_my_rank) fprintf(stderr, "usage: run DATA QUERY THREADS [root|edge|endpoint|random|plain|full|residual|reuse|auto] [batch size 1..4096]\n");
        worker_finalize();
        return 2;
    }
    if (argc >= 5) {
        int i = 0;
        while (i <= edge_run::Auto && strcmp(argv[4], edge_run::mode_names[i])) ++i;
        if (i > edge_run::Auto) {
            if (!_my_rank) fprintf(stderr, "Unknown query mode: %s\n", argv[4]);
            worker_finalize();
            return 2;
        }
        edge_run::mode = static_cast<edge_run::Mode>(i);
    }
    if (argc == 6) edge_run::batch = atoi(argv[5]);
    if (edge_run::batch < 1 || edge_run::batch > 4096) {
        if (!_my_rank) fprintf(stderr, "Batch size must be 1..4096\n");
        worker_finalize();
        return 2;
    }
    int collect = getenv("PSPARQL_STATS") && !strcmp(getenv("PSPARQL_STATS"), "1");
    MPI_Bcast(&collect, 1, MPI_INT, 0, MPI_COMM_WORLD);
    int footprint = getenv("PSPARQL_FOOTPRINT") && !strcmp(getenv("PSPARQL_FOOTPRINT"), "1");
    MPI_Bcast(&footprint, 1, MPI_INT, 0, MPI_COMM_WORLD);
    edge_run::footprints = footprint;
    edge_run::stats = collect || footprint;
    WorkerParams param;
    param.input_path = argv[1]; // the path of data graph in HDFS
    param.query_graph_path = argv[2]; // the path of query graph in HDFS
    int thread_num = atoi(argv[3]); // number of threads per process
    // cout << "-------------------------------" << endl;
    // cout << "\nthe path of data graph in HDFS: " << param.input_path
    //      << "\nthe path of query graph in HDFS: " << param.query_graph_path
    //      << "\nnumber of threads per process: " << thread_num << endl;
    param.force_write = true;
    param.native_dispatcher = false;
    //------
    GMatchTrimmer trimmer;
    GMatchAgg aggregator;
    GMatchWorker worker(thread_num);
    // worker.setTrimmer(&trimmer);
    worker.setAggregator(&aggregator);
    worker.run(param);
    edge_run::finish_footprints();
    edge_run::report(_my_rank);
    worker_finalize();
    return 0;
}