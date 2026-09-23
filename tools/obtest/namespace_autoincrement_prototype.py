#!/usr/bin/env python3
"""Check implicit AUTO_INCREMENT allocation in a forked namespace."""
import argparse
import resource

from namespace_inprocess_prototype import connect
from namespace_worker_bootstrap_prototype import BootstrapExperiment


def run(binary):
    experiment = BootstrapExperiment(binary, "inprocess_autoincrement", prototype=6)
    try:
        experiment.start()
        experiment.sql("CREATE NAMESPACE ai_child")
        experiment.sql("CREATE NAMESPACE ai_other")
        with connect(experiment, "root@ai_child") as child:
            experiment.sql("CREATE DATABASE ai", child)
            experiment.sql("CREATE TABLE ai.owned(id BIGINT PRIMARY KEY AUTO_INCREMENT, v INT)", child)
            experiment.record("autoincrement_rows",
                              child=experiment.sql("SELECT sequence_key,column_id FROM oceanbase.__all_auto_increment", child),
                              parent=experiment.sql("SELECT sequence_key,column_id FROM oceanbase.__all_auto_increment"))
            experiment.sql("INSERT INTO ai.owned(v) VALUES(30)", child)
            experiment.sql("INSERT INTO ai.owned(id,v) VALUES(10,40)", child)
            experiment.sql("INSERT INTO ai.owned(v) VALUES(50)", child)
            assert experiment.sql("SELECT id,v FROM ai.owned ORDER BY id", child) == (
                (1, 30), (10, 40), (11, 50))
            experiment.sql("ALTER TABLE ai.owned AUTO_INCREMENT=100", child)
            experiment.record("after_alter",
                              sequence=experiment.sql("SELECT sequence_key,sequence_value,sync_value FROM oceanbase.__all_auto_increment", child),
                              schema=experiment.sql("SELECT table_name,auto_increment FROM oceanbase.__all_table WHERE table_name='owned'", child))
            experiment.sql("INSERT INTO ai.owned(v) VALUES(60)", child)
            assert experiment.sql("SELECT id FROM ai.owned WHERE v=60", child) == ((100,),)
        with connect(experiment, "root@ai_other") as other:
            experiment.sql("CREATE DATABASE ai", other)
            experiment.sql("CREATE TABLE ai.owned(id BIGINT PRIMARY KEY AUTO_INCREMENT, v INT)", other)
            experiment.sql("INSERT INTO ai.owned(v) VALUES(70)", other)
            assert experiment.sql("SELECT id FROM ai.owned", other) == ((1,),)
        assert "ai" not in {row[0] for row in experiment.sql("SHOW DATABASES")}
        experiment.connection.close(); experiment.connection = None
        experiment.proc.terminate(); experiment.proc.wait(timeout=20)
        experiment.start()
        with connect(experiment, "root@ai_child") as child:
            experiment.sql("INSERT INTO ai.owned(v) VALUES(80)", child)
            assert experiment.sql("SELECT id FROM ai.owned WHERE v=80", child)[0][0] > 100
            experiment.sql("CREATE TABLE ai.later(id BIGINT PRIMARY KEY, v INT)", child)
            experiment.sql("INSERT INTO ai.later VALUES(1,5)", child)
            experiment.sql("ALTER TABLE ai.later MODIFY id BIGINT AUTO_INCREMENT", child)
            experiment.sql("INSERT INTO ai.later(v) VALUES(6)", child)
            assert experiment.sql("SELECT id FROM ai.later WHERE v=6", child)[0][0] > 1
        with connect(experiment, "root@ai_other") as other:
            experiment.sql("INSERT INTO ai.owned(v) VALUES(90)", other)
            assert experiment.sql("SELECT id FROM ai.owned WHERE v=90", other)[0][0] > 1
        experiment.record("PASS", case="inprocess_autoincrement", restart=True,
                          independent_namespaces=True)
    finally:
        experiment.close()


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", required=True)
    args = parser.parse_args()
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    run(args.binary)
