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

#ifndef OCEANBASE_SHARE_AI_COLUMN_OB_AI_COLUMN_STRUCT_H_
#define OCEANBASE_SHARE_AI_COLUMN_OB_AI_COLUMN_STRUCT_H_

#include "lib/string/ob_string.h"
#include "lib/hash/ob_hashmap.h"
#include "share/ob_define.h"

namespace oceanbase
{
namespace share
{

// task states of __all_ai_column_task.state
enum ObAiColumnTaskState
{
  AI_TASK_INVALID = 0,
  AI_TASK_PENDING = 1,   // waiting for worker
  AI_TASK_RETRYING = 3,  // request failed, waiting for next round (test: state = 3)
  AI_TASK_MAX,
};

static const int64_t OB_AI_COLUMN_MAX_RETRIES_DEFAULT = 0;
static const char *OB_AI_STATE_COLUMN_PREFIX = "__ai_state_";
static const char *OB_AI_REQUEST_COLUMN_PREFIX = "__ai_request_";
static const char *OB_AI_RESULT_SPEC_COLUMN_PREFIX = "__ai_rsp_spec_";

// normalized_expr is persisted as a JSON object:
//   {"type":"complete"|"embed","model":"<model name>","tmpl":"Classify: {0}",
//    "args":["content", ...],"system":"<typed system prompt>","dim":N}
struct ObAiColumnExpr
{
  ObAiColumnExpr() : is_embed_(false), dim_(0) { reset(); }
  void reset()
  {
    is_embed_ = false;
    dim_ = 0;
    model_.reset();
    user_template_.reset();
    system_prompt_.reset();
    arg_columns_.reuse();
  }
  bool is_valid() const { return !model_.empty(); }
  bool is_embed() const { return is_embed_; }
  int serialize(common::ObIAllocator &allocator, common::ObString &out) const;
  int parse_from(const common::ObString &expr_str, common::ObIAllocator &allocator);
  bool is_embed_;
  int64_t dim_;  // only for embed
  common::ObString model_;
  common::ObString user_template_;   // e.g. "Classify: {0}"
  common::ObString system_prompt_;   // typed hint, may be empty
  common::ObSEArray<common::ObString, 4> arg_columns_;  // dependency column names (lower case)
};

// one row of __all_ai_column
struct ObAiColumnMeta
{
  ObAiColumnMeta()
      : table_id_(common::OB_INVALID_ID),
        column_id_(common::OB_INVALID_ID),
        max_retries_(0),
        state_column_id_(common::OB_INVALID_ID),
        request_column_id_(common::OB_INVALID_ID),
        result_spec_column_id_(common::OB_INVALID_ID)
  {}
  void reset();
  bool is_valid() const
  {
    return common::OB_INVALID_ID != table_id_ && common::OB_INVALID_ID != column_id_
        && !column_name_.empty() && expr_.is_valid();
  }
  uint64_t table_id_;
  common::ObString column_name_;
  uint64_t column_id_;
  int64_t max_retries_;
  uint64_t state_column_id_;
  uint64_t request_column_id_;
  uint64_t result_spec_column_id_;
  ObAiColumnExpr expr_;
  common::ObString normalized_expr_;  // serialized expr_, owned by caller allocator
};

// one row of __all_ai_column_task
struct ObAiColumnTask
{
  ObAiColumnTask()
      : table_id_(common::OB_INVALID_ID),
        column_id_(common::OB_INVALID_ID),
        state_(AI_TASK_PENDING),
        retry_count_(0),
        eligible_ts_(0)
  {}
  void reset();
  bool is_valid() const
  {
    return common::OB_INVALID_ID != table_id_ && common::OB_INVALID_ID != column_id_
        && !request_id_.empty() && !rowkey_predicate_.empty() && !column_name_.empty();
  }
  common::ObString request_id_;
  uint64_t table_id_;
  uint64_t column_id_;
  common::ObString column_name_;  // not persisted, filled when scanning for worker
  common::ObString rowkey_predicate_;  // e.g. "`id` = 2"
  int64_t state_;
  int64_t retry_count_;
  int64_t eligible_ts_;  // micro second since epoch
};

// build the typed system prompt for a column data type; empty for types without hint
void gen_ai_column_system_prompt(const common::ObObjType col_type,
                                 const bool is_embed,
                                 char *buf, const int64_t buf_len, int64_t &pos);

// format of rowkey_predicate: "`id` = 2" or "`a` = 1 AND `b` = 'x'" (backticks are
// always present; values are rendered as SQL literals, strings use single quotes)
int build_rowkey_predicate(const common::ObIArray<common::ObString> &rowkey_col_names,
                           const common::ObIArray<common::ObString> &rowkey_col_values,
                           common::ObString &predicate,
                           common::ObIAllocator &allocator);

}  // end namespace share
}  // end namespace oceanbase

#endif  // OCEANBASE_SHARE_AI_COLUMN_OB_AI_COLUMN_STRUCT_H_
