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

#define USING_LOG_PREFIX SQL_PARSER
#include "sql/plan_cache/fast_parse.h"
#include "rust/sql_parser.h"
#include "share/ob_define.h"
#include "lib/worker.h"

using namespace oceanbase::sql;
using namespace oceanbase::common;

namespace {
int scan(const ObString &stmt, const FPContext &context, ObIAllocator &allocator,
         ObFastParserResult &result)
{
  peg::ScanOptions options{
      peg::SqlMode{bool(context.sql_mode_ & SMO_ANSI_QUOTES), bool(context.sql_mode_ & SMO_NO_BACKSLASH_ESCAPES),
                   bool(context.sql_mode_ & SMO_PIPES_AS_CONCAT), bool(context.sql_mode_ & SMO_HIGH_NOT_PRECEDENCE),
                   bool(context.sql_mode_ & SMO_REAL_AS_FLOAT)},
      context.parameterize_, context.ignore_hint_, context.is_format_, context.enable_batched_multi_stmt_};
  int ret = OB_SUCCESS;
  if (OB_FAIL(THIS_WORKER.check_status())) return ret;
  auto scanner = peg::begin_scan(rust::Slice<const uint8_t>(reinterpret_cast<const uint8_t *>(stmt.ptr()), stmt.length()), options);
  bool finished = false;
  do {
    finished = scanner->advance();
    if (OB_FAIL(THIS_WORKER.check_status())) return ret;
  } while (!finished);
  auto scanned = peg::finish_scan(std::move(scanner));
  if (!scanned.error.empty()) return OB_ERR_PARSER_SYNTAX;
  if (scanned.ordinal_parameters != 0 && !scanned.parameter_names.empty()) return OB_NOT_SUPPORTED;
  auto copy = [&](const char *text, size_t size) -> char * {
    char *owned = static_cast<char *>(allocator.alloc(size + 1));
    if (owned != nullptr) { MEMCPY(owned, text, size); owned[size] = '\0'; }
    return owned;
  };
  char *sql = copy(syntax_text(scanned.sql).ptr(), scanned.sql.size());
  if (sql == nullptr) return OB_ALLOCATE_MEMORY_FAILED;
  result.pc_key_.name_.assign_ptr(sql, static_cast<int64_t>(scanned.sql.size()));
  result.values_token_pos_ = scanned.insert_values_offset;
  const int64_t count = context.parameterize_ ? scanned.literals.size() : 0;
  result.raw_params_.reset();
  if (count > 0) {
    auto *tokens = static_cast<LiteralToken *>(allocator.alloc(sizeof(LiteralToken) * count));
    auto *params = static_cast<ObPCParam *>(allocator.alloc(sizeof(ObPCParam) * count));
    if (tokens == nullptr || params == nullptr) return OB_ALLOCATE_MEMORY_FAILED;
    if (OB_FAIL(result.raw_params_.reserve(count))) return ret;
    const char *text = nullptr;
    if (!scanned.literal_text.empty()) {
      text = copy(reinterpret_cast<const char *>(scanned.literal_text.data()), scanned.literal_text.size());
      if (text == nullptr) return OB_ALLOCATE_MEMORY_FAILED;
    }
    int64_t i = 0;
    for (const auto &literal : scanned.literals) {
      auto *token = new (tokens + i) LiteralToken{
          literal.kind, literal.value, ObString(),
          ObString(literal.end - literal.start, stmt.ptr() + literal.start),
          literal.concatenated, static_cast<int32_t>(literal.first_part_length)};
      if (token->is_numeric()) {
        token->text = token->source;
      } else if (literal.content_start != literal.content_end) {
        token->text.assign_ptr(text + literal.content_start,
                               static_cast<int64_t>(literal.content_end - literal.content_start));
      }
      auto *param = new (params + i) ObPCParam();
      param->literal_ = token;
      param->source_offset_ = literal.start;
      param->sql_position_ = literal.sql_offset;
      if (OB_FAIL(result.raw_params_.push_back(param))) return ret;
      ++i;
    }
  }
  {
    auto &placeholders = result.question_mark_ctx_;
    result.reset_question_mark_ctx();
    placeholders.by_ordinal_ = scanned.ordinal_parameters > 0;
    placeholders.by_name_ = !scanned.parameter_names.empty();
    placeholders.count_ = scanned.ordinal_parameters + scanned.parameter_names.size();
    for (const auto &position : scanned.values) {
      if (OB_FAIL(result.values_tokens_.push_back(ObValuesTokenPos(position.sql_offset, position.literal_index)))) break;
    }
  }
  return ret;
}
}

int ObFastParser::parse(const common::ObString &stmt,
                        const FPContext &context,
                        common::ObIAllocator &allocator,
                        ObFastParserResult &result)
{
  return scan(stmt, context, allocator, result);
}

int ObFastParser::compress_insert(common::ObIAllocator &allocator,
                                   int64_t values_token_pos,
                                   const ObString &old_no_param_sql,
                                   ObString &new_truncated_sql,
                                   bool &can_batch_opt,
                                   int64_t &params_count,
                                   int64_t &upd_params_count,
                                   int64_t &length_delta,
                                   int64_t &row_count,
                                   bool &is_insert_values)
{
  can_batch_opt = false;
  is_insert_values = false;
  params_count = upd_params_count = length_delta = row_count = 0;
  if (values_token_pos <= 0) return OB_SUCCESS;
  const auto result = peg::compress_insert(
      rust::Slice<const uint8_t>(reinterpret_cast<const uint8_t *>(old_no_param_sql.ptr()),
                                 old_no_param_sql.length()), values_token_pos - 1);
  is_insert_values = result.recognized_rows;
  if (!result.eligible) return OB_SUCCESS;
  char *buffer = static_cast<char *>(allocator.alloc(result.sql.size()));
  if (buffer == nullptr) return OB_ALLOCATE_MEMORY_FAILED;
  MEMCPY(buffer, result.sql.data(), result.sql.size());
  new_truncated_sql.assign_ptr(buffer, static_cast<int64_t>(result.sql.size()));
  params_count = result.row_parameters;
  upd_params_count = result.suffix_parameters;
  length_delta = result.removed_length;
  row_count = result.rows;
  can_batch_opt = true;
  return OB_SUCCESS;
}
