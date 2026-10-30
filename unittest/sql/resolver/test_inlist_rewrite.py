#!/usr/bin/env python3
# Copyright (c) 2025 OceanBase.
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
# http://www.apache.org/licenses/LICENSE-2.0
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
"""Live SQL regressions for #1512.

Run against a freshly built server, for example:
  python3 unittest/sql/resolver/test_inlist_rewrite.py -- mysql --host=127.0.0.1 -P2881 -uroot
Or pass: -- docker exec -i seekdb-competition mysql --host=127.0.0.1 -P2881 -uroot
The client must accept SQL on stdin. Only the database named below is modified.
"""

import random
import subprocess
import sys
import unittest


CLIENT = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
sys.argv = sys.argv[:1]
DATABASE = "regression_issue_1512"


def sql(statement):
    result = subprocess.run(
        CLIENT + ["--batch", "--raw", "--skip-column-names"],
        input=f"USE {DATABASE};\n{statement}\n", text=True,
        stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=True,
    )
    return result.stdout.strip()


class InListRewrite(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if not CLIENT:
            raise unittest.SkipTest("Pass a mysql-compatible client after --")
        subprocess.run(CLIENT, input=f"CREATE DATABASE IF NOT EXISTS {DATABASE};",
                       text=True, check=True, stdout=subprocess.PIPE)
        sql("CREATE TABLE IF NOT EXISTS members (id INT PRIMARY KEY, v BIGINT);"
            "REPLACE INTO members VALUES (1,1),(2,2),(3,3),(4,4),(5,NULL),"
            "(6,-9223372036854775808),(7,9223372036854775807);")

    def query(self, values, op="IN", threshold=8):
        return (f"SELECT /*+ OPT_PARAM('inlist_rewrite_threshold', {threshold}) */ "
                f"id FROM members WHERE v {op} ({values}) ORDER BY id")

    def test_duplicate_membership_remains_inline(self):
        values = [1, 2, 3] * 1024
        random.Random(1024).shuffle(values)
        query = self.query(",".join(map(str, values)))
        self.assertEqual("1\n2\n3", sql(query))
        self.assertNotIn("VALUES TABLE ACCESS", sql("EXPLAIN " + query))

    def test_null_and_not_in(self):
        values = "1,2,NULL,1,2,NULL,1,2,NULL"
        for op, expected in (("IN", "1\n2"), ("NOT IN", "")):
            with self.subTest(op=op):
                query = self.query(values, op)
                self.assertEqual(expected, sql(query))
                self.assertNotIn("VALUES TABLE ACCESS", sql("EXPLAIN " + query))
        self.assertEqual("3\n4\n6\n7", sql(self.query("1,2,1,2,1,2,1,2", "NOT IN")))

    def test_threshold_boundary(self):
        below = self.query("1,2,3,4,5,6,7,1")
        at = self.query("1,2,3,4,5,6,7,8")
        self.assertNotIn("VALUES TABLE ACCESS", sql("EXPLAIN " + below))
        self.assertIn("VALUES TABLE ACCESS", sql("EXPLAIN " + at))

    def test_integer_limits(self):
        values = ",".join(["-9223372036854775808", "9223372036854775807"] * 5)
        self.assertEqual("6\n7", sql(self.query(values)))

    def test_non_integer_and_row_lists(self):
        # These inputs retain their existing coercion and row comparison rules.
        self.assertEqual("1\n2", sql(self.query("'1','2','1','2','1','2','1','2'")))
        self.assertEqual("1\n2", sql(
            "SELECT /*+ OPT_PARAM('inlist_rewrite_threshold', 8) */ id FROM members "
            "WHERE (id,v) IN ((1,1),(2,2),(1,1),(2,2),(1,1),(2,2),(1,1),(2,2)) ORDER BY id"))

    def test_prepared_statement_rebind(self):
        output = sql(
            "PREPARE p FROM \"SELECT /*+ OPT_PARAM('inlist_rewrite_threshold', 8) */ "
            "id FROM members WHERE v IN (?,?,?,?,?,?,?,?) ORDER BY id\";"
            "SET @a=1,@b=2,@c=3,@d=4;"
            "EXECUTE p USING @a,@a,@a,@a,@a,@a,@a,@a;"
            "EXECUTE p USING @b,@b,@c,@c,@d,@d,@b,@c;"
            "DEALLOCATE PREPARE p;")
        self.assertEqual("1\n2\n3\n4", output)

    def test_vector_membership(self):
        sql("CREATE TABLE IF NOT EXISTS vectors (id INT PRIMARY KEY, v INT, "
            "embedding VECTOR(3), VECTOR INDEX vi(embedding) "
            "WITH (distance=l2, type=hnsw, lib=vsag)) ORGANIZATION HEAP;")
        rows = ",".join(f"({i},{i % 5},'[{i},0,1]')" for i in range(1, 21))
        sql("REPLACE INTO vectors VALUES " + rows)
        values = ",".join(map(str, [1, 2, 3] * 1024))
        query = ("SELECT /*+ OPT_PARAM('inlist_rewrite_threshold', 8) */ id "
                 f"FROM vectors WHERE v IN ({values}) "
                 "ORDER BY l2_distance(embedding, '[0,0,1]') APPROXIMATE LIMIT 3")
        self.assertEqual("1\n2\n3", sql(query))
        self.assertNotIn("VALUES TABLE ACCESS", sql("EXPLAIN " + query))


if __name__ == "__main__":
    unittest.main(verbosity=2)
