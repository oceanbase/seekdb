/*
 * Copyright (c) 2026 OceanBase.
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

#ifndef SEEKDB_SQL_ENGINE_EXPR_PLUGIN_FUNCTION_EXPR_H_
#define SEEKDB_SQL_ENGINE_EXPR_PLUGIN_FUNCTION_EXPR_H_

#include <memory>
#include <string>
#include <vector>

#include "seekdb/plugin/sql_catalog.h"
#include "share/rc/ob_module_provider.h"
#include "sql/engine/expr/ob_expr_operator.h"
#include "sql/engine/expr/ob_i_expr_extra_info.h"

namespace oceanbase
{
namespace share { namespace schema { class ObRoutineInfo; } }
namespace sql
{
class ObRawExprFactory;
class ObSysFunRawExpr;
class ObOpRawExpr;
class ObCaseOpRawExpr;
class ObSelectStmt;

// CASE and set-query branches share the Rust logical-type policy. Native-only
// promotion remains owned by the existing SQL operators.
class PluginBranchType final
{
public:
  struct Result { std::string type_id_; uint64_t epoch_ = 0; };
  static int prepare_case(ObRawExprFactory *factory, ObCaseOpRawExpr &raw,
                     const ObSQLSessionInfo *session, std::string &type_id, uint64_t &epoch);
  static int prepare_set(ObRawExprFactory *factory, const ObSQLSessionInfo *session,
                        common::ObIArray<ObSelectStmt *> &statements, bool distinct,
                        std::vector<Result> &results, bool recursive = false);
  static int finish(ObRawExpr &raw, const std::string &type_id, uint64_t epoch);
  static int finish_set(common::ObIArray<ObSelectStmt *> &statements, int64_t column, const Result &result);
  static int preserve_native_cast(const ObRawExpr &source, ObRawExpr &target);
};

// Per-plugin-expression plan data, not an added field on every SQL value.
// All strings are owned by the plan allocator, including after deserialization.
// Argument identifiers additionally have a trailing NUL for borrowed C ABI use.
struct PluginStoredArgument
{
  OB_UNIS_VERSION(1);
public:
  uint32_t index_ = 0; // excludes the hidden dispatch-name argument
  seekdb_plugin_sql_binding_v1_t binding_ = {};
  bool valid() const;
  TO_STRING_KV(K_(index));
};

class PluginFunctionExtraInfo final : public ObIExprExtraInfo
{
  OB_UNIS_VERSION(2);
public:
  PluginFunctionExtraInfo(common::ObIAllocator &allocator, ObExprOperatorType type)
      : ObIExprExtraInfo(allocator, type), allocator_(allocator), arguments_(allocator), stored_(allocator) {}
  int initialize(const seekdb_plugin_sql_binding_v1_t &binding,
                 const std::vector<std::string> &arguments,
                 const std::vector<PluginStoredArgument> &stored = {});
  int binding(seekdb_plugin_sql_binding_v1_t &out) const;
  int deep_copy(common::ObIAllocator &allocator, ObExprOperatorType type,
                ObIExprExtraInfo *&out) const override;
  const common::ObIArray<common::ObString> &arguments() const { return arguments_; }
  const common::ObIArray<PluginStoredArgument> &stored() const { return stored_; }
private:
  common::ObIAllocator &allocator_;
  common::ObString sql_name_, object_id_, owner_, result_type_;
  uint64_t generation_ = 0, epoch_ = 0, flags_ = 0;
  uint32_t minimum_arity_ = 0, maximum_arity_ = 0;
  common::ObFixedArray<common::ObString, common::ObIAllocator> arguments_;
  // Sparse, sorted by argument index. No allocation for ordinary parameters.
  common::ObFixedArray<PluginStoredArgument, common::ObIAllocator> stored_;
};

class PluginFunctionExpr final : public ObFuncExprOperator
{
public:
  static constexpr const char *SQL_DISPATCH_NAME = "__seekdb_plugin_function";

  explicit PluginFunctionExpr(common::ObIAllocator &allocator);

  int calc_result_typeN(ObExprResType &type,
                        ObExprResType *arguments,
                        int64_t argument_count,
                        common::ObExprTypeCtx &type_context) const override;
  bool need_rt_ctx() const override { return true; }
  int cg_expr(ObExprCGCtx &cg_context,
              const ObRawExpr &raw_expression,
              ObExpr &runtime_expression) const override;

  static int evaluate(const ObExpr &expression,
                      ObEvalCtx &context,
                      common::ObDatum &result);
  static int evaluate_batch(const ObExpr &expression, ObEvalCtx &context,
                            const ObBitVector &skip, int64_t size);
  // Native catalog routines have no hidden dispatch-name argument. Their UDF
  // caller owns catalog/ACL checks; both paths share marshalling and execution.
  static int evaluate_bound(const ObExpr &, ObEvalCtx &, common::ObDatum &,
                            const PluginFunctionExtraInfo *, uint32_t argument_offset);
  static int evaluate_bound_batch(const ObExpr &, ObEvalCtx &, const ObBitVector &, int64_t,
                                  const PluginFunctionExtraInfo *, uint32_t argument_offset);
  static int resolve_native_binding(const share::schema::ObRoutineInfo &routine,
      seekdb_plugin_sql_binding_v1_t &binding, std::vector<std::string> &arguments,
      int64_t call_argument_count = -1);
  // Unary cast/type carriers preserve batch execution through their source.
  static int evaluate_argument_batch(const ObExpr &expression, ObEvalCtx &context,
                                     const ObBitVector &skip, int64_t size);
  static int resolve_raw_binding(const ObRawExpr &expression,
                                seekdb_plugin_sql_binding_v1_t &binding,
                                std::vector<std::string> &arguments, uint32_t depth = 0,
                                std::vector<PluginStoredArgument> *stored = nullptr);

private:
  class RuntimeContext final : public ObExprOperatorCtx
  {
  public:
    RuntimeContext() : initialized_(false), binding_() {}

    bool initialized_;
    seekdb_plugin_sql_binding_v1_t binding_;
  };
};

class PluginCastExtraInfo final : public ObIExprExtraInfo
{
  OB_UNIS_VERSION(1);
public:
  PluginCastExtraInfo(common::ObIAllocator &allocator, ObExprOperatorType type)
      : ObIExprExtraInfo(allocator, type) {}
  seekdb_plugin_sql_cast_binding_v1_t binding_ = {};
  uint8_t decode_source_ = 0;
  PluginStoredArgument source_;
  bool valid() const;
  int deep_copy(common::ObIAllocator &allocator, ObExprOperatorType type,
                ObIExprExtraInfo *&out) const override;
};

// Internal coercion: value + a hidden, versioned binding constant. The raw
// expression owns that constant; codegen copies it into plan extra-info. Neither
// repeated inference nor execution performs cast selection again.
class PluginCastExpr final : public ObFuncExprOperator
{
public:
  explicit PluginCastExpr(common::ObIAllocator &allocator);
  static int build(ObRawExprFactory &factory, const common::ObString &target_type,
                   seekdb_plugin_cast_context_t requested_context,
                   ObRawExpr *&value, const ObSQLSessionInfo *session);
  static int read_binding(const ObRawExpr &raw, PluginCastExtraInfo &info, uint32_t depth = 0);
  // Called after source-column resolution, before the ordinary SQL CAST
  // deduces its physical conversion. Implicit representation casts are exempt.
  static int coerce_sql_cast(ObRawExprFactory *factory, ObSysFunRawExpr &raw,
                            const ObSQLSessionInfo *session);
  int calc_result_type2(ObExprResType &type, ObExprResType &value, ObExprResType &metadata,
                       common::ObExprTypeCtx &context) const override;
  int cg_expr(ObExprCGCtx &context, const ObRawExpr &raw, ObExpr &runtime) const override;
  static int evaluate(const ObExpr &expression, ObEvalCtx &context, common::ObDatum &result);
};

class PluginTypeEncodeExtraInfo final : public ObIExprExtraInfo
{
  OB_UNIS_VERSION(1);
public:
  PluginTypeEncodeExtraInfo(common::ObIAllocator &allocator, ObExprOperatorType type)
      : ObIExprExtraInfo(allocator, type) {}
  PluginStoredArgument target_;
  bool valid() const { return target_.valid() && target_.index_ == 0; }
  int deep_copy(common::ObIAllocator &allocator, ObExprOperatorType type,
                ObIExprExtraInfo *&out) const override;
};

// A typed value need not call a conversion function: SQL NULL and an already
// matching logical value have no cast implementation to acquire. Stored values
// of the same type still need their decoder, with its own real TYPE binding.
class PluginTypeComparisonExtraInfo;
class PluginTypeValueExtraInfo final : public ObIExprExtraInfo
{
  OB_UNIS_VERSION(1);
public:
  enum Mode : uint8_t { IDENTITY = 0, TYPED_NULL = 1, DECODE = 2, ORDERED = 3 };
  PluginTypeValueExtraInfo(common::ObIAllocator &allocator, ObExprOperatorType type)
      : ObIExprExtraInfo(allocator, type), allocator_(allocator) {}
  common::ObIAllocator &allocator_;
  PluginTypeComparisonExtraInfo *ordering_ = nullptr;
  char logical_id_[SEEKDB_PLUGIN_MAX_IDENTIFIER_BYTES + 1] = {};
  uint64_t catalog_epoch_ = 0;
  uint8_t mode_ = IDENTITY;
  PluginStoredArgument source_;
  bool valid() const;
  int deep_copy(common::ObIAllocator &allocator, ObExprOperatorType type,
                ObIExprExtraInfo *&out) const override;
};

class PluginTypeValueExpr final : public ObFuncExprOperator
{
public:
  explicit PluginTypeValueExpr(common::ObIAllocator &allocator);
  // Parsing can preserve a target SQL name without resolving a not-yet-bound
  // source column. The first type-deduction pass replaces it with owned wire.
  static int prepare(ObRawExprFactory &factory, const common::ObString &sql_type,
                     ObRawExpr *&value);
  // The caller must first resolve source-column names. Native conversions use
  // the existing Rust-selected cast; identity/NULL never synthesize a cast ID.
  static int build(ObRawExprFactory &factory, const common::ObString &sql_type,
                   ObRawExpr *&value, const ObSQLSessionInfo *session);
  static int read_binding(const ObRawExpr &raw, PluginTypeValueExtraInfo &info, uint32_t depth = 0);
  // Keep a query-bound TYPE comparator with a value consumed by an operator.
  // Native identities are unchanged; stored values acquire a decoder first.
  static int prepare_ordering(ObRawExprFactory &factory, ObRawExpr *&value,
                              const ObSQLSessionInfo *session);
  static int compare_ordered(const ObExpr *value, ObEvalCtx &context,
                             const common::ObDatum &left, const common::ObDatum &right,
                             bool &handled, int &ordering);
  int calc_result_type2(ObExprResType &type, ObExprResType &value, ObExprResType &metadata,
                       common::ObExprTypeCtx &context) const override;
  int cg_expr(ObExprCGCtx &context, const ObRawExpr &raw, ObExpr &runtime) const override;
  static int evaluate(const ObExpr &expression, ObEvalCtx &context, common::ObDatum &result);
};

// Unlike PluginStoredArgument this binding also permits nonpersistent TYPEs.
// Serialize fields, never ABI padding or executable pointers.
class PluginTypeComparisonExtraInfo final : public ObIExprExtraInfo
{
  OB_UNIS_VERSION(1);
public:
  PluginTypeComparisonExtraInfo(common::ObIAllocator &allocator, ObExprOperatorType type)
      : ObIExprExtraInfo(allocator, type) {}
  seekdb_plugin_sql_binding_v1_t binding_ = {};
  uint8_t null_safe_ = 0;
  bool valid() const;
  int deep_copy(common::ObIAllocator &allocator, ObExprOperatorType type, ObIExprExtraInfo *&out) const override;
};

class PluginTypeComparisonExpr final : public ObFuncExprOperator
{
public:
  explicit PluginTypeComparisonExpr(common::ObIAllocator &allocator);
  // Before physical demotion: custom a OP b becomes compare(a,b,binding) OP 0.
  // BETWEEN uses a single-evaluation internal expression and native boolean
  // BETWEEN/NOT BETWEEN. Native common types keep the ordinary SQL comparator.
  static int prepare(ObRawExprFactory *factory, ObOpRawExpr &raw, const ObSQLSessionInfo *session);
  // Nested row consumers need leaf deduction before native row validation.
  static bool has_nested_row_operands(const ObRawExpr &raw);
  static int read_binding(const ObRawExpr &raw, PluginTypeComparisonExtraInfo &info);
  int calc_result_type3(ObExprResType &type, ObExprResType &left, ObExprResType &right,
                       ObExprResType &metadata, common::ObExprTypeCtx &context) const override;
  int cg_expr(ObExprCGCtx &context, const ObRawExpr &raw, ObExpr &runtime) const override;
  static int evaluate(const ObExpr &expression, ObEvalCtx &context, common::ObDatum &result);
  // Shared operand batching for binary comparison and BETWEEN.
  static int evaluate_batch(const ObExpr &expression, ObEvalCtx &context,
                            const ObBitVector &skip, int64_t size);
};

// Value, lower bound, upper bound, pointer-free TYPE binding. Keeps the native
// BETWEEN argument evaluation order and SQL three-valued logic.
class PluginTypeBetweenExpr final : public ObFuncExprOperator
{
public:
  explicit PluginTypeBetweenExpr(common::ObIAllocator &allocator);
  static int read_binding(const ObRawExpr &raw, PluginTypeComparisonExtraInfo &info);
  int calc_result_typeN(ObExprResType &type, ObExprResType *types, int64_t count,
                       common::ObExprTypeCtx &context) const override;
  int cg_expr(ObExprCGCtx &context, const ObRawExpr &raw, ObExpr &runtime) const override;
  static int evaluate(const ObExpr &expression, ObEvalCtx &context, common::ObDatum &result);
};

// Scalar IN list: one value, one or more candidates, then a TYPE binding.
// No carrier hash table is legal without the plugin's hash/equality contract.
class PluginTypeInExpr final : public ObFuncExprOperator
{
public:
  explicit PluginTypeInExpr(common::ObIAllocator &allocator);
  static int read_binding(const ObRawExpr &raw, PluginTypeComparisonExtraInfo &info);
  int calc_result_typeN(ObExprResType &type, ObExprResType *types, int64_t count,
                       common::ObExprTypeCtx &context) const override;
  int cg_expr(ObExprCGCtx &context, const ObRawExpr &raw, ObExpr &runtime) const override;
  static int evaluate(const ObExpr &expression, ObEvalCtx &context, common::ObDatum &result);
  static int evaluate_batch(const ObExpr &expression, ObEvalCtx &context,
                            const ObBitVector &skip, int64_t size);
};

// Internal assignment expression. It emits encoded bytes; the ordinary column
// conversion above it still owns LOB representation and column constraints.
class PluginTypeEncodeExpr final : public ObFuncExprOperator
{
public:
  explicit PluginTypeEncodeExpr(common::ObIAllocator &allocator);
  static int build(ObRawExprFactory &factory, const ObRawExpr &target,
                   ObRawExpr *&value, const ObSQLSessionInfo *session);
  static int read_binding(const ObRawExpr &raw, PluginTypeEncodeExtraInfo &info);
  int calc_result_type2(ObExprResType &type, ObExprResType &argument, ObExprResType &metadata,
                       common::ObExprTypeCtx &context) const override;
  int cg_expr(ObExprCGCtx &context, const ObRawExpr &raw, ObExpr &runtime) const override;
  static int evaluate(const ObExpr &expression, ObEvalCtx &context, common::ObDatum &result);
};

class PluginTableFunctionExtraInfo final : public ObIExprExtraInfo
{
  OB_UNIS_VERSION(1);
public:
  PluginTableFunctionExtraInfo(common::ObIAllocator &allocator, ObExprOperatorType type)
      : ObIExprExtraInfo(allocator, type), allocator_(allocator), arguments_(allocator), columns_(allocator) {}
  seekdb_plugin_sql_binding_v1_t binding_ = {};
  common::ObIAllocator &allocator_;
  common::ObFixedArray<common::ObString, common::ObIAllocator> arguments_;
  struct Column : seekdb_plugin_sql_column_v1_t {
    TO_STRING_KV(K(sql_name), K(type_id), K(nullable));
  };
  common::ObFixedArray<Column, common::ObIAllocator> columns_;
  bool valid() const;
  int deep_copy(common::ObIAllocator &allocator, ObExprOperatorType type, ObIExprExtraInfo *&out) const override;
};

class PluginTableFunctionExpr final : public ObFuncExprOperator
{
public:
  static constexpr const char *SQL_DISPATCH_NAME =
      "__seekdb_plugin_table_function";

  explicit PluginTableFunctionExpr(common::ObIAllocator &allocator);

  int calc_result_typeN(ObExprResType &type,
                        ObExprResType *arguments,
                        int64_t argument_count,
                        common::ObExprTypeCtx &type_context) const override;
  bool need_rt_ctx() const override { return true; }
  int cg_expr(ObExprCGCtx &cg_context,
              const ObRawExpr &raw_expression,
              ObExpr &runtime_expression) const override;

  static int resolve_binding(const ObRawExpr &expression,
                             seekdb_plugin_sql_binding_v1_t &binding);
  static int read_binding(const ObRawExpr &expression, PluginTableFunctionExtraInfo &info);
  static int column_type(const seekdb_plugin_sql_column_v1_t &column, ObExprResType &type);
  static int fetch_row(const ObExpr &expression,
                       ObEvalCtx &context,
                       const common::ObIArray<ObExpr *> &columns);
  // Columns are indexed by the plugin's declared ordinal; nullptr means that
  // SQL pruned this column. Output datums must have batch storage for >1 row.
  static int fetch_batch(const ObExpr &expression, ObEvalCtx &context,
                        const common::ObIArray<ObExpr *> &columns,
                        uint32_t maximum_rows, uint32_t &emitted_rows);
  static int rescan(const ObExpr &expression, ObEvalCtx &context);
  static int close(const ObExpr &expression, ObEvalCtx &context);

private:
  static int fetch(const ObExpr &expression, ObEvalCtx &context,
                   const common::ObIArray<ObExpr *> &columns,
                   uint32_t maximum_rows, bool batch, uint32_t &emitted_rows);
  static int evaluate(const ObExpr &expression,
                      ObEvalCtx &context,
                      common::ObDatum &result);

  class RuntimeContext final : public ObExprOperatorCtx
  {
  public:
    RuntimeContext() : initialized_(false), ended_(false), error_(0), binding_(), cursor_() {}

    bool initialized_;
    bool ended_;
    int error_;
    seekdb_plugin_sql_binding_v1_t binding_;
    std::unique_ptr<share::IPluginTableCursor> cursor_;
  };
};

} // namespace sql
} // namespace oceanbase

#endif // SEEKDB_SQL_ENGINE_EXPR_PLUGIN_FUNCTION_EXPR_H_
