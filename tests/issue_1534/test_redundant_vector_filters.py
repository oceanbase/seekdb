
import argparse
import re
import pymysql

parser = argparse.ArgumentParser()
parser.add_argument("--mode", choices=["before", "after"], required=True)
args = parser.parse_args()

conn = pymysql.connect(
    host="127.0.0.1",
    port=2881,
    user="root",
    database="bug1471",
    autocommit=True,
)

conditions = {
    "single": "age < 10",
    "duplicate": "age < 10 AND age < 10",
    "redundant_2": "age < 10 AND age < 11",
    "redundant_129": " AND ".join(
        f"age < {n}" for n in range(10, 139)
    ),
    "different_columns": "age < 10 AND category5 = 1",
    "mixed_bounds": "age < 10 AND age > 5",
}

try:
    with conn.cursor() as cur:
        cur.execute("SELECT embedding FROM vector_test LIMIT 1")
        vector = cur.fetchone()[0]
        if isinstance(vector, bytes):
            vector = vector.decode()

        # Reference result without redundant predicates.
        baseline_sql = """
            SELECT id
            FROM vector_test
            WHERE age < 10
            ORDER BY cosine_distance(embedding, %s) APPROXIMATE
            LIMIT 10
        """
        cur.execute(baseline_sql, (vector,))
        baseline_ids = [row[0] for row in cur.fetchall()]

        for name, condition in conditions.items():
            sql = f"""
                SELECT id
                FROM vector_test
                WHERE {condition}
                ORDER BY cosine_distance(embedding, %s) APPROXIMATE
                LIMIT 10
            """

            cur.execute("EXPLAIN " + sql, (vector,))
            plan = "\n".join(str(row[0]) for row in cur.fetchall())

            # Count age upper-bound predicates in the generated plan.
            age_bounds = re.findall(
                r"vector_test\.age\s*<\s*(\d+)",
                plan,
                flags=re.IGNORECASE,
            )

            cur.execute(sql, (vector,))
            ids = [row[0] for row in cur.fetchall()]

            if name in {
                "single", "duplicate",
                "redundant_2", "redundant_129"
            }:
                assert ids == baseline_ids, (
                    f"{name}: query results differ from baseline"
                )

            print(
                name,
                "upper_bound_count =", len(age_bounds),
                "bounds =", age_bounds[:5],
                "rows =", len(ids),
            )

            if args.mode == "before":
                expected = {
                    "single": 1,
                    "duplicate": 2,
                    "redundant_2": 2,
                    "redundant_129": 129,
                    "different_columns": 1,
                    "mixed_bounds": 1,
                }[name]
            else:
                expected = 1

            assert len(age_bounds) == expected, (
                f"{name}: expected {expected}, "
                f"got {len(age_bounds)}"
            )

        print(f"\nPASS: mode={args.mode}")

finally:
    conn.close()
