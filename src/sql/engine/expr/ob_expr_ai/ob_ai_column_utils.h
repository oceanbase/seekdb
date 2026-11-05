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

#pragma once

#include "lib/string/ob_sql_string.h"
#include "lib/container/ob_se_array.h"
#include "common/object/ob_object.h"

namespace oceanbase
{
namespace common
{
class ObMySQLProxy;
}
namespace sql
{
class ObExecContext;
}
namespace share
{
namespace schema
{
class ObTableSchema;
class ObColumnSchemaV2;
}
}
namespace sql
{

// In-memory description of one AI column. The durable copy lives in
// __all_ai_column plus the comment of the hidden result-spec column.
struct ObAiColumnSpec
{
  ObAiColumnSpec()
    : result_column_(),
      model_name_(),
      normalized_expr_(),
      dependency_columns_(),
      prompt_type_(0),
      max_retries_(0),
      target_type_(common::ObNullType),
      target_length_(-1),
      target_precision_(-1),
      target_scale_(-1),
      result_column_id_(common::OB_INVALID_ID),
      state_column_id_(common::OB_INVALID_ID),
      request_column_id_(common::OB_INVALID_ID),
      result_spec_column_id_(common::OB_INVALID_ID)
  {
  }
  void reset()
  {
    result_column_.reset();
    model_name_.reset();
    normalized_expr_.reset();
    dependency_columns_.reset();
    dependency_column_ids_.reset();
    prompt_type_ = 0;
    max_retries_ = 0;
    target_type_ = common::ObNullType;
    target_length_ = -1;
    target_precision_ = -1;
    target_scale_ = -1;
    result_column_id_ = common::OB_INVALID_ID;
    state_column_id_ = common::OB_INVALID_ID;
    request_column_id_ = common::OB_INVALID_ID;
    result_spec_column_id_ = common::OB_INVALID_ID;
  }
  TO_STRING_KV(K_(result_column), K_(model_name), K_(normalized_expr),
               K_(dependency_columns), K_(prompt_type), K_(max_retries),
               K_(target_type), K_(result_column_id), K_(state_column_id),
               K_(request_column_id), K_(result_spec_column_id));

  common::ObString result_column_;
  common::ObString model_name_;
  common::ObString normalized_expr_;
  common::ObSEArray<common::ObString, 4> dependency_columns_;
  common::ObSEArray<uint64_t, 4> dependency_column_ids_;
  int64_t prompt_type_;   // 0 text, 1 json, 2 integer, 3 vector
  int64_t max_retries_;
  int64_t target_type_;
  int64_t target_length_;
  int64_t target_precision_;
  int64_t target_scale_;
  uint64_t result_column_id_;
  uint64_t state_column_id_;
  uint64_t request_column_id_;
  uint64_t result_spec_column_id_;
};

class ObAiColumnUtils
{
public:
  static constexpr const char *SPEC_PREFIX = "__ai_spec_";
  static constexpr const char *STATE_PREFIX = "__ai_state_";
  static constexpr const char *REQUEST_PREFIX = "__ai_req_";

  static bool is_spec_column(const common::ObString &name);
  static bool is_state_column(const common::ObString &name);
  static bool is_request_column(const common::ObString &name);

  // Serialize/deserialize the fixed key=value text stored in the comment of
  // the hidden result-spec column. The expression and model are base64 encoded
  // so that arbitrary SQL text cannot break the field separators.
  static int build_spec_text(const ObAiColumnSpec &spec,
                             common::ObIAllocator &alloc,
                             common::ObString &text);
  static int parse_spec_text(const common::ObString &text,
                             common::ObIAllocator &alloc,
                             ObAiColumnSpec &spec);

  // Fill the ai column descriptions of a table from its hidden spec columns.
  static int load_table_ai_columns(const share::schema::ObTableSchema &table_schema,
                                   common::ObIAllocator &alloc,
                                   common::ObIArray<ObAiColumnSpec> &specs);
  // Locate the description of the AI column whose visible result column is
  // result_column.
  static int find_ai_column(const common::ObIArray<ObAiColumnSpec> &specs,
                            const common::ObString &result_column,
                            const ObAiColumnSpec *&spec);

  static const char *system_prompt_for(int64_t prompt_type);

  // Persist the __all_ai_column rows of one table. When only_results is not
  // null only those result columns are written (used by ALTER TABLE ADD).
  static int write_ai_column_metadata(common::ObMySQLProxy *sql_proxy,
                                      const share::schema::ObTableSchema &table_schema,
                                      uint64_t table_id,
                                      const common::ObIArray<common::ObString> *only_results = nullptr);

  // Reject a statement that references an AI column which still has rows in
  // PENDING/FAILED state while ai_column_strict_read is on.
  static int check_strict_read(ObExecContext &ctx,
                               uint64_t table_id,
                               const common::ObIArray<uint64_t> &access_column_ids);

  static uint32_t next_task_id();
  static uint64_t next_request_id();
};

} // namespace sql
} // namespace oceanbase
