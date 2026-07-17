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

#define USING_LOG_PREFIX SQL_OPT
#include "sql/optimizer/ob_log_monitoring_dump.h"

using namespace oceanbase;
using namespace sql;
using namespace oceanbase::common;

namespace oceanbase
{
namespace sql
{

const char *ObLogMonitoringDump::get_name() const
{
  static const char *monitoring_name_[1] =
  {
    "MONITORING DUMP",
  };
  return monitoring_name_[0];
}

int ObLogMonitoringDump::est_cost()
{
  int ret = OB_SUCCESS;
  ObLogicalOperator *child = NULL;
  if (OB_ISNULL(child = get_child(first_child))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("get unexpected null", K(child), K(ret));
  } else {
    set_op_cost(0.0);
    set_cost(op_cost_ + child->get_cost());
    set_card(child->get_card());
  }
  return ret;
}

}
}
