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

#define USING_LOG_PREFIX SHARE
#include "share/ob_scheduled_job_utils.h"
#include "lib/string/ob_sql_string.h"
#include "share/ob_timezone_mgr.h"
#include "lib/oblog/ob_log_module.h"

namespace oceanbase
{
using namespace common;
namespace share
{

int ObScheduledJobUtils::get_time_zone_offset(const ObSysVariableSchema &sys_variable,
                                                       int32_t &offset_sec)
{
  int ret = OB_SUCCESS;
  const ObSysVarSchema *sysvar_schema = NULL;
  if (OB_FAIL(sys_variable.get_sysvar_schema(share::SYS_VAR_TIME_ZONE, sysvar_schema))) {
  } else if (OB_ISNULL(sysvar_schema)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("get unexpected null", K(ret), K(sysvar_schema));
  } else {
    ObArenaAllocator calc_buf(ObModIds::OB_SQL_PARSER);
    char *buf = NULL;
    int32_t buf_len = sysvar_schema->get_value().length();
    if (OB_ISNULL(buf = static_cast<char*>(calc_buf.alloc(buf_len)))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("failed to allocate memory", K(ret), K(buf_len));
    } else {
      MEMCPY(buf, sysvar_schema->get_value().ptr(), buf_len);
      ObString trimed_tz_str(buf_len, buf);
      trimed_tz_str = trimed_tz_str.trim();
      int ret_more = OB_SUCCESS;
      if (OB_FAIL(ObTimeConverter::str_to_offset(trimed_tz_str, offset_sec, ret_more,
                                                 true))) {
        if (ret != OB_ERR_UNKNOWN_TIME_ZONE) {
          LOG_WARN("fail to convert str_to_offset", K(trimed_tz_str), K(ret));
        } else if (ret_more != OB_SUCCESS) {
          ret = ret_more;
          LOG_WARN("invalid time zone hour or minute", K(trimed_tz_str), K(ret));
        }
      }
      if (OB_ERR_UNKNOWN_TIME_ZONE == ret) {
        ObTimeZoneInfoPos tz_info;
        ObTZMapWrap tz_map_wrap;
        ObTimeZoneInfoManager *tz_info_mgr = NULL;
        if (OB_FAIL(OTTZ_MGR.get_timezone(tz_map_wrap, tz_info_mgr))) {
        } else if (OB_ISNULL(tz_info_mgr)) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("tz info mgr is null", K(ret));
        } else if (OB_FAIL(tz_info_mgr->find_time_zone_info(trimed_tz_str, tz_info))) {
        } else if (OB_FAIL(tz_info.get_timezone_offset(ObTimeUtility::current_time(), offset_sec))) {
        } else {/*do nothing*/}
      }
    }
  }
  return ret;
}

int ObScheduledJobUtils::check_job_exists(common::ObMySQLProxy *sql_proxy,
                                                   const char* job_name,
                                                   bool &is_join_exists)
{
  int ret = OB_SUCCESS;
  is_join_exists = false;
  ObSqlString select_sql;
  int64_t row_count = 0;
  if (OB_FAIL(select_sql.append_fmt("SELECT count(*) FROM %s WHERE job_name = '%s';",
                                    share::OB_ALL_SCHEDULER_JOB_TNAME,
                                    job_name))) {
  } else {
    SMART_VAR(ObMySQLProxy::MySQLResult, proxy_result) {
      sqlclient::ObMySQLResult *client_result = NULL;
      auto &sql_client_retry_weak = *sql_proxy;
      if (OB_FAIL(sql_client_retry_weak.read(proxy_result, select_sql.ptr()))) {
      } else if (OB_ISNULL(client_result = proxy_result.get_result())) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("failed to execute sql", K(ret));
      } else {
        //expected only get one row.
        while (OB_SUCC(ret) && OB_SUCC(client_result->next())) {
          int64_t idx = 0;
          ObObj obj;
          if (OB_FAIL(client_result->get_obj(idx, obj))) {
          } else if (OB_FAIL(obj.get_int(row_count))) {
          } else if (OB_UNLIKELY(row_count != 2 && row_count != 0)) {
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("get unexpected error", K(ret), K(row_count));
          } else {
            is_join_exists = row_count > 0;
          }
        }
        ret = OB_ITER_END == ret ? OB_SUCCESS : ret;
      }
      int tmp_ret = OB_SUCCESS;
      if (NULL != client_result) {
        if (OB_SUCCESS != (tmp_ret = client_result->close())) {
          LOG_WARN("close result set failed", K(ret), K(tmp_ret));
          ret = COVER_SUCC(tmp_ret);
        }
      }
    }
    LOG_INFO("succeed to check job exists", K(ret), K(select_sql), K(is_join_exists), K(row_count));
  }
  return ret;
}

} // namespace share
} // namespace oceanbase
