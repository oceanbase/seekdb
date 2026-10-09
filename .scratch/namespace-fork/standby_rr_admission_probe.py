#!/usr/bin/env python3
"""Restart the isolated pair and exercise pure reads without catalog GC."""
import os,subprocess,time,json,signal
from pathlib import Path
import pymysql
base=Path('/data/1/nijia.nj/test/namespace_standby_20261003_v1/work')
children=[]
try:
 for name,port,rpc,role in [('db_p',42035,42000,'PRIMARY'),('db_s',42036,42001,'STANDBY')]:
  root=base/(name+'.z1.obs0');env=os.environ.copy();env['LD_LIBRARY_PATH']=str(root/'lib')
  cmd=[str(root/'bin/observer'),'--nodaemon','--port',str(port),'--data-dir',str(root/'store'),'--redo-dir',str(root/'store/clog'),'--role='+role]
  for option in ['enable_rpc_service=true','rpc_port='+str(rpc),'datafile_size=2G','log_disk_size=2G','cpu_count=4','memory_limit=8G']:cmd+=['--parameter',option]
  with (root/'rr-admission.stdout').open('w') as out:children.append(subprocess.Popen(cmd,cwd=root,env=env,stdout=out,stderr=subprocess.STDOUT))
 def connect():return pymysql.connect(host='127.0.0.1',port=42036,user='root@sys',database='replica_ns',autocommit=True,read_timeout=5,connect_timeout=1)
 end=time.monotonic()+30
 while True:
  try:c=connect();break
  except pymysql.MySQLError:
   if time.monotonic()>end:raise
   time.sleep(.2)
 with c:
  def sql(q):
   with c.cursor() as cur:cur.execute(q);return cur.fetchall()
  sql('set ob_query_timeout=1000000')
  print(json.dumps({'case':'rc_select','rows':sql('select * from view_late')}),flush=True)
  sql('set session transaction isolation level repeatable read')
  sql('begin')
  print(json.dumps({'case':'explicit_rr_select','rows':sql('select * from view_late')}),flush=True)
  sql('commit')
  print(json.dumps({'case':'autocommit_rr_select','rows':sql('select * from view_late')}),flush=True)
  sql('set session transaction isolation level serializable')
  print(json.dumps({'case':'autocommit_serializable_select','rows':sql('select * from view_late')}),flush=True)
finally:
 for child in children:child.terminate()
 for child in children:
  try:child.wait(timeout=3)
  except subprocess.TimeoutExpired:child.kill();child.wait()
