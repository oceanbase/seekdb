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

#define USING_LOG_PREFIX SQL_SESSION

#include "sql/session/ob_local_session_var.h"
#include "sql/session/ob_basic_session_info.h"

using namespace oceanbase::common;
using namespace oceanbase::share;

namespace oceanbase
{
namespace sql
{

const ObSysVarClassType ObLocalSessionVarHelper::ALL_LOCAL_VARS[] = {
  SYS_VAR_TIME_ZONE,
  SYS_VAR_SQL_MODE,
  SYS_VAR_COLLATION_CONNECTION,
  SYS_VAR_MAX_ALLOWED_PACKET
};

int ObLocalSessionVarHelper::remove_vars_same_with_session(ObLocalSessionVar &local_vars,
                                                           const ObBasicSessionInfo *session)
{
  int ret = OB_SUCCESS;
  ObSEArray<const ObSessionSysVar *, 8> old_var_array;
  if (OB_ISNULL(session)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected null", K(ret), KP(session));
  } else if (OB_FAIL(local_vars.get_local_vars(old_var_array))) {
  } else {
    bool is_same = false;
    ObSEArray<const ObSessionSysVar *, 4> new_var_array;
    for (int64_t i = 0; OB_SUCC(ret) && i < old_var_array.count(); ++i) {
      if (OB_FAIL(check_var_same_with_session(*session, old_var_array.at(i), is_same))) {
      } else if (is_same) {
        /* do nothing */
      } else if (OB_FAIL(new_var_array.push_back(old_var_array.at(i)))) {
      }
    }
    if (OB_SUCC(ret) && new_var_array.count() != old_var_array.count()) {
      local_vars.reset();
      if (OB_FAIL(local_vars.set_local_var_capacity(new_var_array.count()))) {
      } else {
        for (int64_t i = 0; OB_SUCC(ret) && i < new_var_array.count(); ++i) {
          if (OB_FAIL(local_vars.add_local_var(new_var_array.at(i)))) {
          }
        }
      }
    }
  }
  return ret;
}

int ObLocalSessionVarHelper::get_different_vars_from_session(const ObLocalSessionVar &local_vars,
                                                             const ObBasicSessionInfo *session,
                                                             ObIArray<const ObSessionSysVar*> &local_diff_vars,
                                                             ObIArray<ObObj> &session_vals)
{
  int ret = OB_SUCCESS;
  ObSEArray<const ObSessionSysVar *, 8> var_array;
  local_diff_vars.reuse();
  if (OB_ISNULL(session)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected null", K(ret), KP(session));
  } else if (OB_FAIL(local_vars.get_local_vars(var_array))) {
  } else {
    bool is_same = false;
    ObObj session_val;
    for (int64_t i = 0; OB_SUCC(ret) && i < var_array.count(); ++i) {
      if (OB_ISNULL(var_array.at(i))) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("unexpected null", K(ret), K(i));
      } else if (SYS_VAR_SQL_MODE == var_array.at(i)->type_) {
        /* just ignore sql mode now */
      } else if (OB_FAIL(check_var_same_with_session(*session, var_array.at(i), is_same, &session_val))) {
      } else if (is_same) {
        /* do nothing */
      } else if (OB_FAIL(local_diff_vars.push_back(var_array.at(i)))) {
      } else if (OB_FAIL(session_vals.push_back(session_val))) {
      }
    }
  }
  return ret;
}

int ObLocalSessionVarHelper::check_var_same_with_session(const ObBasicSessionInfo &session,
                                                         const ObSessionSysVar *local_var,
                                                         bool &is_same,
                                                         ObObj *diff_val)
{
  int ret = OB_SUCCESS;
  is_same = false;
  ObObj session_val;
  if (OB_ISNULL(local_var)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected null", K(ret), KP(local_var));
  } else if (SYS_VAR_SQL_MODE == local_var->type_) {
    is_same = local_var->val_.get_uint64() == session.get_sql_mode();
    if (!is_same && NULL != diff_val) {
      diff_val->set_uint64(session.get_sql_mode());
    }
  } else if (OB_FAIL(session.get_sys_variable(local_var->type_, session_val))) {
  } else {
    is_same = local_var->is_equal(session_val);
    if (!is_same && NULL != diff_val) {
      *diff_val = session_val;
    }
  }
  return ret;
}

int ObLocalSessionVarHelper::load_session_vars(const ObBasicSessionInfo *session,
                                               ObLocalSessionVar &local_vars)
{
  int ret = OB_SUCCESS;
  int64_t var_num = sizeof(ALL_LOCAL_VARS) / sizeof(ObSysVarClassType);
  if (OB_ISNULL(session)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected null session", K(ret));
  } else if (0 != local_vars.get_var_count()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("local_session_vars can only be inited once", K(ret));
  } else if (OB_FAIL(local_vars.set_local_var_capacity(var_num))) {
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < var_num; ++i) {
      ObObj var;
      if (OB_FAIL(session->get_sys_variable(ALL_LOCAL_VARS[i], var))) {
      } else if (OB_FAIL(local_vars.add_local_var(ALL_LOCAL_VARS[i], var))) {
      }
    }
  }
  return ret;
}

int ObLocalSessionVarHelper::reserve_max_local_vars_capacity(ObLocalSessionVar &local_vars)
{
  int ret = OB_SUCCESS;
  int64_t var_num = sizeof(ALL_LOCAL_VARS) / sizeof(ObSysVarClassType);
  if (0 != local_vars.get_var_count()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("local_session_vars can only be inited once", K(ret));
  } else if (OB_FAIL(local_vars.set_local_var_capacity(var_num))) {
  }
  return ret;
}

int ObLocalSessionVarHelper::update_session_vars_with_local(const ObLocalSessionVar &local_vars,
                                                            ObBasicSessionInfo &session)
{
  int ret = OB_SUCCESS;
  ObSEArray<const ObSessionSysVar *, 8> var_array;
  if (OB_FAIL(local_vars.get_local_vars(var_array))) {
  }
  for (int64_t i = 0; OB_SUCC(ret) && i < var_array.count(); ++i) {
    if (OB_ISNULL(var_array.at(i))) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("unexpected null", K(ret));
    } else if (OB_FAIL(session.update_sys_variable(var_array.at(i)->type_, var_array.at(i)->val_))) {
    }
  }
  return ret;
}

int ObLocalSessionVarHelper::get_sys_var_val_str(const ObSysVarClassType var_type,
                                                 const ObObj &var_val,
                                                 ObIAllocator &allocator,
                                                 ObString &val_str)
{
  int ret = OB_SUCCESS;
  val_str.reset();
  char *buffer = NULL;
  int64_t length = 0;
  int64_t pos = 0;
  if (SYS_VAR_SQL_MODE == var_type) {
    ObObj res_obj;
    if (OB_FAIL(ob_sql_mode_to_str(var_val, res_obj, &allocator))) {
    } else if (OB_FAIL(res_obj.get_string(val_str))) {
    }
  } else if (OB_FAIL(var_val.print_sql_literal(buffer, length, pos, allocator))) {
  } else {
    val_str.assign(buffer, pos);
  }
  return ret;
}

} // namespace sql
} // namespace oceanbase
