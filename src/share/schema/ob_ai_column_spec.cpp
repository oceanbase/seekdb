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

#define USING_LOG_PREFIX SHARE

#include "share/schema/ob_ai_column_spec.h"
#include "share/schema/ob_column_schema.h"
#include "lib/string/ob_sql_string.h"
#include "lib/json/ob_json.h"
#include "common/json_type/ob_json_parse.h"
#include "lib/ob_errno.h"

namespace oceanbase
{
using namespace common;
namespace share
{
namespace schema
{

const char *const ObAiColumnSpec::FUNC_COMPLETE = "complete";
const char *const ObAiColumnSpec::FUNC_EMBED = "embed";
const char *const ObAiColumnSpec::SYNC_ASYNC = "async";
const char *const ObAiColumnSpec::JSON_TYPE_PROMPT = "Return exactly one valid JSON value.";
const char *const ObAiColumnSpec::INT_TYPE_PROMPT = "Return exactly one base-10 integer.";

void ObAiColumnSpec::reset()
{
  func_type_.reset();
  model_name_.reset();
  prompt_expr_.reset();
  system_prompt_.reset();
  config_.reset();
  sync_mode_.reset();
  normalized_.reset();
  max_retries_ = 0;
  dep_column_cnt_ = 0;
  state_column_id_ = OB_INVALID_ID;
  request_column_id_ = OB_INVALID_ID;
  result_spec_column_id_ = OB_INVALID_ID;
}

namespace
{
// Append `str` as a JSON string literal (including the surrounding quotes).
int append_json_string(ObSqlString &buf, const ObString &str)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(buf.append("\""))) {
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < str.length(); ++i) {
      const char c = str.ptr()[i];
      if ('"' == c || '\\' == c) {
        char esc[2] = {'\\', c};
        ret = buf.append(esc, 2);
      } else if ('\n' == c) {
        ret = buf.append("\\n");
      } else if ('\r' == c) {
        ret = buf.append("\\r");
      } else if ('\t' == c) {
        ret = buf.append("\\t");
      } else if (static_cast<unsigned char>(c) < 0x20) {
        char oct[8];
        snprintf(oct, sizeof(oct), "\\u%04x", static_cast<unsigned char>(c));
        ret = buf.append(oct);
      } else {
        ret = buf.append(&c, 1);
      }
    }
    if (OB_SUCC(ret)) {
      ret = buf.append("\"");
    }
  }
  return ret;
}

int deep_copy_str(ObIAllocator &alloc, const ObString &src, ObString &dst)
{
  int ret = OB_SUCCESS;
  if (src.empty()) {
    dst.reset();
  } else {
    char *buf = static_cast<char *>(alloc.alloc(src.length() + 1));
    if (OB_ISNULL(buf)) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("fail to alloc memory", K(ret), K(src.length()));
    } else {
      MEMCPY(buf, src.ptr(), src.length());
      buf[src.length()] = '\0';
      dst.assign_ptr(buf, static_cast<int32_t>(src.length()));
    }
  }
  return ret;
}
} // namespace

int ObAiColumnSpec::copy_str(ObIAllocator &alloc, const ObString &src, ObString &dst)
{
  return deep_copy_str(alloc, src, dst);
}

int ObAiColumnSpec::to_text(ObIAllocator &alloc, ObString &text) const
{
  int ret = OB_SUCCESS;
  ObSqlString buf;
  if (OB_FAIL(buf.append("{\"func\":"))) {
  } else if (OB_FAIL(append_json_string(buf, func_type_))) {
  } else if (OB_FAIL(buf.append(",\"model\":"))) {
  } else if (OB_FAIL(append_json_string(buf, model_name_))) {
  } else if (OB_FAIL(buf.append(",\"prompt\":"))) {
  } else if (OB_FAIL(append_json_string(buf, prompt_expr_))) {
  } else if (OB_FAIL(buf.append(",\"system\":"))) {
  } else if (OB_FAIL(append_json_string(buf, system_prompt_))) {
  } else if (OB_FAIL(buf.append(",\"config\":"))) {
  } else if (OB_FAIL(append_json_string(buf, config_))) {
  } else if (OB_FAIL(buf.append(",\"sync\":"))) {
  } else if (OB_FAIL(append_json_string(buf, sync_mode_.empty() ? ObString(SYNC_ASYNC) : sync_mode_))) {
  } else if (OB_FAIL(buf.append(",\"normalized\":"))) {
  } else if (OB_FAIL(append_json_string(buf, normalized_))) {
  } else if (OB_FAIL(buf.append_fmt(",\"retries\":%ld,\"deps\":[", max_retries_))) {
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < dep_column_cnt_; ++i) {
      if (i > 0 && OB_FAIL(buf.append(","))) {
      } else {
        ret = buf.append_fmt("%lu", dep_column_ids_[i]);
      }
    }
    if (OB_SUCC(ret)) {
      ret = buf.append_fmt("],\"state\":%lu,\"req\":%lu,\"spec\":%lu}",
                              state_column_id_, request_column_id_, result_spec_column_id_);
    }
    if (OB_SUCC(ret)) {
      char *out = static_cast<char *>(alloc.alloc(buf.length() + 1));
      if (OB_ISNULL(out)) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_WARN("fail to alloc memory", K(ret));
      } else {
        MEMCPY(out, buf.ptr(), buf.length());
        out[buf.length()] = '\0';
        text.assign_ptr(out, static_cast<int32_t>(buf.length()));
      }
    }
  }
  return ret;
}

