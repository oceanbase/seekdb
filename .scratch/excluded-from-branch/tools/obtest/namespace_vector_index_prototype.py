#!/usr/bin/env python3
"""Verify a child namespace can build a vector index with its own schemas."""
import argparse
import resource

from namespace_inprocess_prototype import connect
from namespace_worker_bootstrap_prototype import BootstrapExperiment


def run(binary, nonempty=False):
    experiment = BootstrapExperiment(binary, "inprocess_vector_index", prototype=6)
    try:
        experiment.start()
        experiment.sql("CREATE NAMESPACE vec_child")
        with connect(experiment, "root@vec_child") as child:
            experiment.sql("CREATE DATABASE vec", child)
            experiment.sql("CREATE TABLE vec.t(id INT PRIMARY KEY, embedding VECTOR(3))", child)
            if nonempty:
                experiment.sql("INSERT INTO vec.t VALUES(1,'[1,0,0]'),(2,'[4,0,0]')", child)
            experiment.sql("CREATE VECTOR INDEX idx_embedding ON vec.t(embedding) "
                           "WITH (distance=l2, type=hnsw, lib=vsag)", child)
            assert experiment.sql("SELECT COUNT(*) FROM vec.t", child) == ((2 if nonempty else 0,),)
            if nonempty:
                assert experiment.sql("SELECT id FROM vec.t ORDER BY "
                                      "l2_distance(embedding,[0,0,0]) APPROXIMATE LIMIT 1", child) == ((1,),)
        experiment.connection.close(); experiment.connection = None
        experiment.proc.terminate(); experiment.proc.wait(timeout=20)
        experiment.start()
        with connect(experiment, "root@vec_child") as child:
            assert experiment.sql("SELECT COUNT(*) FROM vec.t", child) == ((2 if nonempty else 0,),)
            if nonempty:
                assert experiment.sql("SELECT id FROM vec.t ORDER BY "
                                      "l2_distance(embedding,[0,0,0]) APPROXIMATE LIMIT 1", child) == ((1,),)
        experiment.record("PASS", case="inprocess_vector_index", nonempty=nonempty, restart=True)
    finally:
        experiment.close()


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", required=True)
    parser.add_argument("--nonempty", action="store_true")
    args = parser.parse_args()
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    run(args.binary, args.nonempty)
