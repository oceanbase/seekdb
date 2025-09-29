#!/usr/bin/env python
# -*- coding: utf-8 -*-

from __future__ import print_function
import sys
import os
import mysql.connector
from mysql.connector import errorcode
import logging
import getopt
import time
import re
import ctypes

if sys.version_info.major == 3:
    def cmp(a, b):
        return (a > b) - (a < b)

class UpgradeParams:
  log_filename = 'upgrade_checker.log'
  old_version = '4.2.5.1'

class PasswordMaskingFormatter(logging.Formatter):
  def format(self, record):
    s = super(PasswordMaskingFormatter, self).format(record)
    return re.sub(r'password="(?:[^"\\]|\\.)*"', 'password="******"', s)

#### --------------start : my_error.py --------------
class MyError(Exception):
  def __init__(self, value):
    self.value = value
  def __str__(self):
    return repr(self.value)
#### --------------start : actions.py------------
class Cursor:
  __cursor = None
  def __init__(self, cursor):
    self.__cursor = cursor
  def exec_sql(self, sql, print_when_succ = True):
    try:
      self.__cursor.execute(sql)
      rowcount = self.__cursor.rowcount
      if True == print_when_succ:
        logging.info('succeed to execute sql: %s, rowcount = %d', sql, rowcount)
      return rowcount
    except mysql.connector.Error as e:
      logging.exception('mysql connector error, fail to execute sql: %s', sql)
      raise
    except Exception as e:
      logging.exception('normal error, fail to execute sql: %s', sql)
      raise
  def exec_query(self, sql, print_when_succ = True):
    try:
      self.__cursor.execute(sql)
      results = self.__cursor.fetchall()
      rowcount = self.__cursor.rowcount
      if True == print_when_succ:
        logging.info('succeed to execute query: %s, rowcount = %d', sql, rowcount)
      return (self.__cursor.description, results)
    except mysql.connector.Error as e:
      logging.exception('mysql connector error, fail to execute sql: %s', sql)
      raise
    except Exception as e:
      logging.exception('normal error, fail to execute sql: %s', sql)
      raise

def set_parameter(cur, parameter, value):
  sql = """alter system set {0} = '{1}'""".format(parameter, value)
  logging.info(sql)
  cur.execute(sql)
  wait_parameter_sync(cur, parameter, value)

def wait_parameter_sync(cur, key, value):
  sql = """select count(*) as cnt from oceanbase.__all_virtual_sys_parameter_stat
           where name = '{0}' and value != '{1}'""".format(key, value)
  times = 10
  while times > 0:
    logging.info(sql)
    cur.execute(sql)
    result = cur.fetchall()
    if len(result) != 1 or len(result[0]) != 1:
      logging.exception('result cnt not match')
      raise MyError('result cnt not match')
    elif result[0][0] == 0:
      logging.info("""{0} is sync, value is {1}""".format(key, value))
      break
    else:
      logging.info("""{0} is not sync, value should be {1}""".format(key, value))

    times -= 1
    if times == 0:
      logging.exception("""check {0}:{1} sync timeout""".format(key, value))
      raise MyError("""check {0}:{1} sync timeout""".format(key, value))
    time.sleep(5)

#### --------------start :  opt.py --------------
help_str = \
"""
Help:
""" +\
sys.argv[0] + """ [OPTIONS]""" +\
'\n\n' +\
'-I, --help          Display this help and exit.\n' +\
'-V, --version       Output version information and exit.\n' +\
'-h, --host=name     Connect to host.\n' +\
'-P, --port=name     Port number to use for connection.\n' +\
'-u, --user=name     User for login.\n' +\
'-t, --timeout=name  Cmd/Query/Inspection execute timeout(s).\n' +\
'-p, --password=name Password to use when connecting to server. If password is\n' +\
'                    not given it\'s empty string "".\n' +\
'-m, --module=name   Modules to run. Modules should be a string combined by some of\n' +\
'                    the following strings: ddl, normal_dml, each_tenant_dml,\n' +\
'                    system_variable_dml, special_action, all. "all" represents\n' +\
'                    that all modules should be run. They are splitted by ",".\n' +\
'                    For example: -m all, or --module=ddl,normal_dml,special_action\n' +\
'-l, --log-file=name Log file path. If log file path is not given it\'s ' + os.path.splitext(sys.argv[0])[0] + '.log\n' +\
'-arc, --cpu-arch=name CPU architecture. Whether machine in cluster support AVX2 arch or not.\n' +\
'                      \'avx2\' for x86 avx2 instruction set supported\n' +\
'                      \'avx2_not_support\' for x86 avx2 instruction set not supported\n' +\
'\n\n' +\
'Maybe you want to run cmd like that:\n' +\
sys.argv[0] + ' -h 127.0.0.1 -P 3306 -u admin -p admin\n'

version_str = """version 1.0.0"""

class Option:
  __g_short_name_set = set([])
  __g_long_name_set = set([])
  __short_name = None
  __long_name = None
  __is_with_param = None
  __is_local_opt = None
  __has_value = None
  __value = None
  def __init__(self, short_name, long_name, is_with_param, is_local_opt, default_value = None):
    if short_name in Option.__g_short_name_set:
      raise MyError('duplicate option short name: {0}'.format(short_name))
    elif long_name in Option.__g_long_name_set:
      raise MyError('duplicate option long name: {0}'.format(long_name))
    Option.__g_short_name_set.add(short_name)
    Option.__g_long_name_set.add(long_name)
    self.__short_name = short_name
    self.__long_name = long_name
    self.__is_with_param = is_with_param
    self.__is_local_opt = is_local_opt
    self.__has_value = False
    if None != default_value:
      self.set_value(default_value)
  def is_with_param(self):
    return self.__is_with_param
  def get_short_name(self):
    return self.__short_name
  def get_long_name(self):
    return self.__long_name
  def has_value(self):
    return self.__has_value
  def get_value(self):
    return self.__value
  def set_value(self, value):
    self.__value = value
    self.__has_value = True
  def is_local_opt(self):
    return self.__is_local_opt
  def is_valid(self):
    return None != self.__short_name and None != self.__long_name and True == self.__has_value and None != self.__value

