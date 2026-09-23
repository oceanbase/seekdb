#!/usr/bin/env python3
"""Verify a dropped fork source stays readable until its last child is dropped."""
import argparse
import resource
import time

import pymysql

from namespace_inprocess_prototype import connect
from namespace_worker_bootstrap_prototype import BootstrapExperiment


def run(binary):
    experiment = BootstrapExperiment(binary, "inprocess_source_drop", prototype=6)
    source = child = None
    try:
        experiment.start()
        experiment.sql("CREATE DATABASE app")
        experiment.sql("CREATE TABLE app.base(id INT PRIMARY KEY,v INT)")
        experiment.sql("INSERT INTO app.base VALUES(1,10)")
        experiment.sql("FORK NAMESPACE source_drop FROM ns1")
        source = connect(experiment, "root@source_drop")
        experiment.sql("CREATE DATABASE owned", source)
        experiment.sql("CREATE TABLE owned.t(id INT PRIMARY KEY,v INT)", source)
        experiment.sql("INSERT INTO owned.t VALUES(1,21),(2,22)", source)
        experiment.sql("FORK NAMESPACE child_drop FROM source_drop")
        source_id = experiment.sql(
            "SELECT namespace_id FROM __fork_proto_meta.namespaces "
            "WHERE name='source_drop'")[0][0]
        child = connect(experiment, "root@child_drop")
        assert experiment.sql("SELECT SUM(v) FROM owned.t", child) == ((43,),)
        try:
            experiment.sql("DROP NAMESPACE source_drop")
        except pymysql.MySQLError:
            pass
        else:
            raise AssertionError("drop accepted an active source connection")
        source.close(); source = None
        experiment.sql("DROP NAMESPACE source_drop")
        try:
            connect(experiment, "root@source_drop").close()
        except pymysql.MySQLError:
            pass
        else:
            raise AssertionError("dropped source accepted login")
        assert experiment.sql("SELECT SUM(v) FROM owned.t", child) == ((43,),)
        assert experiment.sql("SELECT v FROM app.base WHERE id=1", child) == ((10,),)
        experiment.sql("UPDATE owned.t SET v=23 WHERE id=1", child)
        assert experiment.sql("SELECT SUM(v) FROM owned.t", child) == ((45,),)
        child.close(); child = None
        experiment.connection.close(); experiment.connection = None
        experiment.proc.terminate(); experiment.proc.wait(timeout=20)
        experiment.start()
        with connect(experiment, "root@child_drop") as recovered:
            assert experiment.sql("SELECT SUM(v) FROM owned.t", recovered) == ((45,),)
            assert experiment.sql("SELECT v FROM app.base WHERE id=1", recovered) == ((10,),)
        experiment.sql("DROP NAMESPACE child_drop")
        for _ in range(20):
            remaining = experiment.sql(
                "SELECT COUNT(*) FROM __fork_proto_meta.exceptions "
                f"WHERE namespace_id={source_id} AND kind=0")[0][0]
            if remaining == 0:
                break
            time.sleep(1)
        assert remaining == 0, ("source tablet GC did not finish", remaining)
        experiment.record("PASS", case="source_drop", restart=True, async_gc=True)
    finally:
        if source is not None:
            source.close()
        if child is not None:
            child.close()
        experiment.close()


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", required=True)
    args = parser.parse_args()
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    run(args.binary)
