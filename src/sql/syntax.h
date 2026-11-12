// Copyright (c) 2026 OceanBase.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include <optional>
#include "common/sql_mode/ob_sql_mode.h"
#include "lib/allocator/ob_allocator.h"
#include "lib/container/ob_iarray.h"
#include "lib/string/ob_string.h"
#include "rust/sql_parser.h"
#include "share/statement/ob_stmt_type.h"

namespace oceanbase::sql {

template <typename Text>
inline common::ObString syntax_text(const Text &text)
{
  return common::ObString(text.size(), reinterpret_cast<const char *>(text.data()));
}

enum ParseMode {
  STD_MODE,
  FP_MODE,
  FP_PARAMERIZE_AND_FILTER_HINT_MODE,
  FP_NO_PARAMERIZE_AND_FILTER_HINT_MODE,
  TRIGGER_MODE,
  DYNAMIC_SQL_MODE,
  METHOD_OPT_MODE,
};

struct ObQuestionMarkCtx {
  int count_ = 0;
  bool by_ordinal_ = false;
  bool by_name_ = false;
};

struct ParseResult {
  const char *input_sql_ = nullptr;
  int input_sql_len_ = 0;
  ObQuestionMarkCtx question_mark_ctx_;
  peg::ParseMode parse_mode_ = peg::ParseMode::Sql;
  bool pl_context_ = false;
  bool contain_sensitive_data_ = false;
  bool is_dynamic_sql_ = false;
  char *no_param_sql_ = nullptr;
  int no_param_sql_len_ = 0;
};

struct ObSqlTraits;
int statement_type(const peg::Node &statement, stmt::StmtType &type);
int report_parse_error(const common::ObString &sql, const peg::ParseDiagnostic &diagnostic);

class ParsedSql {
public:
  ParsedSql() = default;
  ParsedSql(ParsedSql &&other) noexcept
      : metadata_(other.metadata_), original_source_(other.original_source_),
        syntax_(std::move(other.syntax_))
  {
    other.reset();
  }
  ParsedSql &operator=(ParsedSql &&other) noexcept
  {
    if (this != &other) {
      metadata_ = other.metadata_;
      original_source_ = other.original_source_;
      syntax_ = std::move(other.syntax_);
      other.reset();
    }
    return *this;
  }
  ParsedSql(const ParsedSql &) = delete;
  ParsedSql &operator=(const ParsedSql &) = delete;

  ParseResult &metadata() { return metadata_; }
  const ParseResult &metadata() const { return metadata_; }
  const peg::Node *statement() const { return syntax_ ? &(*syntax_)->root() : nullptr; }
  common::ObString source() const
  {
    if (syntax_) {
      const auto expanded_sql = (*syntax_)->expanded_sql();
      if (!expanded_sql.empty()) {
        return syntax_text(expanded_sql);
      }
    }
    return original_source_;
  }
  common::ObString original_source() const { return original_source_; }
  common::ObString original_source(const peg::Node &node) const
  {
    const auto span = (*syntax_)->original_span(node);
    return common::ObString(span.end - span.start, original_source_.ptr() + span.start);
  }
  common::ObString source(const peg::Node &node) const
  {
    const auto span = node.span();
    return common::ObString(span.end - span.start, source().ptr() + span.start);
  }
  int preserve_source(common::ObIAllocator &allocator);
  int statement_type(stmt::StmtType &type) const;
  void sql_traits(ObSqlTraits &traits);
  bool is_end_transaction() const;
  void reset() { syntax_.reset(); original_source_.reset(); metadata_ = {}; }

private:
  friend class ObParser;
  ParseResult metadata_{};
  common::ObString original_source_;
  std::optional<rust::Box<peg::ParseTree>> syntax_;
};

class ObMPParseStat {
public:
  void reset() { *this = {}; }
  bool parse_fail_ = false;
  int fail_ret_ = common::OB_SUCCESS;
  int64_t fail_query_idx_ = -1;
};

struct PreParseResult {
  common::ObString trace_id_;
};

class ObParser {
public:
  ObParser(common::ObIAllocator &allocator, ObSQLMode mode)
      : allocator_(allocator), sql_mode_(mode) {}
  int parse(const common::ObString &query, ParsedSql &parsed, ParseMode mode = STD_MODE);
  int parse_expression(const common::ObString &text, ParsedSql &parsed);
  int inspect_expression(const common::ObString &text, peg::ExpressionInfo &info);
  int parse_pl_sql(const common::ObString &text, ParsedSql &parsed);
  int parse_pl_expression(const common::ObString &text, ParsedSql &parsed);
  int parse_routine_body(const common::ObString &text, ParsedSql &parsed);
  int parse_routine_definition(const common::ObString &definition, ParsedSql &parsed);
  int split_multiple_stmt(const common::ObString &stmt,
                          common::ObIArray<common::ObString> &queries,
                          ObMPParseStat &parse_stat, bool first_only = false);
  static int pre_parse(const common::ObString &stmt, PreParseResult &result);
  static bool is_explain_stmt(const common::ObString &stmt, const char *&body);

private:
  int parse_entry(const common::ObString &sql, ParsedSql &parsed, peg::ParseMode mode,
                  bool dynamic_sql = false);
  common::ObIAllocator &allocator_;
  ObSQLMode sql_mode_;
};

}
