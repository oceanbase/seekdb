#!/usr/bin/env python3
"""Check MVT SQL contracts against two isolated running test instances."""

import argparse
import json
import re
import statistics
import subprocess
import time
from pathlib import Path

from mvt_codec_compatibility import semantic


def sql(port, query, allow_error=False):
    result = subprocess.run(
        ["mysql", "--connect-timeout=5", "--protocol=TCP", "-h127.0.0.1",
         "-P" + str(port), "-uroot", "-N", "-B", "--raw", "-e", query],
        capture_output=True, text=True, timeout=120,
    )
    if result.returncode:
        match = re.search(r"ERROR (\d+)", result.stderr)
        if allow_error and match:
            return {"error": int(match[1])}
        raise RuntimeError(result.stderr)
    return result.stdout.strip()


def prepare(port):
    sql(port, """
      CREATE DATABASE IF NOT EXISTS mvt_codec_contracts;
      DROP TABLE IF EXISTS mvt_codec_contracts.tiles;
      CREATE TABLE mvt_codec_contracts.tiles(
        id BIGINT, label VARCHAR(40), flag TINYINT, score DOUBLE, geom GEOMETRY, meta JSON);
      INSERT INTO mvt_codec_contracts.tiles VALUES
        (0, 'one', 1, 1.25, ST_GeomFromText('POINT(1 2)'),
          '{"a":-7,"b":true,"ignored":[1],"nested":{},"null":null}'),
        (2, 'two', 0, 2.5, ST_GeomFromText('LINESTRING(1 2,3 4,5 2)'), '{"c":7}'),
        (-1, NULL, NULL, NULL, ST_GeomFromText('POLYGON((0 0,4 0,4 4,0 4,0 0))'), NULL),
        (99, 'skip', 1, 9.0, NULL, NULL);
    """)


def contracts(port):
    table = "mvt_codec_contracts.tiles"
    cases = {
        "default": f"SELECT HEX(_st_asmvt(t.*)) FROM {table} t WHERE id=0",
        "id_zero": f"SELECT HEX(_st_asmvt(t.*, 'roads', 8192, 'geom', 'id')) FROM {table} t WHERE id=0",
        "negative_id": f"SELECT HEX(_st_asmvt(t.*, 'roads', 8192, 'geom', 'id')) FROM {table} t WHERE id=-1",
        "negative_extent": f"SELECT HEX(_st_asmvt(t.*, 'x', -1)) FROM {table} t WHERE id=0",
        "empty_name": f"SELECT HEX(_st_asmvt(t.*, '')) FROM {table} t WHERE id=0",
        "line": f"SELECT HEX(_st_asmvt(t.*)) FROM {table} t WHERE id=2",
        "polygon": f"SELECT HEX(_st_asmvt(t.*)) FROM {table} t WHERE id=-1",
        "null_geometry": f"SELECT HEX(_st_asmvt(t.*)) FROM {table} t WHERE id=99",
        "empty_input": f"SELECT HEX(_st_asmvt(t.*)) FROM {table} t WHERE 1=0",
        "upper_name": f"SELECT HEX(_st_asmvt(t.*, 'Bad')) FROM {table} t",
        "zero_extent": f"SELECT HEX(_st_asmvt(t.*, 'x', 0)) FROM {table} t",
        "missing_geometry": f"SELECT HEX(_st_asmvt(t.*, 'x', 4096, 'missing')) FROM {table} t",
        "missing_id": f"SELECT HEX(_st_asmvt(t.*, 'x', 4096, 'geom', 'missing')) FROM {table} t",
        "geom_default": "SELECT ST_AsText(_st_asmvtgeom(ST_GeomFromText('POINT(1 2)'), ST_GeomFromText('POLYGON((0 0,10 0,10 10,0 10,0 0))')))",
        "geom_extent": "SELECT ST_AsText(_st_asmvtgeom(ST_GeomFromText('POINT(1 2)'), ST_GeomFromText('POLYGON((0 0,10 0,10 10,0 10,0 0))'), 100, 0, TRUE))",
        "geom_null": "SELECT _st_asmvtgeom(NULL, ST_GeomFromText('POLYGON((0 0,10 0,10 10,0 10,0 0))'))",
    }
    result = {}
    for name, query in cases.items():
        value = sql(port, query, allow_error=True)
        if isinstance(value, str) and value not in ("NULL", "") and not name.startswith("geom_"):
            value = semantic(bytes.fromhex(value))
        result[name] = value
    assert result["empty_input"] == "NULL"
    assert result["geom_default"] == "POINT(410 3277)"
    assert result["geom_extent"] == "POINT(10 80)"
    assert result["id_zero"][0][4][0][0:2] == (True, 0)
    assert result["negative_id"][0][4][0][0] is False
    assert result["default"][0][4][0][2:4] == (1, (9, 2, 4))
    assert result["line"][0][4][0][2] == 2
    assert result["polygon"][0][4][0][2] == 3
    for case in ("upper_name", "zero_extent", "missing_geometry", "missing_id"):
        assert isinstance(result[case], dict) and "error" in result[case], case
    return result


