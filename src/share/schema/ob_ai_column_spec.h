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
  *
 * // OB-P:a4L9E_IDIyEM2Ol_ZvDXqw
*/

#pragma once

#include "lib/string/ob_string.h"
#include "lib/container/ob_array.h"
#include "lib/allocator/ob_allocator.h"

namespace oceanbase
{
namespace share
{
namespace schema
{
class ObColumnSchemaV2;

// Specification of an asynchronous AI column (AI COLUMN).
//
// The spec is persisted as a JSON document in the AI result column's
// cur_default_value_ (the same storage used by generated columns for their
// expression text), so that it survives restarts and travels with the column
// schema through DDL arguments. It is re-parsed on schema load and on demand
// by the DML hook / worker threads; it is intentionally not part of the
// column schema serialization format.
class ObAiColumnSpec
{
public:
  static const char *const FUNC_COMPLETE;   // "complete"
  static const char *const FUNC_EMBED;      // "embed"
  static const char *const SYNC_ASYNC;      // "async"
  // Typed system prompts injected into the request according to the result
  // column type. The wording is part of the observable contract: the mock AI
  // service keys its deterministic answers off these phrases and the DDL test
  // asserts that the JSON prompt is persisted in normalized_expr.
  static const char *const JSON_TYPE_PROMPT;  // "Return exactly one valid JSON value."
  static const char *const INT_TYPE_PROMPT;   // "Return exactly one base-10 integer."

  ObAiColumnSpec()
      : max_retries_(0),
        dep_column_cnt_(0),
        state_column_id_(common::OB_INVALID_ID),
        request_column_id_(common::OB_INVALID_ID),
        result_spec_column_id_(common::OB_INVALID_ID)
  {
  }
  ~ObAiColumnSpec() = default;

  void reset();

  // Parse the persisted JSON text. Strings are deep copied into `alloc`.
  int parse(const common::ObString &text, common::ObIAllocator &alloc);
  // Serialize the spec into a JSON document allocated from `alloc`.
  int to_text(common::ObIAllocator &alloc, common::ObString &text) const;
  // Deep copy a string into the allocator (helper for callers building a spec).
  static int copy_str(common::ObIAllocator &alloc, const common::ObString &src,
                      common::ObString &dst);

  bool is_complete() const { return 0 == func_type_.case_compare(ObString::make_string(FUNC_COMPLETE)); }
  bool is_embed() const { return 0 == func_type_.case_compare(ObString::make_string(FUNC_EMBED)); }
  bool is_async() const { return sync_mode_.empty() || 0 == sync_mode_.case_compare(ObString::make_string(SYNC_ASYNC)); }

  // Compute the typed system prompt for an AI result column of the given
  // type. Returns an empty prompt for types that need no instruction
  // (varchar/text and embeddings).
  static int compute_system_prompt(const ObColumnSchemaV2 &column,
                                   common::ObIAllocator &alloc,
                                   common::ObString &prompt);

  TO_STRING_KV(K_(func_type), K_(model_name), K_(prompt_expr),
               K_(system_prompt), K_(config), K_(sync_mode), K_(max_retries),
               K_(dep_column_cnt), K_(state_column_id), K_(request_column_id),
               K_(result_spec_column_id), K_(normalized));

public:
  static const int MAX_DEP_COLUMN_CNT = 64;

  common::ObString func_type_;    // "complete" | "embed"
  common::ObString model_name_;   // logical AI model name
  common::ObString prompt_expr_;  // SQL text of the prompt/content expression
  common::ObString system_prompt_;  // typed prompt injected as system message
  common::ObString config_;       // extra request config (JSON text), may be empty
  common::ObString sync_mode_;    // always "async" for now
  common::ObString normalized_;   // normalized expression text (display)
  int64_t max_retries_;
  uint64_t dep_column_ids_[MAX_DEP_COLUMN_CNT];
  int64_t dep_column_cnt_;
  uint64_t state_column_id_;
  uint64_t request_column_id_;
  uint64_t result_spec_column_id_;

  int add_dep_column_id(uint64_t col_id)
  {
    int ret = OB_SUCCESS;
    if (dep_column_cnt_ >= MAX_DEP_COLUMN_CNT) {
      ret = OB_SIZE_OVERFLOW;
    } else {
      dep_column_ids_[dep_column_cnt_++] = col_id;
    }
    return ret;
  }
};

} // namespace schema
} // namespace share
} // namespace oceanbase
