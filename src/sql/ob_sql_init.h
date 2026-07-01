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

#ifndef _OB_SQL_INIT_H
#define _OB_SQL_INIT_H 1

#include "lib/alloc/malloc_hook.h"
#include "engine/ob_physical_plan.h"
#include "sql/engine/expr/ob_sql_expression.h"
#include "sql/engine/expr/ob_expr_operator.h"
#include "sql/engine/expr/ob_expr_uuid.h"
#include "sql/engine/expr/ob_expr_res_type_map.h"
#include "sql/engine/expr/ob_expr_extra_info_factory.h"
#include "sql/plan_cache/ob_plan_cache_value.h"
#include "sql/plan_cache/ob_plan_set.h"
#include "sql/plan_cache/ob_lib_cache_register.h"
#include "sql/executor/ob_task_runner_notifier_service.h"
#include "sql/engine/px/ob_px_sqc_handler.h"
#include "sql/ob_end_trans_callback.h"
#include "sql/plan_cache/ob_cache_object_factory.h"
#include "lib/alloc/ob_malloc_allocator.h"
#include "share/object/ob_obj_cast.h"
#include "engine/ob_serializable_function.h"

namespace oceanbase
{
namespace sql
{
//inline void register_phy_operator_classes()
//{
//  ObRootTransmit *root_trans = new (std::nothrow) ObRootTransmit();
//  delete root_trans;
//}

inline int init_sql_factories()
{
  //**Note**, do not delete this line of log, the log is for initializing ObLog's thread-local
  //variable LogBufferMgr, to avoid new operation on this thread-local variable in jit malloc hook,
  //which leads to a circular call between malloc hook and log module.
  SQL_LOG(INFO, "init sql factories");
  int ret = common::OB_SUCCESS;
  ObExprOperatorFactory::register_expr_operators();
  ObExprExtraInfoFactory::register_expr_extra_infos();
  ObLibCacheRegister::register_cache_objs();
  //register_phy_operator_classes();
  return OB_SUCCESS;
}

inline int init_sql_expr_static_var()
{
  int ret = common::OB_SUCCESS;
  static ObArenaAllocator allocator("init_sql");
  if (OB_FAIL(ObExprTRDateFormat::init())) {
    SQL_LOG(ERROR, "failed to init vars in oracle trunc", K(ret));
  } else if (OB_FAIL(ObExprUuid::init())) {
    SQL_LOG(ERROR, "failed to init vars in uuid", K(ret));
  } else if (OB_FAIL(common::ObNumberConstValue::init(allocator))) {
    SQL_LOG(ERROR, "failed to init ObNumberConstValue", K(ret));
  } else if (OB_FAIL(ARITH_RESULT_TYPE_ORACLE.init())) {
    SQL_LOG(ERROR, "failed to init ORACLE_ARITH_RESULT_TYPE", K(ret));
  } else if (OB_FAIL(ObCharset::init_charset())) {
    SQL_LOG(ERROR, "fail to init charset", K(ret));
  } else if (OB_FAIL(wide::ObDecimalIntConstValue::init_const_values(allocator))) {
    SQL_LOG(ERROR, "failed to init ObDecimalIntConstValue", K(ret));
  }
  return ret;
}

inline int init_sql_executor_singletons()
{
  int ret = common::OB_SUCCESS;
  if (OB_FAIL(ObTaskRunnerNotifierService::build_instance())) {
    SQL_LOG(ERROR, "fail to build ObTaskRunnerNotifierService instance", K(ret));
  } else {
    ObFuncSerialization::init();
  }
  if (OB_FAIL(ret)) {
    SQL_LOG(ERROR, "fail to init sql singletons", K(ret));
  }
  return ret;
}

inline void print_sql_stat()
{
  // do nothing
}
} // end namespace sql
} // end namespace oceanbase

#endif /* _OB_SQL_INIT_H */