def prepare_benchmark(port):
    columns = ",".join("p%d VARCHAR(80)" % i for i in range(8))
    sql(port, "DROP TABLE IF EXISTS mvt_codec_contracts.bench; CREATE TABLE mvt_codec_contracts.bench(" + columns + ",geom GEOMETRY)")
    for start in range(0, 1000, 100):
        rows = []
        for i in range(start, start + 100):
            values = ",".join("'property-%d-value-%d'" % (j, i % 10) for j in range(8))
            rows.append("(" + values + ",ST_GeomFromText('MULTIPOINT(1 2,3 4,5 6,7 8,9 10,11 12,13 14,15 16)'))")
        sql(port, "INSERT INTO mvt_codec_contracts.bench VALUES " + ",".join(rows))


def benchmark(ports):
    query = "SELECT LENGTH(_st_asmvt(t.*)) FROM mvt_codec_contracts.bench t;"
    expected = None
    configuration = {}
    for port in ports:
        configuration[str(port)] = {
            name: sql(port, "SHOW PARAMETERS LIKE '" + name + "'").split("\t")[3]
            for name in ("syslog_level", "memory_budget", "cpu_count", "datafile_size", "log_disk_size")
        }
        prepare_benchmark(port)
        tile = semantic(bytes.fromhex(sql(port, query.replace("LENGTH", "HEX"))))
        assert len(tile[0][4]) == 1000
        if expected is None:
            expected = tile
        else:
            assert tile == expected, "benchmark inputs have different decoded SQL results"
        sql(port, query)
    assert all(values == configuration[str(ports[0])] for values in configuration.values()), "benchmark configurations differ"
    samples = {str(port): [] for port in ports}
    for repeat in range(7):
        for port in (ports if repeat % 2 == 0 else list(reversed(ports))):
            start = time.perf_counter()
            results = sql(port, query * 10).splitlines()
            elapsed = time.perf_counter() - start
            assert len(results) == 10 and all(int(value) > 0 for value in results)
            samples[str(port)].append(elapsed / 10)
    return {port: {"configuration": configuration[port], "median_seconds": statistics.median(values),
                   "range_seconds": [min(values), max(values)], "samples_seconds": values}
            for port, values in samples.items()}


def json_default(value):
    if isinstance(value, bytes):
        return {"hex": value.hex()}
    raise TypeError(type(value))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--before-port", type=int, required=True)
    parser.add_argument("--after-port", type=int)
    parser.add_argument("--skip-benchmark", action="store_true")
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    prepare(args.before_port)
    before = contracts(args.before_port)
    result = {"before": before}
    if args.after_port:
        prepare(args.after_port)
        after = contracts(args.after_port)
        assert after == before, "SQL contract mismatch"
        result["after"] = after
        if not args.skip_benchmark:
            result["benchmark"] = benchmark([args.before_port, args.after_port])
    args.output.write_text(json.dumps(result, default=json_default, indent=2) + "\n")
    print("MVT SQL contracts passed for", args.before_port, args.after_port)


if __name__ == "__main__":
    main()
