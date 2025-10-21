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
#include "ob_dbms_external_table.h"
#include "share/external_table/ob_external_table_file_mgr.h"

namespace oceanbase
{
using namespace sql;
using namespace common;

namespace pl
{


int ObDBMSExternalTable::auto_refresh_external_table(ObExecContext &exec_ctx, ParamStore &params, ObObj &result)
{
  int ret = OB_SUCCESS;
  int32_t interval = 0;
  if (params.count() == 0) {
    interval = 0;
  } else {
    const ObObjParam &param0 = params.at(0);
    if (param0.is_null()) {
      //do nothing
    } else if (OB_FAIL(param0.get_int32(interval))) {
      LOG_WARN("failed to get number", K(ret), K(param0));
    }
  }

  if (OB_SUCC(ret)) {
    OZ (ObExternalTableFileManager::get_instance().auto_refresh_external_table(exec_ctx, interval));
  }
  return ret;
}

} // end of pl
} // end oceanbase
