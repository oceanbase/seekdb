#!/usr/bin/env python3
"""Verify FORK TABLE resolves child-owned and inherited source tablets."""
import argparse
import resource

from namespace_inprocess_prototype import connect
from namespace_worker_bootstrap_prototype import BootstrapExperiment


def run(binary):
    experiment = BootstrapExperiment(binary, "inprocess_fork_table", prototype=6)
    try:
        experiment.start()
        experiment.sql("CREATE DATABASE shared")
        experiment.sql("CREATE TABLE shared.base(id INT PRIMARY KEY, v INT)")
        experiment.sql("INSERT INTO shared.base VALUES(1,10)")
        experiment.sql("FORK NAMESPACE ft_inherited FROM ns1")
        experiment.sql("UPDATE shared.base SET v=99 WHERE id=1")
        experiment.sql("CREATE NAMESPACE ft_child")
        with connect(experiment, "root@ft_child") as child:
            experiment.sql("CREATE DATABASE ft", child)
            experiment.sql("CREATE TABLE ft.src(id INT PRIMARY KEY, v INT)", child)
            experiment.sql("INSERT INTO ft.src VALUES(1,10),(2,20)", child)
            experiment.sql("FORK TABLE ft.src TO ft.dst", child)
            assert experiment.sql("SELECT id,v FROM ft.dst ORDER BY id", child) == (
                (1, 10), (2, 20))
            experiment.sql("CREATE TABLE ft.auto(id BIGINT PRIMARY KEY AUTO_INCREMENT, v INT)", child)
            experiment.sql("INSERT INTO ft.auto(v) VALUES(30),(40)", child)
            experiment.sql("FORK TABLE ft.auto TO ft.auto_copy", child)
            experiment.sql("INSERT INTO ft.auto_copy(v) VALUES(50)", child)
            assert experiment.sql("SELECT id FROM ft.auto_copy WHERE v=50", child)[0][0] > 2
            assert experiment.sql("SELECT COUNT(*) FROM ft.auto", child) == ((2,),)
        with connect(experiment, "root@ft_inherited") as inherited:
            assert experiment.sql("SELECT v FROM shared.base WHERE id=1", inherited) == ((10,),)
            experiment.sql("FORK TABLE shared.base TO shared.copy", inherited)
            assert experiment.sql("SELECT v FROM shared.copy WHERE id=1", inherited) == ((10,),)
        experiment.connection.close(); experiment.connection = None
        experiment.proc.terminate(); experiment.proc.wait(timeout=20)
        experiment.start()
        with connect(experiment, "root@ft_inherited") as inherited:
            assert experiment.sql("SELECT v FROM shared.copy WHERE id=1", inherited) == ((10,),)
        assert experiment.sql("SELECT v FROM shared.base WHERE id=1") == ((99,),)
        experiment.record("PASS", case="inprocess_fork_table", inherited=True,
                          restart=True)
    finally:
        experiment.close()


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", required=True)
    args = parser.parse_args()
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    run(args.binary)
