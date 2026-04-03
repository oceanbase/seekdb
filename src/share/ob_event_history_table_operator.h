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

#ifndef OCEANBASE_SHARE_OB_EVENT_HISTORY_TABLE_OPERATOR_H_
#define OCEANBASE_SHARE_OB_EVENT_HISTORY_TABLE_OPERATOR_H_

#include "lib/oblog/ob_log.h"
#include "lib/time/ob_time_utility.h"
#include "lib/guard/ob_unique_guard.h"
#include "lib/string/ob_sql_string.h"
#include "lib/string/ob_string_holder.h"
#include "lib/queue/ob_dedup_queue.h"
#include "lib/thread/ob_work_queue.h"
#include "lib/lock/ob_mutex.h"
#include "lib/mysqlclient/ob_mysql_proxy.h"
#include "share/inner_table/ob_inner_table_schema.h"
#include "share/ob_dml_sql_splicer.h"
#include "share/ob_occam_timer.h"
#include "share/ob_task_define.h"
#include "share/storage/ob_server_event_history_table_storage.h"
#include "share/storage/ob_tenant_event_history_table_storage.h"
#include "lib/utility/ob_print_utils.h"
#include <utility>
#include <type_traits>

namespace oceanbase
{
namespace common
{
class ObMySQLProxy;
}
namespace share
{
namespace detector
{
class ObDeadLockEventHistoryTableOperator;
}
class ObEventHistoryTableOperator;
class ObEventTableClearTask : public common::ObAsyncTimerTask
{
public:
  ObEventTableClearTask(
    ObEventHistoryTableOperator &rs_event_operator,
    ObEventHistoryTableOperator &server_event_operator,
    ObEventHistoryTableOperator &deadlock_history_operator,
    common::ObWorkQueue &work_queue);
  virtual ~ObEventTableClearTask() {}

  // interface of AsyncTask
  virtual int process() override;
  virtual int64_t get_deep_copy_size() const override { return sizeof(*this); }
  virtual ObAsyncTask *deep_copy(char *buf, const int64_t buf_size) const override;
private:
  ObEventHistoryTableOperator &rs_event_operator_;
  ObEventHistoryTableOperator &server_event_operator_;
  ObEventHistoryTableOperator &deadlock_history_operator_;
};

class ObEventHistoryTableOperator
{
public:
  static const int64_t EVENT_TABLE_CLEAR_INTERVAL = 2L * 3600L * 1000L * 1000L; // 2 Hours
  template<bool Truncate, typename T>
  struct ValueConverter {};
  class ObEventTableUpdateTask : public common::IObDedupTask
  {
  public:
    ObEventTableUpdateTask(ObEventHistoryTableOperator &table_operator, const bool is_delete,
        const int64_t create_time, const uint64_t exec_tenant_id);
    virtual ~ObEventTableUpdateTask() {}
    int init(const char *ptr, const int64_t buf_size, const uint64_t exec_tenant_id = OB_SYS_TENANT_ID);
    bool is_valid() const;
    virtual int64_t hash() const;
    virtual bool operator==(const common::IObDedupTask &other) const;
    virtual int64_t get_deep_copy_size() const { return sizeof(*this) + sql_.length(); }
    virtual common::IObDedupTask *deep_copy(char *buf, const int64_t buf_size) const;
    virtual int64_t get_abs_expired_time() const { return 0; }
    virtual uint64_t get_exec_tenant_id() const { return exec_tenant_id_; }
    virtual int process();
  public:
    void assign_ptr(char *ptr, const int64_t buf_size)
    { sql_.assign_ptr(ptr, static_cast<int32_t>(buf_size));}
    TO_STRING_KV(K_(sql), K_(is_delete), K_(create_time), K_(exec_tenant_id));
  private:
    ObEventHistoryTableOperator &table_operator_;
    common::ObString sql_;
    bool is_delete_;
    int64_t create_time_;
    uint64_t exec_tenant_id_;

