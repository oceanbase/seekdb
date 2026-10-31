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

#define USING_LOG_PREFIX SQL_ENG

#include "sql/engine/expr/ob_expr_ai/ob_ai_column_utils.h"

#include <atomic>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <strings.h>

#include "lib/encode/ob_base64_encode.h"
#include "lib/time/ob_time_utility.h"
#include "share/schema/ob_table_schema.h"
#include "share/schema/ob_column_schema.h"
#include "share/ob_dml_sql_splicer.h"
#include "share/inner_table/ob_inner_table_schema.h"
#include "common/mysqlclient/ob_mysql_proxy.h"
#include "common/mysqlclient/ob_mysql_transaction.h"
#include "common/mysqlclient/ob_mysql_result.h"
#include "lib/allocator/page_arena.h"
#include "sql/engine/ob_exec_context.h"
#include "sql/session/ob_sql_session_info.h"
#include "share/system_variable/ob_sys_var_class_type.h"

using namespace oceanbase::common;
using namespace oceanbase::share::schema;

namespace oceanbase
{
namespace sql
{

bool ObAiColumnUtils::is_spec_column(const ObString &name)
{
  return name.length() > static_cast<int32_t>(strlen(SPEC_PREFIX))
      && 0 == strncmp(name.ptr(), SPEC_PREFIX, strlen(SPEC_PREFIX));
}

bool ObAiColumnUtils::is_state_column(const ObString &name)
{
  return name.length() > static_cast<int32_t>(strlen(STATE_PREFIX))
      && 0 == strncmp(name.ptr(), STATE_PREFIX, strlen(STATE_PREFIX));
}

bool ObAiColumnUtils::is_request_column(const ObString &name)
{
  return name.length() > static_cast<int32_t>(strlen(REQUEST_PREFIX))
      && 0 == strncmp(name.ptr(), REQUEST_PREFIX, strlen(REQUEST_PREFIX));
}

namespace
{

int get_spec_field(const ObString &text, const char *key, ObString &value)
{
  int ret = OB_SUCCESS;
  value.reset();
  const int64_t key_len = static_cast<int64_t>(strlen(key));
  int64_t pos = 0;
  bool found = false;
  while (!found && pos < text.length()) {
    int64_t end = pos;
    while (end < text.length() && ';' != text[end]) {
      ++end;
    }
    const int64_t seg_len = end - pos;
    if (seg_len > key_len && 0 == strncmp(text.ptr() + pos, key, key_len)
        && '=' == text[pos + key_len]) {
      value.assign_ptr(text.ptr() + pos + key_len + 1,
                       static_cast<int32_t>(seg_len - key_len - 1));
      found = true;
    }
    pos = end + 1;
  }
  if (!found) {
    ret = OB_ENTRY_NOT_EXIST;
  }
  return ret;
}

int encode_to_string(ObIAllocator &alloc, const ObString &input, ObString &output)
{
  int ret = OB_SUCCESS;
  const int64_t needed = ObBase64Encoder::needed_encoded_length(input.length());
  char *buf = nullptr;
  int64_t pos = 0;
  if (OB_ISNULL(buf = static_cast<char *>(alloc.alloc(needed)))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  } else if (OB_FAIL(ObBase64Encoder::encode(
                 reinterpret_cast<const uint8_t *>(input.ptr()), input.length(),
                 buf, needed, pos))) {
  } else {
    output.assign_ptr(buf, static_cast<int32_t>(pos));
  }
  return ret;
}

int decode_to_string(ObIAllocator &alloc, const ObString &input, ObString &output)
{
  int ret = OB_SUCCESS;
  const int64_t needed = ObBase64Encoder::needed_decoded_length(input.length());
  uint8_t *buf = nullptr;
  int64_t pos = 0;
  if (OB_UNLIKELY(input.empty())) {
    output.reset();
  } else if (OB_ISNULL(buf = static_cast<uint8_t *>(alloc.alloc(needed)))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  } else if (OB_FAIL(ObBase64Encoder::decode(input.ptr(), input.length(), buf, needed, pos))) {
  } else {
    output.assign_ptr(reinterpret_cast<const char *>(buf), static_cast<int32_t>(pos));
  }
  return ret;
}

int parse_spec_int(const ObString &value, int64_t &result)
{
  int ret = OB_SUCCESS;
  char buf[32];
  char *end = nullptr;
  if (OB_UNLIKELY(value.empty() || value.length() >= static_cast<int32_t>(sizeof(buf)))) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    MEMCPY(buf, value.ptr(), value.length());
    buf[value.length()] = '\0';
    errno = 0;
    const long long parsed = strtoll(buf, &end, 10);
    if (OB_UNLIKELY(0 != errno || end == buf || '\0' != *end)) {
      ret = OB_INVALID_ARGUMENT;
    } else {
      result = static_cast<int64_t>(parsed);
    }
  }
  return ret;
}

} // anonymous namespace

int ObAiColumnUtils::build_spec_text(const ObAiColumnSpec &spec,
                                     ObIAllocator &alloc,
                                     ObString &text)
{
  int ret = OB_SUCCESS;
  ObString model_b64;
  ObString expr_b64;
  ObSqlString buf;
  if (OB_FAIL(encode_to_string(alloc, spec.model_name_, model_b64))) {
  } else if (OB_FAIL(encode_to_string(alloc, spec.normalized_expr_, expr_b64))) {
  } else if (OB_FAIL(buf.append("v1"))) {
  } else if (OB_FAIL(buf.append_fmt(";result=%.*s", spec.result_column_.length(),
                                    spec.result_column_.ptr()))) {
  } else if (OB_FAIL(buf.append(";dep="))) {
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < spec.dependency_columns_.count(); ++i) {
      if (OB_FAIL(buf.append_fmt("%s%.*s", 0 == i ? "" : ",",
                                 spec.dependency_columns_.at(i).length(),
                                 spec.dependency_columns_.at(i).ptr()))) {
      }
    }
    if (OB_SUCC(ret) && OB_FAIL(buf.append_fmt(
            ";pt=%ld;max=%ld;tt=%ld;len=%ld;prec=%ld;scale=%ld",
            spec.prompt_type_, spec.max_retries_, spec.target_type_,
            spec.target_length_, spec.target_precision_, spec.target_scale_))) {
    } else if (OB_SUCC(ret) && OB_FAIL(buf.append_fmt(";model64=%.*s;expr64=%.*s",
                                        model_b64.length(), model_b64.ptr(),
                                        expr_b64.length(), expr_b64.ptr()))) {
    } else if (OB_FAIL(ob_write_string(alloc, ObString(buf.length(), buf.ptr()), text))) {
    }
  }
  return ret;
}

