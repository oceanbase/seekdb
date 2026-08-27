#include "share/ob_dml_sql_splicer.h"
#include "share/ob_table_access_helper.h"
#include "share/rc/ob_server_runtime.h"
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
#include "storage/tablelock/ob_table_lock_live_detector.h"
#include "storage/tablelock/ob_table_lock_service.h"
#include "query/session/ob_deadlock_session.h"
#include "query/tablelock/ob_table_lock_runtime.h"

namespace oceanbase
{
using namespace share;
namespace transaction
{
namespace tablelock
{
int ObTableLockDetectFuncList::detect_session_alive(const uint32_t session_id, bool &is_alive)
{
  int ret = OB_SUCCESS;
  ObTableLockService *lock_service =
      ::oceanbase::share::server_service<::oceanbase::transaction::tablelock::ObTableLockService>();
  if (OB_ISNULL(lock_service)) {
    ret = OB_NOT_INIT;
    LOG_WARN("table lock service is not installed", K(ret));
  } else {
    ret = query::is_session_alive(
        lock_service->get_deadlock_session_service(),
        session_id,
        is_alive);
  }
  return ret;
}

int ObTableLockDetectFuncList::do_session_alive_detect(common::ObISQLClient &sql_client)
{
  int ret = OB_SUCCESS;
  ObArray<ObTableLockOwnerID *> owner_ids;
  bool session_alive = true;
  ObTableLockOwnerID owner_id;
  uint32_t session_id = common::INVALID_SESSID;
  ObArenaAllocator allocator;

  if (OB_FAIL(get_owner_id_list_from_table_(sql_client, allocator, owner_ids))) {
  } else {
    for (int64_t i = 0; i < owner_ids.count() && OB_SUCC(ret); i++) {
      session_alive = true;
      owner_id = *owner_ids.at(i);
      if (!owner_id.is_valid()) {
        ret = OB_INVALID_ARGUMENT;
        LOG_WARN("owner_id is invalid", K(ret), K(owner_id));
      } else if (OB_FAIL(owner_id.convert_to_sessid(session_id))) {
      } else if (OB_FAIL(detect_session_alive(session_id, session_alive))) {
      } else if (!session_alive) {
        LOG_INFO(
          "find session is not alive, we will clean all recodrs of it later", K(ret), K(session_id), K(owner_id));
        ObTableLockDetector::remove_lock_by_owner_id(owner_id);
      }
    }
  }
  for (int64_t i = 0; i < owner_ids.count(); i++) {
    ObTableLockOwnerID *ptr = owner_ids.at(i);
    if (OB_ISNULL(ptr)) {
      LOG_ERROR_RET(OB_ERR_UNEXPECTED, "the owner_id should not be null", K(ret));
    } else {
      ptr->~ObTableLockOwnerID();
      allocator.free(ptr);
    }
  }
  return ret;
}

int ObTableLockDetectFuncList::get_owner_id_list_from_table_(common::ObISQLClient &sql_client,
                                                             ObIAllocator &allocator,
                                                             ObArray<ObTableLockOwnerID *> &owner_ids)
{
  int ret = OB_SUCCESS;
  char table_name[OB_MAX_TABLE_NAME_BUF_LENGTH] = {0};
  char where_cond[64] = {"WHERE detect_func_no = 1 GROUP BY owner_id"};
  void *ptr = nullptr;
  ObTableLockOwnerID *new_owner_id = nullptr;

  if (OB_FAIL(ObTableLockDetector::get_table_name(table_name))) {
  } else {
    ObArray<ObTuple<int64_t, int64_t>> tmp_owner_ids;
    if (OB_FAIL(ObTableAccessHelper::read_multi_row(
            sql_client, {"owner_type", "owner_id"}, table_name, where_cond, tmp_owner_ids))) {
      if (OB_ITER_END == ret) {
        ret = OB_SUCCESS;
      } else {
        LOG_WARN("read from inner table __all_detect_lock_info_v2 failed", K(ret));
      }
    } else {
      for (int64_t i = 0; i < tmp_owner_ids.count() && OB_SUCC(ret); i++) {
        if (OB_ISNULL(ptr = allocator.alloc(sizeof(ObTableLockOwnerID)))) {
          ret = OB_ALLOCATE_MEMORY_FAILED;
          LOG_WARN("allocate memory for ObTableLockOwnerID failed", K(ret), K(owner_ids.at(i)));
        } else if (FALSE_IT(new_owner_id = new (ptr) ObTableLockOwnerID(static_cast<unsigned char>(tmp_owner_ids.at(i).element<0>()), tmp_owner_ids.at(i).element<1>()))) {
        } else if (OB_FAIL(owner_ids.push_back(new_owner_id))) {
        }
        if (OB_FAIL(ret) && OB_NOT_NULL(new_owner_id)) {
          new_owner_id->~ObTableLockOwnerID();
          allocator.free(ptr);
        }
      }
    }
  }
  return ret;
}

ObTableLockDetectFunc<common::ObISQLClient &> ObTableLockDetector::func1(
    DETECT_SESSION_ALIVE, ObTableLockDetectFuncList::do_session_alive_detect);

const char *ObTableLockDetector::detect_columns[8] = {
  "task_type", "obj_type", "obj_id", "lock_mode", "owner_id", "cnt", "detect_func_no", "detect_func_param"};

int ObTableLockDetector::record_detect_info_to_inner_table(share::ObILockMetadataSession &session_io,
                                                           const ObTableLockTaskType &task_type,
                                                           const ObLockRequest &lock_req,
                                                           const bool for_dbms_lock,
                                                           bool &need_record_to_lock_table)
{
  int ret = OB_SUCCESS;
  bool is_existed = false;

  need_record_to_lock_table = true;
  if (!(LOCK_OBJECT == task_type || LOCK_TABLE == task_type)) {
    ret = OB_NOT_SUPPORTED;
    LOG_WARN("do not support detect task type", K(ret), K(task_type));
  } else if (OB_UNLIKELY(!session_io.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("session inner SQL is invalid", K(ret), K(session_io.server_session_id()));
  } else if (for_dbms_lock
             && OB_FAIL(check_lock_exist_in_inner_table(session_io, task_type, lock_req, is_existed))) {
    LOG_WARN("check dbms_lock record exist failed", K(ret), K(task_type), K(lock_req));
  }

  if (OB_FAIL(ret)) {
  } else if (for_dbms_lock && is_existed) {
    need_record_to_lock_table = false;
  } else if (OB_FAIL(record_detect_info_to_inner_table_(
               session_io, task_type, lock_req, need_record_to_lock_table))) {
  }

  return ret;
}

int ObTableLockDetector::remove_detect_info_from_inner_table(share::ObILockMetadataSession &session_io,
                                                             const ObTableLockTaskType &task_type,
                                                             const ObLockRequest &lock_req,
                                                             bool &need_remove_from_lock_table)
{
  int ret = OB_SUCCESS;
  char full_table_name[OB_MAX_TABLE_NAME_BUF_LENGTH];
  int64_t cnt = 0;
  share::ObDMLSqlSplicer dml;

  // Only when delete_record successfully, it needs to be removed from lock_table.
  // So we initialize it to false here, if delete failed, we will try to removed
  // it next time.
  need_remove_from_lock_table = false;
  if (OB_UNLIKELY(!session_io.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("session inner SQL is invalid", K(ret), K(session_io.server_session_id()));
  } else if (OB_FAIL(get_cnt_of_lock_(session_io, task_type, lock_req, cnt))) {
  } else if (OB_FAIL(
               get_table_name_and_dml_with_pk_column_(task_type, lock_req, full_table_name, dml))) {
  } else if (cnt <= 1) {
    if (cnt <= 0) {
      LOG_WARN("the reocrd in __all_detect_lock_info_v2 didn't remove before", K(lock_req));
    } else if (OB_FAIL(delete_record_(full_table_name, session_io, dml))) {
    } else {
      need_remove_from_lock_table = true;
    }
  } else {
    if (OB_FAIL(update_cnt_of_lock_(full_table_name, session_io, dml))) {
    } else {
      need_remove_from_lock_table = false;
    }
  }

  // didn't get any lock in old and new tables, should return OB_EMPTY_RESULT to make release_lock return NULL
  if (cnt == 0) {
    ret = OB_EMPTY_RESULT;
  }
  return ret;
}

int ObTableLockDetector::remove_detect_info_from_inner_table(share::ObILockMetadataSession &session_io,
                                                             const ObTableLockTaskType &task_type,
                                                             const ObLockRequest &lock_req,
                                                             int64_t &cnt)
{
  int ret = OB_SUCCESS;
  int64_t cnt_in_new_table = 0;

  if (OB_UNLIKELY(!session_io.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("session inner SQL is invalid", K(ret), K(session_io.server_session_id()));
  } else if (OB_FAIL(get_cnt_of_lock_(session_io, task_type, lock_req, cnt_in_new_table))) {
  } else if (OB_FAIL(remove_detect_info_from_table_(session_io, task_type, lock_req, cnt_in_new_table, cnt))) {
  }
  return ret;
}

int ObTableLockDetector::do_detect_and_clear(common::ObISQLClient &sql_client)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(func1.call_function_directly(sql_client))) {
  }
  remove_expired_lock_id(sql_client);

  return ret;
}

int ObTableLockDetector::remove_lock_by_owner_id(const ObTableLockOwnerID &owner_id)
{
  int ret = query::release_locks_for_dead_owner(owner_id.type(), owner_id.id());
  if (OB_FAIL(ret)) {
  }
  return ret;
}

int ObTableLockDetector::remove_expired_lock_id(common::ObISQLClient &sql_client)
{
  int ret =OB_SUCCESS;
  char dbms_lock_table_name[OB_MAX_TABLE_NAME_BUF_LENGTH] = {0};
  char new_table_name[OB_MAX_TABLE_NAME_BUF_LENGTH] = {0};
  ObSqlString where_cond;
  ObSqlString detect_table_cond;
  ObSqlString obj_type_cond;
  const int64_t now = ObTimeUtility::current_time();
  // delete 10 rows each time, to avoid causing abnormal delays due to deleting too many rows
  const int delete_limit = 10;

  OZ (databuff_printf(dbms_lock_table_name, OB_MAX_TABLE_NAME_BUF_LENGTH,
                      "%s.%s", OB_SYS_DATABASE_NAME, OB_ALL_DBMS_LOCK_ALLOCATED_TNAME));
  OZ (get_table_name(new_table_name));
  OZ (obj_type_cond.assign_fmt(" WHERE obj_type = %d or obj_type = %d",
                               static_cast<int>(ObLockOBJType::OBJ_TYPE_MYSQL_LOCK_FUNC),
                               static_cast<int>(ObLockOBJType::OBJ_TYPE_DBMS_LOCK)));

  OZ (detect_table_cond.assign_fmt("SELECT obj_id FROM %s", new_table_name));
  OZ (detect_table_cond.append(obj_type_cond.ptr(), obj_type_cond.length()));
  OZ (where_cond.assign_fmt("expiration <= usec_to_time(%" PRId64 ")"
                            "AND lockid NOT IN"
                            "( %s )"
                            " LIMIT %d",
                            now,
                            detect_table_cond.ptr(),
                            delete_limit));
  OZ (ObTableAccessHelper::delete_row(sql_client, dbms_lock_table_name, where_cond.string()));
  return ret;
}

int ObTableLockDetector::check_lock_id_exist_in_inner_table(share::ObILockMetadataSession &session_io,
                                                            const uint64_t &obj_id,
                                                            const ObLockOBJType &obj_type,
                                                            bool &exist)
{
  int ret = OB_SUCCESS;
  ObSqlString where_cond;
  if (OB_UNLIKELY(!session_io.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("session inner SQL is invalid", K(ret), K(session_io.server_session_id()));
  } else if (OB_FAIL(where_cond.assign_fmt("obj_id = %" PRIu64 " AND obj_type = %d", obj_id, static_cast<int>(obj_type)))) {
  } else if (OB_FAIL(check_lock_exist_(session_io, where_cond, exist))) {
  }
  return ret;
}

int ObTableLockDetector::check_lock_owner_exist_in_inner_table(
                                                               share::ObILockMetadataSession &session_io,
                                                               const uint32_t session_id,
                                                               const uint64_t session_create_ts,
                                                               bool &exist)
{
  int ret = OB_SUCCESS;
  ObSqlString where_cond;

  if (OB_UNLIKELY(!session_io.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
  } else if (session_create_ts <= 0) {
    // if session_create_ts <= 0, means there's no accurate session_create_ts
    // (from lock live detector), so we only judge session_id in this situation
    OZ (where_cond.assign_fmt(
        "(owner_id & %" PRId64 ") = %" PRIu32, ObTableLockOwnerID::SESS_ID_MASK, session_id));
    OZ (check_lock_exist_(session_io, where_cond, exist));
  } else {
    ObTableLockOwnerID lock_owner;
    OZ (lock_owner.convert_from_session_id(session_id, session_create_ts));
    OZ (check_lock_exist_(session_io, where_cond, lock_owner, exist));
  }
  return ret;
}

int ObTableLockDetector::check_lock_exist_in_inner_table(share::ObILockMetadataSession &session_io,
                                                         const ObTableLockTaskType &task_type,
                                                         const ObLockRequest &lock_req,
                                                         bool &exist)
{
  int ret = OB_SUCCESS;
  ObSqlString where_cond;
  uint64_t obj_type = static_cast<uint64_t>(ObLockOBJType::OBJ_TYPE_INVALID);
  uint64_t obj_id = OB_INVALID_ID;

  if (OB_UNLIKELY(!session_io.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
  } else if (LOCK_OBJECT != task_type) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("the task_type for DBMS_LOCK should be LOCK_OBJECT", K(ret), K(task_type), K(lock_req));
  } else {
    const ObLockObjsRequest &arg = static_cast<const ObLockObjsRequest &>(lock_req);
    if (arg.objs_.count() > 1) {
      ret = OB_NOT_SUPPORTED;
      LOG_WARN("do not support detect batch lock obj request right now", K(arg));
    } else {
      obj_type = static_cast<uint64_t>(arg.objs_[0].obj_type_);
      obj_id = arg.objs_[0].obj_id_;
    }
  }

  if (OB_FAIL(ret)) {
  // to ensure subsequent SQL queries, we do not check the owner_type column
  } else if (OB_FAIL(where_cond.assign_fmt("task_type = %d AND obj_type = %" PRIu64 " AND obj_id = %" PRIu64,
                                           static_cast<int>(task_type),
                                           obj_type,
                                           obj_id))) {
  } else if (OB_FAIL(check_lock_exist_(session_io, where_cond, lock_req.owner_id_, exist))) {
  }

  return ret;
}

int ObTableLockDetector::get_lock_owner_by_lock_id(common::ObISQLClient &sql_client,
                                                   const uint64_t &lock_id,
                                                   ObTableLockOwnerID &lock_owner)
{
  int ret = OB_SUCCESS;
  ObSqlString where_cond;

  char table_name[OB_MAX_TABLE_NAME_BUF_LENGTH] = {0};
  int64_t owner_id = 0;
  int64_t owner_type = 0;

  OZ (where_cond.assign_fmt("WHERE obj_type = '%d' AND"
                            " obj_id = %ld AND lock_mode = %d",
                            static_cast<int>(ObLockOBJType::OBJ_TYPE_MYSQL_LOCK_FUNC),
                            lock_id,
                            static_cast<int>(EXCLUSIVE)));
  OZ (get_table_name(table_name));
  OZ (ObTableAccessHelper::read_single_row(
      sql_client, {"owner_id", "owner_type"}, table_name, where_cond.string(), owner_id, owner_type));
  OX (lock_owner.convert_from_value(static_cast<ObLockOwnerType>(owner_type), owner_id));

  return ret;
}

int ObTableLockDetector::get_unlock_request_list(share::ObILockMetadataSession &session_io,
                                                 const ObTableLockOwnerID &owner_id,
                                                 const ObTableLockTaskType task_type,
                                                 ObIAllocator &allocator,
                                                 ObIArray<ObLockRequest *> &arg_list)
{
  int ret = OB_SUCCESS;
  ObSqlString sql;
  arg_list.reset();

  if (OB_UNLIKELY(!session_io.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("session inner SQL is invalid", K(ret), K(session_io.server_session_id()));
  } else if (OB_FAIL(generate_get_unlock_request_sql_(owner_id, task_type, sql))) {
  } else {
    SMART_VAR(ObMySQLProxy::MySQLResult, res)
    {
      common::sqlclient::ObMySQLResult *result = NULL;
      if (OB_FAIL(session_io.execute_read(sql, res))) {
      } else if (OB_ISNULL(result = res.get_result())) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("failed to get result", KR(ret));
      } else if (OB_FAIL(get_unlock_request_list_(result, allocator, arg_list))) {
      } else if (OB_FAIL(fill_owner_id_for_unlock_request_(owner_id, arg_list))) {
      }
    }  // end SMART_VAR
  }

  return ret;
}

int ObTableLockDetector::check_lock_exist_(share::ObILockMetadataSession &session_io,
                                           const ObSqlString &where_cond,
                                           const ObTableLockOwnerID &lock_owner,
                                           bool &exist)
{
  int ret = OB_SUCCESS;

  // Only check the V2 table when the owner_id is the new version
  OZ (check_lock_exist_in_table_(session_io, where_cond, lock_owner, exist));
  return ret;
}

int ObTableLockDetector::check_lock_exist_(share::ObILockMetadataSession &session_io,
                                           const ObSqlString &where_cond,
                                           bool &exist)
{
  int ret = OB_SUCCESS;
  char table_name[OB_MAX_TABLE_NAME_BUF_LENGTH] = {0};

  // Only check the V2 table when the owner_id is the new version
  OZ (get_table_name(table_name));
  OZ (check_lock_exist_in_table_(session_io, table_name, where_cond, exist));

  return ret;
}

int ObTableLockDetector::check_lock_exist_in_table_(share::ObILockMetadataSession &session_io,
                                                    const char *table_name,
                                                    const ObSqlString &where_cond,
                                                    bool &exist)
{
  int ret = OB_SUCCESS;
  SMART_VAR(ObMySQLProxy::MySQLResult, res)
  {
    ObSqlString sql;
    common::sqlclient::ObMySQLResult *result = NULL;
    if (OB_FAIL(sql.assign_fmt("SELECT owner_id FROM %s WHERE %s", table_name, where_cond.ptr()))) {
    } else if (OB_FAIL(session_io.execute_read(sql, res))) {
    } else if (OB_ISNULL(result = res.get_result())) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("failed to get result", KR(ret));
    } else if (OB_FAIL(result->next())) {
      if (OB_ITER_END == ret) {
        ret = OB_SUCCESS;
        exist = false;
      } else {
        LOG_WARN("fail to get next", KR(ret));
      }
    } else {
      exist = true;
    }
  }
  return ret;
}

int ObTableLockDetector::check_lock_exist_in_table_(share::ObILockMetadataSession &session_io,
                                                    const ObSqlString &where_cond,
                                                    const ObTableLockOwnerID lock_owner,
                                                    bool &exist)
{
  int ret = OB_SUCCESS;
  char table_name[OB_MAX_TABLE_NAME_BUF_LENGTH] = {0};
  ObSqlString lock_owner_cond;
  ObSqlString new_where_cond;

  OZ (get_table_name(table_name));
  OZ (get_lock_owner_where_cond_(lock_owner, lock_owner_cond));
  OZ (new_where_cond.assign(lock_owner_cond));
  if (!where_cond.empty()) {
    OZ (new_where_cond.append_fmt(" AND %s", where_cond.ptr()));
  }
  OZ (check_lock_exist_in_table_(session_io, table_name, new_where_cond, exist));

  return ret;
}

int ObTableLockDetector::record_detect_info_to_inner_table_(share::ObILockMetadataSession &session_io,
                                                            const ObTableLockTaskType &task_type,
                                                            const ObLockRequest &lock_req,
                                                            bool &need_record_to_lock_table)
{
  int ret = OB_SUCCESS;
  share::ObDMLSqlSplicer dml;
  ObSqlString insert_sql;
  int64_t affected_rows = 0;
  char table_name[OB_MAX_TABLE_NAME_BUF_LENGTH];

  if (OB_FAIL(get_table_name(table_name))) {
  } else if (OB_FAIL(generate_insert_dml_(task_type, lock_req, dml))) {
  } else if (OB_FAIL(dml.splice_insert_sql(table_name, insert_sql))) {
  } else if (OB_FAIL(insert_sql.append(" ON DUPLICATE  KEY UPDATE cnt = cnt + 1"))) {
  } else if (OB_FAIL(session_io.execute_write(insert_sql, affected_rows))) {
  } else if (affected_rows == 2) {
    need_record_to_lock_table = false;
    LOG_INFO("there's the same lock in __all_detect_lock_info_v2 table, no need to record it to the lock table",
             K(lock_req));
  } else if (affected_rows != 1) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("only can affetct 1 row due to insert, or 2 rows due to insert on duplicate key", K(affected_rows));
  }
  return ret;
}

int ObTableLockDetector::generate_insert_dml_(const ObTableLockTaskType &task_type,
                                              const ObLockRequest &lock_req,
                                              share::ObDMLSqlSplicer &dml)
{
  int ret = OB_SUCCESS;
  uint64_t cnt = 1;
  if (!(LOCK_OBJECT == task_type || LOCK_TABLE == task_type)) {
    ret = OB_NOT_SUPPORTED;
    LOG_WARN("do not support detect task type", K(ret), K(task_type));
  } else if (OB_FAIL(add_pk_column_to_dml_(task_type, lock_req, dml))) {
  } else if (OB_FAIL(dml.add_column("cnt", cnt))) {
  } else {
    switch (task_type) {
    case LOCK_OBJECT: {
      const ObLockObjsRequest &arg = static_cast<const ObLockObjsRequest &>(lock_req);
      if (OB_FAIL(dml.add_column("detect_func_no", static_cast<uint64_t>(arg.detect_func_no_)))
          || OB_FAIL(dml.add_column("detect_func_param", ObHexEscapeSqlStr(arg.detect_param_)))) {
        LOG_WARN("add column for insert dml failed", K(ret));
      }
      break;
    }
    case LOCK_TABLE: {
      const ObLockTableRequest &arg = static_cast<const ObLockTableRequest&>(lock_req);
      if (OB_FAIL(dml.add_column("detect_func_no", static_cast<uint64_t>(arg.detect_func_no_)))
          || OB_FAIL(dml.add_column("detect_func_param", ObHexEscapeSqlStr(arg.detect_param_)))) {
        LOG_WARN("add column for insert dml failed", K(ret));
      }
      break;
    }
    default: {
      ret = OB_NOT_SUPPORTED;
      LOG_WARN("do not support detect lock live for the task_type which is not LOCK_OBJECT right now",
               K(task_type),
               K(lock_req));
    }
    }
  }

  return ret;
}

int ObTableLockDetector::add_pk_column_to_dml_(const ObTableLockTaskType &task_type,
                                               const ObLockRequest &lock_req,
                                               share::ObDMLSqlSplicer &dml)
{
  int ret = OB_SUCCESS;
  uint64_t obj_type = static_cast<uint64_t>(ObLockOBJType::OBJ_TYPE_INVALID);
  uint64_t obj_id = OB_INVALID_ID;
  int64_t raw_owner_id = ObTableLockOwnerID::INVALID_ID;

  switch (task_type) {
  case LOCK_OBJECT: {
    const ObLockObjsRequest &arg = static_cast<const ObLockObjsRequest &>(lock_req);
    if (arg.objs_.count() > 1) {
      ret = OB_NOT_SUPPORTED;
      LOG_WARN("do not support detect batch lock obj request right now", K(arg));
    } else {
      obj_type = static_cast<uint64_t>(arg.objs_[0].obj_type_);
      obj_id = arg.objs_[0].obj_id_;
    }
    break;
  }
  case LOCK_TABLE: {
    const ObLockTableRequest &arg = static_cast<const ObLockTableRequest&>(lock_req);
    obj_type = static_cast<uint64_t>(ObLockOBJType::OBJ_TYPE_TABLE);
    obj_id = arg.table_id_;
    break;
  }
  default: {
    ret = OB_NOT_SUPPORTED;
    LOG_WARN("do not support detect lock live for the task_type which is not LOCK_OBJECT right now",
             K(task_type),
             K(lock_req));
  }
  }

  raw_owner_id = lock_req.owner_id_.id();

  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(dml.add_pk_column("task_type", static_cast<uint64_t>(task_type)))
             || OB_FAIL(dml.add_pk_column("obj_type", obj_type))
             || OB_FAIL(dml.add_pk_column("obj_id", obj_id))
             || OB_FAIL(dml.add_pk_column("lock_mode", static_cast<uint64_t>(lock_req.lock_mode_)))
             || OB_FAIL(dml.add_pk_column("owner_id", raw_owner_id))) {
    LOG_WARN("add pk column to dml failed", K(ret), K(lock_req));
  } else {
    if (OB_FAIL(dml.add_pk_column("owner_type", lock_req.owner_id_.type()))) {
    }
  }
  return ret;
}

int ObTableLockDetector::generate_update_sql_(const char *table_name,
                                              const share::ObDMLSqlSplicer &dml,
                                              ObSqlString &sql)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(sql.append_fmt("UPDATE %s SET cnt = cnt - 1 WHERE ", table_name))) {
  } else if (OB_FAIL(dml.splice_predicates(sql))) {
  }
  return ret;
}

int ObTableLockDetector::generate_select_sql_(const char *table_name,
                                              const share::ObDMLSqlSplicer &dml,
                                              ObSqlString &sql)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(sql.append_fmt("SELECT cnt FROM %s WHERE ", table_name))) {
  } else if (OB_FAIL(dml.splice_predicates(sql))) {
  }
  return ret;
}