    DISALLOW_COPY_AND_ASSIGN(ObEventTableUpdateTask);
  };

  virtual ~ObEventHistoryTableOperator();
  int init(common::ObMySQLProxy &proxy);
  int init(ObSQLiteConnectionPool *pool, ObEventHistoryType event_type);
  bool is_inited() const { return inited_; }
  void stop();
  void wait();
  void destroy();
  // number of others should not less than 0, or more than 13
  // if number of others is not 13, should be even, every odd of them are name, every even of them are value
  // If Truncate is true, then too long value will be truncated to fit in the field.
  // Note: Only enable Truncate for rootservice_event_history!
  template <bool Truncate, typename ...Rest>
  int add_event(const char *module, const char *event, Rest &&...others);
  // number of others should not less than 0, or more than 13
  // if number of others is not 13, should be even, every odd of them are name, every even of them are value
  template <typename ...Rest>
  int sync_add_event(const char *module, const char *event, Rest &&...others);
  // number of others should not less than 0, or more than 13
  // if number of others is not 13, should be even, every odd of them are name, every even of them are value
  template <typename ...Rest>
  int async_add_tenant_event(const uint64_t tenant_id, const char *module, const char *event,
      const int64_t event_timestamp, const int user_ret, const int64_t cost_sec, Rest &&...others);
  // number of others should not less than 0, or more than 13
  template <typename ...Rest>
  int add_event_with_retry(const char *module, const char *event, Rest &&...others);

  virtual int async_delete() = 0;
protected:
  virtual int default_async_delete();
  // recursive begin
  template <int Floor, bool Truncate, typename Name, typename Value, typename ...Rest>
  int add_event_helper_(share::ObDMLSqlSplicer &dml, Name &&name, Value &&value, Rest &&...others);
  // recursive end if there is no extra_info
  template <int Floor, bool Truncate>
  int add_event_helper_(share::ObDMLSqlSplicer &dml);
  // recursive end if there is an extra_info
  template <int Floor, bool Truncate, typename Value>
  int add_event_helper_(share::ObDMLSqlSplicer &dml, Value &&extro_info);
  int add_event_to_timer_(const common::ObSqlString &sql);
  void set_addr(const common::ObAddr self_addr,
                bool is_rs_ev,
                bool is_server_ev)
  {
    self_addr_ = self_addr;
    is_rootservice_event_history_ = is_rs_ev;
    is_server_event_history_ = is_server_ev;
  }
  const common::ObAddr &get_addr() const { return self_addr_; }
  int add_task(const common::ObSqlString &sql, const bool is_delete = false,
      const int64_t create_time = OB_INVALID_TIMESTAMP, const uint64_t exec_tenant_id = OB_SYS_TENANT_ID);
  int gen_event_ts(int64_t &event_ts);
  // Helper method to build entry from variadic template args
  template <int Floor, bool Truncate, typename Name, typename Value, typename ...Rest>
  int build_entry_helper_(ObServerEventHistoryEntry &entry, common::ObCStringHelperV2 &helper, Name &&name, Value &&value, Rest &&...others);
  template <int Floor, bool Truncate>
  int build_entry_helper_(ObServerEventHistoryEntry &entry, common::ObCStringHelperV2 &helper);
  template <int Floor, bool Truncate, typename Value>
  int build_entry_helper_(ObServerEventHistoryEntry &entry, common::ObCStringHelperV2 &helper, Value &&extra_info);
protected:
  static constexpr const char * names[7] = {"name1", "name2", "name3", "name4", "name5", "name6", "extra_info"}; // only valid in compile time
  static constexpr const char * values[6] = {"value1", "value2", "value3", "value4", "value5", "value6"}; // only valid in compile time
  static const int64_t TOTAL_LIMIT = 320L * 1024L * 1024L; // 320MB
  static const int64_t HOLD_LIMIT = 160L * 1024L * 1024L;   // 160MB
  static const int64_t ALLOC_PAGE_SIZE = common::OB_MALLOC_NORMAL_BLOCK_SIZE;
  static const int64_t TASK_MAP_SIZE = 20 * 1024;
  static const int64_t TASK_QUEUE_SIZE = 20 *1024;
  static const int64_t MAX_RETRY_COUNT = 12;

