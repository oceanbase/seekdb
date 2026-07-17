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

#define USING_LOG_PREFIX PL

#include "pl/sys_package/ob_dbms_mview_mysql.h"
#include "storage/mview/cmd/ob_mview_purge_log_executor.h"
#include "storage/mview/cmd/ob_mview_refresh_executor.h"

namespace oceanbase
{
namespace pl
{
using namespace common;
using namespace sql;
using namespace storage;

/*
PROCEDURE purge_log(
    IN     master_name            VARCHAR(65535),
    IN     purge_log_parallel     INT            DEFAULT 1);
*/
int ObDBMSMViewMysql::purge_log(ObExecContext &ctx, ParamStore &params, ObObj &result)
{
  UNUSED(result);
  int ret = OB_SUCCESS;
  if (2 != params.count() 
      || !params.at(0).is_varchar() 
      || !params.at(1).is_int32()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument for mlog purge", KR(ret));
  }
  if (OB_SUCC(ret)) {
    ObMViewPurgeLogArg purge_params;
    ObMViewPurgeLogExecutor purge_executor;
    // fill params
    purge_params.master_ = params.at(0).get_varchar();
    purge_params.purge_log_parallel_ = params.at(1).get_int() >= 0 ? params.at(1).get_int() : 1;
    if (OB_FAIL(purge_executor.execute(ctx, purge_params))) {
      LOG_WARN("fail to execute mlog purge", KR(ret), K(purge_params));
    }
  }
  return ret;
}

/*
PROCEDURE refresh(
    IN     mv_name                VARCHAR(65535),
    IN     method                 VARCHAR(65535) DEFAULT NULL,
    IN     refresh_parallel       INT            DEFAULT 1,
    IN     nested                 BOOLEAN        DERAULT FALSE); -- 4.3.5.3
    IN     nested_refresh_mode    VARCHAR(65535) DEFAULT NULL); -- 4.3.5.3 
*/
int ObDBMSMViewMysql::refresh(ObExecContext &ctx, ParamStore &params, ObObj &result)
{
  UNUSED(result);
  int ret = OB_SUCCESS;
  common::ObObj nested(false);
  ObString nested_refresh_mode;
  bool nested_consistent_refresh = false;

  if (params.count() < ObDBMSMViewRefreshParam::MAX_PARAM) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected params count", K(ret), K(params.count()));
  } else if (params.count() == ObDBMSMViewRefreshParam::MAX_PARAM) {
    if (!params.at(ObDBMSMViewRefreshParam::NESTED).is_tinyint() ||
        (!params.at(ObDBMSMViewRefreshParam::NESTED_REFRESH_MODE).is_null() &&
          !params.at(ObDBMSMViewRefreshParam::NESTED_REFRESH_MODE).is_varchar())) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("invalid argument for materialized view refresh", K(ret));
    } else {
      nested.reset();
      // copy params to obj
      params.at(ObDBMSMViewRefreshParam::NESTED).copy_value_or_obj(nested, true);
      if (!params.at(ObDBMSMViewRefreshParam::NESTED_REFRESH_MODE).is_null()) {
        if (!nested.get_bool()) {
          ret = OB_INVALID_ARGUMENT;
          LOG_WARN("nested is false, invalid argument", K(ret));
        } else {
          nested_refresh_mode = params.at(ObDBMSMViewRefreshParam::NESTED_REFRESH_MODE).get_varchar();
          if (0 == nested_refresh_mode.case_compare(ObString("CONSISTENT"))) {
            nested_consistent_refresh = true;
          } else if (0 == nested_refresh_mode.case_compare(ObString("INCONSISTENT"))) {
            nested_consistent_refresh = false;
          } else {
            ret = OB_INVALID_ARGUMENT;
            LOG_WARN("invalid argument", K(ret), K(nested_refresh_mode));
          }
          LOG_INFO("get consistent param", KR(ret), K(nested_refresh_mode), K(nested_consistent_refresh));
        }
      } else {
        nested_consistent_refresh = false;
      }
    }
  } else {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected params count", K(ret), K(params.count()));
  }
  if (OB_FAIL(ret)) {
  } else if (!params.at(ObDBMSMViewRefreshParam::MV_LIST).is_varchar() ||
             (!params.at(ObDBMSMViewRefreshParam::METHOD).is_null() &&
              !params.at(ObDBMSMViewRefreshParam::METHOD).is_varchar()) ||
             !params.at(ObDBMSMViewRefreshParam::REFRESH_PARALLEL).is_int32()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument for materialized view refresh", K(ret));
  }
  if (OB_SUCC(ret)) {
    ObMViewRefreshArg refresh_params;
    ObMViewRefreshExecutor refresh_executor;
    refresh_params.list_ =
            params.at(ObDBMSMViewRefreshParam::MV_LIST).get_varchar();
    refresh_params.method_ = 
            params.at(ObDBMSMViewRefreshParam::METHOD).is_varchar() ?
            params.at(ObDBMSMViewRefreshParam::METHOD).get_varchar() : NULL;
    refresh_params.refresh_parallel_ = 
            params.at(ObDBMSMViewRefreshParam::REFRESH_PARALLEL).get_int();
    refresh_params.nested_ = nested.get_bool();
    refresh_params.nested_consistent_refresh_ = nested_consistent_refresh; 
    if (OB_FAIL(refresh_executor.execute(ctx, refresh_params))) {
      LOG_WARN("fail to execute mview refresh", KR(ret), K(refresh_params));
    }
  }
  return ret;
}

} // namespace pl
} // namespace oceanbase
