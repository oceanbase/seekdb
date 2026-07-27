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
#include "share/ob_sys_time_zone_util.h"
#include "share/schema/ob_multi_version_schema_service.h"
#include "share/ob_server_struct.h"
#include "share/ob_timezone_mgr.h"
#include "common/timezone/ob_timezone_info.h"
namespace oceanbase
{
using namespace common;
namespace share
{
int ObSysTimeZoneUtil::get_runtime_sys_time_zone_wrap(ObFixedLengthString<common::OB_MAX_TIMESTAMP_TZ_LENGTH> &time_zone,
    ObTimeZoneInfoWrap &time_zone_info_wrap)
{
  int ret = OB_SUCCESS;
  schema::ObMultiVersionSchemaService *schema_service = nullptr;
  ObSchemaGetterGuard schema_guard;
  ObTZMapWrap tz_map_wrap;
  const schema::ObSysVarSchema *var_schema = nullptr;
  ObTimeZoneInfoManager *tz_info_mgr = nullptr;
  if (OB_ISNULL(schema_service = GCTX.schema_service_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("schema service must not be null", K(ret));
  } else if (OB_FAIL(schema_service->get_runtime_schema_guard(schema_guard))) {
    LOG_WARN("failed to get_runtime_schema_guard", KR(ret));
  } else if (OB_FAIL(schema_guard.get_system_variable(share::SYS_VAR_SYSTEM_TIME_ZONE, var_schema))) {
    LOG_WARN("fail to get runtime system variable", K(ret));
  } else if (OB_ISNULL(var_schema)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("var schema must not be null", K(ret));
  } else if (OB_FAIL(OTTZ_MGR.get_timezone(tz_map_wrap, tz_info_mgr))) {
    LOG_WARN("failed to get time zone", K(ret));
  } else if (OB_FAIL(time_zone.assign(var_schema->get_value()))) {
    LOG_WARN("failed to assign timezone", K(ret));
  } else if (OB_FAIL(time_zone_info_wrap.init_time_zone(var_schema->get_value(), OB_INVALID_VERSION, 
             *(const_cast<ObTZInfoMap *>(tz_map_wrap.get_tz_map()))))) {
    LOG_WARN("failed to init time zone", K(ret));
  }
  return ret;
}


} // namespace share
} // namespace oceanbase
