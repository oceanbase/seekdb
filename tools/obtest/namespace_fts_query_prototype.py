#!/usr/bin/env python3
"""Verify fulltext queries against child-owned indexes."""
import argparse
import resource

from namespace_inprocess_prototype import connect
from namespace_worker_bootstrap_prototype import BootstrapExperiment


def run(binary):
    experiment = BootstrapExperiment(binary, "inprocess_fts_query", prototype=6)
    try:
        experiment.start()
        experiment.sql("CREATE DATABASE fts_parent")
        experiment.sql("CREATE TABLE fts_parent.t(id INT PRIMARY KEY, body TEXT)")
        experiment.sql("INSERT INTO fts_parent.t VALUES(1,'alpha word'),(2,'beta word')")
        experiment.sql("CREATE FULLTEXT INDEX idx_body ON fts_parent.t(body)")
        query = "SELECT id FROM {}.t WHERE MATCH(body) AGAINST('alpha') ORDER BY id"
        assert experiment.sql(query.format("fts_parent")) == ((1,),)
        experiment.sql("CREATE NAMESPACE fts_child")
        with connect(experiment, "root@fts_child") as child:
            experiment.sql("CREATE DATABASE fts", child)
            experiment.sql("CREATE TABLE fts.t(id INT PRIMARY KEY, body TEXT)", child)
            experiment.sql("INSERT INTO fts.t VALUES(1,'alpha word'),(2,'beta word')", child)
            experiment.sql("CREATE FULLTEXT INDEX idx_body ON fts.t(body)", child)
            assert experiment.sql(query.format("fts"), child) == ((1,),)
            assert experiment.sql("SELECT id FROM fts.t WHERE MATCH(body) "
                                  "AGAINST('beta') ORDER BY id", child) == ((2,),)
        experiment.connection.close(); experiment.connection = None
        experiment.proc.terminate(); experiment.proc.wait(timeout=20)
        experiment.start()
        with connect(experiment, "root@fts_child") as child:
            assert experiment.sql(query.format("fts"), child) == ((1,),)
        experiment.record("PASS", case="inprocess_fts_query", restart=True)
    finally:
        experiment.close()


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", required=True)
    args = parser.parse_args()
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    run(args.binary)
