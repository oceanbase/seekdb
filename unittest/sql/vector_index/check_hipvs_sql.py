#!/usr/bin/env python3
"""Check hipVS SQL contracts on an enabled build; recreates hipvs_contract in a dedicated database."""
import argparse
import json
from pathlib import Path
import re
import subprocess
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=2881)
    parser.add_argument("--user", default="root")
    parser.add_argument("--client", default="mysql")
    parser.add_argument("--database", default="hipvs_contract_check")
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    if not re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", args.database) or not 1 <= args.port <= 65535:
        parser.error("invalid database identifier or port")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    checks = []
    report = {"started_utc": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
              "completed": False, "checks": checks,
              "scope": "actual SQL assertions; not the mysqltest runner"}
    client = [args.client, "--protocol=TCP", f"-h{args.host}", f"-P{args.port}", f"-u{args.user}",
              "--batch", "--raw", "--skip-column-names", "--connect-timeout=10"]

    def sql(statement, database=True, expected_error=None):
        result = subprocess.run(client + ([args.database] if database else []),
                                input="SET ob_query_timeout=600000000;" + statement,
                                text=True, capture_output=True, timeout=650)
        if expected_error:
            if not (result.returncode and "ERROR 1235" in result.stderr and expected_error in result.stderr):
                raise RuntimeError("wrong declaration result: " + result.stdout + result.stderr)
            checks.append({"sql": statement, "passed": True, "expected_error": result.stderr.strip()})
        elif result.returncode:
            raise RuntimeError(result.stderr.strip())
        return result.stdout.strip()

    try:
        report["server_version"] = sql("SELECT VERSION()", database=False)
        sql(f"CREATE DATABASE IF NOT EXISTS {args.database}", database=False)
        sql("DROP TABLE IF EXISTS hipvs_contract; CREATE TABLE hipvs_contract(id INT PRIMARY KEY,vec VECTOR(3))")
        for lib in ("hipvs", "cuvs", "CuVs"):
            for params in ("distance=cosine,type=hnsw,m=16", "distance=l2,type=hnsw_sq,m=16",
                           "distance=l2,type=hnsw_bq,m=16", "distance=l2,type=hnsw,m=20"):
                sql(f"CREATE VECTOR INDEX bad ON hipvs_contract(vec) WITH ({params},lib={lib})",
                    expected_error="hipVS requires dense float32 L2 HNSW")
        sql("INSERT INTO hipvs_contract VALUES (11,'[1,0,0]'),(29,'[2,0,0]')")
        for lib in ("cuvs", "CuVs", "hipvs"):
            sql(f"CREATE VECTOR INDEX alias_idx ON hipvs_contract(vec) WITH (distance=l2,type=hnsw,lib={lib},m=16)")
            got = sql("SELECT id FROM hipvs_contract ORDER BY l2_distance(vec,'[1,0,0]') APPROXIMATE LIMIT 1")
            ddl = sql("SHOW CREATE TABLE hipvs_contract")
            if got != "11" or not ("CUVS" in ddl.upper() or "HIPVS" in ddl.upper()):
                raise RuntimeError(f"alias query/DDL mismatch: {got!r}, {ddl!r}")
            checks.append({"lib": lib, "passed": True, "tiny_snapshot_query_id": 11, "ddl": ddl})
            sql("DROP INDEX alias_idx ON hipvs_contract")
        for index in range(12):
            sql("CREATE VECTOR INDEX cpu_idx ON hipvs_contract(vec) WITH (distance=l2,type=hnsw,lib=vsag,m=16)")
            got = sql("SELECT id FROM hipvs_contract ORDER BY l2_distance(vec,'[1,0,0]') APPROXIMATE LIMIT 1")
            if got != "11":
                raise RuntimeError(f"CPU rebuild query mismatch: {got!r}")
            sql("DROP INDEX cpu_idx ON hipvs_contract")
            checks.append({"cpu_rebuild_round": index + 1, "passed": True, "id": 11})
        sql("DROP TABLE hipvs_contract")
        report["completed"] = True
    except Exception as error:
        report["error"] = str(error)
        raise
    finally:
        report["finished_utc"] = time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime())
        args.output.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
        print(json.dumps({"completed": report["completed"], "checks": len(checks),
                          "output": str(args.output)}), flush=True)


if __name__ == "__main__":
    main()
