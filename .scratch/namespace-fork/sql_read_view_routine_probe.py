#!/usr/bin/env python3
"""Regression for schema-refresh reads after child routine publication."""
import argparse,resource,os
from fork_parent_truncate_probe import BootstrapExperiment,connect
p=argparse.ArgumentParser();p.add_argument('--binary',required=True);p.add_argument('--instrumented',action='store_true');a=p.parse_args()
if a.instrumented: os.environ['SEEKDB_SQL_VIEW_PROBE']='1'
resource.setrlimit(resource.RLIMIT_CORE,(0,0))
e=BootstrapExperiment(a.binary,'sql_view_routine',prototype=6)
try:
 e.start();e.sql('CREATE DATABASE routines');e.sql('CREATE TABLE routines.t(id INT PRIMARY KEY)');e.sql('INSERT INTO routines.t VALUES(1)');e.sql('FORK NAMESPACE routine_child FROM ns1')
 with connect(e,'root@routine_child') as c:
  e.sql('SET ob_query_timeout=10000000',c)
  e.sql('CREATE PROCEDURE routines.p() BEGIN SELECT SUM(id) FROM routines.t; SELECT COUNT(*) FROM routines.t; END',c)
  with c.cursor() as cur:
   cur.execute('CALL routines.p()');assert cur.fetchall()==((1,),);assert cur.nextset();assert cur.fetchall()==((1,),)
   while cur.nextset(): pass
  e.record('PASS',case='child_routine_schema_refresh_nested_reads')
finally:e.close()