g_opts =\
[\
Option('I', 'help', False, True),\
Option('V', 'version', False, True),\
Option('h', 'host', True, False),\
Option('P', 'port', True, False),\
Option('u', 'user', True, False),\
Option('t', 'timeout', True, False, 0),\
Option('p', 'password', True, False, ''),\
# Which module to run, default is all
Option('m', 'module', True, False, 'all'),\
# Log file path, the default value will be changed to different values in the main function of different scripts
Option('l', 'log-file', True, False),\
Option('C', 'cpu-arch', True, False, 'unknown')
]\

def change_opt_defult_value(opt_long_name, opt_default_val):
  global g_opts
  for opt in g_opts:
    if opt.get_long_name() == opt_long_name:
      opt.set_value(opt_default_val)
      return

def has_no_local_opts():
  global g_opts
  no_local_opts = True
  for opt in g_opts:
    if opt.is_local_opt() and opt.has_value():
      no_local_opts = False
  return no_local_opts

def check_db_client_opts():
  global g_opts
  for opt in g_opts:
    if not opt.is_local_opt() and not opt.has_value():
      raise MyError('option "-{0}" has not been specified, maybe you should run "{1} --help" for help'\
          .format(opt.get_short_name(), sys.argv[0]))

def parse_option(opt_name, opt_val):
  global g_opts
  for opt in g_opts:
    if opt_name in (('-' + opt.get_short_name()), ('--' + opt.get_long_name())):
      opt.set_value(opt_val)

def parse_options(argv):
  global g_opts
  short_opt_str = ''
  long_opt_list = []
  for opt in g_opts:
    if opt.is_with_param():
      short_opt_str += opt.get_short_name() + ':'
    else:
      short_opt_str += opt.get_short_name()
  for opt in g_opts:
    if opt.is_with_param():
      long_opt_list.append(opt.get_long_name() + '=')
    else:
      long_opt_list.append(opt.get_long_name())
  (opts, args) = getopt.getopt(argv, short_opt_str, long_opt_list)
  for (opt_name, opt_val) in opts:
    parse_option(opt_name, opt_val)
  if has_no_local_opts():
    check_db_client_opts()

def deal_with_local_opt(opt):
  if 'help' == opt.get_long_name():
    global help_str
    print(help_str)
  elif 'version' == opt.get_long_name():
    global version_str
    print(version_str)

def deal_with_local_opts():
  global g_opts
  if has_no_local_opts():
    raise MyError('no local options, can not deal with local options')
  else:
    for opt in g_opts:
      if opt.is_local_opt() and opt.has_value():
        deal_with_local_opt(opt)
        # Only process one
        return

def get_opt_host():
  global g_opts
  for opt in g_opts:
    if 'host' == opt.get_long_name():
      return opt.get_value()

def get_opt_port():
  global g_opts
  for opt in g_opts:
    if 'port' == opt.get_long_name():
      return opt.get_value()

def get_opt_user():
  global g_opts
  for opt in g_opts:
    if 'user' == opt.get_long_name():
      return opt.get_value()

def get_opt_password():
  global g_opts
  for opt in g_opts:
    if 'password' == opt.get_long_name():
      return opt.get_value()

def get_opt_timeout():
  global g_opts
  for opt in g_opts:
    if 'timeout' == opt.get_long_name():
      return opt.get_value()

def get_opt_module():
  global g_opts
  for opt in g_opts:
    if 'module' == opt.get_long_name():
      return opt.get_value()

def get_opt_log_file():
  global g_opts
  for opt in g_opts:
    if 'log-file' == opt.get_long_name():
      return opt.get_value()

def get_opt_cpu_arch():
  global g_opts
  for opt in g_opts:
    if 'cpu-arch' == opt.get_long_name():
      return opt.get_value()
#### ---------------end----------------------

#### --------------start :  do_upgrade_pre.py--------------
def config_logging_module(log_filenamme):
  logger = logging.getLogger('')
  logger.setLevel(logging.INFO)
  # Define log print format
  formatter = PasswordMaskingFormatter('[%(asctime)s] %(levelname)s %(filename)s:%(lineno)d %(message)s', '%Y-%m-%d %H:%M:%S')
  #######################################
  # Define a Handler to print INFO and above level logs to sys.stdout
  stdout_handler = logging.StreamHandler(sys.stdout)
  stdout_handler.setLevel(logging.INFO)
  stdout_handler.setFormatter(formatter)
  # Define a Handler to handle file output
  file_handler = logging.FileHandler(log_filenamme, mode='w')
  file_handler.setLevel(logging.INFO)
  file_handler.setFormatter(formatter)
  logging.getLogger('').addHandler(stdout_handler)
  logging.getLogger('').addHandler(file_handler)
#### ---------------end----------------------


fail_list=[]

def get_version(version_str):
  versions = version_str.split(".")

  if len(versions) != 4:
    logging.exception("""version:{0} is invalid""".format(version_str))
    raise MyError("""version:{0} is invalid""".format(version_str))

  major = int(versions[0])
  minor = int(versions[1])
  major_patch = int(versions[2])
  minor_patch = int(versions[3])

  if major > 0xffffffff or minor > 0xffff or major_patch > 0xff or minor_patch > 0xff:
    logging.exception("""version:{0} is invalid""".format(version_str))
    raise MyError("""version:{0} is invalid""".format(version_str))

  version = (major << 32) | (minor << 16) | (major_patch << 8) | (minor_patch)
  return version

