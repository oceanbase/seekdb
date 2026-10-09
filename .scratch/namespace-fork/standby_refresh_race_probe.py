import json,time
import pymysql

def connect(port):
 return pymysql.connect(host='127.0.0.1',port=port,user='root@sys',database='oceanbase',autocommit=True,read_timeout=6,connect_timeout=2)
def sql(c,text):
 with c.cursor() as q:q.execute(text);return q.fetchall()
with connect(42035) as p, connect(42036) as s:
 for i in range(20):
  name='schema_refresh_race_'+str(time.time_ns())
  sql(p,f'create table replica_ns.{name}(id int primary key,v int)')
  sql(p,f'insert into replica_ns.{name} values(1,10)')
  sql(p,f'truncate table replica_ns.{name}')
  sql(p,f'drop table replica_ns.{name}')
  start=time.monotonic()
  try:
   for attempt in range(100):
    try:
     row=sql(s,'select role,sync_scn from __all_virtual_server_stat');break
    except pymysql.MySQLError as e:
     if e.args[0]!=5627:raise
     time.sleep(.03)
   else:raise AssertionError('publication never settled')
  except Exception as e:
   print(json.dumps({'iteration':i,'error':str(e),'seconds':time.monotonic()-start}),flush=True)
   raise
  print(json.dumps({'iteration':i,'seconds':time.monotonic()-start,'result':row}),flush=True)
  time.sleep(.08)
