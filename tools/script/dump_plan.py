#!/usr/bin/env python
# -*- coding: utf-8 -*-
import sys
import traceback
import getopt
import mysql.connector
from mysql.connector import errorcode

def to_str(re):
    return '({0}, \'{1}\', {2}, {3})'.format(re[0], re[1], re[2], re[3])

def query(cur, sql):
    cur.execute(sql)
    results = cur.fetchall()
    return results

def get_table_defs(my_host, my_port, my_user, my_passwd, my_database):
  try:
    conn = mysql.connector.connect(user=my_user, password=my_passwd, host=my_host, port=my_port, database=my_database, charset='utf8')
    cur = conn.cursor()
    rows = query(cur, 'show tables')
    if len(rows) == 0:
      print 'no tables'
    tables = [_[0] for _ in rows]
    table_defs = [query(cur, 'show create table {0}'.format(_))[0][1] for _ in tables]
    for table_def in table_defs:
      print '{0};\n'.format(table_def)
  except mysql.connector.Error, e:
    print e
    raise e
  except Exception, e:
    print e
    raise e
  finally:
    cur.close()
    conn.close()

def print_plan(my_host, my_port, my_user, my_passwd):
  try:
    conn = mysql.connector.connect(user=my_user, password=my_passwd, host=my_host, port=my_port, database='oceanbase', charset='utf8')
    cur = conn.cursor()
    #fetch_info = 'select tenant_id, svr_ip, svr_port, plan_id, statement from GV$OB_PLAN_CACHE_PLAN_STAT where tenant_id != 1 and statement like \'%{0}%\''.format(format_str);
    fetch_info = 'select tenant_id, svr_ip, svr_port, plan_id, statement, sql_id, type from GV$OB_PLAN_CACHE_PLAN_STAT where statement not like \'%@session%\' and statement not like \'%@@version_comment%\'';
    print fetch_info
    cur.execute(fetch_info)
    results = cur.fetchall()
    fetch_plan_tmp = 'select tenant_id, ip, port, plan_id, operator, name from GV$OB_PLAN_CACHE_PLAN_EXPLAIN where (tenant_id, ip, port, plan_id)in ({0})'
    for info in results:
      fetch_plan_sql = fetch_plan_tmp.format(to_str(info))
      #print fetch_plan_sql
      cur.execute(fetch_plan_sql)
      plan_results = cur.fetchall()
      i = 0
      print '\n'
      i = i + 1
      print 't:{0}, sql_id:{1},p_id:{3},{6},{5:<3} ip:{2}, {4}'.format(info[0], info[5], info[1], info[3], '<<<<<<<<<<', i, info[6])
      i = i + 1
      print 't:{0}, sql_id:{4},p_id:{2},{6},{5:<3} ip:{1}, sql:{3}'.format(info[0], info[1], info[3],info[4], info[5], i, info[6])
      print '\n'
      for pr in plan_results:
        i = i + 1
        print 't:{0}, sql_id:{5},p_id:{2},{7},{6:<3} ip:{1},  {3}  {4}'.format(info[0], info[1], info[3], pr[4], pr[5], info[5], i, info[6])
      i = i + 1
      print 't:{0}, sql_id:{1},p_id:{3},{6},{5:<3} ip:{2}, {4}'.format(info[0], info[5], info[1], info[3], '>>>>>>>>>', i, info[6])
  except mysql.connector.Error, e:
    print e
    traceback.print_exc()
    raise e
  except Exception, e:
    traceback.print_exc()
  finally:
    cur.close()
    conn.close()

if __name__ == "__main__":
  reload(sys)
  sys.setdefaultencoding('utf8')
  func_type = 0;
  my_host = ''
  my_port = 0
  my_user = ''
  my_passwd = ''
  my_database = ''
  def usage():
    myusage = '''
     dump plan command,if use sys tenant, dump all tenant plan; if use normal tenant, only dump normal tenant plan:
       python ./dump_plan.py -d -h host -P port -u user -p password;
       eg: python dump_plan.py -d -h '10.101.192.90' -P 50211 -u 'root@sys' -p 'xxx' -D 'xy_d'

     dump schema command, only dump normal tenant:
       python ./dump_plan.py -s -h host -P port -u user -p password -D database;
       eg: python dump_plan.py -s -h '10.101.192.90' -P 50211 -u 'root@huijin' -p 'xxx' -D 'xy_d'
    '''
    print "Usage:" + myusage
  opts, args = getopt.getopt(sys.argv[1:], "dsh:P:u:p:D:")
  for op, value in opts:
    if op == "-d":
      func_type = 1
    elif op == "-s":
      func_type = 2
    elif op == "-h":
      my_host = value
    elif op == "-P":
      my_port = value
    elif op == "-u":
      my_user = value
    elif op == "-p":
      my_passwd = value
    elif op == "-D":
      my_database = value
    else:
      usage()
      sys.exit()
  print "parameters from cmd: host=\"%s\", port=%s, user=\"%s\", password=\"%s\"" %(my_host, my_port, my_user, my_passwd)
  if func_type == 1:
    print_plan(my_host, my_port, my_user, my_passwd)
  elif func_type == 2:
    get_table_defs(my_host, my_port, my_user, my_passwd, my_database)
  else:
    print "invalid argument.\n"
    usage()
    sys.exit()

