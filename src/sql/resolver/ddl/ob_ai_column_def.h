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

#include <stdlib.h>
#include <string.h>

#include <string.h>
#include "lib/string/ob_string.h"
#include "share/ob_define.h"
#include "lib/container/ob_iarray.h"
#include "lib/utility/ob_macro_utils.h"
#include "lib/utility/alloc_assist.h"
#include "lib/string/ob_sql_string.h"
#include "lib/string/ob_string_buffer.h"
#include "lib/utility/ob_print_utils.h"

namespace oceanbase
{
namespace sql
{

// Runtime description of one AI COLUMN declared on a user table.
// The definition is produced by the DDL resolver and consumed by:
//   1. the DDL executors, which persist it into oceanbase.__all_ai_column;
//   2. the DML / worker side, which reads it back through the AI column cache.
struct ObAiColumnDef
{
  ObAiColumnDef()
    : column_name_(),
      model_name_(),
      input_expr_(),
      normalized_expr_(),
      ai_func_type_(0),
      result_type_(0),
      max_retries_(0),
      sync_mode_(1),
      definition_version_(0),
      dep_column_names_(),
      result_column_id_(common::OB_INVALID_ID),
      state_column_id_(common::OB_INVALID_ID),
      request_column_id_(common::OB_INVALID_ID),
      result_spec_column_id_(common::OB_INVALID_ID)
  {}

  void reset() { *this = ObAiColumnDef(); }

  // user visible result column
  common::ObString column_name_;
  // registered AI model name
  common::ObString model_name_;
  // text of the second argument of AI_COMPLETE / AI_EMBED, replayed by the worker
  // to compute the model input, e.g. "concat('Classify: ', content)"
  common::ObString input_expr_;
  // full user expression plus the type instruction appended by the resolver
  common::ObString normalized_expr_;
  // T_FUN_SYS_AI_COMPLETE or T_FUN_SYS_AI_EMBED
  int64_t ai_func_type_;
  // result column data type (ObObjType)
  int64_t result_type_;
  int64_t max_retries_;
  // 1 == async, the only supported mode today
  int64_t sync_mode_;
  uint64_t definition_version_;
  // columns referenced by input_expr_, comma separated user column names
  common::ObString dep_column_names_;
  // column ids, valid after the DDL statement finished
  uint64_t result_column_id_;
  uint64_t state_column_id_;
  uint64_t request_column_id_;
  uint64_t result_spec_column_id_;

