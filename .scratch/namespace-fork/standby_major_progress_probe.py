"""Local assertion for the primary major-compaction prerequisite of standby copy."""
import argparse, json, time
import pymysql

p = argparse.ArgumentParser()
p.add_argument('--trigger', action='store_true')
p.add_argument('--timeout', type=float, default=10)
a = p.parse_args()
with pymysql.connect(host='127.0.0.1', port=42035, user='root@sys',
                     database='oceanbase', autocommit=True) as c:
    with c.cursor() as cur:
        if a.trigger:
            cur.execute('ALTER SYSTEM MAJOR FREEZE')
        deadline = time.monotonic() + a.timeout
        while time.monotonic() < deadline:
            cur.execute('select FROZEN_SCN,GLOBAL_BROADCAST_SCN,LAST_SCN,STATUS from DBA_OB_MAJOR_COMPACTION')
            rows = cur.fetchall()
            if rows and rows[0][0] > 1 and rows[0][0] == rows[0][1] == rows[0][2]:
                print(json.dumps(dict(case='primary_major_compaction', status='PASS', rows=rows)))
                break
            time.sleep(.2)
        else:
            cur.execute('select tablet_id,finished_scn,max_received_scn from __all_virtual_tablet_compaction_info order by tablet_id limit 6')
            print(json.dumps(dict(case='primary_major_compaction', status='FAIL', rows=rows, tablets=cur.fetchall())))
            raise SystemExit(1)
