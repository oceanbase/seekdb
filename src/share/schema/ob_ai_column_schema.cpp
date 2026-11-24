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

#include "share/schema/ob_ai_column_schema.h"

namespace oceanbase
{
namespace share
{
namespace schema
{

OB_SERIALIZE_MEMBER(ObAIColumnSchema,
                    table_id_,
                    column_id_,
                    column_name_,
                    expr_text_,
                    normalized_expr_,
                    dependency_column_ids_,
                    sql_mode_,
                    time_zone_,
                    spec_version_,
                    max_retries_,
                    state_column_id_,
                    request_column_id_,
                    result_spec_column_id_,
                    schema_version_);

ObAIColumnSchema::ObAIColumnSchema()
  : ObSchema(),
    dependency_column_ids_()
{
  reset();
}

ObAIColumnSchema::ObAIColumnSchema(common::ObIAllocator *allocator)
  : ObSchema(allocator),
    dependency_column_ids_(SCHEMA_SMALL_MALLOC_BLOCK_SIZE,
                           common::ModulePageAllocator(*allocator))
{
  reset();
}

void ObAIColumnSchema::reset()
{
  table_id_ = OB_INVALID_ID;
  column_id_ = OB_INVALID_ID;
  column_name_.reset();
  expr_text_.reset();
  normalized_expr_.reset();
  dependency_column_ids_.reset();
  sql_mode_ = 0;
  time_zone_.reset();
  spec_version_ = OB_INVALID_VERSION;
  max_retries_ = 0;
  state_column_id_ = OB_INVALID_ID;
  request_column_id_ = OB_INVALID_ID;
  result_spec_column_id_ = OB_INVALID_ID;
  schema_version_ = OB_INVALID_VERSION;
  ObSchema::reset();
}

bool ObAIColumnSchema::is_valid() const
{
  return is_valid_for_persistence()
      && OB_INVALID_ID != state_column_id_
      && OB_INVALID_ID != request_column_id_
      && OB_INVALID_ID != result_spec_column_id_;
}

bool ObAIColumnSchema::is_valid_for_persistence() const
{
  return ObSchema::is_valid()
      && OB_INVALID_ID != table_id_
      && OB_INVALID_ID != column_id_
      && !column_name_.empty()
      && !expr_text_.empty()
      && !normalized_expr_.empty()
      && !dependency_column_ids_.empty()
      && !time_zone_.empty()
      && spec_version_ > 0
      && max_retries_ >= 0
      && OB_INVALID_VERSION != schema_version_;
}

int64_t ObAIColumnSchema::get_convert_size() const
{
  return sizeof(*this)
      + column_name_.length() + 1
      + expr_text_.length() + 1
      + normalized_expr_.length() + 1
      + dependency_column_ids_.get_data_size()
      + time_zone_.length() + 1;
}

int ObAIColumnSchema::assign(const ObAIColumnSchema &other)
{
  int ret = OB_SUCCESS;
  if (this != &other) {
    reset();
    if (OB_FAIL(set_column_name(other.column_name_))) {
      LOG_WARN("failed to copy ai column name", K(ret));
    } else if (OB_FAIL(set_expr_text(other.expr_text_))) {
      LOG_WARN("failed to copy ai column expression", K(ret));
    } else if (OB_FAIL(set_normalized_expr(other.normalized_expr_))) {
      LOG_WARN("failed to copy normalized ai column expression", K(ret));
    } else if (OB_FAIL(set_dependency_column_ids(other.dependency_column_ids_))) {
      LOG_WARN("failed to copy ai column dependencies", K(ret));
    } else if (OB_FAIL(set_time_zone(other.time_zone_))) {
      LOG_WARN("failed to copy ai column time zone", K(ret));
    } else {
      table_id_ = other.table_id_;
      column_id_ = other.column_id_;
      sql_mode_ = other.sql_mode_;
      spec_version_ = other.spec_version_;
      max_retries_ = other.max_retries_;
      state_column_id_ = other.state_column_id_;
      request_column_id_ = other.request_column_id_;
      result_spec_column_id_ = other.result_spec_column_id_;
      schema_version_ = other.schema_version_;
    }
  }
  return ret;
}

int ObAIColumnSchema::add_dependency_column_id(const uint64_t column_id)
{
  int ret = OB_SUCCESS;
  if (OB_INVALID_ID == column_id) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid ai column dependency", K(ret), K(column_id));
  } else if (OB_FAIL(dependency_column_ids_.push_back(column_id))) {
    LOG_WARN("failed to add ai column dependency", K(ret), K(column_id));
  }
  return ret;
}

int ObAIColumnSchema::set_dependency_column_ids(const common::ObIArray<uint64_t> &column_ids)
{
  int ret = OB_SUCCESS;
  dependency_column_ids_.reset();
  for (int64_t i = 0; OB_SUCC(ret) && i < column_ids.count(); ++i) {
    if (OB_FAIL(add_dependency_column_id(column_ids.at(i)))) {
      LOG_WARN("failed to set ai column dependency", K(ret), K(i));
    }
  }
  return ret;
}

} // namespace schema
} // namespace share
} // namespace oceanbase
