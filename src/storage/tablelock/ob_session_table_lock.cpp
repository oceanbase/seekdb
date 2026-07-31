/*
 * Copyright (c) 2025 OceanBase.
 * Licensed under the Apache License, Version 2.0 (the "License");
 */

#define USING_LOG_PREFIX TABLELOCK

#include "data_plane/tablelock/ob_session_table_lock.h"

#include "share/rc/ob_server_runtime.h"
#include "storage/ob_common_id_utils.h"
#include "storage/tablelock/ob_table_lock_live_detector.h"
#include "storage/tablelock/ob_table_lock_rpc_struct.h"
#include "storage/tablelock/ob_table_lock_service.h"

namespace oceanbase
{
namespace data_plane
{
namespace
{

using namespace transaction;
using namespace transaction::tablelock;

int make_owner(const ObSessionLockOwner &source, ObTableLockOwnerID &target)
{
  return target.convert_from_session_id(source.session_id_,
                                        source.session_create_ts_);
}

int make_owner(const ObPersistedLockOwner &source, ObTableLockOwnerID &target)
{
  return target.convert_from_value(
      static_cast<ObLockOwnerType>(source.owner_type_), source.owner_id_);
}

ObTableLockTaskType task_type_for_scope(ObSessionLockScope scope)
{
  ObTableLockTaskType task_type = INVALID_LOCK_TASK_TYPE;
  if (ObSessionLockScope::NAMED_LOCK == scope) {
    task_type = LOCK_OBJECT;
  } else if (ObSessionLockScope::TABLE_LOCK == scope) {
    task_type = LOCK_TABLE;
  }
  return task_type;
}

int unlock_request(ObTxDesc &tx,
                   const ObTxParam &tx_param,
                   const ObLockRequest &request)
{
  int ret = common::OB_SUCCESS;
  ObTableLockService *service = ::oceanbase::share::server_service<::oceanbase::transaction::tablelock::ObTableLockService>();
  if (OB_ISNULL(service)) {
    ret = common::OB_NOT_INIT;
  } else {
    switch (request.type_) {
      case ObLockRequest::ObLockMsgType::UNLOCK_OBJ_REQ:
        ret = service->unlock(
            tx, tx_param, static_cast<const ObUnLockObjsRequest &>(request));
        break;
      case ObLockRequest::ObLockMsgType::UNLOCK_TABLE_REQ:
        ret = service->unlock(
            tx, tx_param, static_cast<const ObUnLockTableRequest &>(request));
        break;
      default:
        ret = common::OB_NOT_SUPPORTED;
        break;
    }
  }
  return ret;
}

} // namespace

int acquire_named_lock(share::ObILockMetadataSession &session_io,
                       transaction::ObTxDesc &tx,
                       const transaction::ObTxParam &tx_param,
                       const ObSessionLockOwner &owner,
                       uint64_t lock_id_value,
                       int64_t timeout_us)
{
  int ret = common::OB_SUCCESS;
  bool need_lock = true;
  transaction::tablelock::ObLockID lock_id;
  transaction::tablelock::ObLockObjsRequest request;
  transaction::tablelock::ObTableLockService *service =
      ::oceanbase::share::server_service<::oceanbase::transaction::tablelock::ObTableLockService>();
  request.lock_mode_ = transaction::tablelock::EXCLUSIVE;
  request.op_type_ = transaction::tablelock::OUT_TRANS_LOCK;
  request.timeout_us_ = timeout_us;
  request.is_from_sql_ = true;
  request.detect_func_no_ = transaction::tablelock::DETECT_SESSION_ALIVE;
  if (OB_ISNULL(service)) {
    ret = common::OB_NOT_INIT;
  } else if (OB_FAIL(lock_id.set(
                 transaction::tablelock::ObLockOBJType::OBJ_TYPE_MYSQL_LOCK_FUNC,
                 lock_id_value))) {
    LOG_WARN("set named lock id failed", KR(ret), K(lock_id_value));
  } else if (OB_FAIL(make_owner(owner, request.owner_id_))) {
    LOG_WARN("make named lock owner failed", KR(ret));
  } else if (OB_FAIL(request.objs_.push_back(lock_id))) {
    LOG_WARN("append named lock id failed", KR(ret));
  } else if (OB_FAIL(
                 transaction::tablelock::ObTableLockDetector::
                     record_detect_info_to_inner_table(
                         session_io, transaction::tablelock::LOCK_OBJECT,
                         request, false, need_lock))) {
    LOG_WARN("record named lock failed", KR(ret));
  } else if (need_lock && OB_FAIL(service->lock(tx, tx_param, request))) {
    LOG_WARN("acquire named lock failed", KR(ret), K(lock_id_value));
  }
  return ret;
}

int acquire_mysql_table_lock(share::ObILockMetadataSession &session_io,
                             transaction::ObTxDesc &tx,
                             const transaction::ObTxParam &tx_param,
                             const ObSessionLockOwner &owner,
                             const ObTableLockTarget &target,
                             int64_t timeout_us)
{
  int ret = common::OB_SUCCESS;
  bool need_lock = true;
  transaction::tablelock::ObLockTableRequest request;
  transaction::tablelock::ObTableLockService *service =
      ::oceanbase::share::server_service<::oceanbase::transaction::tablelock::ObTableLockService>();
  request.table_id_ = target.table_id_;
  request.lock_mode_ = target.lock_mode_;
  request.op_type_ = transaction::tablelock::OUT_TRANS_LOCK;
  request.timeout_us_ = timeout_us;
  request.is_from_sql_ = true;
  request.detect_func_no_ = transaction::tablelock::DETECT_SESSION_ALIVE;
  if (OB_ISNULL(service)) {
    ret = common::OB_NOT_INIT;
  } else if (OB_UNLIKELY(transaction::tablelock::NO_LOCK == target.lock_mode_)) {
    ret = common::OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(make_owner(owner, request.owner_id_))) {
    LOG_WARN("make table lock owner failed", KR(ret));
  } else if (OB_FAIL(
                 transaction::tablelock::ObTableLockDetector::
                     record_detect_info_to_inner_table(
                         session_io, transaction::tablelock::LOCK_TABLE,
                         request, false, need_lock))) {
    LOG_WARN("record MySQL table lock failed", KR(ret));
  } else if (need_lock && OB_FAIL(service->lock(tx, tx_param, request))) {
    LOG_WARN("acquire MySQL table lock failed", KR(ret), K(target));
  }
  return ret;
}

int release_named_lock(share::ObILockMetadataSession &session_io,
                       transaction::ObTxDesc &tx,
                       const transaction::ObTxParam &tx_param,
                       const ObSessionLockOwner &owner,
                       uint64_t lock_id_value,
                       int64_t &release_count)
{
  int ret = common::OB_SUCCESS;
  bool need_unlock = false;
  bool lock_exists = false;
  transaction::tablelock::ObLockID lock_id;
  transaction::tablelock::ObUnLockObjsRequest request;
  release_count = -2;
  request.lock_mode_ = transaction::tablelock::EXCLUSIVE;
  request.op_type_ = transaction::tablelock::OUT_TRANS_UNLOCK;
  request.timeout_us_ = 1000 * 1000L;
  request.is_from_sql_ = true;
  if (OB_FAIL(lock_id.set(
          transaction::tablelock::ObLockOBJType::OBJ_TYPE_MYSQL_LOCK_FUNC,
          lock_id_value))) {
  } else if (OB_FAIL(request.objs_.push_back(lock_id))) {
  } else if (OB_FAIL(make_owner(owner, request.owner_id_))) {
  } else if (OB_FAIL(
                 transaction::tablelock::ObTableLockDetector::
                     remove_detect_info_from_inner_table(
                         session_io, transaction::tablelock::LOCK_OBJECT,
                         request, need_unlock))) {
    if (common::OB_EMPTY_RESULT == ret) {
      const int lookup_ret =
          transaction::tablelock::ObTableLockDetector::
              check_lock_id_exist_in_inner_table(
                  session_io, lock_id_value,
                  transaction::tablelock::ObLockOBJType::
                      OBJ_TYPE_MYSQL_LOCK_FUNC,
                  lock_exists);
      if (common::OB_SUCCESS == lookup_ret) {
        release_count = lock_exists ? 0 : -1;
      }
    }
  } else {
    release_count = 1;
    if (need_unlock && OB_FAIL(unlock_request(tx, tx_param, request))) {
      release_count = -2;
    }
  }
  if (-2 != release_count) {
    ret = common::OB_SUCCESS;
  }
  return ret;
}

int release_session_locks(share::ObILockMetadataSession &session_io,
                          transaction::ObTxDesc &tx,
                          const transaction::ObTxParam &tx_param,
                          const ObSessionLockOwner &owner,
                          ObSessionLockScope scope,
                          int64_t &release_count)
{
  transaction::tablelock::ObTableLockOwnerID lock_owner;
  int ret = make_owner(owner, lock_owner);
  if (OB_SUCC(ret)) {
    const ObPersistedLockOwner persisted(lock_owner.type(), lock_owner.id());
    ret = release_persisted_locks(session_io, tx, tx_param, persisted,
                                  scope, release_count);
  }
  return ret;
}

int release_persisted_locks(share::ObILockMetadataSession &session_io,
                            transaction::ObTxDesc &tx,
                            const transaction::ObTxParam &tx_param,
                            const ObPersistedLockOwner &owner,
                            ObSessionLockScope scope,
                            int64_t &release_count)
{
  int ret = common::OB_SUCCESS;
  int tmp_ret = common::OB_SUCCESS;
  int64_t removed = 0;
  transaction::tablelock::ObTableLockOwnerID lock_owner;
  common::ObArenaAllocator allocator(common::ObModIds::OB_SQL_RES_TYPE);
  common::ObArray<transaction::tablelock::ObLockRequest *> requests;
  release_count = 0;
  if (OB_FAIL(make_owner(owner, lock_owner))) {
  } else if (OB_FAIL(
                 transaction::tablelock::ObTableLockDetector::
                     get_unlock_request_list(
                         session_io, lock_owner, task_type_for_scope(scope),
                         allocator, requests))) {
    LOG_WARN("get session unlock requests failed", KR(ret));
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < requests.count(); ++i) {
      transaction::tablelock::ObLockRequest *request = requests.at(i);
      removed = 0;
      if (OB_ISNULL(request)) {
        ret = common::OB_ERR_UNEXPECTED;
      } else if (OB_FAIL(
                     transaction::tablelock::ObTableLockDetector::
                         remove_detect_info_from_inner_table(
                             session_io, task_type_for_scope(scope), *request,
                             removed))) {
        LOG_WARN("remove session lock record failed", KR(ret));
      } else if (OB_FAIL(unlock_request(tx, tx_param, *request))) {
        LOG_WARN("release session lock failed", KR(ret));
      } else {
        release_count += removed;
      }
    }
  }
  for (int64_t i = 0; i < requests.count(); ++i) {
    transaction::tablelock::ObLockRequest *request = requests.at(i);
    if (OB_ISNULL(request)) {
      tmp_ret = common::OB_ERR_UNEXPECTED;
    } else {
      request->~ObLockRequest();
      allocator.free(request);
    }
  }
  if (OB_FAIL(ret)) {
    release_count = -2;
  }
  return ret;
}

int session_has_locks(share::ObILockMetadataSession &session_io,
                      const ObSessionLockOwner &owner,
                      bool &has_locks)
{
  return transaction::tablelock::ObTableLockDetector::
      check_lock_owner_exist_in_inner_table(
          session_io, owner.session_id_,
          owner.session_create_ts_, has_locks);
}

int named_lock_exists(share::ObILockMetadataSession &session_io,
                      uint64_t lock_id,
                      bool &exists)
{
  return transaction::tablelock::ObTableLockDetector::
      check_lock_id_exist_in_inner_table(
          session_io, lock_id,
          transaction::tablelock::ObLockOBJType::OBJ_TYPE_MYSQL_LOCK_FUNC,
          exists);
}

int get_named_lock_owner_session(common::ObISQLClient &sql_client,
                                 uint64_t lock_id,
                                 uint32_t &session_id)
{
  int ret = common::OB_SUCCESS;
  transaction::tablelock::ObTableLockOwnerID owner;
  if (OB_FAIL(transaction::tablelock::ObTableLockDetector::
                  get_lock_owner_by_lock_id(sql_client, lock_id, owner))) {
  } else if (OB_FAIL(owner.convert_to_sessid(session_id))) {
  }
  return ret;
}

int session_lock_owners_equal(const ObSessionLockOwner &left,
                              const ObSessionLockOwner &right,
                              bool &equal)
{
  int ret = common::OB_SUCCESS;
  transaction::tablelock::ObTableLockOwnerID left_owner;
  transaction::tablelock::ObTableLockOwnerID right_owner;
  equal = false;
  if (OB_FAIL(make_owner(left, left_owner))) {
  } else if (OB_FAIL(make_owner(right, right_owner))) {
  } else {
    equal = left_owner == right_owner;
  }
  return ret;
}

int persist_session_lock_owner(const ObSessionLockOwner &owner,
                               ObPersistedLockOwner &persisted)
{
  int ret = common::OB_SUCCESS;
  transaction::tablelock::ObTableLockOwnerID storage_owner;
  if (OB_FAIL(make_owner(owner, storage_owner))) {
  } else {
    persisted.owner_type_ = storage_owner.type();
    persisted.owner_id_ = storage_owner.id();
  }
  return ret;
}

int get_persisted_lock_owner_session(const ObPersistedLockOwner &owner,
                                     uint32_t &session_id)
{
  int ret = common::OB_SUCCESS;
  transaction::tablelock::ObTableLockOwnerID storage_owner;
  if (OB_FAIL(make_owner(owner, storage_owner))) {
  } else if (OB_FAIL(storage_owner.convert_to_sessid(session_id))) {
  }
  return ret;
}

int generate_named_lock_identity(const common::ObString &lock_name,
                                 uint64_t min_lock_id,
                                 uint64_t max_lock_id,
                                 uint64_t &lock_id,
                                 uint64_t &name_hash)
{
  int ret = common::OB_SUCCESS;
  ObCommonID unique_id;
  lock_id = 0;
  name_hash = 0;
  if (OB_UNLIKELY(lock_name.empty() || min_lock_id > max_lock_id)) {
    ret = common::OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(ObCommonIDUtils::gen_unique_id(unique_id))) {
    LOG_WARN("generate named-lock unique id failed", KR(ret));
  } else {
    name_hash = murmurhash(lock_name.ptr(), lock_name.length(), name_hash);
    lock_id = unique_id.id() % (max_lock_id - min_lock_id + 1) + min_lock_id;
  }
  return ret;
}

} // namespace data_plane
} // namespace oceanbase
