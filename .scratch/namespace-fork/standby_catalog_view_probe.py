#!/usr/bin/env python3
"""Replica RR late open after primary logically deletes the selected root page."""
import json,os,time,signal,subprocess
from pathlib import Path
import pymysql

def connect(port):
 return pymysql.connect(host='127.0.0.1',port=port,user='root@sys',database='oceanbase',autocommit=True,connect_timeout=2,read_timeout=20)
def sql(c,q):
 with c.cursor() as cur:cur.execute(q);return cur.fetchall()
def wait(fn,predicate,seconds=40):
 end=time.monotonic()+seconds;last=None
 while time.monotonic()<end:
  try:
   last=fn()
   if predicate(last):return last
  except pymysql.MySQLError as error:last=error.args
  time.sleep(.1)
 raise AssertionError(last)
def root(c):
 return next(json.loads(v) for k,v in sql(c,'select key_json,value_json from __all_virtual_instance_metadata where collection_id=1') if int(json.loads(k)['namespace_id'])==1)
def has_page(c,page):
 return any(int(json.loads(k)['page_id'])==page for k, in sql(c,'select key_json from __all_virtual_instance_metadata where collection_id=5'))
def sync(primary,replica):
 target=int(sql(primary,'select sync_scn from __all_virtual_server_stat')[0][0])
 wait(lambda:int(sql(replica,'select sync_scn from __all_virtual_server_stat')[0][0]),lambda v:v>=target)
trigger=Path(os.environ['SEEKDB_CATALOG_GC_TRIGGER'])
primary_base=Path('/data/1/nijia.nj/test/namespace_standby_20261003_v1/work/db_p.z1.obs0')
owned=[]
for process in Path('/proc').iterdir():
 if not process.name.isdigit():continue
 try:exe=(process/'exe').resolve()
 except OSError:continue
 if str(exe).startswith(str(primary_base)+'/bin/'):owned.append(int(process.name))
assert len(owned)==1,owned
os.kill(owned[0],signal.SIGTERM)
wait(lambda:Path('/proc/'+str(owned[0])+'/exe').exists(),lambda v:not v)
env=os.environ.copy();env['LD_LIBRARY_PATH']=str(primary_base/'lib')
command=[str(primary_base/'bin/observer'),'--nodaemon','--port','42035','--data-dir',str(primary_base/'store'),'--redo-dir',str(primary_base/'store/clog'),'--role=PRIMARY']
for option in ['enable_rpc_service=true','rpc_port=42000','datafile_size=2G','log_disk_size=2G','cpu_count=4','memory_limit=8G']:
 command+=['--parameter',option]
with (primary_base/'catalog-view-restart.stdout').open('w') as output:
 subprocess.Popen(command,cwd=primary_base,env=env,stdout=output,stderr=subprocess.STDOUT,start_new_session=True)
wait(lambda:connect(42035),lambda v:v is not None).close()
with connect(42035) as primary,connect(42036) as replica,connect(42036) as reader:
 sql(primary,'create table replica_ns.view_anchor(id int primary key)')
 sql(primary,'insert into replica_ns.view_anchor values(1)')
 sql(primary,'create table replica_ns.view_late(id int primary key,v int)')
 sql(primary,'insert into replica_ns.view_late values(1,40)')
 sync(primary,replica)
 wait(lambda:sql(replica,'select id,v from replica_ns.view_late'),lambda v:v==((1,40),))
 selected=int(root(replica)['directory_page'])
 sql(reader,'set session transaction isolation level repeatable read')
 sql(reader,'begin')
 assert sql(reader,'select * from replica_ns.view_anchor')==((1,),)
 sql(primary,'update replica_ns.view_late set v=60')
 sql(primary,'create table replica_ns.view_churn(id int primary key)')
 assert int(root(primary)['directory_page'])!=selected
 for attempt in range(30):
  if not has_page(primary,selected):break
  trigger.write_text('collect\n')
  wait(lambda:trigger.exists(),lambda v:not v,seconds=10)
  time.sleep(.2)
 else:raise AssertionError(('old root page never collected',selected))
 sync(primary,replica)
 assert not has_page(replica,selected),selected
 print(json.dumps({'case':'old_root_delete_replayed','page':selected}),flush=True)
 # This is the first scan of this table in the old transaction. It must read
 # its immutable root at S, not the latest row after primary page GC.
 assert sql(reader,'select id,v from replica_ns.view_late')==((1,40),)
 sql(reader,'commit')
 assert sql(reader,'select id,v from replica_ns.view_late')==((1,60),)
 sql(reader,'set session transaction isolation level serializable')
 assert sql(reader,'select id,v from replica_ns.view_late')==((1,60),)
 print(json.dumps({'case':'replica_rr_late_open_after_page_gc','passed':True}),flush=True)
trigger.unlink(missing_ok=True)
