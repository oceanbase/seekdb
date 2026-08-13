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

#include "pl/ob_pl_package.h"
#include "pl/ob_pl_dependency_util.h"
namespace oceanbase
{
using namespace common;
using namespace sql;
using namespace share::schema;

namespace pl
{

int ObPLPackageAST::init(const ObString &db_name,
                         const ObString &package_name,
                         ObPackageType package_type,
                         uint64_t database_id,
                         uint64_t package_id,
                         int64_t package_version,
                         ObPLPackageAST *parent_package_ast)
{
  int ret = OB_SUCCESS;
  const ObPLUserTypeTable *parent_user_type_table = NULL;
  ObPLRoutineTable *parent_routine_table = NULL;
  ObPLConditionTable *parent_condition_table = NULL;
  db_name_ = db_name;
  name_ = package_name;
  id_ = package_id;
  database_id_ = database_id;
  package_type_ = package_type;
  version_ = package_version;
  if (OB_NOT_NULL(parent_package_ast)) {
    compile_flag_ = parent_package_ast->get_compile_flag();
    parent_user_type_table = &parent_package_ast->get_user_type_table();
    parent_routine_table = &parent_package_ast->get_routine_table();
    parent_condition_table = &parent_package_ast->get_condition_table();
    if (parent_package_ast->get_compile_flag().compile_with_invoker_right()) {
      set_invoker_db_name(parent_package_ast->get_invoker_db_name());
      set_invoker_db_id(parent_package_ast->get_invoker_db_id());
    }
  }
  if (OB_NOT_NULL(parent_user_type_table)) {
    user_type_table_.set_type_start_gen_id(parent_user_type_table->get_type_start_gen_id());
  }
  if (OB_FAIL(routine_table_.init(parent_routine_table))) {
  }
  if (OB_NOT_NULL(parent_condition_table)) {
    OZ (condition_table_.init(*parent_condition_table));
  }

  if (OB_SUCC(ret)
      && parent_package_ast != NULL
      && !ObTriggerInfo::is_trigger_package_id(package_id)
      && PL_PACKAGE_BODY == package_type) {
    ObSchemaObjVersion obj_version;
    obj_version.object_id_ = parent_package_ast->get_id();
    obj_version.version_ = parent_package_ast->get_version();
    obj_version.object_type_ = DEPENDENCY_PACKAGE;
    if (OB_FAIL(ObPLDependencyUtil::add_dependency_object_impl(dependency_table_, obj_version))) {
    }
  }

  if (OB_SUCC(ret)) {
    ObSchemaObjVersion obj_version;
    if (ObTriggerInfo::is_trigger_package_id(package_id)) {
      obj_version.object_id_ = ObTriggerInfo::get_package_trigger_id(package_id);
      obj_version.object_type_ = DEPENDENCY_TRIGGER;
    } else {
      obj_version.object_id_ = package_id;
      obj_version.object_type_ = (PL_PACKAGE_SPEC == package_type) ? DEPENDENCY_PACKAGE : DEPENDENCY_PACKAGE_BODY;
    }
    obj_version.version_ = package_version;
    if (OB_FAIL(ObPLDependencyUtil::add_dependency_object_impl(dependency_table_, obj_version))) {
    }
  }
  if (OB_SUCC(ret)) {
    inited_ = true;
  }
  return ret;
}

int ObPLPackageAST::process_generic_type()
{
  int ret = OB_SUCCESS;
  static const char *generic_type_table[PL_GENERIC_MAX] = {
    "", // INVALID

    "<ADT_1>",
    "<RECORD_1>",
    "<TUPLE_1>",
    "<VARRAY_1>",
    "<V2_TABLE_1>",
    "<TABLE_1>",
    "<COLLECTION_1>",
    "", // retired Oracle cursor-type slot

    "<TYPED_TABLE>",
    "<ADT_WITH_OID>",
    " SYS$INT_V2TABLE",
    " SYS$BULK_ERROR_RECORD",
    " SYS$REC_V2TABLE",
    "<ASSOC_ARRAY_1>"
  };
  if (0 == get_name().case_compare("STANDARD")
      && 0 == get_db_name().case_compare(OB_SYS_DATABASE_NAME)) {
    const ObPLUserTypeTable &user_type_table = get_user_type_table();
    const ObUserDefinedType *user_type = NULL;
    for (int64_t i = 0; OB_SUCC(ret) && i < user_type_table.get_count(); ++i) {
      if (OB_NOT_NULL(user_type = user_type_table.get_type(i))) {
        for (int64_t j = 0; OB_SUCC(ret) && j < PL_GENERIC_MAX; ++j) {
          if (0 == user_type->get_name().case_compare(generic_type_table[j])) {
            (const_cast<ObUserDefinedType *>(user_type))
              ->set_generic_type(static_cast<ObPLGenericType>(j));
          }
        }
      }
    }
  }
  return ret;
}

ObPLPackage::~ObPLPackage()
{
  for (int64_t i = 0; i < var_table_.count(); ++i) {
    if (OB_NOT_NULL(var_table_.at(i))) {
      var_table_.at(i)->~ObPLVar();
    }
  }
  for (int64_t i = 0; i < type_table_.count(); ++i) {
    if (OB_NOT_NULL(type_table_.at(i))) {
      type_table_.at(i)->~ObUserDefinedType();
    }
  }
}

int ObPLPackage::init(const ObPLPackageAST &package_ast)
{
  int ret = OB_SUCCESS;
  database_id_ = package_ast.get_database_id();
  id_ = package_ast.get_id();
  version_ = package_ast.get_version();
  package_type_ = package_ast.get_package_type();
  if (OB_FAIL(ob_write_string(get_allocator(), const_cast<ObString &>(package_ast.get_db_name()), db_name_))) {
  } else if (OB_FAIL(ob_write_string(get_allocator(), const_cast<ObString &>(package_ast.get_name()), name_))) {
  } else {
    inited_ = true;
  }
  return ret;
}

int ObPLPackage::instantiate_package_state(const ObPLResolveCtx &resolve_ctx,
                                           ObExecContext &exec_ctx,
                                           ObPLPackageState &package_state)
{
  int ret = OB_SUCCESS;
  ObObj value;
  ARRAY_FOREACH(var_table_, var_idx) {
    const ObPLVar *var = var_table_.at(var_idx);
    const ObPLDataType &var_type = var->get_type();
    OZ (package_state.add_package_var_val(value, var_type.get_type()));
  }
  ARRAY_FOREACH(var_table_, var_idx) {
    const ObPLVar *var = var_table_.at(var_idx);
    const ObPLDataType &var_type = var->get_type();
    value.reset();
    if (OB_ISNULL(var)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("variable is null", K(ret), KPC(var), K(var_idx));
    } else if (var_type.is_cursor_type()
        && OB_FAIL(resolve_ctx.session_info_.init_cursor_cache())) {
      LOG_WARN("failed to init cursor cache", K(ret));
    } else if (OB_FAIL(var_type.init_session_var(resolve_ctx,
                                                 var_type.is_cursor_type() ?
                                                   package_state.get_pkg_cursor_allocator()
                                                   : package_state.get_pkg_allocator(),
                                                 exec_ctx,
                                                 (var->is_formal_param()) ? NULL : get_default_expr(var->get_default()),
                                                 var->is_default_construct(),
                                                 value))) {
    } else if (value.is_null_or_empty_string() && var_type.is_not_null()) {
      ret = OB_ERR_NUMERIC_OR_VALUE_ERROR;
      LOG_WARN("cannot assign null to var with not null attribution", K(ret));
    }
    OZ (package_state.set_package_var_val(var_idx, value, false));
    if (OB_SUCC(ret)) {
      const sql::ObSqlExpression *default_expr =
          var->is_formal_param() ? NULL : get_default_expr(var->get_default());
      if (OB_NOT_NULL(default_expr)
          && !IS_CONST_TYPE(default_expr->get_expr_items().at(0).get_item_type())) {
        resolve_ctx.session_info_.set_pl_can_retry(false);
      }
    }
    if (OB_FAIL(ret)) {
      ObUserDefinedType::destruct_objparam(package_state.get_pkg_allocator(), value, &(resolve_ctx.session_info_));
    }
  }
  if (OB_SUCC(ret)) {
    if (OB_FAIL(execute_init_routine(resolve_ctx.allocator_, exec_ctx))) {
    }
  }
  return ret;
}

int ObPLPackage::execute_init_routine(ObIAllocator &allocator, ObExecContext &exec_ctx)
{
  UNUSED(allocator);
  int ret = OB_SUCCESS;
  ObPLFunction *init_routine = routine_table_.at(ObPLRoutineTable::INIT_ROUTINE_IDX);
  if (OB_NOT_NULL(init_routine)) {
    pl::ObPL *pl_engine = NULL;
    CK (OB_NOT_NULL(exec_ctx.get_my_session()));
    CK (OB_NOT_NULL(pl_engine = exec_ctx.get_pl_engine()));

    if (OB_SUCC(ret)) {
      ParamStore params;
      ObObj result;
      int status;
      ObSEArray<int64_t, 2> subp_path;
      OZ (pl_engine->execute(exec_ctx,
                             exec_ctx.get_allocator(),
                             init_routine->get_package_id(),
                             init_routine->get_routine_id(),
                             subp_path,
                             params,
                             result,
                             &status,
                             false,
                             init_routine->is_function()));
    }
  } else {
    LOG_DEBUG("package init routine function is null",
              K(init_routine), K(get_name()), K(get_db_name()),
              K(get_id()), K(get_version()), K(get_package_type()));
  }
  return ret;
}



int ObPLPackage::get_var(const ObString &var_name, const ObPLVar *&var, int64_t &var_idx) const
{
  int ret = OB_SUCCESS;
  var = NULL;
  var_idx = OB_INVALID_INDEX;
  for (int64_t i = 0; OB_ISNULL(var) && i < var_table_.count(); ++i) {
    ObPLVar *tmp_var = var_table_.at(i);
    if (!tmp_var->is_formal_param()
        && ObCharset::case_insensitive_equal(var_name, tmp_var->get_name())) {
      if (tmp_var->is_dup_declare()) {
        ret = OB_ERR_SP_DUP_VAR;
        LOG_WARN("package var dup", K(ret), K(var_idx));
        LOG_USER_ERROR(OB_ERR_SP_DUP_VAR, tmp_var->get_name().length(), tmp_var->get_name().ptr());
      } else {
        var = tmp_var;
        var_idx = i;
      }
    }
  }
  return ret;
}

int ObPLPackage::get_var(int64_t var_idx, const ObPLVar *&var) const
{
  int ret = OB_SUCCESS;
  var = NULL;
  if (var_idx < 0 || var_idx >= var_table_.count()) {
     LOG_WARN("var index invalid", K(var_idx), K(ret));
  } else {
    var = var_table_.at(var_idx);
  }
  return ret;
}


int ObPLPackage::get_condition(const ObString &condition_name, const ObPLCondition *&value)
{
  int ret = OB_SUCCESS;
  value = NULL;
  for (int64_t i = 0; OB_SUCC(ret) && i < condition_table_.count(); ++i) {
    const ObPLCondition *tmp = condition_table_.at(i);
    if (OB_ISNULL(tmp)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("condition is null", K(ret), K(tmp));
    } else if (ObCharset::case_insensitive_equal(condition_name, tmp->get_name())) {
      value = tmp;
      break;
    }
  }
  return ret;
}

int ObPLPackage::get_cursor(int64_t cursor_idx, const ObPLCursor *&cursor) const
{
  int ret = OB_SUCCESS;
  cursor = NULL;
  if (cursor_table_.get_count() > cursor_idx && cursor_idx >= 0) {
    cursor = cursor_table_.get_cursor(cursor_idx);
    CK (OB_NOT_NULL(cursor));
  }
  return ret;
}

int ObPLPackage::get_cursor(
  int64_t package_id, int64_t routine_id, int64_t index, const ObPLCursor *&cursor) const
{
  int ret = OB_SUCCESS;
  cursor = NULL;
  for (int64_t i = 0; OB_SUCC(ret) && i < cursor_table_.get_count(); ++i) {
    const ObPLCursor *it = cursor_table_.get_cursor(i);
    CK (OB_NOT_NULL(it));
    if (it->get_package_id() == package_id
        && it->get_routine_id() == routine_id
        && it->get_index() == index) {
      cursor = it;
      break;
    }
  }
  return ret;
}

int ObPLPackage::get_cursor(
  const ObString &cursor_name, const ObPLCursor *&cursor, int64_t &cursor_idx) const
{
  int ret = OB_SUCCESS;
  cursor = NULL;
  cursor_idx = OB_INVALID_INDEX;
  for (int64_t i = 0; OB_SUCC(ret) && i < cursor_table_.get_count(); ++i) {
    const ObPLCursor *it = cursor_table_.get_cursor(i);
    const ObPLVar *v = NULL;
    CK (OB_NOT_NULL(it));
    CK (OB_NOT_NULL(v = get_var_table().at(it->get_index())));
    if (OB_SUCC(ret) && 0 == v->get_name().case_compare(cursor_name)) {
      cursor = it;
      cursor_idx = i;
      break;
    }
  }
  return ret;
}


int ObPLPackage::get_type(const common::ObString type_name, const ObUserDefinedType *&type) const
{
  int ret = OB_SUCCESS;
  type = NULL;
  for (int64_t i = 0; OB_SUCC(ret) && i < type_table_.count(); ++i) {
    const ObUserDefinedType *tmp_type = type_table_.at(i);
    if (ObCharset::case_insensitive_equal(type_name, tmp_type->get_name())) {
      if (OB_NOT_NULL(type)) {
        ret = OB_ERR_SP_DUP_TYPE;
        LOG_USER_ERROR(OB_ERR_SP_DUP_TYPE, type_name.length(), type_name.ptr());
      } else {
        type = tmp_type;
      }
    }
  }
  return ret;
}

int ObPLPackage::get_type(uint64_t type_id, const ObUserDefinedType *&type) const
{
  int ret = OB_SUCCESS;
  type = NULL;
  if (OB_INVALID_ID == type_id) {
    LOG_WARN("type id invalid", K(type_id), K(ret));
  } else {
    for (int64_t i = 0; OB_ISNULL(type) && i < type_table_.count(); ++i) {
      const ObUserDefinedType *tmp_type = type_table_.at(i);
      if (OB_ISNULL(tmp_type)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("user type invalid", K(type_id), K(ret));
      } else {
        if (tmp_type->get_user_type_id() == type_id) {
          type = tmp_type;
        }
      }
    }
  }
  return ret;
}
} // end namespace pl
} // end namespace oceanbase
