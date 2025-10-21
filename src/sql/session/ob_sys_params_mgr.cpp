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

#include "sql/session/ob_sys_params_mgr.h"

using namespace oceanbase::common;
using namespace oceanbase::sql;

namespace oceanbase
{
namespace sql
{
ObSysParamsMgr::ObSysParamsMgr()
{
  // default values are set here
  sort_mem_size_limit_ = 500000;  //500M
  group_mem_size_limit_ = 500000;  //500M
}

ObSysParamsMgr::~ObSysParamsMgr()
{
}

}//end of ns sql
}//end of ns oceanbase
