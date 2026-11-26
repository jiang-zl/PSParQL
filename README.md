# PSParQL: Parallel SPARQL query Processing

We study how to exploit the inherent parallelism of parallel computing systems for intraquery parallelism during subgraph query processing over large graphs. Different from widely used hash-based parallelism techniques, we utilize and extend locality sensitive hashing based on the structures of subgraph queries to merge multiple tasks during a single subgraph query processing or multiple subgraph queries. 



## Prerequisities

- Linux
- GCC 4.8.5 or higher version
- MPICH3
- JDK 7 or higher version
- Hadoop 2.7.7 or higher version



## Data Format

The data input into the program needs to be in adjacency list format, as follows:

```
vertex-ID label \t  neighbor1-ID  neighbor1-label  neighbor1-edge-label  neighbor2-ID  neighbor2-label  neighbor2-edge-label  neighbor3-ID   neighbor3-label   neighbor3-edge-label...
```

In the **sample_data** directory of the project, there are sample data graph and query graph.

If you need to input multiple query graphs, please put multiple query graphs in one file. The id of the vertex of each query graph must be unique, and the query graphs are separated by blank lines. **In the query graph file, please use CRLF for line breaks.**

Sample/data_graph:

```
0 0	1 2 1 2 1 1 11 2 1
1 2	2 1 1 3 1 1
2 1	11 2 1
3 1	6 2 1
4 3	3 1 1
5 0	1 2 1 3 1 1 6 2 1
6 2	7 1 1 12 1 1 4 3 1
8 3	7 1 1
9 3	7 1 1
10 1	6 2 1
11 2	7 1 1
13 2	12 1 1
14 3	12 1 2
15 0	6 2 1 10 1 1
```

Sample/query_graph:

```
0 0	1 2 1 2 1 1
1 2	3 1 1
2 1	1 2 1
4 3	3 1 1
```


## RDF conversion and counting

The standalone converter uses Raptor 2 (development headers and library).
Compile it once and convert the data and its queries together so that constants
and predicates share one dictionary. Input is one triple per line. Query
patterns can use variables in subject and object positions and fixed predicates.
Extract the triple patterns from the SPARQL WHERE block before conversion.

```sh
c++ -O2 -std=c++11 data_proc/data_proc.cpp -o data_proc/data_proc $(pkg-config --cflags --libs raptor2)
data_proc/data_proc data.nt data.adj q1.patterns q1.adj q2.patterns q2.adj
```

With no arguments the converter uses the original YAGO2 filenames. Each
distinct directed RDF triple is kept once. Parallel predicates and reciprocal
edges remain distinct. The matcher counts complete BGP bindings, including
bindings in which two variables refer to the same RDF term. For SELECT without
DISTINCT this preserves the number of projected rows.

## Configuration

In the `ydhdfs2.h` file in the `$PSP_PATH/system/util` (**$PSP_PATH** is the root directory of the project), you need to specify the hostname and port of the master machine (aka NameNode) of the Hadoop cluster. 

```c++
// $PSP_PATH/system/util/ydhdfs2.h
hdfsBuilderSetNameNode(bld, "master"); // master machine's hostname, default is master
hdfsBuilderSetNameNodePort(bld, 9000); // port, default is 9000
```



## Compile

In the root directory of the project, execute the following command, you can get an executable file `run`.

```shell
cd app_gmatch
make
```



## Run

Before running the code, please check the following:

- All prerequisites have been met
- The `ydhdfs2.h` file has been updated with your master machine’s hostname, port
- Data graph and query graph have been converted into adjacency list format
- The data graph and query graph have been uploaded to HDFS
- All executable files have been sent to each machine in the cluster (if you want to run on multiple machines )

### Running on multiple machines

You can use the `mpiexec` command to run the program on multiple machines, the command is as follows:

```shell
mpiexec -n <N> -f <hosts> ./run <data_graph_path> <query_graph_path> <t>

# mv run ~
# cd ~
# scp run slave1:~
# scp run slave2:~
# mpiexec --allow-run-as-root -mca btl_tcp_if_include eno1 -host master,slave1,slave2 -n 3 ./run /psp/sample_data/data_graph_edge1_2.txt /psp/sample_data/query_graph_edge1.txt 3
```

where **N** is the number of machines/processes, **hosts** is the hosts file, **data_graph_path** is the HDFS path of the data graph, **query_graph_path** is the HDFS path of the query graph, and **t** is the number of threads per machine/process.

### Running on a single machine

If you only want to test on a single machine, you can execute the following command:

```shell
./run <data_graph_path> <query_graph_path> <t>
# ./runWithEnv ./run /psp/sample_data/data_graph_edge1_2.txt /psp/sample_data/query_graph_edge1.txt 2
```

### Results of the run

For `data_graph_edge1_2.txt` with `query_graph_edge1.txt`, the final count is:

```text
the number of matching results for query is: 6
```

## Count regression tests

Build the matcher on the deployment machine before distributing it to workers.
For small test graphs, a trace build emits each complete binding. The regression
script compares their multiset and the reported count against a triple-pattern
join. Use the same binary on all MPI ranks.

```sh
make -B -C app_gmatch CXXFLAGS=-DPSPARQL_TRACE_BINDINGS
python3 tests/rdf_terms.py data_proc/data_proc
python3 tests/rdf_counts.py --hdfs-root /tmp --threads 2 -- mpiexec -n 4 /absolute/path/to/run
```

Repeat with `CXXFLAGS="-DPSPARQL_TRACE_BINDINGS -DENABLE_QUERY_GRAPH_COMBINE=0"`
to check task merging. Rebuild without the trace flag for benchmarks. With a
normal build the test runner accepts `--counts-only`.
