#!/usr/bin/env python3
"""Probe empty hybrid vector maintenance through a child Namespace session."""
import argparse
import resource

from namespace_inprocess_prototype import connect
from namespace_fork_prototype import Experiment


def run(binary):
    experiment = Experiment(binary, "hybrid_manual", prototype=6)
    try:
        experiment.start()
        experiment.sql("ALTER SYSTEM SET vector_index_optimize_duty_time='[00:00:00,23:59:59]'")
        experiment.sql("CREATE DATABASE hybrid_manual")
        experiment.sql(
            "CALL DBMS_AI_SERVICE.CREATE_AI_MODEL('ob_embed', "
            "'{\"type\":\"dense_embedding\",\"model_name\":\"bge-M3\"}')")
        experiment.sql("FORK NAMESPACE hybrid_manual_child FROM ns1")
        with connect(experiment, "root@hybrid_manual_child") as child:
            experiment.sql(
                "CREATE TABLE hybrid_manual.t(id INT PRIMARY KEY, txt VARCHAR(100), "
                "VECTOR INDEX idx_txt(txt) WITH (distance=l2,type=hnsw,"
                "model=ob_embed,dim=1024,sync_mode=immediate))", child)
            experiment.sql("SET ob_query_timeout=30000000", child)
            experiment.sql(
                "CALL dbms_vector.refresh_index('hybrid_manual.idx_txt',"
                "'hybrid_manual.t','txt',1,'FAST')", child)
            experiment.sql(
                "CALL dbms_vector.rebuild_index('hybrid_manual.idx_txt',"
                "'hybrid_manual.t','txt',0)", child)
            child_active = experiment.sql("SELECT COUNT(*) FROM oceanbase.__all_vector_index_task", child)
            active = experiment.sql("SELECT COUNT(*) FROM oceanbase.__all_vector_index_task")
            history = experiment.sql("SELECT COUNT(*) FROM oceanbase.__all_vector_index_task_history")
            assert active[0][0] + history[0][0] > 0, (active, history)
            assert child_active == ((0,),), child_active
            experiment.record("hybrid_manual_tasks", active=active, history=history)
        experiment.record("PASS", case="hybrid_manual", child=True, empty_index=True)
    finally:
        experiment.close()


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", required=True)
    args = parser.parse_args()
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    run(args.binary)