  virtual int process_task(const common::ObString &sql, const bool is_delete, const int64_t create_time, const uint64_t exec_tenant_id);
private:
  bool inited_;
  volatile bool stopped_;
  int64_t last_event_ts_;
  lib::ObMutex lock_;
  common::ObMySQLProxy *proxy_;
  common::ObDedupQueue event_queue_;
  common::ObAddr self_addr_;
  bool is_rootservice_event_history_;
  bool is_server_event_history_;
  // timer for add_event_with_retry
  common::ObOccamTimer timer_;
  // SQLite storage (unified for all three event history tables)
  ObEventHistoryType event_type_;
protected:
  ObEventHistoryTableOperator();
  ObServerEventHistoryTableStorage storage_;
  ObTenantEventHistoryTableStorage tenant_storage_;
private:
  DISALLOW_COPY_AND_ASSIGN(ObEventHistoryTableOperator);
};

template<typename T>
struct ObEventHistoryTableOperator::ValueConverter<true, T>
{
  const ObString & convert(const char *value) {
    truncated_str_.assign_ptr(value, min(static_cast<int64_t>(STRLEN(value)), MAX_ROOTSERVICE_EVENT_VALUE_LENGTH));
    return truncated_str_;
  }

  template<typename U = T, typename std::enable_if<common::__has_to_string__<U>::value, bool>::type = true>
  const ObString & convert(const U &value) {
    int64_t len = value.to_string(buffer_, sizeof(buffer_));
    truncated_str_.assign_ptr(buffer_, len);
    return truncated_str_;
  }

  template<typename U = T, typename std::enable_if<!common::__has_to_string__<U>::value, bool>::type = true>
  inline const U & convert(const U &value) {
    return value;
  }