#### START ####
# 1. Check previous version
def check_observer_version(query_cur, upgrade_params):
  (desc, results) = query_cur.exec_query("""select distinct value from GV$OB_PARAMETERS  where name='min_observer_version'""")
  if len(results) != 1:
    fail_list.append('min_observer_version is not sync')
  elif cmp(results[0][0], upgrade_params.old_version) < 0 :
    fail_list.append('old observer version is expected equal or higher than: {0}, actual version:{1}'.format(upgrade_params.old_version, results[0][0]))
  logging.info('check observer version success, version = {0}'.format(results[0][0]))

def check_data_version(query_cur):
  min_cluster_version = 0
  sql = """select distinct value from GV$OB_PARAMETERS  where name='min_observer_version'"""
  (desc, results) = query_cur.exec_query(sql)
  if len(results) != 1:
    fail_list.append('min_observer_version is not sync')
  elif len(results[0]) != 1:
    fail_list.append('column cnt not match')
  else:
    min_cluster_version = get_version(results[0][0])

    # check data version
    if min_cluster_version < get_version("4.1.0.0"):
      # last barrier cluster version should be 4.1.0.0
      fail_list.append('last barrier cluster version is 4.1.0.0. prohibit cluster upgrade from cluster version less than 4.1.0.0')
    else:
      data_version_str = ''
      data_version = 0
      # check compatible is same
      sql = """select distinct value from oceanbase.__all_virtual_tenant_parameter_info where name='compatible'"""
      (desc, results) = query_cur.exec_query(sql)
      if len(results) != 1:
        fail_list.append('compatible is not sync')
      elif len(results[0]) != 1:
        fail_list.append('column cnt not match')
      else:
        data_version_str = results[0][0]
        data_version = get_version(results[0][0])

        if data_version < get_version("4.1.0.0"):
          # last barrier data version should be 4.1.0.0
          fail_list.append('last barrier data version is 4.1.0.0. prohibit cluster upgrade from data version less than 4.1.0.0')
        else:
          # check target_data_version/current_data_version
          sql = "select count(*) from oceanbase.__all_tenant"
          (desc, results) = query_cur.exec_query(sql)
          if len(results) != 1 or len(results[0]) != 1:
            fail_list.append('result cnt not match')
          else:
            # check upgrade_begin_data_version
            tenant_count = results[0][0]

            sql = "select count(*) from __all_virtual_core_table where column_name in ('target_data_version', 'current_data_version') and column_value = {0}".format(data_version)
            (desc, results) = query_cur.exec_query(sql)
            if len(results) != 1 or len(results[0]) != 1:
              fail_list.append('result cnt not match')
            elif 2 * tenant_count != results[0][0]:
              fail_list.append('target_data_version/current_data_version not match with {0}, tenant_cnt:{1}, result_cnt:{2}'.format(data_version_str, tenant_count, results[0][0]))
            else:
              logging.info("check data version success, all tenant's compatible/target_data_version/current_data_version is {0}".format(data_version_str))

            if data_version >= get_version("4.3.5.1"):
              # check upgrade_begin_data_version
              sql = "select count(*) from __all_virtual_core_table where column_name in ('upgrade_begin_data_version') and column_value = {0}".format(data_version)
              (desc, results) = query_cur.exec_query(sql)
              if len(results) != 1 or len(results[0]) != 1:
                fail_list.append('result cnt not match')
              elif tenant_count != results[0][0]:
                fail_list.append('upgrade_begin_data_version not match with {0}, tenant_cnt:{1}, result_cnt:{2}'.format(data_version_str, tenant_count, results[0][0]))
              else:
                logging.info("check data version success, all tenant's upgrade_begin_data_version is {0}".format(data_version_str))
# 2. Check if paxos replicas are synchronized, if there are any missing paxos replicas
def check_paxos_replica(query_cur):
  # 2.1 Check if paxos replicas are synchronized
  (desc, results) = query_cur.exec_query("""select count(1) as unsync_cnt from GV$OB_LOG_STAT where in_sync = 'NO'""")
  if results[0][0] > 0 :
    fail_list.append('{0} replicas unsync, please check'.format(results[0][0]))
  # 2.2 Check if there are any missing paxos replicas TODO
  logging.info('check paxos replica success')
# 3. Check if there is a balance, locality change
def check_rebalance_task(query_cur):
  # 3.1 Check if there is a locality change
  (desc, results) = query_cur.exec_query("""select count(1) as cnt from DBA_OB_TENANT_JOBS where job_status='INPROGRESS' and result_code is null""")
  if results[0][0] > 0 :
    fail_list.append('{0} locality tasks is doing, please check'.format(results[0][0]))
  # 3.2 Check if balance has been done
  (desc, results) = query_cur.exec_query("""select count(1) as rebalance_task_cnt from CDB_OB_LS_REPLICA_TASKS""")
  if results[0][0] > 0 :
    fail_list.append('{0} rebalance tasks is doing, please check'.format(results[0][0]))
  logging.info('check rebalance task success')
# 4. Check cluster status
def check_cluster_status(query_cur):
  # 4.1 Check if not in merge state
  (desc, results) = query_cur.exec_query("""select count(1) from CDB_OB_MAJOR_COMPACTION where (GLOBAL_BROADCAST_SCN > LAST_SCN or STATUS != 'IDLE')""")
  if results[0][0] > 0 :
    fail_list.append('{0} tenant is merging, please check'.format(results[0][0]))
  (desc, results) = query_cur.exec_query("""select /*+ query_timeout(1000000000) */ count(1) from __all_virtual_tablet_compaction_info where max_received_scn > finished_scn and max_received_scn > 0""")
  if results[0][0] > 0 :
    fail_list.append('{0} tablet is merging, please check'.format(results[0][0]))
  logging.info('check cluster status success')
