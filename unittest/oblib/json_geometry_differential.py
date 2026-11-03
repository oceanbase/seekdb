#!/usr/bin/env python3
"""Compare GeoJSON values, binary sizes, and reads on two local instances."""

import argparse
import collections
import itertools
import json

import pymysql
from json_domain_differential import execute


GEOMETRIES = [
    "POINT(1 2)", "POINT(1.23456789 -2.3456789)",
    "LINESTRING(1 2,3.456789 4.56789)", "MULTIPOINT(1 2,3 4)",
    "MULTILINESTRING((1 2,3 4),(5 6,7 8))",
    "POLYGON((0 0,0 1,1 1,1 0,0 0))",
    "POLYGON((0 0,0 10,10 10,10 0,0 0),(2 2,3 2,3 3,2 3,2 2))",
    "MULTIPOLYGON(((0 0,0 1,1 1,1 0,0 0)),((2 2,2 3,3 3,3 2,2 2)))",
    "GEOMETRYCOLLECTION(POINT(1 2),LINESTRING(3 4,5 6))",
    "GEOMETRYCOLLECTION(GEOMETRYCOLLECTION(POINT(1 2)),MULTIPOINT(3 4,5 6))",
] + [kind + " EMPTY" for kind in [
    "POINT", "LINESTRING", "POLYGON", "MULTIPOINT", "MULTILINESTRING", "MULTIPOLYGON", "GEOMETRYCOLLECTION",
]]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--baseline-port", type=int, required=True)
    parser.add_argument("--candidate-port", type=int, required=True)
    args = parser.parse_args()
    connections = [pymysql.connect(host="127.0.0.1", port=port, user="root", database="test", autocommit=True)
                   for port in [args.baseline_port, args.candidate_port]]
    sql = ("select j,json_storage_size(j),json_depth(j),json_extract(j,'$.coordinates[0]') "
           "from (select st_asgeojson(st_geomfromtext(%s,%s),%s,%s) as j) as geo_values")
    counts = collections.Counter()
    failures = []
    try:
        for arguments in itertools.product(GEOMETRIES, [0, 4326, 3857], [0, 1, 2, 5, 17], range(8)):
            before, after = [execute(connection, sql, arguments) for connection in connections]
            counts["runs"] += 1
            counts[after[0]] += 1
            if before != after:
                counts["differences"] += 1
                if len(failures) < 30:
                    failures.append(dict(arguments=arguments, baseline=before, candidate=after))
    finally:
        for connection in connections:
            connection.close()
    print(json.dumps(dict(counts=counts, failures=failures), ensure_ascii=False))
    return int(bool(failures))


if __name__ == "__main__":
    raise SystemExit(main())
