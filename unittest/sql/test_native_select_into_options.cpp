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

#include "common/datum/datum_parameter_codec.h"
#include "share/system_variable/ob_system_variable_init.h"
#include "sql/code_generator/ob_static_engine_cg.h"
#include "sql/engine/basic/ob_select_into_op.h"
#include "sql/engine/ob_exec_context.h"
#include "sql/engine/ob_physical_plan.h"
#include "sql/engine/ob_physical_plan_ctx.h"
#include "sql/optimizer/ob_log_select_into.h"
#include "sql/optimizer/ob_log_values.h"
#include "sql/optimizer/ob_select_log_plan.h"
#include "sql/parser/ob_parser.h"
#include "sql/resolver/dml/ob_select_resolver.h"
#include "sql/resolver/expr/ob_raw_expr_copier.h"
#include "sql/session/ob_sql_session_info.h"
#include <gtest/gtest.h>
#include <cstring>
#include <string>

namespace oceanbase {
namespace sql {
struct NativeSelectIntoOptionsTestPeer {
  static int generate(ObStaticEngineCG &cg, ObPhysicalPlan &plan,
                      ObLogSelectInto &logical, ObSelectIntoSpec &spec)
  {
    cg.phy_plan_ = &plan;
    return cg.generate_spec(logical, spec, true);
  }
};
} // namespace sql
} // namespace oceanbase

