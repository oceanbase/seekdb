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

#include "share/ai_column/ob_ai_column_struct.h"

#include "common/json_type/ob_json_parse.h"
#include "common/json_type/ob_json_tree.h"

namespace oceanbase
{
namespace share
{

using namespace common;

static int escape_json_str(ObIAllocator &allocator, const ObString &src, ObString &dst)
{
  int ret = OB_SUCCESS;
  ObStringBuffer buf(&allocator);
  char hex[8];
  for (int64_t i = 0; OB_SUCC(ret) && i < src.length(); ++i) {
    char c = src.ptr()[i];
    switch (c) {
      case '"':
        ret = buf.append("\\\"");
        break;
      case '\\':
        ret = buf.append("\\\\");
        break;
      case '\n':
        ret = buf.append("\\n");
        break;
      case '\r':
        ret = buf.append("\\r");
        break;
      case '\t':
        ret = buf.append("\\t");
        break;
      default:
        if (static_cast<unsigned char>(c) < 0x20) {
          snprintf(hex, sizeof(hex), "\\u%04x", c);
          ret = buf.append(hex);
        } else {
          char one[2] = {c, '\0'};
          ret = buf.append(one);
        }
        break;
    }
  }
  if (OB_SUCC(ret)) {
    dst = buf.string();
  }
  return ret;
}

// serialize to JSON text: {"type":"complete"|"embed","model":"..","tmpl":"..",
//                          "system":"..","args":["a","b"][,"dim":N]}
int ObAiColumnExpr::serialize(ObIAllocator &allocator, ObString &out) const
{
  int ret = OB_SUCCESS;
  ObStringBuffer buf(&allocator);
  ObString esc_model, esc_tmpl, esc_sys, esc_col;
  if (OB_FAIL(escape_json_str(allocator, model_, esc_model))) {
  } else if (OB_FAIL(escape_json_str(allocator, user_template_, esc_tmpl))) {
  } else if (OB_FAIL(escape_json_str(allocator, system_prompt_, esc_sys))) {
  } else {
    char head[2048];
    int n = snprintf(head, sizeof(head),
                     "{\"type\":\"%s\",\"model\":\"%.*s\",\"tmpl\":\"%.*s\",\"system\":\"%.*s\",\"args\":[",
                     is_embed_ ? "embed" : "complete",
                     static_cast<int>(esc_model.length()), esc_model.ptr(),
                     static_cast<int>(esc_tmpl.length()), esc_tmpl.ptr(),
                     static_cast<int>(esc_sys.length()), esc_sys.ptr());
    if (n <= 0) {
      ret = OB_SIZE_OVERFLOW;
    } else if (OB_FAIL(buf.append(head, static_cast<uint64_t>(n)))) {
    }
  }
  for (int64_t i = 0; OB_SUCC(ret) && i < arg_columns_.count(); ++i) {
    if (OB_FAIL(escape_json_str(allocator, arg_columns_.at(i), esc_col))) {
    } else {
      char item[600];
      int n = snprintf(item, sizeof(item), "%s\"%.*s\"", i == 0 ? "" : ",",
                       static_cast<int>(esc_col.length()), esc_col.ptr());
      if (n <= 0) {
        ret = OB_SIZE_OVERFLOW;
      } else if (OB_FAIL(buf.append(item, static_cast<uint64_t>(n)))) {
      }
    }
  }
  if (OB_SUCC(ret)) {
    if (is_embed_) {
      char tail[64];
      int n = snprintf(tail, sizeof(tail), "],\"dim\":%ld}", dim_);
      if (n <= 0) {
        ret = OB_SIZE_OVERFLOW;
      } else if (OB_FAIL(buf.append(tail, static_cast<uint64_t>(n)))) {
      }
    } else if (OB_FAIL(buf.append("]}"))) {
    }
  }
  if (OB_SUCC(ret)) {
    out = buf.string();
  }
  return ret;
}

int ObAiColumnExpr::parse_from(const ObString &expr_str, ObIAllocator &allocator)
{
  int ret = OB_SUCCESS;
  ObJsonNode *tree = nullptr;
  reset();
  if (expr_str.empty()) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(ObJsonParser::get_tree(&allocator, expr_str, tree))) {
  } else if (OB_ISNULL(tree) || tree->json_type() != ObJsonNodeType::J_OBJECT) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    ObJsonObject *obj = static_cast<ObJsonObject *>(tree);
    ObIJsonBase *j_val = nullptr;
    ObString str_val;
    if (OB_FAIL(obj->get_object_value("type", j_val)) || OB_ISNULL(j_val)
        || j_val->json_type() != ObJsonNodeType::J_STRING) {
      ret = OB_ERR_UNEXPECTED;
    } else {
      str_val.assign_ptr(j_val->get_data(), static_cast<int32_t>(j_val->get_data_length()));
      is_embed_ = (str_val.case_compare("embed") == 0);
    }
    if (OB_FAIL(ret)) {
    } else if (OB_FAIL(obj->get_object_value("model", j_val)) || OB_ISNULL(j_val)
               || j_val->json_type() != ObJsonNodeType::J_STRING) {
      ret = OB_ERR_UNEXPECTED;
    } else if (OB_FAIL(ob_write_string(allocator,
               ObString(j_val->get_data_length(), j_val->get_data()), model_))) {
    }
    if (OB_FAIL(ret)) {
    } else if (OB_FAIL(obj->get_object_value("tmpl", j_val)) || OB_ISNULL(j_val)
               || j_val->json_type() != ObJsonNodeType::J_STRING) {
      ret = OB_ERR_UNEXPECTED;
    } else if (OB_FAIL(ob_write_string(allocator,
               ObString(j_val->get_data_length(), j_val->get_data()), user_template_))) {
    }
    if (OB_FAIL(ret)) {
    } else if (OB_SUCCESS == obj->get_object_value("system", j_val) && OB_NOT_NULL(j_val)) {
      // system prompt is optional
      if (OB_FAIL(ob_write_string(allocator,
               ObString(j_val->get_data_length(), j_val->get_data()), system_prompt_))) {
      }
    }
    if (OB_FAIL(ret)) {
    } else if (OB_FAIL(obj->get_object_value("args", j_val)) || OB_ISNULL(j_val)
               || j_val->json_type() != ObJsonNodeType::J_ARRAY) {
      ret = OB_ERR_UNEXPECTED;
    } else {
      ObJsonArray *arr = static_cast<ObJsonArray *>(j_val);
      for (uint64_t i = 0; OB_SUCC(ret) && i < arr->element_count(); ++i) {
        ObIJsonBase *elem = nullptr;
        ObString col_copy;
        if (OB_FAIL(arr->get_array_element(i, elem)) || OB_ISNULL(elem)
            || elem->json_type() != ObJsonNodeType::J_STRING) {
          ret = OB_ERR_UNEXPECTED;
        } else if (OB_FAIL(ob_write_string(allocator,
                   ObString(elem->get_data_length(), elem->get_data()), col_copy))) {
        } else if (OB_FAIL(arg_columns_.push_back(col_copy))) {
        }
      }
    }
    if (OB_SUCC(ret) && is_embed_) {
      if (OB_FAIL(obj->get_object_value("dim", j_val)) || OB_ISNULL(j_val)) {
        ret = OB_ERR_UNEXPECTED;
      } else {
        dim_ = j_val->get_int();
      }
    }
  }
  return ret;
}

