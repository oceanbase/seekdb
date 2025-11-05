#!/usr/bin/env python
# -*- coding: utf-8 -*-

from my_error import MyError
import sys
import mysql.connector
from mysql.connector import errorcode
import logging
import json
import config
import opts
import run_modules
import actions
import upgrade_health_checker
import tenant_upgrade_action
import upgrade_post_checker
import re
# Due to the use of /*+read_consistency(WEAK) */ for querying, tenant creation or deletion cannot be allowed during the upgrade period

class UpgradeParams:
  log_filename = config.post_upgrade_log_filename
  sql_dump_filename = config.post_upgrade_sql_filename
  rollback_sql_filename =  config.post_upgrade_rollback_sql_filename

class PasswordMaskingFormatter(logging.Formatter):
  def format(self, record):
    s = super(PasswordMaskingFormatter, self).format(record)
    return re.sub(r'password="(?:[^"\\]|\\.)*"', 'password="******"', s)

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

def print_stats():
  logging.info('==================================================================================')
  logging.info('============================== STATISTICS BEGIN ==================================')
  logging.info('==================================================================================')
  logging.info('succeed run sql(except sql of special actions): \n\n%s\n', actions.get_succ_sql_list_str())
  logging.info('commited sql(except sql of special actions): \n\n%s\n', actions.get_commit_sql_list_str())
  logging.info('==================================================================================')
  logging.info('=============================== STATISTICS END ===================================')
  logging.info('==================================================================================')

def do_upgrade(my_host, my_port, my_user, my_passwd, timeout, my_module_set, upgrade_params):
  try:
    conn = mysql.connector.connect(user = my_user,
                                   password = my_passwd,
                                   host = my_host,
                                   port = my_port,
                                   database = 'oceanbase',
                                   raise_on_warnings = True)
    cur = conn.cursor(buffered=True)
    try:
      query_cur = actions.QueryCursor(cur)
      actions.check_server_version_by_cluster(cur)
      conn.commit()

      if run_modules.MODULE_HEALTH_CHECK in my_module_set:
        logging.info('================begin to run health check action ===============')
        upgrade_health_checker.do_check(my_host, my_port, my_user, my_passwd, upgrade_params, timeout, False)  # need_check_major_status = False
        logging.info('================succeed to run health check action ===============')

      if run_modules.MODULE_END_ROLLING_UPGRADE in my_module_set:
        logging.info('================begin to run end rolling upgrade action ===============')
        conn.autocommit = True
        actions.do_end_rolling_upgrade(cur, timeout)
        conn.autocommit = False
        actions.refresh_commit_sql_list()
        logging.info('================succeed to run end rolling upgrade action ===============')

      if run_modules.MODULE_TENANT_UPRADE in my_module_set:
        logging.info('================begin to run tenant upgrade action ===============')
        conn.autocommit = True
        tenant_upgrade_action.do_upgrade(conn, cur, timeout, my_user, my_passwd)
        conn.autocommit = False
        actions.refresh_commit_sql_list()
        logging.info('================succeed to run tenant upgrade action ===============')

      if run_modules.MODULE_END_UPRADE in my_module_set:
        logging.info('================begin to run end upgrade action ===============')
        conn.autocommit = True
        actions.do_end_upgrade(cur, timeout)
        conn.autocommit = False
        actions.refresh_commit_sql_list()
        logging.info('================succeed to run end upgrade action ===============')

      if run_modules.MODULE_POST_CHECK in my_module_set:
        logging.info('================begin to run post check action ===============')
        conn.autocommit = True
        upgrade_post_checker.do_check(conn, cur, query_cur, timeout)
        conn.autocommit = False
        actions.refresh_commit_sql_list()
        logging.info('================succeed to run post check action ===============')

    except Exception as e:
      logging.exception('run error')
      raise
    finally:
      # Print statistics information
      print_stats()
      # Write the rollback sql to the file
      # actions.dump_rollback_sql_to_file(upgrade_params.rollback_sql_filename)
      cur.close()
      conn.close()
  except mysql.connector.Error as e:
    logging.exception('connection error')
    raise
  except Exception as e:
    logging.exception('normal error')
    raise

def do_upgrade_by_argv(argv):
  upgrade_params = UpgradeParams()
  opts.change_opt_defult_value('log-file', upgrade_params.log_filename)
  opts.parse_options(argv)
  if not opts.has_no_local_opts():
    opts.deal_with_local_opts('upgrade_post')
  else:
    opts.check_db_client_opts()
    log_filename = opts.get_opt_log_file()
    upgrade_params.log_filename = log_filename
    # Log configuration is placed here to prevent previous operations from overwriting the log file
    config_logging_module(upgrade_params.log_filename)
    try:
      host = opts.get_opt_host()
      port = int(opts.get_opt_port())
      user = opts.get_opt_user()
      password = opts.get_opt_password()
      timeout = int(opts.get_opt_timeout())
      cmd_module_str = opts.get_opt_module()
      module_set = set([])
      all_module_set = run_modules.get_all_module_set()
      cmd_module_list = cmd_module_str.split(',')
      for cmd_module in cmd_module_list:
        if run_modules.ALL_MODULE == cmd_module:
          module_set = module_set | all_module_set
        elif cmd_module in all_module_set:
          module_set.add(cmd_module)
        else:
          raise MyError('invalid module: {0}'.format(cmd_module))
      logging.info('parameters from cmd: host=\"%s\", port=%s, user=\"%s\", password=\"%s\", timeout=\"%s\", module=\"%s\", log-file=\"%s\"',\
          host, port, user, password.replace('"', '\\"'), timeout, module_set, log_filename)
      do_upgrade(host, port, user, password, timeout, module_set, upgrade_params)
    except mysql.connector.Error as e:
      logging.exception('mysql connctor error')
      logging.exception('run error, maybe you can reference ' + upgrade_params.rollback_sql_filename + ' to rollback it')
      raise 
    except Exception as e:
      logging.exception('normal error')
      logging.exception('run error, maybe you can reference ' + upgrade_params.rollback_sql_filename + ' to rollback it')
      raise



