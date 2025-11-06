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

#ifndef OCEANBASE_SHARE_SYSTEM_VARIABLE_OB_SYSTEM_VARIABLE_INIT_
#define OCEANBASE_SHARE_SYSTEM_VARIABLE_OB_SYSTEM_VARIABLE_INIT_
#include "share/system_variable/ob_sys_var_class_type.h"
#include <stdint.h>
#include "common/object/ob_object.h"
namespace oceanbase
{
namespace share
{
// The value of ObSysVarFlag must not be arbitrarily added, deleted, or modified; any additions, deletions, or modifications must be synchronized to the flag_value_dict variable in sql/session/gen_ob_sys_variables.py at the same time
struct ObSysVarFlag
{
  const static int64_t NONE = 0LL;
  const static int64_t GLOBAL_SCOPE = 1LL;
  const static int64_t SESSION_SCOPE = (1LL << 1);
  const static int64_t READONLY = (1LL << 2);
  const static int64_t SESSION_READONLY = (1LL << 3);
  const static int64_t INVISIBLE = (1LL << 4);
  const static int64_t NULLABLE = (1LL << 5);
  const static int64_t INFLUENCE_PLAN = (1LL << 6);
  const static int64_t NEED_SERIALIZE = (1LL << 7);
  const static int64_t QUERY_SENSITIVE = (1LL << 8);
  const static int64_t ORACLE_ONLY = (1LL << 9);
  const static int64_t WITH_CREATE = (1LL << 10);
  const static int64_t WITH_UPGRADE = (1LL << 11);
  const static int64_t MYSQL_ONLY = (1LL << 12);
  const static int64_t INFLUENCE_PL = (1LL << 13);
};
struct ObSysVarFromJson{
  ObSysVarClassType id_;
  common::ObString name_;
  common::ObObjType data_type_;
  common::ObString default_value_; // used for init tenant
  common::ObString base_value_; // used for session sync
  common::ObString min_val_;
  common::ObString max_val_;
  common::ObString enum_names_;
  common::ObString info_;
  int64_t flags_;
  common::ObString alias_;
  common::ObString base_class_;
  common::ObString on_check_and_convert_func_;
  common::ObString on_update_func_;
  common::ObString to_select_obj_func_;
  common::ObString to_show_str_func_;
  common::ObString get_meta_type_func_;
  common::ObString session_special_update_func_;

  ObSysVarFromJson():id_(SYS_VAR_INVALID), name_(""), data_type_(common::ObNullType), default_value_(""), base_value_(""), min_val_(""), max_val_(""), enum_names_(""), info_(""), flags_(ObSysVarFlag::NONE), alias_(""), base_class_(""), on_check_and_convert_func_(), on_update_func_(), to_select_obj_func_(), to_show_str_func_(), get_meta_type_func_(), session_special_update_func_() {}
};

class ObSysVariables
{
public:
  static ObSysVarClassType get_sys_var_id(int64_t i);
  static common::ObString get_name(int64_t i);
  static common::ObObjType get_type(int64_t i);
  static common::ObString get_value(int64_t i);
  static common::ObString get_base_str_value(int64_t i);
  static common::ObString get_min(int64_t i);
  static common::ObString get_max(int64_t i);
  static common::ObString get_info(int64_t i);
  static int64_t get_flags(int64_t i);
  static const common::ObObj &get_default_value(int64_t i);
  static const common::ObObj &get_base_value(int64_t i);
  static int64_t get_amount();
  static ObCollationType get_default_sysvar_collation();
  static int set_value(const char *name, const char * new_value);
  static int set_value(const common::ObString &name, const common::ObString &new_value);
  static int set_base_value(const char *name, const char * new_value);
  static int set_base_value(const common::ObString &name, const common::ObString &new_value);
  static int init_default_values();
};

class ObSysVarsToIdxMap
{
public:
  static int64_t get_store_idx(int64_t var_id);
  static bool has_invalid_sys_var_id();
};

} // end namespace share
} // end namespace oceanbase

#endif /* OCEANBASE_SHARE_SYSTEM_VARIABLE_OB_SYSTEM_VARIABLE_INIT_ */