# 5. Check for abnormal tenants (creating, delayed deletion, restoring, tenant unit has leftovers)
def check_tenant_status(query_cur):

  # check tenant schema
  (desc, results) = query_cur.exec_query("""select count(*) as count from DBA_OB_TENANTS where status != 'NORMAL'""")
  if len(results) != 1 or len(results[0]) != 1:
    fail_list.append('results len not match')
  elif 0 != results[0][0]:
    fail_list.append('has abnormal tenant, should stop')
  else:
    logging.info('check tenant status success')

  # check tenant info
  # don't support restore tenant upgrade
  (desc, results) = query_cur.exec_query("""select count(*) as count from oceanbase.__all_virtual_tenant_info where tenant_role != 'PRIMARY' and tenant_role != 'STANDBY'""")
  if len(results) != 1 or len(results[0]) != 1:
    fail_list.append('results len not match')
  elif 0 != results[0][0]:
    fail_list.append('has abnormal tenant info, should stop')
  else:
    logging.info('check tenant info success')

   # check tenant lock status
  (desc, results) = query_cur.exec_query("""select count(*) from DBA_OB_TENANTS where LOCKED = 'YES'""")
  if len(results) != 1 or len(results[0]) != 1:
    fail_list.append('results len not match')
  elif 0 != results[0][0]:
    fail_list.append('has locked tenant, should unlock')
  else:
    logging.info('check tenant lock status success')

  # check all deleted tenant's unit is freed
  (desc, results) = query_cur.exec_query("select count(*) from oceanbase.gv$ob_units a, oceanbase.__all_tenant_history b where b.is_deleted = 1 and a.tenant_id = b.tenant_id")
  if len(results) != 1 or len(results[0]) != 1:
    fail_list.append('results len not match')
  elif 0 != results[0][0]:
    fail_list.append('has deleted tenant with unit not freed')
  else:
    logging.info('check deleted tenant unit gc success')
# 6. Check for no recovery tasks
def check_restore_job_exist(query_cur):
  (desc, results) = query_cur.exec_query("""select count(1) from CDB_OB_RESTORE_PROGRESS""")
  if len(results) != 1 or len(results[0]) != 1:
    fail_list.append('failed to restore job cnt')
  elif results[0][0] != 0:
      fail_list.append("""still has restore job, upgrade is not allowed temporarily""")
  logging.info('check restore job success')

def check_is_primary_zone_distributed(primary_zone_str):
  semicolon_pos = len(primary_zone_str)
  for i in range(len(primary_zone_str)):
    if primary_zone_str[i] == ';':
      semicolon_pos = i
      break
  comma_pos = len(primary_zone_str)
  for j in range(len(primary_zone_str)):
    if primary_zone_str[j] == ',':
      comma_pos = j
      break
  if comma_pos < semicolon_pos:
    return True
  else:
    return False
# 7. Primary zone needs to have only one before upgrade
def check_tenant_primary_zone(query_cur):
  sql = """select distinct value from GV$OB_PARAMETERS  where name='min_observer_version'"""
  (desc, results) = query_cur.exec_query(sql)
  if len(results) != 1:
    fail_list.append('min_observer_version is not sync')
  elif len(results[0]) != 1:
    fail_list.append('column cnt not match')
  else:
    min_cluster_version = get_version(results[0][0])
    if min_cluster_version < get_version("4.1.0.0"):
      (desc, results) = query_cur.exec_query("""select tenant_name,primary_zone from DBA_OB_TENANTS where  tenant_id != 1""");
      for item in results:
        if cmp(item[1], "RANDOM") == 0:
          fail_list.append('{0} tenant primary zone random before update not allowed'.format(item[0]))
        elif check_is_primary_zone_distributed(item[1]):
          fail_list.append('{0} tenant primary zone distributed before update not allowed'.format(item[0]))
      logging.info('check tenant primary zone success')
# 8. Modify the permanent offline time to avoid missing copies during the upgrade process
def modify_server_permanent_offline_time(cur):
  set_parameter(cur, 'server_permanent_offline_time', '72h')
# 9. Check if there are any DDL tasks executing
def check_ddl_task_execute(query_cur):
  (desc, results) = query_cur.exec_query("""select count(1) from __all_virtual_ddl_task_status""")
  if 0 != results[0][0]:
    fail_list.append("There are DDL task in progress")
  logging.info('check ddl task execut status success')
# 10. Check tasks without backup
def check_backup_job_exist(query_cur):
  # Backup jobs cannot be in-progress during upgrade.
  (desc, results) = query_cur.exec_query("""select count(1) from CDB_OB_BACKUP_JOBS""")
  if len(results) != 1 or len(results[0]) != 1:
    fail_list.append('failed to backup job cnt')
  elif results[0][0] != 0:
    fail_list.append("""still has backup job, upgrade is not allowed temporarily""")
  else:
    logging.info('check backup job success')
# 11. Check for unarchived tasks
def check_archive_job_exist(query_cur):
  min_cluster_version = 0
  sql = """select distinct value from GV$OB_PARAMETERS  where name='min_observer_version'"""
  (desc, results) = query_cur.exec_query(sql)
  if len(results) != 1:
    fail_list.append('min_observer_version is not sync')
  elif len(results[0]) != 1:
    fail_list.append('column cnt not match')
  else:
    min_cluster_version = get_version(results[0][0])

    # Archive jobs cannot be in-progress before upgrade from 4.0.
    if min_cluster_version < get_version("4.1.0.0"):
      (desc, results) = query_cur.exec_query("""select count(1) from CDB_OB_ARCHIVELOG where status!='STOP'""")
      if len(results) != 1 or len(results[0]) != 1:
        fail_list.append('failed to archive job cnt')
      elif results[0][0] != 0:
        fail_list.append("""still has archive job, upgrade is not allowed temporarily""")
      else:
        logging.info('check archive job success')
