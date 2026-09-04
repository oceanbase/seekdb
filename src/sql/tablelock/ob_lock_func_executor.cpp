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
#include "sql/tablelock/ob_lock_func_executor.h"

#include "data_plane/tablelock/ob_session_table_lock.h"
#include "sql/engine/ob_exec_context.h"

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

  OZ (ObLockContext::valid_execute_context(ctx));
  OV (OB_NOT_NULL(sess), OB_ERR_UNEXPECTED);
  if (OB_SUCC(ret)) {
    const data_plane::ObSessionLockOwner owner(
        sess->get_server_sid(), sess->get_sess_create_time());
    OZ (data_plane::acquire_named_lock(lock_name, owner, timeout_us));
    if (OB_SUCC(ret)) {
      mark_lock_session_(sess, true);
    }
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

  OZ (ObLockContext::valid_execute_context(ctx));
  OV (OB_NOT_NULL(sess), OB_ERR_UNEXPECTED);
  if (OB_SUCC(ret)) {
    const data_plane::ObSessionLockOwner owner(
        sess->get_server_sid(), sess->get_sess_create_time());
    OZ (data_plane::release_named_lock(lock_name, owner, release_cnt));
    OZ (data_plane::session_has_named_locks(owner, has_lock));
    if (OB_SUCC(ret) && !has_lock) {
      mark_lock_session_(sess, false);
    }
  }
  return ret;
}

int ObReleaseAllLockExecutor::execute(ObExecContext &ctx,
                                      int64_t &release_cnt)
{
  int ret = OB_SUCCESS;
  ObSQLSessionInfo *sess = ctx.get_my_session();

  OZ (ObLockContext::valid_execute_context(ctx));
  OV (OB_NOT_NULL(sess), OB_ERR_UNEXPECTED);
  if (OB_SUCC(ret)) {
    const data_plane::ObSessionLockOwner owner(
        sess->get_server_sid(), sess->get_sess_create_time());
    OZ (data_plane::release_all_named_locks(owner, release_cnt));
    if (OB_SUCC(ret)) {
      mark_lock_session_(sess, false);
    }
  }
  return ret;
}

int ObISFreeLockExecutor::execute(ObExecContext &ctx,
                                  const ObString &lock_name)
{
  int ret = OB_SUCCESS;
  bool is_free = false;

  OZ (ObLockContext::valid_execute_context(ctx));
  OZ (data_plane::named_lock_is_free(lock_name, is_free));
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
  OZ (ObLockContext::valid_execute_context(ctx));
  OZ (data_plane::get_named_lock_owner_session(lock_name, sess_id));
  return ret;
}

} // namespace tablelock
} // namespace transaction
} // namespace oceanbase
