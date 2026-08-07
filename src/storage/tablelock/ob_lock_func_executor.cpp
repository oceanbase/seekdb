/*
 * Copyright (c) 2025 OceanBase.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#define USING_LOG_PREFIX TABLELOCK
#include "storage/tablelock/ob_lock_func_executor.h"
#include "share/rc/ob_module_provider.h"

#include "sql/engine/ob_exec_context.h"
#include "storage/tablelock/ob_table_lock_service.h"

namespace oceanbase
{
using namespace sql;
using namespace transaction;
using namespace common;
using namespace observer;

namespace transaction
{

namespace tablelock
{
int ObGetLockExecutor::execute(ObExecContext &ctx,
                               const ObString &lock_name,
                               const int64_t timeout_us)
{
  int ret = OB_SUCCESS;
  ObSQLSessionInfo *sess = ctx.get_my_session();
  ObTableLockOwnerID owner_id;
  ObTableLockService *lock_service = share::g_mp->table_lock_service();

  OZ (ObLockContext::valid_execute_context(ctx));
  OV (OB_NOT_NULL(sess), OB_ERR_UNEXPECTED);
  OV (OB_NOT_NULL(lock_service), OB_ERR_UNEXPECTED);
  OZ (owner_id.convert_from_session_id(sess->get_server_sid(), sess->get_sess_create_time()));
  OZ (lock_service->get_named_lock_manager().acquire(lock_name, owner_id, timeout_us));
  if (OB_SUCC(ret)) {
    mark_lock_session_(sess, true);
  }

  return ret;
}

int ObReleaseLockExecutor::execute(ObExecContext &ctx,
                                   const ObString &lock_name,
                                   int64_t &release_cnt)
{
  int ret = OB_SUCCESS;
  bool has_lock = false;
  ObSQLSessionInfo *sess = ctx.get_my_session();
  ObTableLockOwnerID owner_id;
  ObTableLockService *lock_service = share::g_mp->table_lock_service();

  OZ (ObLockContext::valid_execute_context(ctx));
  OV (OB_NOT_NULL(sess), OB_ERR_UNEXPECTED);
  OV (OB_NOT_NULL(lock_service), OB_ERR_UNEXPECTED);
  OZ (owner_id.convert_from_session_id(sess->get_server_sid(), sess->get_sess_create_time()));
  OZ (lock_service->get_named_lock_manager().release(lock_name, owner_id, release_cnt));
  OZ (lock_service->get_named_lock_manager().has_lock(owner_id, has_lock));
  if (OB_SUCC(ret) && !has_lock) {
    mark_lock_session_(sess, false);
  }
  return ret;
}

int ObReleaseAllLockExecutor::execute(ObExecContext &ctx,
                                      int64_t &release_cnt)
{
  int ret = OB_SUCCESS;
  ObSQLSessionInfo *sess = ctx.get_my_session();
  ObTableLockOwnerID owner_id;
  ObTableLockService *lock_service = share::g_mp->table_lock_service();

  OZ (ObLockContext::valid_execute_context(ctx));
  OV (OB_NOT_NULL(sess), OB_ERR_UNEXPECTED);
  OV (OB_NOT_NULL(lock_service), OB_ERR_UNEXPECTED);
  OZ (owner_id.convert_from_session_id(sess->get_server_sid(), sess->get_sess_create_time()));
  OZ (lock_service->get_named_lock_manager().release_all(owner_id, release_cnt));
  if (OB_SUCC(ret)) {
    mark_lock_session_(sess, false);
  }
  return ret;
}

int ObISFreeLockExecutor::execute(ObExecContext &ctx,
                                  const ObString &lock_name)
{
  int ret = OB_SUCCESS;
  bool is_free = false;
  ObTableLockService *lock_service = share::g_mp->table_lock_service();

  OZ (ObLockContext::valid_execute_context(ctx));
  OV (OB_NOT_NULL(lock_service), OB_ERR_UNEXPECTED);
  OZ (lock_service->get_named_lock_manager().is_free(lock_name, is_free));
  if (OB_SUCC(ret) && is_free) {
    ret = OB_EMPTY_RESULT;
  }
  return ret;
}

int ObISUsedLockExecutor::execute(ObExecContext &ctx,
                                  const ObString &lock_name,
                                  uint32_t &sess_id)
{
  int ret = OB_SUCCESS;
  ObTableLockOwnerID lock_owner;
  ObTableLockService *lock_service = share::g_mp->table_lock_service();

  OZ (ObLockContext::valid_execute_context(ctx));
  OV (OB_NOT_NULL(lock_service), OB_ERR_UNEXPECTED);
  OZ (lock_service->get_named_lock_manager().get_owner(lock_name, lock_owner));
  OZ (lock_owner.convert_to_sessid(sess_id));
  return ret;
}


} // tablelock
} // transaction
} // oceanbase