int ObTableLockDetector::delete_record_(const char *table_name,
                                        share::ObILockMetadataSession &session_io,
                                        const share::ObDMLSqlSplicer &dml)
{
  int ret = OB_SUCCESS;
  ObSqlString sql;
  int64_t affected_rows = 0;
  if (OB_FAIL(dml.splice_delete_sql(table_name, sql))) {
  } else if (OB_FAIL(session_io.execute_write(sql, affected_rows))) {
  } else if (affected_rows != 1) {
    LOG_WARN("do not delete the record", KR(ret), K(sql), K(affected_rows));
  }
  return ret;
}

int ObTableLockDetector::update_cnt_of_lock_(const char *table_name,
                                             share::ObILockMetadataSession &session_io,
                                             const share::ObDMLSqlSplicer &dml)
{

  int ret = OB_SUCCESS;
  ObSqlString sql;
  int64_t affected_rows = 0;
  if (OB_FAIL(generate_update_sql_(table_name, dml, sql))) {
  } else if (OB_FAIL(session_io.execute_write(sql, affected_rows))) {
  } else if (affected_rows != 1) {
    LOG_WARN("do not update the record", K(sql));
  }
  return ret;
}

int ObTableLockDetector::get_cnt_of_lock_(share::ObILockMetadataSession &session_io,
                                          const ObTableLockTaskType &task_type,
                                          const ObLockRequest &lock_req,
                                          int64_t &cnt)
{
  int ret = OB_SUCCESS;

  cnt = 0;
  if (OB_FAIL(get_lock_cnt_in_table_(session_io, task_type, lock_req, cnt))) {
  }

  return ret;
}

