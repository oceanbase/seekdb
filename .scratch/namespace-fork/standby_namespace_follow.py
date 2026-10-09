import argparse, contextlib, json, time
from pathlib import Path
import pymysql

p=argparse.ArgumentParser(); p.add_argument('--initial-only',action='store_true'); args=p.parse_args()

def connect(port,ns='sys'):
 return pymysql.connect(host='127.0.0.1',port=port,user='root@'+ns,database='oceanbase',autocommit=True,connect_timeout=2,read_timeout=25,write_timeout=25)

def sql(c,text):
 with c.cursor() as q: q.execute(text); return q.fetchall()

def record(case,**facts):
 print(json.dumps(dict(case=case,**facts),ensure_ascii=False),flush=True)

def wait(fn,expected,timeout=40):
 end=time.monotonic()+timeout; last=None
 while time.monotonic()<end:
  try:
   last=fn()
   if last==expected:return last
  except pymysql.MySQLError as e:last=e.args
  time.sleep(.2)
 raise AssertionError((expected,last))

def sync(primary,standby):
 target=int(sql(primary,'select sync_scn from __all_virtual_server_stat')[0][0])
 wait(lambda:int(sql(standby,'select sync_scn from __all_virtual_server_stat')[0][0])>=target,True)

def read_ns(ns,query):
 with connect(42036,ns) as c:return sql(c,query)

with connect(42035) as root,connect(42036) as standby:
 expected=((1,10,16384),)
 query='select id,v,length(note) from replica_ns.t order by id'
 wait(lambda:read_ns('replica_seed',query),expected)
 names=sql(root,'select key_json,value_json from __all_virtual_instance_metadata where collection_id=1')
 nsid=next(int(json.loads(k)['namespace_id']) for k,v in names if json.loads(v)['name']=='replica_seed')
 with connect(42035,'replica_seed') as child:
  logical=int(sql(child,"select tablet_id from __all_table where table_name='t' and database_id=(select database_id from __all_database where database_name='replica_ns')")[0][0])
 physical=(1<<62)|(nsid<<37)|logical
 # The primary now materializes cold namespaces in the background. A copied
 # physical tablet is legitimate; standby reads must not create one absent on
 # the primary.
 assert sql(standby,f'select count(*) from __all_virtual_tablet_info where tablet_id={physical}')[0][0] <= sql(root,f'select count(*) from __all_virtual_tablet_info where tablet_id={physical}')[0][0]
 with connect(42036,'replica_seed') as c:
  try: sql(c,"insert into replica_ns.t values(100,100,'forbidden')")
  except pymysql.MySQLError as e: assert e.args[0]==4688,e.args
  else: raise AssertionError('standby child accepted a write')
 assert sql(standby,f'select count(*) from __all_virtual_tablet_info where tablet_id={physical}')[0][0] <= sql(root,f'select count(*) from __all_virtual_tablet_info where tablet_id={physical}')[0][0]
 record('initial_restore_fork_read_and_write_rejection',namespace_id=nsid,logical_tablet=logical)
 if args.initial_only:raise SystemExit(0)
 sql(root,"insert into replica_ns.t values(2,20,'parent later')")
 sql(root,'FORK NAMESPACE replica_cold FROM ns1')
 sync(root,standby)
 wait(lambda:read_ns('replica_cold',query),((1,10,16384),(2,20,12)))
 record('dynamic_namespace_discovery')
 with connect(42035,'replica_seed') as child:
  sql(child,"insert into replica_ns.t values(3,30,'child value')")
  sync(root,standby)
  wait(lambda:read_ns('replica_seed',query),((1,10,16384),(3,30,11)))
  wait(lambda:sql(standby,f'select count(*) from __all_virtual_tablet_info where tablet_id={physical}'),((1,),))
  record('materialization_transaction_replay_with_lob')
  sql(child,'alter table replica_ns.t add column extra int default 7')
  sync(root,standby)
  wait(lambda:read_ns('replica_seed','select id,extra from replica_ns.t order by id'),((1,7),(3,7)))
  record('child_ddl_refresh')
 sql(root,'truncate table replica_ns.t')
 sql(root,"insert into replica_ns.t values(9,90,'new parent')")
 sql(root,'drop table replica_ns.t')
 sync(root,standby)
 wait(lambda:read_ns('replica_cold',query),((1,10,16384),(2,20,12)))
 wait(lambda:read_ns('replica_seed',query),((1,10,16384),(3,30,11)))
 record('historical_source_after_parent_truncate_and_drop')
 sql(root,'FORK NAMESPACE replica_drop FROM replica_seed')
 sync(root,standby)
 wait(lambda:read_ns('replica_drop',query),((1,10,16384),(3,30,11)))
 sql(root,'DROP NAMESPACE replica_drop')
 sync(root,standby)
 def absent():
  try:
   with connect(42036,'replica_drop'):return False
  except pymysql.MySQLError as e:return e.args[0]==1049
 wait(absent,True);record('replicated_namespace_drop_admission')
 sql(root,'ALTER SYSTEM SWITCHOVER TO STANDBY')
 sql(standby,'ALTER SYSTEM SWITCHOVER TO PRIMARY')
 with connect(42036,'replica_seed') as child:
  sql(child,"insert into replica_ns.t values(4,40,'promoted child',8)")
  assert sql(child,'select id,v,extra from replica_ns.t order by id')==((1,10,7),(3,30,7),(4,40,8))
 sql(standby,'FORK NAMESPACE replica_promoted FROM replica_seed')
 with connect(42036,'replica_cold') as cold:
  sql(cold,"insert into replica_ns.t values(5,50,'promoted cold')")
  assert sql(cold,'select id,v from replica_ns.t order by id')==((1,10),(2,20),(5,50))
 record('promotion_lazy_schema_recovery_and_writes')
 with connect(42036,'replica_promoted') as c:
  assert sql(c,'select id,v from replica_ns.t order by id')==((1,10),(3,30),(4,40))
 record('fork_after_promotion')
