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

#ifndef OCEANBASE_SHARE_SCHEMA_OB_AI_COLUMN_SCHEMA_H_
#define OCEANBASE_SHARE_SCHEMA_OB_AI_COLUMN_SCHEMA_H_

#include "lib/container/ob_se_array.h"
#include "share/schema/ob_schema_struct.h"

namespace oceanbase
{
namespace share
{
namespace schema
{

// Stable row states are stored in __ai_state_<column_id>.  Transient claim
// states intentionally live only in the task table.
enum ObAIColumnValueState : int8_t
{
  AI_COLUMN_VALUE_STATE_INVALID = 0,
  AI_COLUMN_VALUE_STATE_PENDING,
  AI_COLUMN_VALUE_STATE_READY,
  AI_COLUMN_VALUE_STATE_FAILED,
  AI_COLUMN_VALUE_STATE_MAX
};

enum ObAIColumnTaskState : int8_t
{
  AI_COLUMN_TASK_STATE_INVALID = 0,
  AI_COLUMN_TASK_STATE_PENDING,
  AI_COLUMN_TASK_STATE_RUNNING,
  AI_COLUMN_TASK_STATE_RETRY,
  AI_COLUMN_TASK_STATE_FAILED,
  AI_COLUMN_TASK_STATE_MAX
};

class ObAIColumnSchema final : public ObSchema
{
  OB_UNIS_VERSION(1);
public:
  ObAIColumnSchema();
  explicit ObAIColumnSchema(common::ObIAllocator *allocator);
  virtual ~ObAIColumnSchema() = default;

  virtual void reset() override;
  virtual bool is_valid() const override;
  bool is_valid_for_persistence() const;
  virtual int64_t get_convert_size() const override;
  int assign(const ObAIColumnSchema &other);

  int set_column_name(const common::ObString &column_name) { return deep_copy_str(column_name, column_name_); }
  int set_expr_text(const common::ObString &expr_text) { return deep_copy_str(expr_text, expr_text_); }
  int set_normalized_expr(const common::ObString &normalized_expr) { return deep_copy_str(normalized_expr, normalized_expr_); }
  int set_time_zone(const common::ObString &time_zone) { return deep_copy_str(time_zone, time_zone_); }
  int add_dependency_column_id(const uint64_t column_id);
  int set_dependency_column_ids(const common::ObIArray<uint64_t> &column_ids);

  void set_table_id(const uint64_t table_id) { table_id_ = table_id; }
  void set_column_id(const uint64_t column_id) { column_id_ = column_id; }
  void set_sql_mode(const uint64_t sql_mode) { sql_mode_ = sql_mode; }
  void set_spec_version(const int64_t spec_version) { spec_version_ = spec_version; }
  void set_max_retries(const int64_t max_retries) { max_retries_ = max_retries; }
  void set_state_column_id(const uint64_t column_id) { state_column_id_ = column_id; }
  void set_request_column_id(const uint64_t column_id) { request_column_id_ = column_id; }
  void set_result_spec_column_id(const uint64_t column_id) { result_spec_column_id_ = column_id; }
  void set_schema_version(const int64_t schema_version) { schema_version_ = schema_version; }

  uint64_t get_table_id() const { return table_id_; }
  uint64_t get_column_id() const { return column_id_; }
  const common::ObString &get_column_name() const { return column_name_; }
  const common::ObString &get_expr_text() const { return expr_text_; }
  const common::ObString &get_normalized_expr() const { return normalized_expr_; }
  const common::ObIArray<uint64_t> &get_dependency_column_ids() const { return dependency_column_ids_; }
  uint64_t get_sql_mode() const { return sql_mode_; }
  const common::ObString &get_time_zone() const { return time_zone_; }
  int64_t get_spec_version() const { return spec_version_; }
  int64_t get_max_retries() const { return max_retries_; }
  uint64_t get_state_column_id() const { return state_column_id_; }
  uint64_t get_request_column_id() const { return request_column_id_; }
  uint64_t get_result_spec_column_id() const { return result_spec_column_id_; }
  int64_t get_schema_version() const { return schema_version_; }

  TO_STRING_KV(K_(table_id),
               K_(column_id),
               K_(column_name),
               K_(dependency_column_ids),
               K_(sql_mode),
               K_(spec_version),
               K_(max_retries),
               K_(state_column_id),
               K_(request_column_id),
               K_(result_spec_column_id),
               K_(schema_version));

private:
  uint64_t table_id_;
  uint64_t column_id_;
  common::ObString column_name_;
  common::ObString expr_text_;
  common::ObString normalized_expr_;
  common::ObSEArray<uint64_t, 4> dependency_column_ids_;
  uint64_t sql_mode_;
  common::ObString time_zone_;
  int64_t spec_version_;
  int64_t max_retries_;
  uint64_t state_column_id_;
  uint64_t request_column_id_;
  uint64_t result_spec_column_id_;
  int64_t schema_version_;

  DISALLOW_COPY_AND_ASSIGN(ObAIColumnSchema);
};

} // namespace schema
} // namespace share
} // namespace oceanbase

#endif // OCEANBASE_SHARE_SCHEMA_OB_AI_COLUMN_SCHEMA_H_
