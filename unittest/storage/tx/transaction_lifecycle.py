"""Native SQL transaction/recovery checks (requires PyMySQL and a running server).

Use an isolated instance: this creates and resets the tx_trace_native database's
test tables. Run normally, then --prepare-recovery, restart/kill the server from
another shell, and run --verify-recovery. Connection settings are TX_TEST_HOST,
TX_TEST_PORT, TX_TEST_USER and TX_TEST_PASSWORD. No credentials are printed.
"""
import argparse
from concurrent.futures import ThreadPoolExecutor
import json
import os
import time

import pymysql


def connect():
    return pymysql.connect(host=os.getenv("TX_TEST_HOST", "127.0.0.1"),
                           port=int(os.getenv("TX_TEST_PORT", "2881")),
                           user=os.getenv("TX_TEST_USER", "root"),
                           password=os.getenv("TX_TEST_PASSWORD", ""),
                           autocommit=True, charset="utf8mb4")


def execute(db, sql, args=None):
    with db.cursor() as cursor:
        cursor.execute(sql, args)
        return cursor.fetchall()


def main():
    parser = argparse.ArgumentParser()
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--prepare-recovery", action="store_true")
    mode.add_argument("--verify-recovery", action="store_true")
    args = parser.parse_args()
    with connect() as db:
        execute(db, "CREATE DATABASE IF NOT EXISTS tx_trace_native")
        execute(db, "USE tx_trace_native")
        if args.verify_recovery:
            assert execute(db, "SELECT count(*),sum(v) FROM recovery") == ((256, 32640),)
            print("PASS: recovered committed rows; interrupted transaction left no rows", flush=True)
            return
        if args.prepare_recovery:
            execute(db, "DROP TABLE IF EXISTS recovery")
            execute(db, "CREATE TABLE recovery(id INT PRIMARY KEY,v BIGINT,payload VARCHAR(1024))")
            db.begin()
            for i in range(256):
                execute(db, "INSERT INTO recovery VALUES(%s,%s,%s)", (i, i, "committed" * 100))
            db.commit()
            db.begin()
            for i in range(256, 512):
                execute(db, "INSERT INTO recovery VALUES(%s,%s,%s)", (i, i, "uncommitted" * 80))
            execute(db, "UPDATE recovery SET v=-1 WHERE id<128")
            print("READY: 256 committed rows and 256 uncommitted rows; interrupt the server now", flush=True)
            # Keep the write transaction live until the external restart kills
            # its connection, so this is recovery rather than disconnect rollback.
            while True:
                time.sleep(1)
                try:
                    execute(db, "SELECT 1")
                except pymysql.Error:
                    print("Observed server connection loss", flush=True)
                    return
        for table in ["accounts", "ledger"]:
            execute(db, "DROP TABLE IF EXISTS " + table)
        execute(db, "CREATE TABLE accounts(id INT PRIMARY KEY,amount BIGINT)")
        execute(db, "CREATE TABLE ledger(id INT PRIMARY KEY,amount INT)")
        execute(db, "INSERT INTO accounts VALUES(1,100),(2,100)")
        checks = []
        with connect() as observer:
            execute(observer, "USE tx_trace_native")
            db.begin()
            execute(db, "UPDATE accounts SET amount=amount-10 WHERE id=1")
            execute(db, "UPDATE accounts SET amount=amount+10 WHERE id=2")
            assert execute(observer, "SELECT amount FROM accounts ORDER BY id") == ((100,), (100,))
            db.commit()
            assert execute(observer, "SELECT amount FROM accounts ORDER BY id") == ((90,), (110,))
            checks.append("commit visibility and no dirty reads")
            db.begin()
            execute(db, "UPDATE accounts SET amount=amount+5 WHERE id=1")
            execute(db, "SAVEPOINT retained")
            execute(db, "UPDATE accounts SET amount=amount+7 WHERE id=1")
            execute(db, "ROLLBACK TO SAVEPOINT retained")
            execute(db, "RELEASE SAVEPOINT retained")
            db.commit()
            assert execute(observer, "SELECT amount FROM accounts ORDER BY id") == ((95,), (110,))
            checks.append("savepoint rollback and release")
            db.begin()
            execute(db, "UPDATE accounts SET amount=999 WHERE id=1")
            db.rollback()
            assert execute(observer, "SELECT amount FROM accounts WHERE id=1") == ((95,),)
            checks.append("explicit rollback")
            db.begin()
            execute(db, "INSERT INTO ledger VALUES(9999,1)")
            try:
                execute(db, "INSERT INTO ledger VALUES(9999,2)")
            except pymysql.IntegrityError as error:
                assert error.args[0] == 1062
            else:
                raise AssertionError("duplicate key did not fail")
            db.commit()
            assert execute(observer, "SELECT amount FROM ledger WHERE id=9999") == ((1,),)
            execute(db, "DELETE FROM ledger WHERE id=9999")
            checks.append("failed statement preserves earlier transaction writes")
        abandoned = connect()
        execute(abandoned, "USE tx_trace_native")
        abandoned.begin()
        execute(abandoned, "UPDATE accounts SET amount=888 WHERE id=1")
        abandoned.close()
        assert execute(db, "SELECT amount FROM accounts WHERE id=1") == ((95,),)
        checks.append("disconnect rolls back active transaction")

        def transfers(worker):
            with connect() as client:
                execute(client, "USE tx_trace_native")
                for index in range(50):
                    client.begin()
                    execute(client, "UPDATE accounts SET amount=amount-1 WHERE id=1")
                    execute(client, "UPDATE accounts SET amount=amount+1 WHERE id=2")
                    execute(client, "INSERT INTO ledger VALUES(%s,1)", (worker * 50 + index,))
                    if index % 5 == 0:
                        client.rollback()
                    else:
                        client.commit()
        with ThreadPoolExecutor(max_workers=4) as workers:
            list(workers.map(transfers, range(4)))
        assert execute(db, "SELECT amount FROM accounts ORDER BY id") == ((-65,), (270,))
        assert execute(db, "SELECT count(*),sum(amount) FROM ledger") == ((160, 160),)
        checks.append("200 concurrent transactions: 160 commits, 40 rollbacks, exact balances")
        print(json.dumps({"passed": len(checks), "checks": checks}), flush=True)


if __name__ == "__main__":
    main()
