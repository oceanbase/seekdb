#!/usr/bin/env python3
"""Exercise child namespace PX range cost and block splitting."""
import argparse
import resource

from namespace_inprocess_prototype import connect
from namespace_worker_bootstrap_prototype import BootstrapExperiment


def run(binary):
    experiment = BootstrapExperiment(binary, "inprocess_range_split", prototype=6)
    try:
        experiment.start()
        experiment.sql("ALTER SYSTEM SET px_task_size='1K'")
        experiment.sql("CREATE DATABASE rangecheck")
        experiment.sql("CREATE TABLE rangecheck.t(id INT PRIMARY KEY, v VARCHAR(256))")
        experiment.sql("INSERT INTO rangecheck.t VALUES(1,REPEAT('x',256))")
        for i in range(10):
            experiment.sql(
                "INSERT INTO rangecheck.t SELECT id + %d, v FROM rangecheck.t"
                % (2 ** i))
        experiment.sql("FORK NAMESPACE rangecheck_child FROM ns1")
        with connect(experiment, "root@rangecheck_child") as child:
            assert experiment.sql(
                "SELECT /*+ parallel(2) */ COUNT(*) FROM rangecheck.t", child
            ) == ((1024,),)
        experiment.record("PASS", case="inprocess_range_split", rows=1024)
    finally:
        experiment.close()


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", required=True)
    args = parser.parse_args()
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    run(args.binary)