int ObTableLockDetector::get_lock_cnt_in_table_(share::ObILockMetadataSession &session_io,
                                                const ObTableLockTaskType &task_type,
                                                const ObLockRequest &lock_req,
                                                int64_t &cnt)
{
  int ret = OB_SUCCESS;
  char table_name[OB_MAX_TABLE_NAME_BUF_LENGTH];
  share::ObDMLSqlSplicer dml;

  if (OB_FAIL(get_table_name_and_dml_with_pk_column_(task_type, lock_req, table_name, dml))) {
  } else if (OB_FAIL(get_lock_cnt_in_table_(session_io, table_name, dml, cnt))) {
  }
  return ret;
}

int ObTableLockDetector::get_lock_cnt_in_table_(share::ObILockMetadataSession &session_io,
                                                const char *table_name,
                                                const share::ObDMLSqlSplicer &dml,
                                                int64_t &cnt)
{
  int ret = OB_SUCCESS;
  SMART_VAR(ObMySQLProxy::MySQLResult, res)
  {
    ObSqlString sql;
    common::sqlclient::ObMySQLResult *result = nullptr;
    if (OB_FAIL(generate_select_sql_(table_name, dml, sql))) {
    } else if (OB_FAIL(session_io.execute_read(sql, res))) {
    } else if (OB_ISNULL(result = res.get_result())) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("failed to get result", KR(ret), K(sql));
    } else if (OB_FAIL(result->next())) {
      if (OB_ITER_END == ret) {
        ret = OB_SUCCESS;
        cnt = 0;
      }
    } else {
      (void)GET_COL_IGNORE_NULL(result->get_int, "cnt", cnt);
    }
  }  // end SMART_VAR
  return ret;
}