# 12. Check if the archive path is cleared
def check_archive_dest_exist(query_cur):
  min_cluster_version = 0
  sql = """select distinct value from GV$OB_PARAMETERS  where name='min_observer_version'"""
  (desc, results) = query_cur.exec_query(sql)
  if len(results) != 1:
    fail_list.append('min_observer_version is not sync')
  elif len(results[0]) != 1:
    fail_list.append('column cnt not match')
  else:
    min_cluster_version = get_version(results[0][0])
    # archive dest need to be cleaned before upgrade from 4.0.
    if min_cluster_version < get_version("4.1.0.0"):
      (desc, results) = query_cur.exec_query("""select count(1) from CDB_OB_ARCHIVE_DEST""")
      if len(results) != 1 or len(results[0]) != 1:
        fail_list.append('failed to archive dest cnt')
      elif results[0][0] != 0:
        fail_list.append("""still has archive destination, upgrade is not allowed temporarily""")
      else:
        logging.info('check archive destination success')
# 13. Check if the backup path is cleared
def check_backup_dest_exist(query_cur):
  min_cluster_version = 0
  sql = """select distinct value from GV$OB_PARAMETERS  where name='min_observer_version'"""
  (desc, results) = query_cur.exec_query(sql)
  if len(results) != 1:
    fail_list.append('min_observer_version is not sync')
  elif len(results[0]) != 1:
    fail_list.append('column cnt not match')
  else:
    min_cluster_version = get_version(results[0][0])
    # backup dest need to be cleaned before upgrade from 4.0.
    if min_cluster_version < get_version("4.1.0.0"):
      (desc, results) = query_cur.exec_query("""select count(1) from CDB_OB_BACKUP_PARAMETER where name='data_backup_dest' and (value!=NULL or value!='')""")
      if len(results) != 1 or len(results[0]) != 1:
        fail_list.append('failed to data backup dest cnt')
      elif results[0][0] != 0:
        fail_list.append("""still has backup destination, upgrade is not allowed temporarily""")
      else:
        logging.info('check backup destination success')

def check_server_version(query_cur):
    sql = """select distinct(substring_index(build_version, '_', 1)) from __all_server""";
    (desc, results) = query_cur.exec_query(sql);
    if len(results) != 1:
      fail_list.append("servers build_version not match")
    else:
      logging.info("check server version success")
# 14. Check if server is available
def check_observer_status(query_cur):
  (desc, results) = query_cur.exec_query("""select count(*) from oceanbase.__all_server where (start_service_time <= 0 or status != "active")""")
  if results[0][0] > 0 :
    fail_list.append('{0} observer not available , please check'.format(results[0][0]))
  logging.info('check observer status success')
# 15  Check if schema refresh was successful
def check_schema_status(query_cur):
  (desc, results) = query_cur.exec_query("""select if (a.cnt = b.cnt, 1, 0) as passed from (select count(*) as cnt from oceanbase.__all_virtual_server_schema_info where refreshed_schema_version > 1 and refreshed_schema_version % 8 = 0) as a join (select count(*) as cnt from oceanbase.__all_server join oceanbase.__all_tenant) as b""")
  if results[0][0] != 1 :
    fail_list.append('{0} schema not available, please check'.format(results[0][0]))
  logging.info('check schema status success')
# 16. Check if there is a tenant named all/all_user/all_meta
def check_not_supported_tenant_name(query_cur):
  names = ["all", "all_user", "all_meta"]
  (desc, results) = query_cur.exec_query("""select tenant_name from oceanbase.DBA_OB_TENANTS""")
  for i in range(len(results)):
    if results[i][0].lower() in names:
      fail_list.append('a tenant named all/all_user/all_meta (case insensitive) cannot exist in the cluster, please rename the tenant')
      break
  logging.info('check special tenant name success')
# 17  Check if log transmission compression uses the zlib compression algorithm, it is necessary to ensure that all observers have not enabled log transmission compression or are using a non-zlib compression algorithm
def check_log_transport_compress_func(query_cur):
  (desc, results) = query_cur.exec_query("""select count(1) as cnt from oceanbase.__all_virtual_tenant_parameter_info where (name like "log_transport_compress_func" and value like "zlib_1.0")""")
  if results[0][0] > 0 :
    fail_list.append('The zlib compression algorithm is no longer supported with log_transport_compress_func, please replace it with other compression algorithms')
  logging.info('check log_transport_compress_func success')
# 18 Check if any tables use zlib compression during the upgrade process, all tables must not use zlib compression before the upgrade
def check_table_compress_func(query_cur):
  (desc, results) = query_cur.exec_query("""select /*+ query_timeout(1000000000) */ count(1) from __all_virtual_table where (compress_func_name like '%zlib%')""")
  if results[0][0] > 0 :
    fail_list.append('There are tables use zlib compression, please replace it with other compression algorithms or do not use compression during the upgrade')
  logging.info('check table compression method success')
# 19 Check if zlib compression is used during the upgrade process for table_api/obkv connections. Before the upgrade, it is necessary to ensure that all obkv/table_api connections have zlib compression transmission disabled or are using a non-zlib compression algorithm
def check_table_api_transport_compress_func(query_cur):
  (desc, results) = query_cur.exec_query("""select count(1) as cnt from GV$OB_PARAMETERS where (name like "tableapi_transport_compress_func" and value like "zlib%");""")
  if results[0][0] > 0 :
    fail_list.append('Table api connection is not allowed to use zlib as compression algorithm during the upgrade, please use other compression algorithms by setting table_api_transport_compress_func')
  logging.info('check table_api_transport_compress_func success')
# 17. Check tenantless clone task
def check_tenant_clone_job_exist(query_cur):
  min_cluster_version = 0
  sql = """select distinct value from GV$OB_PARAMETERS  where name='min_observer_version'"""
  (desc, results) = query_cur.exec_query(sql)
  if len(results) != 1:
    fail_list.append('min_observer_version is not sync')
  elif len(results[0]) != 1:
    fail_list.append('column cnt not match')
  else:
    min_cluster_version = get_version(results[0][0])
    if min_cluster_version >= get_version("4.3.0.0"):
      (desc, results) = query_cur.exec_query("""select count(1) from __all_virtual_clone_job""")
      if len(results) != 1 or len(results[0]) != 1:
        fail_list.append('failed to tenant clone job cnt')
      elif results[0][0] != 0:
        fail_list.append("""still has tenant clone job, upgrade is not allowed temporarily""")
      else:
        logging.info('check tenant clone job success')