int ObAiColumnUtils::parse_spec_text(const ObString &text,
                                     ObIAllocator &alloc,
                                     ObAiColumnSpec &spec)
{
  int ret = OB_SUCCESS;
  ObString value;
  spec.reset();
  if (text.empty()) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(get_spec_field(text, "result", value))) {
  } else if (OB_FAIL(ob_write_string(alloc, value, spec.result_column_))) {
  } else if (OB_FAIL(get_spec_field(text, "dep", value))) {
  } else {
    while (OB_SUCC(ret) && !value.empty()) {
      int32_t comma = 0;
      while (comma < value.length() && ',' != value[comma]) {
        ++comma;
      }
      ObString dep;
      if (OB_FAIL(ob_write_string(alloc, ObString(comma, value.ptr()), dep))) {
      } else if (OB_FAIL(spec.dependency_columns_.push_back(dep))) {
      }
      value.assign_ptr(value.ptr() + comma + (comma < value.length() ? 1 : 0),
                       value.length() - comma - (comma < value.length() ? 1 : 0));
    }
    int64_t number = 0;
    if (OB_FAIL(ret)) {
    } else if (OB_FAIL(get_spec_field(text, "pt", value))) {
    } else if (OB_FAIL(parse_spec_int(value, number))) {
    } else {
      spec.prompt_type_ = number;
    }
    if (OB_FAIL(ret)) {
    } else if (OB_FAIL(get_spec_field(text, "max", value))) {
    } else if (OB_FAIL(parse_spec_int(value, number))) {
    } else {
      spec.max_retries_ = number;
    }
    if (OB_FAIL(ret)) {
    } else if (OB_FAIL(get_spec_field(text, "tt", value))) {
    } else if (OB_FAIL(parse_spec_int(value, number))) {
    } else {
      spec.target_type_ = number;
    }
    if (OB_FAIL(ret)) {
    } else if (OB_FAIL(get_spec_field(text, "len", value))) {
    } else if (OB_FAIL(parse_spec_int(value, number))) {
    } else {
      spec.target_length_ = number;
    }
    if (OB_FAIL(ret)) {
    } else if (OB_FAIL(get_spec_field(text, "prec", value))) {
    } else if (OB_FAIL(parse_spec_int(value, number))) {
    } else {
      spec.target_precision_ = number;
    }
    if (OB_FAIL(ret)) {
    } else if (OB_FAIL(get_spec_field(text, "scale", value))) {
    } else if (OB_FAIL(parse_spec_int(value, number))) {
    } else {
      spec.target_scale_ = number;
    }
    if (OB_FAIL(ret)) {
    } else if (OB_FAIL(get_spec_field(text, "model64", value))) {
    } else if (OB_FAIL(decode_to_string(alloc, value, spec.model_name_))) {
    } else if (OB_FAIL(get_spec_field(text, "expr64", value))) {
    } else if (OB_FAIL(decode_to_string(alloc, value, spec.normalized_expr_))) {
    }
  }
  return ret;
}