int ObTableLockDetector::remove_detect_info_from_table_(share::ObILockMetadataSession &session_io,
                                                        const ObTableLockTaskType &task_type,
                                                        const ObLockRequest &lock_req,
                                                        const int64_t cnt_in_new_table,
                                                        int64_t &real_del_cnt)
{
  int ret = OB_SUCCESS;
  real_del_cnt = 0;

  if (cnt_in_new_table > 0) {
    OZ (remove_detect_info_from_table_(session_io, task_type, lock_req));
  }

  OX (real_del_cnt = cnt_in_new_table);
  return ret;
}

int ObTableLockDetector::remove_detect_info_from_table_(share::ObILockMetadataSession &session_io,
                                                        const ObTableLockTaskType &task_type,
                                                        const ObLockRequest &lock_req)
{
  int ret = OB_SUCCESS;
  char table_name[OB_MAX_TABLE_NAME_BUF_LENGTH];
  ObSqlString delete_sql;
  share::ObDMLSqlSplicer dml;
  int64_t affected_rows = 0;

  OZ (get_table_name_and_dml_with_pk_column_(task_type, lock_req, table_name, dml));
  OZ (dml.splice_delete_sql(table_name, delete_sql));
  OZ (session_io.execute_write(delete_sql, affected_rows));
  return ret;
}