# 18. Check non-tenant snapshot task
def check_tenant_snapshot_task_exist(query_cur):
  min_cluster_version = 0
  sql = """select distinct value from GV$OB_PARAMETERS  where name='min_observer_version'"""
  (desc, results) = query_cur.exec_query(sql)
  if len(results) != 1:
    fail_list.append('min_observer_version is not sync')
  elif len(results[0]) != 1:
    fail_list.append('column cnt not match')
  else:
    min_cluster_version = get_version(results[0][0])
    if min_cluster_version >= get_version("4.3.0.0"):
      (desc, results) = query_cur.exec_query("""select count(1) from __all_virtual_tenant_snapshot where status!='NORMAL'""")
      if len(results) != 1 or len(results[0]) != 1:
        fail_list.append('failed to tenant snapshot task')
      elif results[0][0] != 0:
        fail_list.append("""still has tenant snapshot task, upgrade is not allowed temporarily""")
      else:
        logging.info('check tenant snapshot task success')
# 17. Check if any tenant has set binlog_row_image to MINIMAL before upgrading to version 4.3.0
def check_variable_binlog_row_image(query_cur):
# Versions before 4.3.0.0, the CDC logs generated in MINIMAL mode cannot be consumed normally (DELETE logs).
# From version 4.3.0, MINIMAL mode has been improved to support CDC consumption, and needs to be enabled after upgrading to 4.3.0.0.
  min_cluster_version = 0
  sql = """select distinct value from GV$OB_PARAMETERS  where name='min_observer_version'"""
  (desc, results) = query_cur.exec_query(sql)
  if len(results) != 1:
    fail_list.append('min_observer_version is not sync')
  elif len(results[0]) != 1:
    fail_list.append('column cnt not match')
  else:
    min_cluster_version = get_version(results[0][0])
    # check cluster version
    if min_cluster_version < get_version("4.3.0.0"):
      (desc, results) = query_cur.exec_query("""select count(*) from CDB_OB_SYS_VARIABLES where NAME='binlog_row_image' and VALUE = '0'""")
      if results[0][0] > 0 :
        fail_list.append('Sys Variable binlog_row_image is set to MINIMAL, please check'.format(results[0][0]))
    logging.info('check variable binlog_row_image success')

# 20. check oracle tenant's standby_replication privs
def check_oracle_standby_replication_exist(query_cur):
  check_success = True
  min_cluster_version = 0
  sql = """select distinct value from GV$OB_PARAMETERS  where name='min_observer_version'"""
  (desc, results) = query_cur.exec_query(sql)
  if len(results) != 1:
    check_success = False
    fail_list.append('min_observer_version is not sync')
  elif len(results[0]) != 1:
    check_success = False
    fail_list.append('column cnt not match')
  else:
    min_cluster_version = get_version(results[0][0])
    (desc, results) = query_cur.exec_query("""select tenant_id from oceanbase.__all_tenant where compatibility_mode = 1""")
    if len(results) > 0 :
      tenant_ids = results
      if (min_cluster_version < get_version("4.2.2.0") or (get_version("4.3.0.0") <= min_cluster_version < get_version("4.3.1.0"))):
        for tenant_id in tenant_ids:
          sql = """select count(1)=1 from oceanbase.__all_virtual_user where user_name='STANDBY_REPLICATION' and tenant_id=%d""" % (tenant_id[0])
          (desc, results) = query_cur.exec_query(sql)
          if results[0][0] == 1 :
            check_success = False
            fail_list.append('{0} tenant standby_replication already exists, please check'.format(tenant_id[0]))
      else :
        for tenant_id in tenant_ids:
          sql = """select count(1)=0 from oceanbase.__all_virtual_user where user_name='STANDBY_REPLICATION' and tenant_id=%d""" % (tenant_id[0])
          (desc, results) = query_cur.exec_query(sql)
          if results[0][0] == 1 :
            check_success = False
            fail_list.append('{0} tenant standby_replication not exist, please check'.format(tenant_id[0]))
  if check_success:
    logging.info('check oracle standby_replication privs success')
# last check of do_check, make sure no function execute after check_fail_list
def check_fail_list():
  if len(fail_list) != 0 :
     error_msg ="upgrade checker failed with " + str(len(fail_list)) + " reasons: " + ", ".join(['['+x+"] " for x in fail_list])
     raise MyError(error_msg)
# Check if the remaining disk space is sufficient for multi-source data format conversion when upgrading to version 4.3.2 or higher
def check_disk_space_for_mds_sstable_compat(query_cur):
  need_check_disk_space = False
  sql = """select distinct value from GV$OB_PARAMETERS where name='min_observer_version'"""
  (desc, results) = query_cur.exec_query(sql)
  if len(results) != 1:
    fail_list.append('min_observer_version is not sync')
  elif len(results[0]) != 1:
    fail_list.append('column cnt not match')
  else:
    min_cluster_version = get_version(results[0][0])
    if min_cluster_version < get_version("4.3.2.0"):
      need_check_disk_space = True
      logging.info("need check disk space for mds sstable, min observer version: {0}".format(results[0][0]))
    else:
      logging.info("no need to check disk space, min observer version: {0}".format(results[0][0]))

  if need_check_disk_space:
    do_check_disk_space_for_compat(query_cur)