  char buffer_[MAX_ROOTSERVICE_EVENT_VALUE_LENGTH + 1];
  ObString truncated_str_;
};

template<typename T>
struct ObEventHistoryTableOperator::ValueConverter<false, T>
{
  inline const T & convert(const T &value) {
    return value;
  }
};

template <bool Truncate, typename ...Rest>
int ObEventHistoryTableOperator::add_event(const char *module, const char *event, Rest &&...others)
{
  static_assert(sizeof...(others) >= 0 && sizeof...(others) <= 13 &&
                (sizeof...(others) == 13 || (sizeof...(others) % 2 == 0)),
                "max support 6 pair of name-value args and 1 extra info, if number of others is not 13, should be even");
  int ret = common::OB_SUCCESS;
  int64_t event_ts = 0;
  if (!inited_) {
    ret = common::OB_NOT_INIT;
    SHARE_LOG(WARN, "not init", K(ret));
  } else if (NULL == module || NULL == event) {
    ret = common::OB_INVALID_ARGUMENT;
    SHARE_LOG(WARN, "neither module or event can be NULL", KP(module), KP(event), K(ret));
  } else if (OB_FAIL(gen_event_ts(event_ts))) {
    SHARE_LOG(WARN, "gen_event_ts failed", K(ret));
  } else if (!storage_.is_inited()) {
    ret = OB_NOT_INIT;
    SHARE_LOG(WARN, "storage not initialized", K(ret));
  } else {
    // Use SQLite storage
    ObServerEventHistoryEntry entry;
    entry.gmt_create_ = event_ts;
    entry.event_type_ = event_type_;
    entry.svr_addr_ = self_addr_;
    entry.module_.assign_ptr(module, static_cast<int32_t>(strlen(module)));
    entry.event_.assign_ptr(event, static_cast<int32_t>(strlen(event)));

    common::ObCStringHelperV2 helper;
    if (OB_FAIL((build_entry_helper_<0, Truncate>(entry, helper, std::forward<Rest>(others)...)))) {
      SHARE_LOG(WARN, "build entry failed", K(ret));
    } else if (OB_FAIL(storage_.insert(entry))) {
      SHARE_LOG(WARN, "failed to insert event", K(ret));
    }
  }
  ObTaskController::get().allow_next_syslog();
  SHARE_LOG(INFO, "event table add event", K(ret), K(event_type_));
  return ret;
}

template <typename ...Rest>
int ObEventHistoryTableOperator::sync_add_event(const char *module, const char *event, Rest &&...others)
{
  static_assert(sizeof...(others) >= 0 && sizeof...(others) <= 13 &&
                (sizeof...(others) == 13 || (sizeof...(others) % 2 == 0)),
                "max support 6 pair of name-value args and 1 extra info, if number of others is not 13, should be even");
  int ret = common::OB_SUCCESS;
  int64_t event_ts = 0;
  if (!inited_) {
    ret = common::OB_NOT_INIT;
    SHARE_LOG(WARN, "not init", K(ret));
  } else if (NULL == module || NULL == event) {
    ret = common::OB_INVALID_ARGUMENT;
    SHARE_LOG(WARN, "neither module or event can be NULL", KP(module), KP(event), K(ret));
  } else if (OB_FAIL(gen_event_ts(event_ts))) {
    SHARE_LOG(WARN, "gen_event_ts failed", K(ret));
  } else if (!storage_.is_inited()) {
    ret = OB_NOT_INIT;
    SHARE_LOG(WARN, "storage not initialized", K(ret));
  } else {
    // Use SQLite storage
    ObServerEventHistoryEntry entry;
    entry.gmt_create_ = event_ts;
    entry.event_type_ = event_type_;
    entry.svr_addr_ = self_addr_;
    entry.module_.assign_ptr(module, static_cast<int32_t>(strlen(module)));
    entry.event_.assign_ptr(event, static_cast<int32_t>(strlen(event)));

    common::ObCStringHelperV2 helper;
    if (OB_FAIL((build_entry_helper_<0, false>(entry, helper, std::forward<Rest>(others)...)))) {
      SHARE_LOG(WARN, "build entry failed", K(ret));
    } else if (OB_FAIL(storage_.insert(entry))) {
      SHARE_LOG(WARN, "failed to insert event", K(ret));
    } else {
      ObTaskController::get().allow_next_syslog();
      SHARE_LOG(INFO, "event table sync add event success", K(ret), K(event_type_));
    }
  }
  return ret;
}

template <typename ...Rest>
int ObEventHistoryTableOperator::async_add_tenant_event(
	  const uint64_t tenant_id, const char *module, const char *event, const int64_t event_timestamp,
    const int user_ret, const int64_t cost_sec, Rest &&...others)
{
  static_assert(sizeof...(others) >= 0 && sizeof...(others) <= 13 &&
      (sizeof...(others) == 13 || (sizeof...(others) % 2 == 0)),
      "max support 6 pair of name-value args and 1 extra info, if number of others is not 13, should be even");
  int ret = common::OB_SUCCESS;
  if (OB_UNLIKELY(!inited_)) {
    ret = common::OB_NOT_INIT;
    SHARE_LOG(WARN, "not init", KR(ret));
  } else if (OB_ISNULL(module) || OB_ISNULL(event)) {
    ret = common::OB_INVALID_ARGUMENT;
    SHARE_LOG(WARN, "neither module or event can be NULL", KR(ret), KP(module), KP(event));
  } else if (OB_UNLIKELY(OB_INVALID_TENANT_ID == tenant_id)) {
    ret = OB_INVALID_ARGUMENT;
    SHARE_LOG(WARN, "tenant_id is invalid", KR(ret), K(tenant_id));
  } else if (!tenant_storage_.is_inited()) {
    ret = OB_NOT_INIT;
    SHARE_LOG(WARN, "tenant storage not initialized", K(ret));
  } else {
    // Use dedicated SQLite table for tenant events
    ObTenantEventHistoryEntry entry;
    entry.tenant_id_ = tenant_id;
    entry.gmt_create_ = event_timestamp;
    entry.svr_addr_ = self_addr_;
    entry.module_.assign_ptr(module, static_cast<int32_t>(strlen(module)));
    entry.event_.assign_ptr(event, static_cast<int32_t>(strlen(event)));
    entry.cost_time_ = cost_sec;
    entry.ret_code_ = user_ret;
    entry.trace_id_.reset();
    entry.error_msg_.reset();

    // Build name-value pairs using helper (same structure as ObServerEventHistoryEntry)
    ObServerEventHistoryEntry temp_entry;
    temp_entry.gmt_create_ = event_timestamp;
    temp_entry.svr_addr_ = self_addr_;
    temp_entry.module_.assign_ptr(module, static_cast<int32_t>(strlen(module)));
    temp_entry.event_.assign_ptr(event, static_cast<int32_t>(strlen(event)));
    common::ObCStringHelperV2 helper;
    if (OB_FAIL((build_entry_helper_<0, false>(temp_entry, helper, std::forward<Rest>(others)...)))) {
      SHARE_LOG(WARN, "build entry failed", K(ret));
    } else {
      // Copy name-value pairs from temp_entry to entry
      entry.name1_ = temp_entry.name1_;
      entry.value1_ = temp_entry.value1_;
      entry.name2_ = temp_entry.name2_;
      entry.value2_ = temp_entry.value2_;
      entry.name3_ = temp_entry.name3_;
      entry.value3_ = temp_entry.value3_;
      entry.name4_ = temp_entry.name4_;
      entry.value4_ = temp_entry.value4_;
      entry.name5_ = temp_entry.name5_;
      entry.value5_ = temp_entry.value5_;
      entry.name6_ = temp_entry.name6_;
      entry.value6_ = temp_entry.value6_;
      entry.extra_info_ = temp_entry.extra_info_;

      if (OB_FAIL(tenant_storage_.insert(entry))) {
        SHARE_LOG(WARN, "failed to insert tenant event", K(ret));
      }
    }
  }

  ObTaskController::get().allow_next_syslog();
  SHARE_LOG(INFO, "event table async add tenant event", K(ret), K(event_type_));
  return ret;
  return ret;
}

template <typename ...Rest>
int ObEventHistoryTableOperator::add_event_with_retry(const char *module, const char *event, Rest &&...others)
{
  static_assert(sizeof...(others) >= 0 && sizeof...(others) <= 13 &&
                (sizeof...(others) == 13 || (sizeof...(others) % 2 == 0)),
                "max support 6 pair of name-value args and 1 extra info, if number of others is not 13, should be even");
  int ret = common::OB_SUCCESS;
  int64_t event_ts = 0;
  TIMEGUARD_INIT(EVENT_HISTORY, 100_ms, 5_s);
  if (!inited_) {
    ret = common::OB_NOT_INIT;
    SHARE_LOG(WARN, "not init", K(ret));
  } else if (NULL == module || NULL == event) {
    ret = common::OB_INVALID_ARGUMENT;
    SHARE_LOG(WARN, "neither module or event can be NULL", KP(module), KP(event), K(ret));
  } else if (stopped_) {
    ret = OB_CANCELED;
    SHARE_LOG(WARN, "observer is stopped, cancel add_event", K(ret));
  } else if (CLICK_FAIL(gen_event_ts(event_ts))) {
    SHARE_LOG(WARN, "gen_event_ts failed", K(ret));
  } else if (!storage_.is_inited()) {
    ret = OB_NOT_INIT;
    SHARE_LOG(WARN, "storage not initialized", K(ret));
  } else {
    // Use SQLite storage
    ObServerEventHistoryEntry entry;
    entry.gmt_create_ = event_ts;
    entry.event_type_ = event_type_;
    entry.svr_addr_ = self_addr_;
    entry.module_.assign_ptr(module, static_cast<int32_t>(strlen(module)));
    entry.event_.assign_ptr(event, static_cast<int32_t>(strlen(event)));

    common::ObCStringHelperV2 helper;
    if (CLICK_FAIL((build_entry_helper_<0, false>(entry, helper, std::forward<Rest>(others)...)))) {
      SHARE_LOG(WARN, "build entry failed", K(ret));
    } else if (CLICK_FAIL(storage_.insert(entry))) {
      SHARE_LOG(WARN, "failed to insert event", K(ret));
    } else {
      SHARE_LOG(INFO, "add_event_with_retry success", K(ret));
    }
  }
  return ret;
}
// recursive begin
template <int Floor, bool Truncate, typename Name, typename Value, typename ...Rest>
int ObEventHistoryTableOperator::add_event_helper_(share::ObDMLSqlSplicer &dml, Name &&name, Value &&value, Rest &&...others)
{
  int ret = OB_SUCCESS;
  common::ObSqlString sql;
  ValueConverter<Truncate, Value> converter;
  #if Truncate
  // Truncate can be true only for __all_rootservice_event_history
  // Note: This check is for legacy MySQL path, SQLite path uses event_type_ instead
  if (OB_UNLIKELY(ObEventHistoryType::ROOTSERVICE != event_type_)) {
    ret = OB_NOT_SUPPORTED;
    SHARE_LOG(WARN, "Truncate event only available for rootservice event history",
              KR(ret), K(event_type_));
  } else
  #endif
  if (OB_FAIL(dml.add_column(names[Floor], name))) {
    SHARE_LOG(WARN, "add column failed", K(ret), K(Floor));
  } else if (OB_FAIL(dml.add_column(values[Floor], converter.convert(value)))) {
    SHARE_LOG(WARN, "add column failed", K(ret), K(Floor));
  } else if (OB_FAIL((add_event_helper_<Floor + 1, Truncate>(dml, std::forward<Rest>(others)...)))){
  }
  return ret;
}

// recursive end if there is no extra_info
// if not all columns are user-specified, set rest fields empty
template <int Floor, bool Truncate>
int ObEventHistoryTableOperator::add_event_helper_(share::ObDMLSqlSplicer &dml)
{
  int ret = OB_SUCCESS;
  if (Floor < 6) {
    for (int64_t idx = Floor; OB_SUCCESS == ret && idx < 6; ++idx) {
      if (OB_FAIL(dml.add_column(values[idx], ""))) {
        SHARE_LOG(WARN, "add column failed", K(ret), K(idx));
      }
    }
  }
  return ret;
}

// recursive end if there is an extra_info
template <int Floor, bool Truncate, typename Value>
int ObEventHistoryTableOperator::add_event_helper_(share::ObDMLSqlSplicer &dml, Value &&extra_info)
{
  static_assert(Floor == 6, "if there is an extra_info column, it must be 13th args in this row, no more, no less");
  int ret = OB_SUCCESS;
  if (OB_FAIL(dml.add_column(names[Floor], extra_info))) {
    SHARE_LOG(WARN, "add column failed", K(ret), K(Floor));
  }
  return ret;
}

// Helper methods to build entry from variadic template args
template <int Floor, bool Truncate, typename Name, typename Value, typename ...Rest>
int ObEventHistoryTableOperator::build_entry_helper_(ObServerEventHistoryEntry &entry, common::ObCStringHelperV2 &helper, Name &&name, Value &&value, Rest &&...others)
{
  int ret = OB_SUCCESS;
  ValueConverter<Truncate, Value> converter;
  common::ObString name_str;
  common::ObString value_str;

  // Convert name to string (name is always const char* in practice)
  // Since name is always const char* in practice, we can safely extract the pointer value
  // Use helper function to convert name to const char* safely
  const char *name_ptr = nullptr;
  {
    // Use helper to convert name to string, which handles all types
    const char *cstr = helper.convert(name);
    if (OB_ISNULL(cstr)) {
      ret = helper.get_ob_errno();
      if (OB_SUCCESS == ret) {
        // If helper returns NULL but no error, name might be NULL pointer
        name_ptr = nullptr;
      } else {
        SHARE_LOG(WARN, "convert name to string failed", K(ret));
        name_ptr = nullptr;
      }
    } else {
      name_ptr = cstr;
    }
  }

  if (OB_SUCC(ret)) {
    if (OB_ISNULL(name_ptr)) {
      name_str.reset();
    } else {
      name_str.assign_ptr(name_ptr, static_cast<int32_t>(strlen(name_ptr)));
    }
  }

  // Convert value to string using ObCStringHelperV2 for universal type conversion
  // Use ObCStringHelperV2 to convert any type to string
  // This handles all types including ObAddr, numeric types, and types with to_string
  const char *cstr = helper.convert(value);
  if (OB_ISNULL(cstr)) {
    ret = helper.get_ob_errno();
    SHARE_LOG(WARN, "convert value to string failed", K(ret));
  } else {
    value_str.assign_ptr(cstr, static_cast<int32_t>(strlen(cstr)));
  }

  // Assign to appropriate field
  switch (Floor) {
    case 0:
      entry.name1_ = name_str;
      entry.value1_ = value_str;
      break;
    case 1:
      entry.name2_ = name_str;
      entry.value2_ = value_str;
      break;
    case 2:
      entry.name3_ = name_str;
      entry.value3_ = value_str;
      break;
    case 3:
      entry.name4_ = name_str;
      entry.value4_ = value_str;
      break;
    case 4:
      entry.name5_ = name_str;
      entry.value5_ = value_str;
      break;
    case 5:
      entry.name6_ = name_str;
      entry.value6_ = value_str;
      break;
    default:
      ret = OB_ERR_UNEXPECTED;
      SHARE_LOG(WARN, "invalid floor", K(Floor), K(ret));
      break;
  }

  if (OB_SUCC(ret)) {
    ret = build_entry_helper_<Floor + 1, Truncate>(entry, helper, std::forward<Rest>(others)...);
  }
  return ret;
}

template <int Floor, bool Truncate>
int ObEventHistoryTableOperator::build_entry_helper_(ObServerEventHistoryEntry &entry, common::ObCStringHelperV2 &helper)
{
  // All fields processed, nothing to do
  UNUSED(helper);
  return OB_SUCCESS;
}

template <int Floor, bool Truncate, typename Value>
int ObEventHistoryTableOperator::build_entry_helper_(ObServerEventHistoryEntry &entry, common::ObCStringHelperV2 &helper, Value &&extra_info)
{
  static_assert(Floor == 6, "if there is an extra_info column, it must be 13th args in this row, no more, no less");
  int ret = OB_SUCCESS;
  common::ObString extra_info_str;

  // Use ObCStringHelperV2 to convert any type to string
  // Handle different types: string, ObString, or other types
  // Use ObCStringHelperV2 to convert any type to string
  const char *cstr = helper.convert(extra_info);
  if (OB_ISNULL(cstr)) {
    ret = helper.get_ob_errno();
    SHARE_LOG(WARN, "convert extra_info to string failed", K(ret));
    extra_info_str.reset();
  } else {
    extra_info_str.assign_ptr(cstr, static_cast<int32_t>(strlen(cstr)));
  }

  entry.extra_info_ = extra_info_str;
  return ret;
}


}//end namespace rootserver
}//end namespace oceanbase

#endif