int ObTableLockDetector::get_table_name(char *table_name)
{
  int ret = OB_SUCCESS;
  memset(table_name, 0, OB_MAX_TABLE_NAME_BUF_LENGTH);
  OZ (databuff_printf(
    table_name, OB_MAX_TABLE_NAME_BUF_LENGTH, "%s.%s", OB_SYS_DATABASE_NAME, OB_ALL_DETECT_LOCK_INFO_V2_TNAME));
  return ret;
}

int ObTableLockDetector::get_table_name_and_dml_with_pk_column_(const ObTableLockTaskType &task_type,
                                                                const ObLockRequest &lock_req,
                                                                char *table_name,
                                                                share::ObDMLSqlSplicer &dml)
{
  int ret = OB_SUCCESS;
  memset(table_name, 0, OB_MAX_TABLE_NAME_BUF_LENGTH);
  dml.reset();
  OZ (get_table_name(table_name));
  OZ (add_pk_column_to_dml_(task_type, lock_req, dml));
  return ret;
}

int ObTableLockDetector::get_unlock_request_list_(common::sqlclient::ObMySQLResult *res,
                                                  ObIAllocator &allocator,
                                                  ObIArray<ObLockRequest *> &arg_list)
{
  int ret = OB_SUCCESS;
  ObLockRequest *unlock_arg = nullptr;

  while (OB_SUCC(ret) && OB_SUCC(res->next())) {
    if (OB_FAIL(parse_unlock_request_(*res, allocator, unlock_arg))) {
    } else if (OB_ISNULL(unlock_arg)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("parse unlock request failed", K(ret));
    } else if (OB_FAIL(arg_list.push_back(unlock_arg))) {
    }
    if (OB_FAIL(ret) && OB_NOT_NULL(unlock_arg)) {
      unlock_arg->~ObLockRequest();
      allocator.free(unlock_arg);
    }
  }
  if (OB_ITER_END == ret) {
    ret = OB_SUCCESS;
  }
  return ret;
}