int ObAiColumnSpec::parse(const ObString &text, ObIAllocator &alloc)
{
  int ret = OB_SUCCESS;
  reset();
  ObJsonNode *tree = NULL;
  const char *syntaxerr = NULL;
  uint64_t offset = 0;
  if (text.empty()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("empty ai column spec", K(ret));
  } else if (OB_FAIL(ObJsonParser::parse_json_text(&alloc, text.ptr(), text.length(),
                                                  syntaxerr, &offset, tree))) {
    LOG_WARN("fail to parse ai column spec json", K(ret), K(text), K(syntaxerr));
  } else if (OB_ISNULL(tree)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("null json tree", K(ret));
  } else if (ObJsonNodeType::J_OBJECT != tree->json_type()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("ai column spec is not a json object", K(ret), K(tree->json_type()));
  } else {
    JsonObjectIterator iter(tree);
    ObJsonObjPair elem;
    while (OB_SUCC(ret) && !iter.end()) {
      if (OB_FAIL(iter.get_elem(elem))) {
        LOG_WARN("fail to get json elem", K(ret));
      } else if (elem.first.case_compare("func") == 0) {
        if (ObJsonNodeType::J_STRING == elem.second->json_type()) {
          ret = deep_copy_str(alloc, ObString(elem.second->get_data_length(), elem.second->get_data()),
                              func_type_);
        }
      } else if (elem.first.case_compare("model") == 0) {
        if (ObJsonNodeType::J_STRING == elem.second->json_type()) {
          ret = deep_copy_str(alloc, ObString(elem.second->get_data_length(), elem.second->get_data()),
                              model_name_);
        }
      } else if (elem.first.case_compare("prompt") == 0) {
        if (ObJsonNodeType::J_STRING == elem.second->json_type()) {
          ret = deep_copy_str(alloc, ObString(elem.second->get_data_length(), elem.second->get_data()),
                              prompt_expr_);
        }
      } else if (elem.first.case_compare("system") == 0) {
        if (ObJsonNodeType::J_STRING == elem.second->json_type()) {
          ret = deep_copy_str(alloc, ObString(elem.second->get_data_length(), elem.second->get_data()),
                              system_prompt_);
        }
      } else if (elem.first.case_compare("config") == 0) {
        if (ObJsonNodeType::J_STRING == elem.second->json_type()) {
          ret = deep_copy_str(alloc, ObString(elem.second->get_data_length(), elem.second->get_data()),
                              config_);
        }
      } else if (elem.first.case_compare("sync") == 0) {
        if (ObJsonNodeType::J_STRING == elem.second->json_type()) {
          ret = deep_copy_str(alloc, ObString(elem.second->get_data_length(), elem.second->get_data()),
                              sync_mode_);
        }
      } else if (elem.first.case_compare("normalized") == 0) {
        if (ObJsonNodeType::J_STRING == elem.second->json_type()) {
          ret = deep_copy_str(alloc, ObString(elem.second->get_data_length(), elem.second->get_data()),
                              normalized_);
        }
      } else if (elem.first.case_compare("retries") == 0) {
        if (ObJsonNodeType::J_INT == elem.second->json_type() ||
            ObJsonNodeType::J_UINT == elem.second->json_type()) {
          max_retries_ = elem.second->get_int();
        }
      } else if (elem.first.case_compare("deps") == 0) {
        if (ObJsonNodeType::J_ARRAY == elem.second->json_type()) {
          const uint64_t cnt = elem.second->element_count();
          for (uint64_t i = 0; OB_SUCC(ret) && i < cnt; ++i) {
            ObIJsonBase *child = NULL;
            if (OB_FAIL(elem.second->get_array_element(i, child))) {
              LOG_WARN("fail to get array element", K(ret), K(i));
            } else if (OB_ISNULL(child)) {
              ret = OB_ERR_UNEXPECTED;
            } else if (ObJsonNodeType::J_INT == child->json_type() ||
                       ObJsonNodeType::J_UINT == child->json_type()) {
              ret = add_dep_column_id(static_cast<uint64_t>(child->get_int()));
            }
          }
        }
      } else if (elem.first.case_compare("state") == 0) {
        if (ObJsonNodeType::J_INT == elem.second->json_type() ||
            ObJsonNodeType::J_UINT == elem.second->json_type()) {
          state_column_id_ = static_cast<uint64_t>(elem.second->get_int());
        }
      } else if (elem.first.case_compare("req") == 0) {
        if (ObJsonNodeType::J_INT == elem.second->json_type() ||
            ObJsonNodeType::J_UINT == elem.second->json_type()) {
          request_column_id_ = static_cast<uint64_t>(elem.second->get_int());
        }
      } else if (elem.first.case_compare("spec") == 0) {
        if (ObJsonNodeType::J_INT == elem.second->json_type() ||
            ObJsonNodeType::J_UINT == elem.second->json_type()) {
          result_spec_column_id_ = static_cast<uint64_t>(elem.second->get_int());
        }
      }
      if (OB_SUCC(ret)) {
        iter.next();
      }
    }
    if (OB_SUCC(ret)) {
      if (func_type_.empty() || model_name_.empty() || prompt_expr_.empty()) {
        ret = OB_INVALID_ARGUMENT;
        LOG_WARN("incomplete ai column spec", K(ret), KPC(this));
      }
    }
  }
  return ret;
}

int ObAiColumnSpec::compute_system_prompt(const ObColumnSchemaV2 &column,
                                          ObIAllocator &alloc,
                                          ObString &prompt)
{
  int ret = OB_SUCCESS;
  prompt.reset();
  const ObObjType type = column.get_data_type();
  if (ob_is_json(type)) {
    ret = deep_copy_str(alloc, ObString::make_string(JSON_TYPE_PROMPT), prompt);
  } else if (ob_is_integer_type(type)) {
    ret = deep_copy_str(alloc, ObString::make_string(INT_TYPE_PROMPT), prompt);
  }
  return ret;
}

} // namespace schema
} // namespace share
} // namespace oceanbase