int ObAiColumnUtils::load_table_ai_columns(const ObTableSchema &table_schema,
                                           ObIAllocator &alloc,
                                           ObIArray<ObAiColumnSpec> &specs)
{
  int ret = OB_SUCCESS;
  for (ObTableSchema::const_column_iterator iter = table_schema.column_begin();
       OB_SUCC(ret) && iter != table_schema.column_end(); ++iter) {
    const ObColumnSchemaV2 *column = *iter;
    if (OB_ISNULL(column)) {
      ret = OB_ERR_UNEXPECTED;
    } else if (!is_spec_column(column->get_column_name_str())) {
    } else {
      ObAiColumnSpec spec;
      if (OB_FAIL(parse_spec_text(column->get_comment_str(), alloc, spec))) {
      } else if (OB_FALSE_IT(spec.result_spec_column_id_ = column->get_column_id())) {
      } else {
        const ObColumnSchemaV2 *result_col =
            table_schema.get_column_schema(spec.result_column_);
        ObSqlString state_name;
        ObSqlString request_name;
        if (OB_ISNULL(result_col)) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("ai spec refers to a missing result column", K(ret),
                   K(spec.result_column_), K(column->get_column_name_str()));
        } else if (OB_FAIL(state_name.append_fmt("%s%.*s", STATE_PREFIX,
                       spec.result_column_.length(), spec.result_column_.ptr()))) {
        } else if (OB_FAIL(request_name.append_fmt("%s%.*s", REQUEST_PREFIX,
                       spec.result_column_.length(), spec.result_column_.ptr()))) {
        } else {
          const ObColumnSchemaV2 *state_col =
              table_schema.get_column_schema(state_name.string());
          const ObColumnSchemaV2 *request_col =
              table_schema.get_column_schema(request_name.string());
          if (OB_ISNULL(state_col) || OB_ISNULL(request_col)) {
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("ai hidden column is missing", K(ret), K(state_name),
                     K(request_name));
          } else {
            spec.result_column_id_ = result_col->get_column_id();
            spec.state_column_id_ = state_col->get_column_id();
            spec.request_column_id_ = request_col->get_column_id();
            for (int64_t i = 0; OB_SUCC(ret) && i < spec.dependency_columns_.count(); ++i) {
              const ObColumnSchemaV2 *dep_col =
                  table_schema.get_column_schema(spec.dependency_columns_.at(i));
              if (OB_ISNULL(dep_col)) {
                ret = OB_ERR_UNEXPECTED;
                LOG_WARN("ai dependency column is missing", K(ret),
                         K(spec.dependency_columns_.at(i)));
              } else if (OB_FAIL(spec.dependency_column_ids_.push_back(
                             dep_col->get_column_id()))) {
              }
            }
            if (OB_SUCC(ret) && OB_FAIL(specs.push_back(spec))) {
            }
          }
        }
      }
    }
  }
  return ret;
}

int ObAiColumnUtils::find_ai_column(const ObIArray<ObAiColumnSpec> &specs,
                                    const ObString &result_column,
                                    const ObAiColumnSpec *&spec)
{
  int ret = OB_SUCCESS;
  spec = nullptr;
  for (int64_t i = 0; OB_SUCC(ret) && nullptr == spec && i < specs.count(); ++i) {
    const ObString &name = specs.at(i).result_column_;
    if (name.length() == result_column.length()
        && 0 == strncasecmp(name.ptr(), result_column.ptr(), name.length())) {
      spec = &specs.at(i);
    }
  }
  return ret;
}

