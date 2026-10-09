import argparse,concurrent.futures,contextlib,json,os,signal,subprocess,time
from pathlib import Path
import pymysql
p=argparse.ArgumentParser();p.add_argument('--binary',required=True);p.add_argument('--owner',choices=['initial','child'],required=True);p.add_argument('--restart-standby',action='store_true');args=p.parse_args()
root=Path('/data/1/nijia.nj/test/namespace_standby_20261003_v1/work')
primary=root/'db_p.z1.obs0'

def process_pid(base):
 for entry in Path('/proc').iterdir():
  if not entry.name.isdigit():continue
  try:exe=(entry/'exe').resolve()
  except OSError:continue
  if str(exe).startswith(str(base)+'/bin/'):return int(entry.name)
 raise AssertionError('owned instance not running')

def connect(port,ns='sys'):
 return pymysql.connect(host='127.0.0.1',port=port,user='root@'+ns,database='oceanbase',autocommit=True,connect_timeout=2,read_timeout=15)
@contextlib.contextmanager
def managed_connection(port,ns='sys'):
 c=connect(port,ns)
 try:yield c
 finally:
  if c.open:c.close()
def sql(c,q):
 with c.cursor() as cur:cur.execute(q);return cur.fetchall()
def wait(fn,predicate,seconds=30):
 end=time.monotonic()+seconds;last=None
 while time.monotonic()<end:
  try:
   last=fn()
   if predicate(last):return last
  except pymysql.MySQLError as e:last=e.args
  time.sleep(.02)
 raise AssertionError(last)
def roots(c,name):
 rows=sql(c,'select key_json,value_json from __all_virtual_instance_metadata where collection_id=1')
 return next((json.loads(k)['namespace_id'],json.loads(v)) for k,v in rows if json.loads(v)['name']==name)

pid=process_pid(primary);os.kill(pid,signal.SIGTERM)
wait(lambda:Path('/proc/'+str(pid)+'/exe').exists(),lambda v:not v)
replacement=primary/'bin/observer.publication-new'
subprocess.run(['cp','--reflink=auto',str(Path(args.binary).resolve()),str(replacement)],check=True)
os.replace(replacement,primary/'bin/observer')
env=os.environ.copy();env['LD_LIBRARY_PATH']=str(primary/'lib');env.pop('SEEKDB_NAMESPACE_DDL_PUBLISH_DELAY_US',None)
command=[str(primary/'bin/observer'),'--nodaemon','--port','42035','--data-dir',str(primary/'store'),'--redo-dir',str(primary/'store/clog'),'--role=PRIMARY']
for option in ['enable_rpc_service=true','rpc_port=42000','datafile_size=2G','log_disk_size=2G','cpu_count=4','memory_limit=8G']:
 command+=['--parameter',option]
with (primary/'publication-restart.stdout').open('w') as output:
 subprocess.Popen(command,cwd=primary,env=env,stdout=output,stderr=subprocess.STDOUT,start_new_session=True)
wait(lambda:connect(42035),lambda v:v is not None).close()
ns='sys' if args.owner=='initial' else 'replica_seed'
name='ns1' if args.owner=='initial' else 'replica_seed'
with contextlib.ExitStack() as connections:
 writer=connections.enter_context(managed_connection(42035,ns))
 standby=connections.enter_context(managed_connection(42036))
 admin=connections.enter_context(managed_connection(42035))
 sql(writer,'create table replica_ns.publication_failover(id int primary key,v int)')
 sql(writer,'insert into replica_ns.publication_failover values(1,10)')
 wait(lambda:roots(standby,name),lambda v:'active_schema_changes' not in v[1] and 'pending_schema_version' not in v[1])
 def read_before():
  with connect(42036,ns) as c:return sql(c,'select id,v from replica_ns.publication_failover')
 wait(read_before,lambda v:v==((1,10),))
 before_version=roots(standby,name)[1]['schema_version']
 with concurrent.futures.ThreadPoolExecutor(max_workers=1) as pool:
  future=pool.submit(sql,writer,'alter table replica_ns.publication_failover add column extra int default 7')
  # SQL schema and catalog roots now commit in one native transaction. The
  # former pending-publication window is gone; kill after the replica sees
  # the new root and verify its SQL schema is already visible and usable.
  record=wait(lambda:roots(standby,name),lambda v:v[1]['schema_version']>before_version and 'active_schema_changes' not in v[1] and 'pending_schema_version' not in v[1])
  with connect(42036,ns) as replica_reader:
   assert sql(replica_reader,'select id,v,extra from replica_ns.publication_failover')==((1,10,7),)
  os.kill(process_pid(primary),signal.SIGKILL)
  try:future.result(timeout=10)
  except pymysql.MySQLError:pass
  print(json.dumps(dict(case='primary_lost_after_atomic_schema_source_publication',owner=args.owner,namespace_id=record[0],state=record[1])),flush=True)
 if args.restart_standby:
  standby.close()
  subprocess.run(['python3','.scratch/namespace-fork/standby_restart_probe.py','pending-publication-'+args.owner,'--binary',args.binary],check=True)
  standby=connections.enter_context(managed_connection(42036))
 sql(standby,'ALTER SYSTEM ACTIVATE STANDBY')
 with connect(42036,ns) as promoted:
  assert sql(promoted,'select id,v,extra from replica_ns.publication_failover')==((1,10,7),)
  sql(promoted,'insert into replica_ns.publication_failover values(2,20,8)')
  assert sql(promoted,'select id,v,extra from replica_ns.publication_failover order by id')==((1,10,7),(2,20,8))
 state=roots(standby,name)[1]
 assert 'active_schema_changes' not in state and 'pending_schema_version' not in state,state
 sql(standby,'FORK NAMESPACE recovered_publication FROM '+name)
 with connect(42036,'recovered_publication') as c:
  assert sql(c,'select id,v,extra from replica_ns.publication_failover order by id')==((1,10,7),(2,20,8))
 print(json.dumps(dict(case='publication_recovered_after_failover',owner=args.owner,restarted=args.restart_standby)),flush=True)
