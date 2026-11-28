"""Paired timing of the frozen revise binary and QPMH variants on one input."""
import argparse
import csv
import json
import os
import platform
import random
import re
import shlex
import statistics
import subprocess
import tempfile
import time
from pathlib import Path


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--baseline", type=Path, required=True)
    p.add_argument("--baseline-observer", type=Path, help="separate observer-only 4079ce5 binary for counters")
    p.add_argument("--candidate", type=Path, required=True)
    p.add_argument("--launcher", required=True, help="e.g. 'mpiexec -f /path/hosts12 -n 12'")
    p.add_argument("--threads", type=int, default=2)
    p.add_argument("--data", required=True)
    p.add_argument("--query", required=True)
    p.add_argument("--expected", type=int, required=True)
    p.add_argument("--modes", nargs="+", default=["baseline","edge","endpoint","random","plain","full","residual","auto"])
    p.add_argument("--batch", type=int, default=4096)
    p.add_argument("--repeat", type=int, default=5)
    p.add_argument("--out", type=Path, required=True)
    p.add_argument("--footprint", action="store_true", help="exact logical subgraph census on separate counter runs")
    p.add_argument("--timeout", type=int, default=600)
    a = p.parse_args()
    rng = random.Random(71)
    a.out.mkdir(exist_ok=True,parents=True)
    config = {k:str(v) for k,v in vars(a).items()}
    config["host"]=platform.node()
    (a.out/"config.json").write_text(json.dumps(config,indent=2)+"\n")
    samples=[]
    def run(mode, repetition, measure):
        baseline=mode=="baseline"
        binary = a.candidate
        if baseline:
            binary = a.baseline_observer if measure and a.baseline_observer else a.baseline
        cmd=shlex.split(a.launcher)+[str(binary.resolve()),a.data,a.query,str(a.threads)]
        if not baseline: cmd.extend([mode,str(a.batch)])
        with tempfile.TemporaryDirectory() as directory:
            start=time.perf_counter()
            r=subprocess.run(cmd,cwd=directory,env=dict(os.environ, PSPARQL_STATS=str(int(measure)),
                                                     PSPARQL_FOOTPRINT=str(int(measure and a.footprint))),
                             stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,timeout=a.timeout)
            wall=time.perf_counter()-start
        (a.out/f"{mode}-{repetition}.log").write_text(r.stdout)
        counts=re.findall(r"number of matching results for query is: (\d+)",r.stdout)
        if r.returncode or counts!=[str(a.expected)]: raise RuntimeError(f"{mode} failed: {r.stdout[-1800:]}")
        task=float(re.search(r"Task Time\s*:\s*([\d.]+)",r.stdout)[1])
        print(mode,repetition,round(wall,4),task,flush=True)
        if measure:
            match=re.search(r"PSPARQL_METRICS (.*)",r.stdout)
            if not match: raise RuntimeError("missing counters")
            return dict(x.split("=") for x in match[1].split())
        samples.append(dict(mode=mode,round=repetition,wall_s=wall,task_s=task,answers=counts[0]))
    for rep in range(a.repeat):
        modes=a.modes[:];rng.shuffle(modes)
        for mode in modes:run(mode,rep,False)
        with (a.out/"timings.csv").open("w") as f:
            w=csv.DictWriter(f,fieldnames=samples[0]);w.writeheader();w.writerows(samples)
    counters=[run(m,"counters",True) for m in dict.fromkeys("root" if m=="baseline" and not a.baseline_observer else m for m in a.modes)]
    with (a.out/"counters.csv").open("w") as f:
        w=csv.DictWriter(f,fieldnames=counters[0]);w.writeheader();w.writerows(counters)
    for m in a.modes:
        print(m, "median wall", statistics.median(x["wall_s"] for x in samples if x["mode"]==m))

if __name__=="__main__": main()