def do_check_disk_space_for_compat(query_cur):
  sql = """select svr_ip, svr_port from __all_server"""
  (desc, results) = query_cur.exec_query(sql)

  success = True
  for idx in range(len(results)):
    svr_ip = results[idx][0]
    svr_port = results[idx][1]

    tablet_cnt = get_tablet_cnt(query_cur, svr_ip, svr_port)
    disk_free_size = get_disk_free_size(query_cur, svr_ip, svr_port)
    needed_size = tablet_cnt * 4096 * 2
    if needed_size > disk_free_size:
      fail_list.append("svr_ip: {0}, svr_port: {1}, disk_free_size {2} is not enough for mds sstable, needed_size is {3}, cannot upgrade".format(svr_ip, svr_port, disk_free_size, needed_size))
      success = False
    else:
      logging.info("svr_ip: {0}, svr_port: {1}, disk_free_size: {2}, needed_size: {3}, can upgrade".format(svr_ip, svr_port, disk_free_size, needed_size))

  if success:
    logging.info("check disk space for mds sstable success")

def get_tablet_cnt(query_cur, svr_ip, svr_port):
  sql = """select /*+ query_timeout(1000000000) */ count(*) from __all_virtual_tablet_pointer_status where svr_ip = '{0}' and svr_port = {1}""".format(svr_ip, svr_port)
  (desc, results) = query_cur.exec_query(sql)
  return results[0][0]

def get_disk_free_size(query_cur, svr_ip, svr_port):
  sql = """select free_size from __all_virtual_disk_stat where svr_ip = '{0}' and svr_port = {1}""".format(svr_ip, svr_port)
  (desc, results) = query_cur.exec_query(sql)
  return results[0][0]

def set_query_timeout(query_cur, timeout):
  if timeout != 0:
    sql = """set @@session.ob_query_timeout = {0}""".format(timeout * 1000 * 1000)
    query_cur.exec_sql(sql)

# Run assembly in python with mmaped byte-code
class ASM:
  def __init__(self, restype=None, argtypes=(), machine_code=[]):
    self.restype = restype
    self.argtypes = argtypes
    self.machine_code = machine_code
    self.prochandle = None
    self.mm = None
    self.func = None
    self.address = None
    self.size = 0

  def compile(self):
    machine_code = bytes.join(b'', self.machine_code)
    self.size = ctypes.c_size_t(len(machine_code))
    from mmap import mmap, MAP_PRIVATE, MAP_ANONYMOUS, PROT_WRITE, PROT_READ, PROT_EXEC

    # Allocate a private and executable memory segment the size of the machine code
    machine_code = bytes.join(b'', self.machine_code)
    self.size = len(machine_code)
    self.mm = mmap(-1, self.size, flags=MAP_PRIVATE | MAP_ANONYMOUS, prot=PROT_WRITE | PROT_READ | PROT_EXEC)

    # Copy the machine code into the memory segment
    self.mm.write(machine_code)
    self.address = ctypes.addressof(ctypes.c_int.from_buffer(self.mm))

    # Cast the memory segment into a function
    functype = ctypes.CFUNCTYPE(self.restype, *self.argtypes)
    self.func = functype(self.address)

  def run(self):
    # Call the machine code like a function
    retval = self.func()

    return retval

  def free(self):
    # Free the function memory segment
    self.mm.close()
    self.prochandle = None
    self.mm = None
    self.func = None
    self.address = None
    self.size = 0

def run_asm(*machine_code):
  asm = ASM(ctypes.c_uint32, (), machine_code)
  asm.compile()
  retval = asm.run()
  asm.free()
  return retval

def is_bit_set(reg, bit):
  mask = 1 << bit
  is_set = reg & mask > 0
  return is_set

def get_max_extension_support():
  # Check for extension support
  max_extension_support = run_asm(
    b"\xB8\x00\x00\x00\x80" # mov ax,0x80000000
    b"\x0f\xa2"             # cpuid
    b"\xC3"                 # ret
  )
  return max_extension_support

def arch_support_avx2():
  bret = False
  if (is_x86_arch() and get_max_extension_support() >= 7):
    ebx = run_asm(
      b"\x31\xC9",            # xor ecx,ecx
      b"\xB8\x07\x00\x00\x00" # mov eax,7
      b"\x0f\xa2"             # cpuid
      b"\x89\xD8"             # mov ax,bx
      b"\xC3"                 # ret
    )
    bret = is_bit_set(ebx, 5)
  return bret

def is_x86_arch():
  import platform
  arch_string_raw = platform.machine().lower()
  bret = False
  if re.match(r'^i\d86$|^x86$|^x86_32$|^i86pc$|^ia32$|^ia-32$|^bepc$', arch_string_raw):
    # x86_32
    bret = True
  elif re.match(r'^x64$|^x86_64$|^x86_64t$|^i686-64$|^amd64$|^ia64$|^ia-64$', arch_string_raw):
    # x86_64
    bret=True
  return bret
# Check if direct_load has already finished, ensure there are no direct_load tasks before starting the upgrade, and try to prohibit direct_load tasks during the upgrade period
def check_direct_load_job_exist(cur, query_cur):
  sql = """select count(1) from __all_virtual_load_data_stat"""
  (desc, results) = query_cur.exec_query(sql)
  if 0 != results[0][0]:
    fail_list.append("There are direct load task in progress")
  logging.info('check direct load task execut status success')
