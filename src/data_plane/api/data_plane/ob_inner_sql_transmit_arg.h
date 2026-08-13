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

#ifndef OCEANBASE_DATA_PLANE_OB_INNER_SQL_TRANSMIT_ARG_H_
#define OCEANBASE_DATA_PLANE_OB_INNER_SQL_TRANSMIT_ARG_H_

#include "common/mysqlclient/ob_mysql_proxy.h"
#include "common/sql_mode/ob_sql_mode.h"
#include "common/timezone/ob_timezone_info.h"
#include "lib/net/ob_addr.h"
#include "lib/ob_define.h"
#include "lib/string/ob_string.h"
#include "lib/utility/ob_print_utils.h"
#include "lib/utility/ob_unify_serialize.h"

namespace oceanbase
{
namespace obcall
{

// Wire request shared by the observer transport adapter and Storage's
// table-lock implementation. Response/scanner details remain in Observer.
class ObInnerSQLTransmitArg
{
  OB_UNIS_VERSION(1);
public:
  enum InnerSQLOperationType
  {
    OPERATION_TYPE_INVALID = 0,
    OPERATION_TYPE_START_TRANSACTION = 1,
    OPERATION_TYPE_ROLLBACK = 2,
    OPERATION_TYPE_COMMIT = 3,
    OPERATION_TYPE_EXECUTE_READ = 4,
    OPERATION_TYPE_EXECUTE_WRITE = 5,
    OPERATION_TYPE_REGISTER_MDS = 6,
    OPERATION_TYPE_LOCK_TABLE = 7,
    OPERATION_TYPE_LOCK_TABLET = 8,
    OPERATION_TYPE_UNLOCK_TABLE = 9,
    OPERATION_TYPE_UNLOCK_TABLET = 10,
    OPERATION_TYPE_LOCK_PART = 11,
    OPERATION_TYPE_UNLOCK_PART = 12,
    OPERATION_TYPE_LOCK_OBJ = 13,
    OPERATION_TYPE_UNLOCK_OBJ = 14,
    OPERATION_TYPE_LOCK_SUBPART = 15,
    OPERATION_TYPE_UNLOCK_SUBPART = 16,
    OPERATION_TYPE_LOCK_ALONE_TABLET = 17,
    OPERATION_TYPE_UNLOCK_ALONE_TABLET = 18,
    OPERATION_TYPE_LOCK_OBJS = 19,
    OPERATION_TYPE_UNLOCK_OBJS = 20,
    OPERATION_TYPE_REPLACE_LOCK = 21,
    OPERATION_TYPE_REPLACE_LOCKS = 22,
    OPERATION_TYPE_MAX = 100
  };

  ObInnerSQLTransmitArg()
    : ctrl_svr_(),
      runner_svr_(),
      conn_id_(common::OB_INVALID_ID),
      inner_sql_(nullptr),
      operation_type_(OPERATION_TYPE_INVALID),
      source_cluster_id_(common::OB_INVALID_ID),
      worker_timeout_(common::OB_DEFAULT_SESSION_TIMEOUT),
      query_timeout_(common::OB_DEFAULT_SESSION_TIMEOUT),
      trx_timeout_(common::OB_DEFAULT_SESSION_TIMEOUT),
      sql_mode_(0),
      tz_info_wrap_(),
      ddl_info_(),
      is_load_data_exec_(false),
      nls_formats_{},
      use_external_session_(false)
  {}

  ObInnerSQLTransmitArg(
      common::ObAddr ctrl_svr,
      common::ObAddr runner_svr,
      uint64_t conn_id,
      common::ObString inner_sql,
      InnerSQLOperationType operation_type,
      int64_t source_cluster_id,
      int64_t worker_timeout,
      int64_t query_timeout,
      int64_t trx_timeout,
      ObSQLMode sql_mode,
      common::ObSessionDDLInfo ddl_info,
      bool is_load_data_exec,
      bool use_external_session)
    : ctrl_svr_(ctrl_svr),
      runner_svr_(runner_svr),
      conn_id_(conn_id),
      inner_sql_(inner_sql),
      operation_type_(operation_type),
      source_cluster_id_(source_cluster_id),
      worker_timeout_(worker_timeout),
      query_timeout_(query_timeout),
      trx_timeout_(trx_timeout),
      sql_mode_(sql_mode),
      tz_info_wrap_(),
      ddl_info_(ddl_info),
      is_load_data_exec_(is_load_data_exec),
      nls_formats_{},
      use_external_session_(use_external_session)
  {}

