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

#define USING_LOG_PREFIX SHARE_SCHEMA
#include "ob_trigger_info.h"

namespace oceanbase
{
using namespace common;
namespace share
{
namespace schema
{

OB_SERIALIZE_MEMBER((ObTriggerInfo, ObSimpleTriggerSchema),
//                  trigger_id_,
                    owner_id_,
//                  database_id_,
//                  schema_version_,
                    base_object_id_,
                    base_object_type_,
                    trigger_type_,
                    trigger_events_.bit_value_,
                    timing_points_.bit_value_,
                    trigger_flags_.bit_value_,
//                  trigger_name_,
                    update_columns_,
                    reference_names_[RT_OLD],
                    reference_names_[RT_NEW],
                    reference_names_[RT_PARENT],
                    when_condition_,
                    trigger_body_,
                    package_spec_info_,
                    package_body_info_,
                    sql_mode_,
                    priv_user_,
                    order_type_,
                    ref_trg_db_name_,
                    ref_trg_name_,
                    action_order_,
                    analyze_flag_);

ObTriggerInfo &ObTriggerInfo::operator =(const ObTriggerInfo &other)
{
  if (this != &other) {
    reset();
    int &ret = error_ret_;
    OZ (deep_copy(other));
  }
  return *this;
}

int ObTriggerInfo::assign(const ObTriggerInfo &other)
{
  int ret = OB_SUCCESS;
  this->operator=(other);
  ret = this->error_ret_;
  return ret;
}

void ObTriggerInfo::reset()
{
////trigger_id_ = OB_INVALID_ID;
  owner_id_ = OB_INVALID_ID;
//database_id_ = OB_INVALID_ID;
//schema_version_ = common::OB_INVALID_VERSION;;
  base_object_id_ = OB_INVALID_ID;
  base_object_type_ = OB_MAX_SCHEMA;
  trigger_type_ = TT_INVALID;
  trigger_events_.reset();
  timing_points_.reset();
  trigger_flags_.reset();
//reset_string(trigger_name_);
  reset_string(update_columns_);
  reset_string(reference_names_[RT_OLD]);
  reset_string(reference_names_[RT_NEW]);
  reset_string(reference_names_[RT_PARENT]);
  reset_string(when_condition_);
  reset_string(trigger_body_);
  // pl_flag / pl_exec_env are reset below.
  package_spec_info_.reset();
  package_body_info_.reset();
  reset_string(priv_user_);
  sql_mode_ = 0;
  order_type_ = OT_INVALID;
  reset_string(ref_trg_db_name_);
  reset_string(ref_trg_name_);
  action_order_ = 0;
  ObSimpleTriggerSchema::reset();
  analyze_flag_ = 0;
}

bool ObTriggerInfo::is_valid_for_create() const
{
  return ObSchema::is_valid() &&
         trigger_type_ != TT_INVALID &&
         trigger_events_.get_value() != 0 &&
         timing_points_.get_value() != 0 &&
         !reference_names_[RT_OLD].empty() &&
         !reference_names_[RT_NEW].empty() &&
         !reference_names_[RT_PARENT].empty() &&
         !trigger_name_.empty();
}

bool ObTriggerInfo::is_valid() const
{
  return ObSimpleTriggerSchema::is_valid() &&
         trigger_type_ != TT_INVALID &&
         trigger_events_.get_value() != 0 &&
         timing_points_.get_value() != 0 &&
         !reference_names_[RT_OLD].empty() &&
         !reference_names_[RT_NEW].empty() &&
         !reference_names_[RT_PARENT].empty();
//       !trigger_name_.empty();
}

int ObTriggerInfo::deep_copy(const ObTriggerInfo &other)
{
  int ret = OB_SUCCESS;
  OZ (ObSimpleTriggerSchema::deep_copy(other));
//OX (set_trigger_id(other.get_trigger_id()));
  OX (set_owner_id(other.get_owner_id()));
//OX (set_database_id(other.get_database_id()));
//OX (set_schema_version(other.get_schema_version()));
  OX (set_base_object_id(other.get_base_object_id()));
  OX (set_base_object_type(other.get_base_object_type()));
  OX (set_trigger_type(other.get_trigger_type()));
  OX (set_trigger_events(other.get_trigger_events()));
  OX (set_timing_points(other.get_timing_points()));
  OX (set_trigger_flags(other.get_trigger_flags()));
//OZ (set_trigger_name(other.get_trigger_name()));
  OZ (set_update_columns(other.get_update_columns()));
  OZ (set_ref_old_name(other.get_ref_old_name()));
  OZ (set_ref_new_name(other.get_ref_new_name()));
  OZ (set_ref_parent_name(other.get_ref_parent_name()));
  OZ (set_when_condition(other.get_when_condition()));
  OZ (set_trigger_body(other.get_trigger_body()));
  OZ (set_package_spec_source(other.get_package_spec_source()));
  OZ (set_package_body_source(other.get_package_body_source()));
  OX (set_package_flag(other.get_package_flag()));
  OZ (set_package_exec_env(other.get_package_exec_env()));
  OX (set_sql_mode(other.get_sql_mode()));
  OZ (set_trigger_priv_user(other.get_trigger_priv_user()));
  OX (set_order_type(other.get_order_type()));
  OZ (set_ref_trg_db_name(other.get_ref_trg_db_name()));
  OZ (set_ref_trg_name(other.get_ref_trg_name()));
  OX (set_action_order(other.get_action_order()));
  OX (set_analyze_flag(other.get_analyze_flag()));
  return ret;
}

int64_t ObTriggerInfo::get_convert_size() const
{
  int64_t convert_size = ObSimpleTriggerSchema::get_convert_size() +
                         sizeof(ObTriggerInfo) +
//                       trigger_name_.length() + 1 +
                         update_columns_.length() + 1 +
                         reference_names_[RT_OLD].length() + 1 +
                         reference_names_[RT_NEW].length() + 1 +
                         reference_names_[RT_PARENT].length() + 1 +
                         when_condition_.length() + 1 +
                         trigger_body_.length() + 1 +
                         priv_user_.length() + 1 +
                         package_spec_info_.get_convert_size() +
                         package_body_info_.get_convert_size() +
                         ref_trg_db_name_.length() + 1 +
                         ref_trg_name_.length();
  convert_size -= (sizeof(ObSimpleTriggerSchema) + sizeof(ObPackageInfo) * 2);
  return convert_size;
}

bool ObTriggerInfo::ActionOrderComparator::operator()(const ObTriggerInfo *left, const ObTriggerInfo *right)
{
  bool bool_ret = false;
  if (OB_UNLIKELY(OB_SUCCESS != ret_)) {
    // ignore
  } else if (OB_UNLIKELY(NULL == left) || OB_UNLIKELY(NULL == right)) {
    ret_ = OB_INVALID_ARGUMENT;
    LOG_WARN_RET(ret_,   "invalid argument", K(ret_), KP(left), KP(right));
  } else {
    bool_ret = (left->get_action_order() < right->get_action_order());
  }
  return bool_ret;
}

} // namespace schema
} // namespace share
} // namespace oceanbase
