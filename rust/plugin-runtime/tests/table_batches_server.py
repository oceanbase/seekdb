#!/usr/bin/env python3
"""Opt-in client regression for installed rust_text; never a standalone CTest pass.

Only SELECT statements are sent to an existing disposable loopback database.
No package installation, DDL, data writes or server-setting changes.
"""

import argparse
import os

import pymysql


def query(connection, statement, arguments=()):
    with connection.cursor() as cursor:
        cursor.execute(statement, arguments)
        return cursor.fetchall()


def run(options):
    with pymysql.connect(
        host="127.0.0.1", port=options.port, user=options.user,
        password=os.environ.get("SEEKDB_TEST_PASSWORD", ""), database=options.database,
        charset="utf8mb4", autocommit=True, connect_timeout=5,
        read_timeout=60, write_timeout=60,
    ) as connection:
        tokens = [f"word{i}中🙂" for i in range(3001)]
        argument = " \t ".join(tokens)
        source = "FROM TABLE(seekdb_rust_words_bytes(%s))"
        # These execute the complete SQL plan, including production codegen,
        # aggregation and filtering, unlike the kernel's manually built spec.
        assert query(connection, "SELECT COUNT(*) " + source, (argument,)) == ((len(tokens),),)
        assert query(connection, "SELECT ordinal " + source + " ORDER BY ordinal", (argument,)) == tuple(
            (i + 1,) for i in range(len(tokens))
        )
        assert query(connection, "SELECT ordinal, seekdb_rust_char_count(token) " + source +
                     " WHERE MOD(ordinal, 7)=0 ORDER BY ordinal", (argument,)) == tuple(
            (i + 1, len(token)) for i, token in enumerate(tokens) if (i + 1) % 7 == 0
        )
        rows = query(connection, "SELECT token " + source + " ORDER BY ordinal LIMIT 5", (argument,))
        actual = [row[0].decode("utf-8") if isinstance(row[0], bytes) else row[0] for row in rows]
        assert actual == tokens[:5]
        for empty in ("", " \t\n", None):
            assert query(connection, "SELECT COUNT(*) " + source, (empty,)) == ((0,),)
        assert query(connection, "SELECT ordinal FROM TABLE(seekdb_rust_words_or_null(NULL))") == ((1,),)
        assert query(connection, "SELECT COUNT(*) FROM TABLE(seekdb_rust_words_strict(NULL))") == ((0,),)
        assert query(connection,
                     "SELECT ordinal FROM TABLE(seekdb_rust_sql_series(%s)) ORDER BY ordinal",
                     ("A中🙂",)) == ((1,), (2,), (3,))
        assert query(connection, "SELECT COLUMN_VALUE FROM TABLE(generator(3)) ORDER BY COLUMN_VALUE") == ((1,), (2,), (3,))
        assert query(connection, "SELECT COLUMN_VALUE FROM TABLE(generator(0))") == ()
        # Re-execute after early termination, exercising cursor cleanup and
        # prepared/cached expression reuse without relying on output row order.
        for _ in range(2):
            limited = query(connection, "SELECT ordinal " + source + " LIMIT 1", (argument,))
            assert len(limited) == 1 and 1 <= limited[0][0] <= len(tokens)
            assert query(connection, "SELECT COUNT(*) " + source, (argument,)) == ((len(tokens),),)
        print("PASS: client table projections, counts, filters, typed consumer, NULL, SQL series, repeated reads")
        print("This checks result correctness; it does not prove native batch size or measure performance.")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", type=int, required=True)
    parser.add_argument("--user", default="root")
    parser.add_argument("--database", required=True)
    parser.add_argument("--confirm-disposable-server", action="store_true", required=True)
    options = parser.parse_args()
    if not 1 <= options.port <= 65535:
        parser.error("port must be between 1 and 65535")
    run(options)


if __name__ == "__main__":
    main()
