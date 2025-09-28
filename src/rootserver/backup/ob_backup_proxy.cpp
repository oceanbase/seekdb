/**
 * Copyright (c) 2022 OceanBase
 * OceanBase CE is licensed under Mulan PubL v2.
 * You can use this software according to the terms and conditions of the Mulan PubL v2.
 * You may obtain a copy of Mulan PubL v2 at:
 *          http://license.coscl.org.cn/MulanPubL-2.0
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
 * EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
 * MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PubL v2 for more details.
 */

#define USING_LOG_PREFIX RS

#include "ob_backup_proxy.h"
#include "ob_archive_scheduler_service.h"

using namespace oceanbase;
using namespace rootserver;
using namespace obrpc;

int ObBackupServiceProxy::handle_backup_database(const obrpc::ObBackupDatabaseArg &arg)
{
  int ret = OB_NOT_SUPPORTED;
  return ret;
}

int ObBackupServiceProxy::handle_backup_database_cancel(const obrpc::ObBackupManageArg &arg)
{
  int ret = OB_NOT_SUPPORTED;
  return ret;
}

int ObBackupServiceProxy::handle_backup_delete(const obrpc::ObBackupCleanArg &arg)
{
  int ret = OB_NOT_SUPPORTED;
  return ret;
}

int ObBackupServiceProxy::handle_delete_policy(const obrpc::ObDeletePolicyArg &arg)
{
  int ret = OB_NOT_SUPPORTED;
  return ret;
}

int ObBackupServiceProxy::handle_archive_log(const obrpc::ObArchiveLogArg &arg)
{
  int ret = OB_NOT_SUPPORTED;
  return ret;
}