# Check if cs_encoding format is compatible, for clusters with CPUs older than version 4.3.3 that do not support the avx2 instruction set, we require that the cs_encoding storage format does not exist on the schema before upgrading
# Note: Here the scenario of DDL changes to row_format on mixed cluster / schema cannot be fully defended
def check_cs_encoding_arch_dependency_compatiblity(query_cur, cpu_arch):
  can_upgrade = True
  need_check_schema = False
  is_arch_support_avx2 = False
  if 'unknown' == cpu_arch:
    is_arch_support_avx2 = arch_support_avx2()
  elif 'avx2' == cpu_arch:
    is_arch_support_avx2 = True
  elif 'avx2_not_support' == cpu_arch:
    is_arch_support_avx2 = False
  else:
    fail_list.append("unexpected cpu_arch option value: {0}".format(cpu_arch))

  sql = """select distinct value from GV$OB_PARAMETERS where name='min_observer_version'"""
  (desc, results) = query_cur.exec_query(sql)
  if len(results) != 1:
    fail_list.append('min_observer_version is not sync')
  elif len(results[0]) != 1:
    fail_list.append('column cnt not match')
  else:
    min_cluster_version = get_version(results[0][0])
    if min_cluster_version < get_version("4.3.3.0"):
      if (is_arch_support_avx2):
        logging.info("current cpu support avx2 inst, no need to check cs_encoding format")
      else:
        get_data_version_sql = """select distinct value from oceanbase.__all_virtual_tenant_parameter_info where name='compatible'"""
        (desc, results) = query_cur.exec_query(sql)
        if len(results) != 1:
          fail_list.append('compatible is not sync')
        elif len(results[0]) != 1:
          fail_list.append('column cnt not match')
        else:
          data_version = get_version(results[0][0])
          if (data_version < get_version("4.3.0.0")):
            logging.info("no need to check cs encoding arch compatibility for data version before version 4.3.0")
          else:
            logging.info("cpu not support avx2 instruction set, check cs_encoding format in schema")
            need_check_schema = True
    else:
      logging.info("no need to check cs encoding arch compatibility for cluster version after version 4.3.3")

  if need_check_schema and can_upgrade:
    ck_all_tbl_sql = """select count(1) from __all_virtual_table where row_store_type = 'cs_encoding_row_store'"""
    (desc, results) = query_cur.exec_query(ck_all_tbl_sql)
    if len(results) != 1:
      fail_list.append("all table query row count not match");
    elif len(results[0]) != 1:
      fail_list.append("all table query column count not match")
    elif results[0][0] != 0:
      can_upgrade = False
      fail_list.append("exist table with row_format cs_encoding_row_store for observer not support avx2 instruction set, table count = {0}".format(results[0][0]));

  if need_check_schema and can_upgrade:
    ck_all_cg_sql = """select count(distinct table_id) from __all_virtual_column_group where row_store_type = 3"""
    (desc, results) = query_cur.exec_query(ck_all_cg_sql)
    if len(results) != 1:
      fail_list.append("all column group query row count not match");
    elif len(results[0]) != 1:
      fail_list.append("all column group query column count not match")
    elif results[0][0] != 0:
      can_upgrade = False
      fail_list.append("exist column group with row_format cs_encoding_row_store for observer not support avx2 instruction set, table count = {0}".format(results[0][0]));

  if can_upgrade:
    logging.info("check upgrade for arch-dependant cs_encoding format success")
  else:
    logging.info("check upgrade for arch-dependant cs_encoding format failed")
# Start the upgrade check before beginning
def do_check(my_host, my_port, my_user, my_passwd, timeout, upgrade_params, cpu_arch):
  try:
    conn = mysql.connector.connect(user = my_user,
                                   password = my_passwd,
                                   host = my_host,
                                   port = my_port,
                                   database = 'oceanbase',
                                   raise_on_warnings = True)
    conn.autocommit = True
    cur = conn.cursor(buffered=True)
    try:
      query_cur = Cursor(cur)
      set_query_timeout(query_cur, timeout)
      check_observer_version(query_cur, upgrade_params)
      check_data_version(query_cur)
      check_paxos_replica(query_cur)
      check_rebalance_task(query_cur)
      check_cluster_status(query_cur)
      check_tenant_status(query_cur)
      check_restore_job_exist(query_cur)
      check_tenant_primary_zone(query_cur)
      check_ddl_task_execute(query_cur)
      check_backup_job_exist(query_cur)
      check_archive_job_exist(query_cur)
      check_archive_dest_exist(query_cur)
      check_backup_dest_exist(query_cur)
      check_observer_status(query_cur)
      check_schema_status(query_cur)
      check_server_version(query_cur)
      check_not_supported_tenant_name(query_cur)
      check_tenant_clone_job_exist(query_cur)
      check_tenant_snapshot_task_exist(query_cur)
      check_log_transport_compress_func(query_cur)
      check_table_compress_func(query_cur)
      check_table_api_transport_compress_func(query_cur)
      check_variable_binlog_row_image(query_cur)
      check_oracle_standby_replication_exist(query_cur)
      check_disk_space_for_mds_sstable_compat(query_cur)
      check_cs_encoding_arch_dependency_compatiblity(query_cur, cpu_arch)
      # all check func should execute before check_fail_list
      check_direct_load_job_exist(cur, query_cur)
      check_fail_list()
      modify_server_permanent_offline_time(cur)
    except Exception as e:
      logging.exception('run error')
      raise
    finally:
      cur.close()
      conn.close()
  except mysql.connector.Error as e:
    logging.exception('connection error')
    raise
  except Exception as e:
    logging.exception('normal error')
    raise

if __name__ == '__main__':
  upgrade_params = UpgradeParams()
  change_opt_defult_value('log-file', upgrade_params.log_filename)
  parse_options(sys.argv[1:])
  if not has_no_local_opts():
    deal_with_local_opts()
  else:
    check_db_client_opts()
    log_filename = get_opt_log_file()
    upgrade_params.log_filename = log_filename
    # Log configuration is placed here to prevent previous operations from overwriting the log file
    config_logging_module(upgrade_params.log_filename)
    try:
      host = get_opt_host()
      port = int(get_opt_port())
      user = get_opt_user()
      password = get_opt_password()
      timeout = int(get_opt_timeout())
      cpu_arch = get_opt_cpu_arch()
      logging.info('parameters from cmd: host=\"%s\", port=%s, user=\"%s\", password=\"%s\", timeout=\"%s\", log-file=\"%s\"',\
          host, port, user, password.replace('"', '\\"'), timeout, log_filename)
      do_check(host, port, user, password, timeout, upgrade_params, cpu_arch)
    except mysql.connector.Error as e:
      logging.exception('mysql connctor error')
      raise
    except Exception as e:
      logging.exception('normal error')
      raise