int ObAiColumnUtils::write_ai_column_metadata(
    ObMySQLProxy *sql_proxy,
    const ObTableSchema &table_schema,
    uint64_t table_id,
    const ObIArray<ObString> *only_results)
{
  int ret = OB_SUCCESS;
  ObArenaAllocator allocator("AiColumnMeta");
  ObSEArray<ObAiColumnSpec, 4> specs;
  ObMySQLTransaction trans;
  if (OB_ISNULL(sql_proxy)) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(load_table_ai_columns(table_schema, allocator, specs))) {
  } else if (specs.empty()) {
    // nothing to do
  } else if (OB_FAIL(trans.start(sql_proxy))) {
    LOG_WARN("failed to start ai column metadata transaction", K(ret));
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < specs.count(); ++i) {
      const ObAiColumnSpec &spec = specs.at(i);
      bool wanted = true;
      if (OB_NOT_NULL(only_results)) {
        wanted = false;
        for (int64_t j = 0; !wanted && j < only_results->count(); ++j) {
          const ObString &name = only_results->at(j);
          wanted = name.length() == spec.result_column_.length()
              && 0 == strncasecmp(name.ptr(), spec.result_column_.ptr(), name.length());
        }
      }
      if (!wanted) {
        continue;
      }
      share::ObDMLSqlSplicer splicer;
      ObSqlString buffer;      int64_t affected_rows = 0;
      if (OB_FAIL(splicer.add_pk_column("table_id", static_cast<int64_t>(table_id)))) {
      } else if (OB_FAIL(splicer.add_pk_column("column_id",
                     static_cast<int64_t>(spec.result_column_id_)))) {
      } else if (OB_FAIL(splicer.add_column("column_name",
                     ObHexEscapeSqlStr(spec.result_column_)))) {
      } else if (OB_FAIL(splicer.add_column("database_id",
                     static_cast<int64_t>(table_schema.get_database_id())))) {
      } else if (OB_FAIL(splicer.add_column("normalized_expr",
                     ObHexEscapeSqlStr(spec.normalized_expr_)))) {
      } else if (OB_FAIL(splicer.add_column("prompt_type", spec.prompt_type_))) {
      } else if (OB_FAIL(splicer.add_column("target_type", spec.target_type_))) {
      } else if (OB_FAIL(splicer.add_column("target_length", spec.target_length_))) {
      } else if (OB_FAIL(splicer.add_column("target_precision", spec.target_precision_))) {
      } else if (OB_FAIL(splicer.add_column("target_scale", spec.target_scale_))) {
      } else if (OB_FAIL(splicer.add_column("model_endpoint",
                     ObHexEscapeSqlStr(spec.model_name_)))) {
      } else if (OB_FAIL(splicer.add_column("sync_mode",
                     ObHexEscapeSqlStr(ObString::make_string("async"))))) {
      } else if (OB_FAIL(splicer.add_column("max_retries", spec.max_retries_))) {
      } else if (OB_FAIL(splicer.add_column("state_column_id",
                     static_cast<int64_t>(spec.state_column_id_)))) {
      } else if (OB_FAIL(splicer.add_column("request_column_id",
                     static_cast<int64_t>(spec.request_column_id_)))) {
      } else if (OB_FAIL(splicer.add_column("result_spec_column_id",
                     static_cast<int64_t>(spec.result_spec_column_id_)))) {
      } else if (OB_FAIL(splicer.splice_insert_sql("oceanbase.__all_ai_column", buffer))) {
      } else if (OB_FAIL(trans.write(buffer.ptr(), affected_rows))) {
      } else if (1 != affected_rows) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("unexpected affected rows for ai column metadata", K(ret),
                 K(affected_rows));
      }
    }
    int end_ret = trans.end(OB_SUCC(ret));
    if (OB_SUCCESS != end_ret && OB_SUCC(ret)) {
      ret = end_ret;
    }
  }
  return ret;
}

