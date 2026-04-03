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

#include "ob_deadlock_inner_table_service.h"
#include "rootserver/ob_root_service.h"
#include "share/ob_server_struct.h"

namespace oceanbase
{
namespace share
{
namespace detector
{
using namespace common;

share::ObDeadlockEventHistoryTableStorage ObDeadLockInnerTableService::storage_;

int ObDeadLockInnerTableService::init()
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(GCTX.meta_db_pool_)) {
    ret = OB_NOT_INIT;
    DETECT_LOG(WARN, "meta_db_pool_ is not initialized", K(ret));
  } else if (OB_FAIL(storage_.init(GCTX.meta_db_pool_))) {
    DETECT_LOG(WARN, "failed to init deadlock event history storage", K(ret));
  }
  return ret;
}

#define INSERT_DEADLOCK_EVENT_SQL "\
  insert into %s \
  values (%lu, %lu, '%.*s', %d, %lu, \
  '%s', %ld, %ld, '%.*s', '%.*s', %lu, '%s', %lu,\
  '%.*s', '%.*s', '%.*s',\
  '%.*s', '%.*s', '%.*s', '%.*s', '%.*s', '%.*s')"

#define LIMIT_VARCHAR_LEN 128

static const char* extra_info_if_exist(const ObIArray<ObString> &extra_info, int64_t idx)
{
  return idx < extra_info.count() ? extra_info.at(idx).ptr() : "";
}

int ObDeadLockInnerTableService::insert(const ObDetectorInnerReportInfo &inner_info,
                                        int64_t idx,
                                        int64_t size,
                                        int64_t current_ts)
{
  int ret = OB_SUCCESS;
  const ObDetectorUserReportInfo &user_info = inner_info.get_user_report_info();
  const ObIArray<ObString> &extra_names = user_info.get_extra_columns_names();
  const ObIArray<ObString> &extra_values = user_info.get_extra_columns_values();

  DETECT_TIME_GUARD(100_ms);
  if (!storage_.is_inited()) {
    ret = OB_NOT_INIT;
    DETECT_LOG(WARN, "storage not init", KR(ret));
  } else {
    ObDeadlockEventHistoryEntry entry;
    entry.tenant_id_ = inner_info.get_tenant_id();
    entry.event_id_ = inner_info.get_event_id();
    entry.svr_addr_ = inner_info.get_addr();
    entry.detector_id_ = inner_info.get_detector_id();
    entry.report_time_ = current_ts;
    entry.cycle_idx_ = idx;
    entry.cycle_size_ = size;
    entry.role_ = inner_info.get_role();
    entry.priority_level_ = ObString(inner_info.get_priority().get_range_str());
    entry.priority_ = inner_info.get_priority().get_value();
    entry.create_time_ = inner_info.get_created_time();
    entry.start_delay_ = inner_info.get_start_delay();
    entry.module_ = user_info.get_module_name();
    entry.visitor_ = user_info.get_resource_visitor();
    entry.object_ = user_info.get_required_resource();
    if (extra_names.count() > 0) {
      entry.extra_name1_ = extra_names.at(0);
    }
    if (extra_values.count() > 0) {
      entry.extra_value1_ = extra_values.at(0);
    }
    if (extra_names.count() > 1) {
      entry.extra_name2_ = extra_names.at(1);
    }
    if (extra_values.count() > 1) {
      entry.extra_value2_ = extra_values.at(1);
    }
    if (extra_names.count() > 2) {
      entry.extra_name3_ = extra_names.at(2);
    }
    if (extra_values.count() > 2) {
      entry.extra_value3_ = extra_values.at(2);
    }

    if (CLICK() && OB_FAIL(storage_.insert(entry))) {
      DETECT_LOG(WARN, "failed to insert deadlock event", KR(ret));
    } else {
      DETECT_LOG(INFO, "insert deadlock event success", KR(ret));
    }
  }

  return ret;
  #undef CLICK_GUARD
}

int ObDeadLockInnerTableService::insert_all(const ObIArray<ObDetectorInnerReportInfo> &infos)
{
  int ret = OB_SUCCESS;

  DETECT_TIME_GUARD(100_ms);
  if (!storage_.is_inited()) {
    ret = OB_NOT_INIT;
    DETECT_LOG(WARN, "storage not init", KR(ret));
  } else {
    const int64_t current_ts = ObClockGenerator::getRealClock();
    ObArray<ObDeadlockEventHistoryEntry> entries;

    for (int64_t i = 0; OB_SUCC(ret) && i < infos.count(); ++i) {
      const ObDetectorInnerReportInfo &info = infos.at(i);
      const ObDetectorUserReportInfo &user_info = info.get_user_report_info();
      const ObIArray<ObString> &extra_names = user_info.get_extra_columns_names();
      const ObIArray<ObString> &extra_values = user_info.get_extra_columns_values();

      ObDeadlockEventHistoryEntry entry;
      entry.tenant_id_ = info.get_tenant_id();
      entry.event_id_ = info.get_event_id();
      entry.svr_addr_ = info.get_addr();
      entry.detector_id_ = info.get_detector_id();
      entry.report_time_ = current_ts;
      entry.cycle_idx_ = i + 1;
      entry.cycle_size_ = infos.count();
      entry.role_ = info.get_role();
      entry.priority_level_ = ObString(info.get_priority().get_range_str());
      entry.priority_ = info.get_priority().get_value();
      entry.create_time_ = info.get_created_time();
      entry.start_delay_ = info.get_start_delay();
      entry.module_ = user_info.get_module_name();
      entry.visitor_ = user_info.get_resource_visitor();
      entry.object_ = user_info.get_required_resource();
      if (extra_names.count() > 0) {
        entry.extra_name1_ = extra_names.at(0);
      }
      if (extra_values.count() > 0) {
        entry.extra_value1_ = extra_values.at(0);
      }
      if (extra_names.count() > 1) {
        entry.extra_name2_ = extra_names.at(1);
      }
      if (extra_values.count() > 1) {
        entry.extra_value2_ = extra_values.at(1);
      }
      if (extra_names.count() > 2) {
        entry.extra_name3_ = extra_names.at(2);
      }
      if (extra_values.count() > 2) {
        entry.extra_value3_ = extra_values.at(2);
      }

      if (CLICK() && OB_FAIL(entries.push_back(entry))) {
        DETECT_LOG(WARN, "failed to push back entry", KR(ret));
      }
    }

    if (OB_SUCC(ret) && !entries.empty()) {
      if (CLICK() && OB_FAIL(storage_.insert_all(entries))) {
        DETECT_LOG(WARN, "failed to insert all deadlock events", KR(ret));
      } else {
        DETECT_LOG(INFO, "insert all deadlock events success", KR(ret), K(entries.count()));
      }
    }
  }

  return ret;
}

// called by rs
int ObDeadLockInnerTableService::ObDeadLockEventHistoryTableOperator::async_delete()
{
  int ret = OB_SUCCESS;
  const int64_t now = ObClockGenerator::getRealClock();
  const int64_t rs_delete_timestap = now - REMAIN_RECORD_DURATION;

  DETECT_TIME_GUARD(3_s);
  if (!storage_.is_inited()) {
    ret = OB_NOT_INIT;
    DETECT_LOG(WARN, "storage not init", KR(ret));
  } else if (CLICK() && OB_FAIL(storage_.delete_expired(rs_delete_timestap, 1024))) {
    DETECT_LOG(WARN, "failed to delete expired records", KR(ret));
  } else {
    DETECT_LOG(INFO, "delete expired deadlock event history success", KR(ret));
  }

  return ret;
}

ObDeadLockInnerTableService::ObDeadLockEventHistoryTableOperator
  &ObDeadLockInnerTableService::ObDeadLockEventHistoryTableOperator::get_instance()
{
  static ObDeadLockInnerTableService::ObDeadLockEventHistoryTableOperator op;
  return op;
}

}// namespace detector
}// namespace share
}// namespace oceanbase