int ObTableLockDetector::generate_get_unlock_request_sql_(const ObTableLockOwnerID &owner_id,
                                                          const ObTableLockTaskType task_type,
                                                          ObSqlString &sql)
{
  int ret = OB_SUCCESS;
  char new_table_name[OB_MAX_TABLE_NAME_BUF_LENGTH] = {0};
  ObSqlString select_sql;
  ObSqlString where_cond;

  sql.reset();
  OZ (select_sql.assign_fmt("SELECT task_type, obj_type, obj_id, lock_mode FROM"));
  OZ (where_cond.assign_fmt(" WHERE"));
  if (LOCK_OBJECT == task_type || LOCK_TABLE == task_type) {
    OZ (where_cond.append_fmt(" task_type = %d AND", static_cast<int>(task_type)));
  }

  OZ (get_table_name(new_table_name));
  OZ (sql.assign_fmt("%s %s", select_sql.ptr(), new_table_name));
  OZ (sql.append_fmt("%s", where_cond.ptr()));
  OZ (sql.append_fmt(" owner_id = %" PRId64 " AND owner_type = %d", owner_id.id(), static_cast<int>(owner_id.type())));


  return ret;
}

int ObTableLockDetector::parse_unlock_request_(common::sqlclient::ObMySQLResult &res,
                                               ObIAllocator &allocator,
                                               ObLockRequest *&arg)
{
  int ret = OB_SUCCESS;
  int64_t task_type = 0;
  int64_t obj_type = 0;
  int64_t obj_id = 0;
  int64_t lock_mode = 0;
  ObLockID lock_id;
  void *ptr = nullptr;

  arg = NULL;

  (void)GET_COL_IGNORE_NULL(res.get_int, "task_type", task_type);
  (void)GET_COL_IGNORE_NULL(res.get_int, "obj_type", obj_type);
  (void)GET_COL_IGNORE_NULL(res.get_int, "obj_id", obj_id);
  (void)GET_COL_IGNORE_NULL(res.get_int, "lock_mode", lock_mode);
  if (OB_FAIL(ret)) {
  } else {
    switch (task_type) {
      case LOCK_TABLE: {
        ObUnLockTableRequest *unlock_arg = NULL;
        if (OB_ISNULL(ptr = allocator.alloc(sizeof(ObUnLockTableRequest)))) {
          ret = OB_EAGAIN;
          LOG_WARN("get unlock request failed", K(ret));
        } else if (FALSE_IT(unlock_arg = new (ptr) ObUnLockTableRequest())) {
        } else {
          unlock_arg->table_id_ = obj_id;
          arg = unlock_arg;
        }
        if (OB_FAIL(ret) && OB_NOT_NULL(unlock_arg)) {
          unlock_arg->~ObUnLockTableRequest();
          allocator.free(ptr);
        }
        break;
      }
      case LOCK_OBJECT: {
        ObUnLockObjsRequest *unlock_arg = NULL;
        bool is_dbms_lock = static_cast<int64_t>(ObLockOBJType::OBJ_TYPE_DBMS_LOCK) == obj_type;
        if (!is_dbms_lock
            && !(static_cast<int64_t>(ObLockOBJType::OBJ_TYPE_MYSQL_LOCK_FUNC) == obj_type
                 && static_cast<int64_t>(EXCLUSIVE) == lock_mode)) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("invalid object type and lock mode", K(ret), K(obj_type), K(lock_mode));
        } else if (OB_ISNULL(ptr = allocator.alloc(sizeof(ObUnLockObjsRequest)))) {
          ret = OB_EAGAIN;
          LOG_WARN("get unlock request failed", K(ret));
        } else if (FALSE_IT(unlock_arg = new (ptr) ObUnLockObjsRequest())) {
        } else if (OB_FAIL(lock_id.set(static_cast<ObLockOBJType>(obj_type), obj_id))) {
        } else if (OB_FAIL(unlock_arg->objs_.push_back(lock_id))) {
        } else {
          arg = unlock_arg;
        }
        if (OB_FAIL(ret) && OB_NOT_NULL(unlock_arg)) {
          unlock_arg->~ObUnLockObjsRequest();
          allocator.free(ptr);
        }
        break;
      }
      default: {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("not supported lock task type", K(ret), K(task_type));
      }
    }
    if (OB_SUCC(ret)) {
      arg->lock_mode_ = lock_mode;
      arg->op_type_ = ObTableLockOpType::OUT_TRANS_UNLOCK;
      arg->timeout_us_ = 0;
      arg->is_from_sql_ = true;
    }
  }
  return ret;
}

int ObTableLockDetector::fill_owner_id_for_unlock_request_(const ObTableLockOwnerID &owner_id, ObIArray<ObLockRequest *> &arg_list)
{
  int ret = OB_SUCCESS;
  for (int64_t i = 0; i < arg_list.count(); i++) {
    arg_list.at(i)->owner_id_ = owner_id;
  }
  return ret;
}

int ObTableLockDetector::get_lock_owner_where_cond_(const ObTableLockOwnerID lock_owner,
                                                    ObSqlString &where_cond)
{
  int ret = OB_SUCCESS;

  where_cond.reset();
  OZ (where_cond.assign_fmt("owner_id = %" PRId64 " AND owner_type = %d", lock_owner.id(), lock_owner.type()));
  return ret;
}

}  // namespace tablelock
}  // namespace transaction
}  // namespace oceanbase