using namespace oceanbase::common;
using namespace oceanbase::sql;
using namespace oceanbase::share;
namespace {
class OptionsResolver final : public ObSelectResolver {
public:
  explicit OptionsResolver(ObResolverParams &params) : ObSelectResolver(params) {}
  using ObSelectResolver::resolve_into_outfile_without_format;
  using ObSelectResolver::resolve_into_const_node;
};
class OptionOwner final : public ObIAllocator {
public:
  ObArenaAllocator arena_;
  bool fail_ = false;
  int64_t calls_ = 0;
  void *alloc(int64_t bytes) override { ++calls_; return fail_ ? nullptr : arena_.alloc(bytes); }
  void *alloc(int64_t bytes, const ObMemAttr &) override { return alloc(bytes); }
  void free(void *ptr) override { arena_.free(ptr); }
};

// This is the exact pre-migration v2 member layout from 18f6daea's
// ob_select_into_op.cpp. It deliberately uses the old Datum codec, not any
// native option helper; the same fixture serves as old reader and old writer.
struct HistoricalOptionValue {
  ObDatum datum_;
  SqlTypeMeta meta_;
  ObString get_string() const { return datum_.get_string(); }
};
class HistoricalSelectIntoSpec final : public ObOpSpec {
  OB_UNIS_VERSION_V(2);
public:
  explicit HistoricalSelectIntoSpec(ObIAllocator &owner)
    : ObOpSpec(owner, PHY_SELECT_INTO), into_type_(T_INTO_OUTFILE), user_vars_(owner),
      is_optional_(false), select_exprs_(owner), is_single_(true),
      max_file_size_(256LL * 1024 * 1024), cs_type_(CS_TYPE_INVALID), parallel_(1),
      buffer_size_(1024 * 1024), is_overwrite_(false), external_properties_(owner) {}
  ObItemType into_type_;
  ObFixedArray<ObString, ObIAllocator> user_vars_;
  HistoricalOptionValue outfile_name_, field_str_, line_str_, closed_cht_;
  bool is_optional_;
  ObFixedArray<ObExpr *, ObIAllocator> select_exprs_;
  bool is_single_;
  int64_t max_file_size_;
  HistoricalOptionValue escaped_cht_;
  ObCollationType cs_type_;
  int64_t parallel_, buffer_size_;
  bool is_overwrite_;
  ObExternalFileFormat::StringData external_properties_;
};
OB_SERIALIZE_MEMBER((HistoricalSelectIntoSpec, ObOpSpec), into_type_, user_vars_,
    outfile_name_.datum_, outfile_name_.meta_,
    field_str_.datum_, field_str_.meta_, line_str_.datum_, line_str_.meta_,
    closed_cht_.datum_, closed_cht_.meta_, is_optional_, select_exprs_, is_single_, max_file_size_,
    escaped_cht_.datum_, escaped_cht_.meta_, cs_type_, parallel_, buffer_size_, is_overwrite_,
    external_properties_);

template <typename T> std::string wire(const T &value)
{
  std::string bytes(value.get_serialize_size(), '\0');
  int64_t pos = 0;
  EXPECT_EQ(OB_SUCCESS, value.serialize(bytes.data(), bytes.size(), pos));
  EXPECT_EQ(bytes.size(), pos);
  return bytes;
}
std::string bytes(const Datum &value)
{
  const ObString text = value.get_string();
  return std::string(text.ptr() == nullptr ? "" : text.ptr(), text.length());
}
SqlTypeMeta varchar_meta()
{
  SqlTypeMeta meta;
  meta.set_varchar(); meta.set_collation_type(CS_TYPE_UTF8MB4_BIN);
  return meta;
}
ParseNode *find_node(ParseNode *node, ObItemType type)
{
  ParseNode *found = node != nullptr && node->type_ == type ? node : nullptr;
  for (int64_t i = 0; found == nullptr && node != nullptr && i < node->num_child_; ++i) {
    found = find_node(node->children_[i], type);
  }
  return found;
}
void assign_logical(ObLogSelectInto &op, const ObSelectIntoItem &item)
{
  ObItemType type = T_INTO_OUTFILE;
  op.set_into_type(type);
  op.set_outfile_name(item.outfile_name_, item.outfile_name_meta_);
  op.set_field_str(item.field_str_, item.field_str_meta_);
  op.set_line_str(item.line_str_, item.line_str_meta_);
  op.set_closed_cht(item.closed_cht_, item.closed_cht_meta_);
  op.set_escaped_cht(item.escaped_cht_, item.escaped_cht_meta_);
  op.set_is_optional(item.is_optional_);
  op.set_is_single(item.is_single_);
  op.set_max_file_size(item.max_file_size_);
  op.set_buffer_size(item.buffer_size_);
  auto collation = item.cs_type_; op.set_cs_type(collation);
}
class NativeSelectIntoOptions : public ::testing::Test {
protected:
  static void SetUpTestCase()
  {
    ASSERT_EQ(OB_SUCCESS, ObCharset::init_charset());
    ASSERT_EQ(OB_SUCCESS, ObSysVariables::init_default_values());
  }
};

TEST_F(NativeSelectIntoOptions, real_parser_ast_copy_and_cg_own_their_option_bytes)
{
  ObSQLSessionInfo session;
  ASSERT_EQ(OB_SUCCESS, session.test_init(1, 1, nullptr));
  ASSERT_EQ(OB_SUCCESS, session.load_default_sys_variable(false, false));
  ObArenaAllocator parsed_owner("IntoParse"), ast_owner("IntoAst"), opt_owner("IntoOpt");
  ObRawExprFactory parsed_factory(parsed_owner), ast_factory(ast_owner), opt_factory(opt_owner);
  ObResolverParams params;
  params.allocator_ = &parsed_owner; params.expr_factory_ = &parsed_factory; params.session_info_ = &session;
  OptionsResolver resolver(params);
  ObSelectIntoItem parsed, copied, borrowed;
  ObParser parser(parsed_owner, 0);
  ParseResult result = {};
  ASSERT_EQ(OB_SUCCESS, parser.parse(ObString::make_string(
      "SELECT 1 INTO OUTFILE 'datum_native.txt' FIELDS TERMINATED BY '|' "
      "OPTIONALLY ENCLOSED BY '\"' ESCAPED BY '' LINES TERMINATED BY '\\r\\n'"), result));
  ParseNode *into = find_node(result.result_tree_, T_INTO_OUTFILE);
  ASSERT_NE(nullptr, into);
  ASSERT_EQ(OB_SUCCESS, resolver.resolve_into_outfile_without_format(into, parsed));
  ASSERT_EQ(OB_SUCCESS, borrowed.assign(parsed));
  ObRawExprCopier copier(ast_factory);
  ASSERT_EQ(OB_SUCCESS, copied.deep_copy(ast_owner, copier, parsed));
  EXPECT_EQ(parsed.outfile_name_.get_reference(), borrowed.outfile_name_.get_reference());
  EXPECT_NE(parsed.outfile_name_.get_reference(), copied.outfile_name_.get_reference());
  EXPECT_EQ("datum_native.txt", bytes(copied.outfile_name_));
  EXPECT_EQ("|", bytes(copied.field_str_));
  EXPECT_EQ("\r\n", bytes(copied.line_str_));
  EXPECT_EQ("\"", bytes(copied.closed_cht_)); EXPECT_TRUE(copied.is_optional_);
  EXPECT_EQ("", bytes(copied.escaped_cht_));
  parser.free_result(result);
  parsed_factory.destory(); parsed_owner.reset();
  EXPECT_EQ("datum_native.txt", bytes(copied.outfile_name_));

  ObSelectStmt statement;
  ObGlobalHint hint;
  ObAddr server;
  ParamStore parameters{ObWrapperAllocator(opt_owner)};
  ObOptimizerContext opt(&session, nullptr, nullptr, nullptr, opt_owner,
                         server, hint, opt_factory, &statement, false);
  ObSelectLogPlan logical_plan(opt, &statement);
  ObLogSelectInto logical(logical_plan);
  ObLogValues child(logical_plan);
  logical.set_child(0, &child);
  ASSERT_EQ(1, logical.get_num_of_child());
  assign_logical(logical, copied);
  ObPhysicalPlan physical;
  ObSelectIntoSpec spec(physical.get_allocator(), PHY_SELECT_INTO);
  spec.plan_ = &physical; // The normal spec factory installs this owning plan.
  ObStaticEngineCG cg;
  ASSERT_EQ(OB_SUCCESS, NativeSelectIntoOptionsTestPeer::generate(cg, physical, logical, spec));
  EXPECT_NE(copied.outfile_name_.get_reference(), spec.outfile_name_.get_reference());
  EXPECT_NE(copied.line_str_.get_reference(), spec.line_str_.get_reference());
  ast_factory.destory(); ast_owner.reset();
  EXPECT_EQ("datum_native.txt", bytes(spec.outfile_name_));
  EXPECT_EQ("\r\n", bytes(spec.line_str_));
  EXPECT_EQ("", bytes(spec.escaped_cht_));
  EXPECT_TRUE(spec.is_optional_);
  EXPECT_EQ(copied.cs_type_, spec.cs_type_);
}

TEST_F(NativeSelectIntoOptions, version_two_matches_historical_member_codec_and_decode_owns_input)
{
  ObArenaAllocator owner("IntoWire"), decoded_owner("IntoRead");
  HistoricalSelectIntoSpec historical(owner);
  ObSelectIntoSpec native(owner, PHY_SELECT_INTO), restored(decoded_owner, PHY_SELECT_INTO);
  const SqlTypeMeta meta = varchar_meta();
  const ObString values[] = {ObString(3, "f\0x"), ObString::make_string("|"),
      ObString::make_string("\n"), ObString::make_string("\""), ObString::make_string("\\")};
  HistoricalOptionValue *old[] = {&historical.outfile_name_, &historical.field_str_, &historical.line_str_,
                       &historical.closed_cht_, &historical.escaped_cht_};
  Datum values_native[5];
  for (int i = 0; i < 5; ++i) {
    old[i]->meta_ = meta; old[i]->datum_.set_string(values[i]);
    ASSERT_EQ(OB_SUCCESS, values_native[i].set_string(values[i]));
  }
  ASSERT_EQ(OB_SUCCESS, native.set_outfile_name_native_value(values_native[0], meta));
  ASSERT_EQ(OB_SUCCESS, native.set_field_str_native_value(values_native[1], meta));
  ASSERT_EQ(OB_SUCCESS, native.set_line_str_native_value(values_native[2], meta));
  ASSERT_EQ(OB_SUCCESS, native.set_closed_cht_native_value(values_native[3], meta));
  ASSERT_EQ(OB_SUCCESS, native.set_escaped_cht_native_value(values_native[4], meta));
  const std::string old_bytes = wire(historical);
  EXPECT_EQ(old_bytes, wire(native));
  std::string input = old_bytes;
  int64_t pos = 0;
  ASSERT_EQ(OB_SUCCESS, restored.deserialize(input.data(), input.size(), pos));
  EXPECT_EQ(input.size(), pos);
  std::memset(input.data(), 'x', input.size());
  EXPECT_EQ(std::string("f\0x", 3), bytes(restored.outfile_name_));
  EXPECT_EQ(old_bytes, wire(restored));
  HistoricalSelectIntoSpec old_reader(decoded_owner);
  pos = 0;
  ASSERT_EQ(OB_SUCCESS, old_reader.deserialize(old_bytes.data(), old_bytes.size(), pos));
  EXPECT_EQ(ObString(3, "f\0x"), old_reader.outfile_name_.get_string());
  EXPECT_EQ(ObString::make_string("|"), old_reader.field_str_.get_string());
  EXPECT_EQ(ObString::make_string("\\"), old_reader.escaped_cht_.get_string());
}

TEST_F(NativeSelectIntoOptions, checked_option_clone_and_decode_failure_do_not_publish_bad_pair)
{
  OptionOwner owner;
  ObSelectIntoSpec target(owner, PHY_SELECT_INTO);
  Datum value; value.set_int(17);
  SqlTypeMeta integer; integer.set_int();
  owner.fail_ = true;
  const int64_t calls = owner.calls_;
  ASSERT_EQ(OB_SUCCESS, target.set_outfile_name_native_value(value, integer));
  EXPECT_EQ(calls, owner.calls_);
  const auto saved = wire(target);
  Datum text; ASSERT_EQ(OB_SUCCESS, text.set_string(ObString::make_string("new path")));
  EXPECT_EQ(OB_ALLOCATE_MEMORY_FAILED, target.set_outfile_name_native_value(text, varchar_meta()));
  EXPECT_EQ(saved, wire(target));

  ObArenaAllocator source;
  ObSelectIntoSpec incoming(source, PHY_SELECT_INTO);
  ASSERT_EQ(OB_SUCCESS, incoming.set_outfile_name_native_value(text, varchar_meta()));
  std::string input = wire(incoming);
  int64_t pos = 0;
  EXPECT_EQ(OB_ALLOCATE_MEMORY_FAILED, target.deserialize(input.data(), input.size(), pos));
  EXPECT_EQ(input.size(), pos); // Outer UNIS keeps its declared record boundary.
  EXPECT_EQ(17, target.outfile_name_.get_int());
  EXPECT_TRUE(target.outfile_name_meta_.is_int());
  owner.fail_ = false;
  pos = 0;
  ASSERT_EQ(OB_SUCCESS, target.deserialize(input.data(), input.size(), pos));
  std::memset(input.data(), 'x', input.size());
  EXPECT_EQ("new path", bytes(target.outfile_name_));
}

TEST_F(NativeSelectIntoOptions, filename_questionmark_open_keeps_index_and_nop_errors)
{
  ObArenaAllocator owner("IntoRuntime");
  ObSQLSessionInfo session;
  ASSERT_EQ(OB_SUCCESS, session.test_init(1, 1, nullptr));
  ASSERT_EQ(OB_SUCCESS, session.load_default_sys_variable(false, false));
  ObRawExprFactory factory(owner);
  ObResolverParams params;
  params.allocator_ = &owner; params.expr_factory_ = &factory; params.session_info_ = &session;
  OptionsResolver resolver(params);
  ObExecContext execution(owner);
  execution.set_my_session(&session);
  ASSERT_EQ(OB_SUCCESS, execution.create_physical_plan_ctx());
  DatumParameter input;
  ASSERT_EQ(OB_SUCCESS, input.set_varchar(ObString::make_string("slot zero"), owner));
  for (int i = 0; i < 4; ++i) { ASSERT_EQ(OB_SUCCESS, execution.get_physical_plan_ctx()->append_parameter_input(input)); }
  // A wrong lookup of slot zero must fail; the requested slot three remains a string.
  DatumParameter nop;
  ASSERT_EQ(OB_SUCCESS, nop.set_ext(ObActionFlag::OP_NOP, owner));
  ASSERT_EQ(OB_SUCCESS, execution.get_physical_plan_ctx()->init_datum_param_store());
  ASSERT_NE(nullptr, execution.get_physical_plan_ctx()->get_native_parameter_type(0));
  ASSERT_EQ(OB_SUCCESS, execution.get_physical_plan_ctx()->set_runtime_parameter(0, nop.get_datum(),
      *execution.get_physical_plan_ctx()->get_native_parameter_type(0), nop.get_metadata()));
  // SELECT INTO VARIABLES has no filename, while empty and embedded-NUL
  // strings are still legal physical option values before any file-path policy.
  for (int variant = 0; variant < 3; ++variant) {
    SCOPED_TRACE(variant);
    ObSelectIntoSpec spec(owner, PHY_SELECT_INTO);
    spec.into_type_ = T_INTO_VARIABLES; spec.cs_type_ = CS_TYPE_UTF8MB4_BIN;
    Datum filename;
    SqlTypeMeta meta;
    if (variant != 0) {
      meta = varchar_meta();
      ASSERT_EQ(OB_SUCCESS, filename.set_string(variant == 1 ? ObString() : ObString(3, "a\0b")));
    }
    ASSERT_EQ(OB_SUCCESS, spec.set_outfile_name_native_value(filename, meta));
    ObSelectIntoOp op(execution, spec, nullptr);
    EXPECT_EQ(OB_SUCCESS, op.inner_open());
  }
  const int64_t indices[] = {-1, 4, 3};
  const int expected[] = {OB_ERR_ILLEGAL_INDEX, OB_ERR_ILLEGAL_INDEX, OB_SUCCESS};
  for (int i = 0; i < 3; ++i) {
    SCOPED_TRACE(indices[i]);
    ObSelectIntoSpec spec(owner, PHY_SELECT_INTO);
    spec.into_type_ = T_INTO_VARIABLES; // Exercise open's selection, without file I/O.
    spec.cs_type_ = CS_TYPE_UTF8MB4_BIN;
    ParseNode node = {}; node.type_ = T_QUESTIONMARK; node.value_ = indices[i];
    Datum index;
    SqlTypeMeta meta;
    ASSERT_EQ(OB_SUCCESS, resolver.resolve_into_const_node(&node, index, meta));
    EXPECT_TRUE(meta.is_unknown());
    EXPECT_EQ(sizeof(int64_t), index.length()); // Generic physical Unknown stays Reference8.
    ASSERT_EQ(OB_SUCCESS, spec.set_outfile_name_native_value(index, meta));
    ObSelectIntoOp op(execution, spec, nullptr);
    EXPECT_EQ(expected[i], op.inner_open());
  }
  ASSERT_NE(nullptr, execution.get_physical_plan_ctx()->get_native_parameter_type(3));
  ASSERT_EQ(OB_SUCCESS, execution.get_physical_plan_ctx()->set_runtime_parameter(3, nop.get_datum(),
      *execution.get_physical_plan_ctx()->get_native_parameter_type(3), nop.get_metadata()));
  ObSelectIntoSpec spec(owner, PHY_SELECT_INTO);
  spec.into_type_ = T_INTO_VARIABLES; spec.cs_type_ = CS_TYPE_UTF8MB4_BIN;
  ParseNode node = {}; node.type_ = T_QUESTIONMARK; node.value_ = 3;
  Datum index;
  SqlTypeMeta meta;
  ASSERT_EQ(OB_SUCCESS, resolver.resolve_into_const_node(&node, index, meta));
  ASSERT_EQ(OB_SUCCESS, spec.set_outfile_name_native_value(index, meta));
  ObSelectIntoOp op(execution, spec, nullptr);
  EXPECT_EQ(OB_ERR_NOP_VALUE, op.inner_open());
}
} // namespace