int ObAiColumnUtils::check_strict_read(ObExecContext &ctx,
                                       uint64_t table_id,
                                       const ObIArray<uint64_t> &access_column_ids)
{
  int ret = OB_SUCCESS;
  ObSQLSessionInfo *session = ctx.get_my_session();
  int64_t strict_read = 0;
  if (OB_ISNULL(session)) {
    // no session (bootstrap/internal scan): nothing to enforce
  } else if (OB_FAIL(session->get_sys_variable(share::SYS_VAR_AI_COLUMN_STRICT_READ,
                                               strict_read))) {
    // session variables may not be ready during bootstrap
  } else if (0 == strict_read) {
    // strict read is off
  } else if (common::is_virtual_table(table_id)) {
    // virtual tables never carry AI columns
  } else {
    ObSchemaGetterGuard schema_guard;
    const ObTableSchema *table_schema = nullptr;
    ObArenaAllocator allocator("AiStrictRead");
    ObSEArray<ObAiColumnSpec, 4> specs;
    if (OB_ISNULL(GCTX.schema_service_)) {
      ret = OB_ERR_UNEXPECTED;
    } else if (OB_FAIL(GCTX.schema_service_->get_runtime_schema_guard(schema_guard))) {
    } else if (OB_FAIL(schema_guard.get_table_schema(table_id, table_schema))) {
    } else if (OB_ISNULL(table_schema)) {
      ret = OB_ERR_UNEXPECTED;
    } else if (OB_FAIL(load_table_ai_columns(*table_schema, allocator, specs))) {
    } else {
      for (int64_t i = 0; OB_SUCC(ret) && i < specs.count(); ++i) {
        const ObAiColumnSpec &spec = specs.at(i);
        bool referenced = false;
        for (int64_t j = 0; !referenced && j < access_column_ids.count(); ++j) {
          referenced = access_column_ids.at(j) == spec.result_column_id_;
        }
        if (!referenced) {
          continue;
        }
        const ObDatabaseSchema *database_schema = nullptr;
        if (OB_FAIL(schema_guard.get_database_schema(
                table_schema->get_database_id(), database_schema))) {
        } else if (OB_ISNULL(database_schema)) {
          ret = OB_ERR_UNEXPECTED;
        } else if (OB_ISNULL(GCTX.sql_proxy_)) {
          ret = OB_ERR_UNEXPECTED;
        } else {
          ObSqlString sql;
          if (OB_FAIL(sql.append_fmt(
                  "SELECT 1 FROM `%.*s`.`%.*s` WHERE `%s%.*s` <> 1 LIMIT 1",
                  database_schema->get_database_name_str().length(),
                  database_schema->get_database_name_str().ptr(),
                  table_schema->get_table_name_str().length(),
                  table_schema->get_table_name_str().ptr(),
                  STATE_PREFIX, spec.result_column_.length(),
                  spec.result_column_.ptr()))) {
          } else {
            SMART_VAR(ObMySQLProxy::MySQLResult, res) {
              common::sqlclient::ObMySQLResult *result = nullptr;
              if (OB_FAIL(GCTX.sql_proxy_->read(res, sql.ptr()))) {
              } else if (OB_ISNULL(result = res.get_result())) {
                ret = OB_ERR_UNEXPECTED;
              } else if (OB_FAIL(result->next())) {
                if (OB_ITER_END == ret) {
                  ret = OB_SUCCESS;
                }
              } else {
                ret = OB_AI_COLUMN_NOT_READY;
                LOG_USER_ERROR(OB_AI_COLUMN_NOT_READY,
                               static_cast<int64_t>(table_id),
                               static_cast<int64_t>(spec.result_column_id_));
              }
            }
          }
        }
      }
    }
  }
  if (OB_SUCCESS != ret && OB_AI_COLUMN_NOT_READY != ret) {
    LOG_WARN("skip strict read check because it could not be evaluated",
             K(ret), K(table_id));
    ret = OB_SUCCESS;
  }
  return ret;
}

const char *ObAiColumnUtils::system_prompt_for(int64_t prompt_type)
{
  const char *prompt = nullptr;
  if (1 == prompt_type) {
    prompt = "Return exactly one valid JSON value";
  } else if (2 == prompt_type) {
    prompt = "Return exactly one base-10 integer";
  }
  return prompt;
}

uint32_t ObAiColumnUtils::next_task_id()
{
  static std::atomic<uint32_t> counter(
      static_cast<uint32_t>(ObTimeUtility::current_time()) & 0x7FFFFFFFU);
  uint32_t value = (++counter) & 0x7FFFFFFFU;
  if (0 == value) {
    value = (++counter) & 0x7FFFFFFFU;
  }
  return value;
}

uint64_t ObAiColumnUtils::next_request_id()
{
  static std::atomic<uint64_t> counter(
      static_cast<uint64_t>(ObTimeUtility::current_time()));
  return ++counter;
}

} // namespace sql
} // namespace oceanbase
