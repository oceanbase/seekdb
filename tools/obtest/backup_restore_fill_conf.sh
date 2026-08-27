#!/bin/bash

if [ -f agent/agentserver.conf.template ];then
  echo "Changing configuration file agentserver.conf.template"
  #META related
  sed -ri "s/(metadb_user_name)[^\n]*/\1=root/" agent/agentserver.conf.template 1>/dev/null 2>&1
  sed -ri "s/(metadb_user_passwd)[^\n]*/\1=/" agent/agentserver.conf.template 1>/dev/null 2>&1
  sed -ri "s/(metadb_database_name)[^\n]*/\1=oceanbase/" agent/agentserver.conf.template 1>/dev/null 2>&1
  #backup database related
  sed -ri "s/(backupdb_user_name)[^\n]*/\1=backup/" agent/agentserver.conf.template 1>/dev/null 2>&1
  sed -ri "s/(backupdb_user_passwd)[^\n]*/\1=oceanbase_backup/" agent/agentserver.conf.template 1>/dev/null 2>&1
  sed -ri "s/(max_inc_backup_data_upload_interval)[^\n]*/\1=2/" agent/agentserver.conf.template 1>/dev/null 2>&1
else
  echo "agent/agentserver.conf.template doesn't exist. Run copy.sh before this script."
  exit 1;
fi

if [ -f agent/agentrestore.conf.template ];then
  echo "Changing configuration file agentrestore.conf.template"
  #restore related
  sed -ri "s/(ocpMetaDb.encryptedPassword)[^\n]*/\1=/" agent/agentrestore.conf.template 1>/dev/null 2>&1
  sed -ri "s/(ocpMetaDb.tenantName)[^\n]*/\1=sys/" agent/agentrestore.conf.template 1>/dev/null 2>&1
  sed -ri "s/(ocpMetaDb.userName)[^\n]*/\1=root/" agent/agentrestore.conf.template 1>/dev/null 2>&1
  sed -ri "s/(ocpMetaDb.dbName)[^\n]*/\1=oceanbase/" agent/agentrestore.conf.template 1>/dev/null 2>&1
else
  echo "agent/agentrestore.conf.template doesn't exist. Run copy.sh before this script."
  exit 1;
fi

export LD_LIBRARY_PATH=agent:$LD_LIBRARY_PATH

bin/observer -V

if [ -f agent/agentserver ]; then
  agent/agentserver -V
fi

if [ -f agent/log4j.properties ]; then
  echo "Changing log level "
  sed -ri "s/INFO/DEBUG/g" agent/log4j.properties 1>/dev/null 2>&1
  sed -ri "s/info/debug/g" agent/log4j.properties 1>/dev/null 2>&1
fi