void ObAiColumnMeta::reset()
{
  table_id_ = OB_INVALID_ID;
  column_name_.reset();
  column_id_ = OB_INVALID_ID;
  max_retries_ = 0;
  state_column_id_ = OB_INVALID_ID;
  request_column_id_ = OB_INVALID_ID;
  result_spec_column_id_ = OB_INVALID_ID;
  expr_.reset();
  normalized_expr_.reset();
}

void ObAiColumnTask::reset()
{
  request_id_.reset();
  table_id_ = OB_INVALID_ID;
  column_id_ = OB_INVALID_ID;
  column_name_.reset();
  rowkey_predicate_.reset();
  state_ = AI_TASK_PENDING;
  retry_count_ = 0;
  eligible_ts_ = 0;
}

void gen_ai_column_system_prompt(const ObObjType col_type, const bool is_embed,
                                 char *buf, const int64_t buf_len, int64_t &pos)
{
  pos = 0;
  if (is_embed) {
    // embeddings carry no typed system prompt
  } else if (ob_is_json_tc(col_type)) {
    databuff_printf(buf, buf_len, pos,
        "Return exactly one valid JSON value only, with no explanation, no markdown fence "
        "and no surrounding text.");
  } else if (ob_is_int_tc(col_type) || ob_is_uint_tc(col_type)) {
    databuff_printf(buf, buf_len, pos,
        "Return exactly one base-10 integer literal only, with no explanation and no "
        "surrounding text.");
  }
}

int build_rowkey_predicate(const ObIArray<ObString> &rowkey_col_names,
                           const ObIArray<ObString> &rowkey_col_values,
                           ObString &predicate, ObIAllocator &allocator)
{
  int ret = OB_SUCCESS;
  ObStringBuffer buf(&allocator);
  if (rowkey_col_names.count() != rowkey_col_values.count()
      || rowkey_col_names.empty()) {
    ret = OB_INVALID_ARGUMENT;
  }
  for (int64_t i = 0; OB_SUCC(ret) && i < rowkey_col_names.count(); ++i) {
    if (i > 0 && OB_FAIL(buf.append(" AND "))) {
    } else if (OB_FAIL(buf.append("`"))) {
    } else if (OB_FAIL(buf.append(rowkey_col_names.at(i)))) {
    } else if (OB_FAIL(buf.append("` = "))) {
    } else if (OB_FAIL(buf.append(rowkey_col_values.at(i)))) {
    }
  }
  if (OB_SUCC(ret)) {
    predicate = buf.string();
  }
  return ret;
}

}  // end namespace share
}  // end namespace oceanbase
