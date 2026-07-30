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

#ifndef OCEANBASE_MYSQLCLIENT_OB_ISQL_CONNECTION_H_
#define OCEANBASE_MYSQLCLIENT_OB_ISQL_CONNECTION_H_

#include "lib/ob_define.h"
#include "common/mysqlclient/ob_isql_client.h"
#include "common/timezone/ob_timezone_info.h"
#include "common/mysqlclient/ob_isql_connection_pool.h"
#include "common/object/ob_object.h"

namespace oceanbase
{
namespace sql
{
class ObSql;
struct ObSqlCtx;
class ObResultSet;
}
namespace share
{
namespace schema
{
class ObRoutineInfo;
}

}

namespace pl
{
class ObUserDefinedType;
}
namespace common
{
class ObIAllocator;
class ObString;

namespace sqlclient
{
class ObISQLConnection;
class ObCommonServerConnectionPool
{
public:
  ObCommonServerConnectionPool() : free_conn_count_(0), busy_conn_count_(0) {}
  virtual ~ObCommonServerConnectionPool() {}

  virtual int release(common::sqlclient::ObISQLConnection *connection, const bool succ) = 0;
  TO_STRING_KV(K_(free_conn_count), K_(busy_conn_count));
protected:
  volatile uint64_t free_conn_count_;
  volatile uint64_t busy_conn_count_;
};

class ObISQLResultHandler;

// execute in sql engine
class ObIExecutor
{
public:
  ObIExecutor() {}
  virtual ~ObIExecutor() {}

  // get schema version, return OB_INVALID_VERSION for newest schema.
  virtual int64_t get_schema_version() const { return OB_INVALID_VERSION; }

  virtual int execute(sql::ObSql &engine, sql::ObSqlCtx &ctx, sql::ObResultSet &res) = 0;

  // process result after result open
  virtual int process_result(sql::ObResultSet &res) = 0;

  virtual int64_t to_string(char *, const int64_t) const { return 0; }
};

// SQL client connection interface
class ObISQLConnection
{
public:
  ObISQLConnection() :
       sessid_(-1),
       usable_(true),
       check_priv_(false)
  {}
  virtual ~ObISQLConnection() {
    allocator_.reset();
  }

  // sql execute interface
  virtual int execute_read(const ObString &sql,
      ObISQLClient::ReadResult &res, bool is_user_sql = false) = 0;
  virtual int execute_write(const ObString &sql,
      int64_t &affected_rows, bool is_user_sql = false) = 0;
  virtual int execute_proc() { return OB_NOT_SUPPORTED; }
  virtual int execute_proc(ObIAllocator &allocator,
                        ParamStore &params,
                        ObString &sql,
                        const share::schema::ObRoutineInfo &routine_info,
                        const common::ObIArray<const pl::ObUserDefinedType *> &udts,
                        const ObTimeZoneInfo *tz_info,
                        ObObj *result,
                        bool is_sql) = 0;
  virtual int prepare(const ObString &sql, int64_t param_count, ObIAllocator *allocator = NULL) {
    UNUSED(sql);
    return OB_NOT_SUPPORTED;
  }
  virtual int bind_basic_type_by_pos(uint64_t position,
                                     void *param,
                                     int64_t param_size,
                                     int32_t datatype,
                                     int32_t &indicator,
                                     bool is_out_param)
  {
    UNUSEDx(position, param, param_size, datatype, indicator, is_out_param);
    return OB_NOT_SUPPORTED;
  }
  virtual int bind_array_type_by_pos(uint64_t position,
                                     void *array,
                                     int32_t *indicators,
                                     int64_t ele_size,
                                     int32_t ele_datatype,
                                     uint64_t array_size,
                                     uint32_t *out_valid_array_size)
  {
    UNUSEDx(position, array, ele_size, ele_datatype, array_size, out_valid_array_size);
    return OB_NOT_SUPPORTED;
  }
  virtual int get_server_major_version(int64_t &major_version) {
    return OB_NOT_SUPPORTED;
  }
  // transaction interface
  virtual int start_transaction(bool with_snap_shot = false) = 0;
  virtual int rollback() = 0;
  virtual int commit() = 0;

  // session environment
  virtual int get_session_variable(const ObString &name, int64_t &val) = 0;
  virtual int set_session_variable(const ObString &name, int64_t val) = 0;
  virtual int set_session_variable(const ObString &name, const ObString &val) = 0;
  virtual bool is_query_sensitive_sys_var_refresh_enabled() const { return true; }
  virtual void set_query_sensitive_sys_var_refresh_enabled(const bool enabled) { UNUSED(enabled); }
  virtual int execute(ObIExecutor &executor)
  {
    UNUSED(executor);
    return OB_NOT_SUPPORTED;
  }


  virtual ObCommonServerConnectionPool *get_common_server_pool() = 0;
  void set_sessid(uint32_t sessid) { sessid_ = sessid; }
  uint32_t get_sessid() { return sessid_; }
  virtual int set_ddl_info(const void *ddl_info) { UNUSED(ddl_info); return OB_NOT_SUPPORTED; }
  virtual int set_tz_info_wrap(const ObTimeZoneInfoWrap &tz_info_wrap) { UNUSED(tz_info_wrap); return OB_NOT_SUPPORTED; }
  virtual void set_is_load_data_exec(bool v) { UNUSED(v); }
  virtual void set_use_external_session(bool v) { UNUSED(v); }
  virtual void set_ob_enable_pl_cache(bool v) { UNUSED(v); }
  virtual void set_user_timeout(int64_t user_timeout) { UNUSED(user_timeout); }
  virtual int64_t get_user_timeout() const { return 0; }
  void set_usable(bool flag) { usable_ = flag; }
  bool usable() { return usable_; }
  virtual int ping() { return OB_SUCCESS; }
  void set_check_priv(bool on) { check_priv_ = on; }
  bool is_check_priv() { return check_priv_; }
protected:
  uint32_t sessid_;
  bool usable_;  // usable_ = false: connection is unusable, should not execute query again.
  common::ObArenaAllocator allocator_;
  bool check_priv_;
};

} // end namespace sqlclient
} // end namespace common
} // end namespace oceanbase

#endif // OCEANBASE_MYSQLCLIENT_OB_ISQL_CONNECTION_H_