  TO_STRING_KV(K_(column_name), K_(model_name), K_(input_expr), K_(ai_func_type),
               K_(result_type), K_(max_retries), K_(sync_mode), K_(definition_version),
               K_(result_column_id), K_(state_column_id), K_(request_column_id),
               K_(result_spec_column_id), K_(dep_column_names));
};

// Hidden column suffixes. The full name is "__ai_<column><suffix>".
constexpr const char *AI_COLUMN_STATE_SUFFIX = "_state";
constexpr const char *AI_COLUMN_REQUEST_SUFFIX = "_request";
constexpr const char *AI_COLUMN_SPEC_SUFFIX = "_spec";
constexpr const char *AI_COLUMN_HIDDEN_PREFIX = "__ai_";

inline bool is_ai_hidden_column_name(const common::ObString &name)
{
  const int32_t prefix_len = static_cast<int32_t>(strlen(AI_COLUMN_HIDDEN_PREFIX));
  return name.length() > prefix_len
         && NULL != name.ptr()
         && 0 == memcmp(name.ptr(), AI_COLUMN_HIDDEN_PREFIX, prefix_len);
}


// ---------------------------------------------------------------------------
// The definition is embedded in the comment of the hidden state column, so the
// DML path can recover it straight from the table schema: no extra catalog
// lookup, no cache, and it survives a restart. oceanbase.__all_ai_column keeps
// the queryable copy for SHOW / tests.
// ---------------------------------------------------------------------------
static const char AI_COLUMN_DEF_MAGIC[] = "AIC1";
static const char AI_COLUMN_DEF_SEP = '\x1f';

inline bool is_encoded_ai_column_def(const common::ObString &text)
{
  const int32_t magic_len = static_cast<int32_t>(strlen(AI_COLUMN_DEF_MAGIC));
  return text.length() > magic_len
         && NULL != text.ptr()
         && 0 == memcmp(text.ptr(), AI_COLUMN_DEF_MAGIC, magic_len)
         && AI_COLUMN_DEF_SEP == text.ptr()[magic_len];
}

inline int encode_ai_column_def(const ObAiColumnDef &def,
                                common::ObIAllocator &alloc,
                                common::ObString &out)
{
  int ret = OB_SUCCESS;
  common::ObStringBuffer buf(&alloc);
  common::ObSqlString num_part;
  if (OB_FAIL(buf.append(AI_COLUMN_DEF_MAGIC))
      || OB_FAIL(buf.append(&AI_COLUMN_DEF_SEP, 1))
      || OB_FAIL(buf.append(def.column_name_))
      || OB_FAIL(buf.append(&AI_COLUMN_DEF_SEP, 1))
      || OB_FAIL(num_part.append_fmt("%ld%c%ld%c%ld%c%ld%c%lu%c",
                                def.ai_func_type_, AI_COLUMN_DEF_SEP,
                                def.result_type_, AI_COLUMN_DEF_SEP,
                                def.max_retries_, AI_COLUMN_DEF_SEP,
                                def.sync_mode_, AI_COLUMN_DEF_SEP,
                                def.definition_version_, AI_COLUMN_DEF_SEP))) {
  } else if (OB_FAIL(buf.append(num_part.ptr(), num_part.length()))) {
  } else if (OB_FAIL(buf.append(def.model_name_))
             || OB_FAIL(buf.append(&AI_COLUMN_DEF_SEP, 1))
             || OB_FAIL(buf.append(def.input_expr_))
             || OB_FAIL(buf.append(&AI_COLUMN_DEF_SEP, 1))
             || OB_FAIL(buf.append(def.normalized_expr_))
             || OB_FAIL(buf.append(&AI_COLUMN_DEF_SEP, 1))
             || OB_FAIL(buf.append(def.dep_column_names_))) {
  } else {
    out = buf.string();
  }
  return ret;
}

inline int decode_ai_column_def(const common::ObString &text,
                                common::ObIAllocator &alloc,
                                ObAiColumnDef &def)
{
  int ret = OB_SUCCESS;
  const int32_t magic_len = static_cast<int32_t>(strlen(AI_COLUMN_DEF_MAGIC));
  def.reset();
  if (!is_encoded_ai_column_def(text)) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    const int64_t AI_DEF_FIELD_CNT = 10;
    common::ObString fields[AI_DEF_FIELD_CNT];
    int32_t field_cnt = 0;
    const char *ptr = text.ptr() + magic_len + 1;
    int32_t remain = text.length() - magic_len - 1;
    int32_t start = 0;
    for (int32_t i = 0; i <= remain && OB_SUCC(ret); ++i) {
      if (i == remain || AI_COLUMN_DEF_SEP == ptr[i]) {
        if (field_cnt >= AI_DEF_FIELD_CNT) {
          ret = OB_INVALID_ARGUMENT;
        } else {
          fields[field_cnt++].assign_ptr(ptr + start, i - start);
          start = i + 1;
        }
      }
    }
    if (OB_SUCC(ret) && field_cnt < AI_DEF_FIELD_CNT) {
      ret = OB_INVALID_ARGUMENT;
    }
    if (OB_SUCC(ret)) {
      char num_buf[32];
      int64_t num_len = 0;
      if (OB_FAIL(common::ob_write_string(alloc, fields[0], def.column_name_))) {
      }
      num_len = MIN(static_cast<int64_t>(fields[1].length()), static_cast<int64_t>(sizeof(num_buf) - 1));
      MEMCPY(num_buf, fields[1].ptr(), num_len);
      num_buf[num_len] = '\0';
      def.ai_func_type_ = atol(num_buf);
      num_len = MIN(static_cast<int64_t>(fields[2].length()), static_cast<int64_t>(sizeof(num_buf) - 1));
      MEMCPY(num_buf, fields[2].ptr(), num_len);
      num_buf[num_len] = '\0';
      def.result_type_ = atol(num_buf);
      num_len = MIN(static_cast<int64_t>(fields[3].length()), static_cast<int64_t>(sizeof(num_buf) - 1));
      MEMCPY(num_buf, fields[3].ptr(), num_len);
      num_buf[num_len] = '\0';
      def.max_retries_ = atol(num_buf);
      num_len = MIN(static_cast<int64_t>(fields[4].length()), static_cast<int64_t>(sizeof(num_buf) - 1));
      MEMCPY(num_buf, fields[4].ptr(), num_len);
      num_buf[num_len] = '\0';
      def.sync_mode_ = atol(num_buf);
      num_len = MIN(static_cast<int64_t>(fields[5].length()), static_cast<int64_t>(sizeof(num_buf) - 1));
      MEMCPY(num_buf, fields[5].ptr(), num_len);
      num_buf[num_len] = '\0';
      def.definition_version_ = strtoull(num_buf, NULL, 10);
      if (OB_SUCC(ret)
          && (OB_FAIL(common::ob_write_string(alloc, fields[6], def.model_name_))
              || OB_FAIL(common::ob_write_string(alloc, fields[7], def.input_expr_))
              || OB_FAIL(common::ob_write_string(alloc, fields[8], def.normalized_expr_))
              || OB_FAIL(common::ob_write_string(alloc, fields[9], def.dep_column_names_)))) {
      }
    }
  }
  return ret;
}

} // namespace sql
} // namespace oceanbase