  ~ObInnerSQLTransmitArg() = default;

  const common::ObAddr &get_ctrl_svr() const { return ctrl_svr_; }
  void set_ctrl_svr(const common::ObAddr &ctrl_svr) { ctrl_svr_ = ctrl_svr; }
  const common::ObAddr &get_runner_svr() const { return runner_svr_; }
  void set_runner_svr(const common::ObAddr &runner_svr) { runner_svr_ = runner_svr; }
  uint64_t get_conn_id() const { return conn_id_; }
  void set_conn_id(uint64_t conn_id) { conn_id_ = conn_id; }
  const common::ObString &get_inner_sql() const { return inner_sql_; }
  void set_inner_sql(const common::ObString &inner_sql) { inner_sql_ = inner_sql; }
  InnerSQLOperationType get_operation_type() const { return operation_type_; }
  void set_operation_type(InnerSQLOperationType operation_type)
  { operation_type_ = operation_type; }
  void set_source_cluster_id(int64_t source_cluster_id)
  { source_cluster_id_ = source_cluster_id; }
  int64_t get_source_cluster_id() const { return source_cluster_id_; }
  void set_worker_timeout(int64_t worker_timeout) { worker_timeout_ = worker_timeout; }
  int64_t get_worker_timeout() const { return worker_timeout_; }
  void set_query_timeout(int64_t query_timeout) { query_timeout_ = query_timeout; }
  int64_t get_query_timeout() const { return query_timeout_; }
  void set_trx_timeout(int64_t trx_timeout) { trx_timeout_ = trx_timeout; }
  int64_t get_trx_timeout() const { return trx_timeout_; }
  int set_tz_info_wrap(const common::ObTimeZoneInfoWrap &other)
  { return tz_info_wrap_.deep_copy(other); }
  void set_nls_formats(
      const common::ObString &nls_date_format,
      const common::ObString &nls_timestamp_format,
      const common::ObString &nls_timestamp_tz_format)
  {
    nls_formats_[0] = nls_date_format;
    nls_formats_[1] = nls_timestamp_format;
    nls_formats_[2] = nls_timestamp_tz_format;
  }
  const common::ObTimeZoneInfoWrap &get_tz_info_wrap() const { return tz_info_wrap_; }
  const common::ObSessionDDLInfo &get_ddl_info() const { return ddl_info_; }
  ObSQLMode get_sql_mode() const { return sql_mode_; }
  bool get_is_load_data_exec() const { return is_load_data_exec_; }
  const common::ObString *get_nls_formats() const { return nls_formats_; }
  bool get_use_external_session() const { return use_external_session_; }

  TO_STRING_KV(
      K_(ctrl_svr),
      K_(runner_svr),
      K_(conn_id),
      K_(inner_sql),
      K_(operation_type),
      K_(source_cluster_id),
      K_(worker_timeout),
      K_(query_timeout),
      K_(trx_timeout),
      K_(sql_mode),
      K_(tz_info_wrap),
      K_(ddl_info),
      K_(is_load_data_exec),
      K_(nls_formats),
      K_(use_external_session));

private:
  common::ObAddr ctrl_svr_;
  common::ObAddr runner_svr_;
  uint64_t conn_id_;
  common::ObString inner_sql_;
  InnerSQLOperationType operation_type_;
  int64_t source_cluster_id_;
  int64_t worker_timeout_;
  int64_t query_timeout_;
  int64_t trx_timeout_;
  ObSQLMode sql_mode_;
  common::ObTimeZoneInfoWrap tz_info_wrap_;
  common::ObSessionDDLInfo ddl_info_;
  bool is_load_data_exec_;
  common::ObString nls_formats_[3];
  bool use_external_session_;
};

} // namespace obcall
} // namespace oceanbase

#endif // OCEANBASE_DATA_PLANE_OB_INNER_SQL_TRANSMIT_ARG_H_
