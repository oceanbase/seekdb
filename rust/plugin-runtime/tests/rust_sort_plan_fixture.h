// Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.
// Real SELECT planning and sorting over a Rust DSO. Only schema/session and
// temporary-directory allocation are fixtures; any file spill is an error here.
#ifndef SEEKDB_TEST_RUST_SORT_PLAN_FIXTURE_H_
#define SEEKDB_TEST_RUST_SORT_PLAN_FIXTURE_H_
#include "sql/engine/sort/ob_sort_op.h"
#include "sql/optimizer/ob_log_sort.h"
#include "storage/tmp_file/ob_tmp_file_manager.h"
#include "share/rc/ob_server_runtime.h"
#include "rust_px_merge_fixture.h"
#include "rust_custom_lob_fixture.h"
#include "rust_custom_numeric_fixture.h"
#include "candidate_graph_fixture.h"
#include "rust_custom_projection_fixture.h"
#include "rust_custom_multi_fixture.h"
#include "rust_custom_dag_fixture.h"
#include "rust_custom_sort_fixture.h"
namespace rust_sort_plan_test {
using namespace oceanbase::common;
using namespace oceanbase::sql;
using namespace oceanbase::share::schema;
class MemoryOnlyDirectories final : public oceanbase::tmp_file::ObTmpFileManager {
  class DirectoryIds final : public oceanbase::tmp_file::ObSNTmpFileManager {
  public:
    int init() override { return OB_SUCCESS; }
    int alloc_dir(int64_t &id) override { id = ++directories_; return OB_SUCCESS; }
    int open(int64_t &, const int64_t &, const char *) override { CHECK(false); return OB_NOT_SUPPORTED; }
    int64_t directories_ = 0;
  } ids_;
public:
  oceanbase::tmp_file::ObSNTmpFileManager &get_sn_file_manager() override { return ids_; }
};
inline const ObSortSpec *find_sort(const ObOpSpec *root)
{
  if (root->get_type() == PHY_SORT) return static_cast<const ObSortSpec *>(root);
  for (int64_t i = 0; i < root->get_child_cnt(); ++i)
    if (const auto *found = find_sort(root->get_child(i))) return found;
  return nullptr;
}
inline const PluginCustomSpec *find_rust_sort(const ObOpSpec *root)
{
  if (root->get_type() == PHY_PLUGIN_CUSTOM) {
    const auto &spec = static_cast<const PluginCustomSpec &>(*root);
    if (spec.parameters_.length() >= 12 && std::memcmp(spec.parameters_.ptr(), "SSO1", 4) == 0) return &spec;
  }
  for (int64_t i = 0; i < root->get_child_cnt(); ++i)
    if (const auto *found = find_rust_sort(root->get_child(i))) return found;
  return nullptr;
}
inline bool has_material(const ObOpSpec *root)
{
  if (root->get_type() == PHY_MATERIAL) return true;
  for (int64_t i = 0; i < root->get_child_cnt(); ++i)
    if (has_material(root->get_child(i))) return true;
  return false;
}
inline bool has_custom(const ObOpSpec *root)
{
  if (root->get_type() == PHY_PLUGIN_CUSTOM) return true;
  for (int64_t i = 0; i < root->get_child_cnt(); ++i)
    if (has_custom(root->get_child(i))) return true;
  return false;
}
inline bool has_custom_lob(const ObOpSpec *root)
{
  if (root->get_type() == PHY_PLUGIN_CUSTOM) {
    const auto &spec = static_cast<const PluginCustomSpec &>(*root);
    for (const auto *expr : spec.columns_)
      if (is_lob_storage(expr->datum_meta_.type_) && expr->obj_meta_.has_lob_header()) return true;
  }
  for (int64_t i = 0; i < root->get_child_cnt(); ++i)
    if (has_custom_lob(root->get_child(i))) return true;
  return false;
}
inline bool has_custom_codec(const ObOpSpec *root)
{
  if (root->get_type() == PHY_PLUGIN_CUSTOM) {
    for (const auto &wire : static_cast<const PluginCustomSpec &>(*root).codecs_)
      if (!wire.empty()) return true;
  }
  for (int64_t i = 0; i < root->get_child_cnt(); ++i)
    if (has_custom_codec(root->get_child(i))) return true;
  return false;
}
inline bool has_custom_float(const ObOpSpec *root)
{
  if (root->get_type() == PHY_PLUGIN_CUSTOM) {
    const auto &spec = static_cast<const PluginCustomSpec &>(*root);
    for (int64_t i = 0; i < spec.columns_.count(); ++i)
      if (spec.columns_.at(i)->datum_meta_.type_ == ObFloatType &&
          spec.type_ids_.at(i) == ObString::make_string("core.type.float64")) return true;
  }
  for (int64_t i = 0; i < root->get_child_cnt(); ++i)
    if (has_custom_float(root->get_child(i))) return true;
  return false;
}
inline bool has_custom_layout(const ObOpSpec *root, bool derived, bool target_only = false)
{
  if (root->get_type() == PHY_PLUGIN_CUSTOM) {
    const auto &spec = static_cast<const PluginCustomSpec &>(*root);
    if (spec.explicit_input_) {
      CHECK(spec.input_columns_.count() == 3 && spec.columns_.count() == (target_only ? 3 : 2));
      CHECK(spec.input_columns_.at(0) == spec.input_columns_.at(2));
      CHECK(spec.columns_.at(1) == spec.input_columns_.at(0));
      CHECK((spec.columns_.at(0) == spec.input_columns_.at(1)) == !derived);
      if (derived) {
        for (auto *expr : spec.get_child(0)->output_) CHECK(expr != spec.columns_.at(0));
        for (auto *expr : spec.calc_exprs_) CHECK(expr != spec.columns_.at(0));
      }
      if (target_only) {
        for (auto *expr : spec.get_child(0)->output_) CHECK(expr != spec.columns_.at(2));
        for (auto *expr : spec.calc_exprs_) CHECK(expr != spec.columns_.at(2));
        CHECK(spec.type_ids_.at(2) == spec.input_type_ids_.at(0));
      }
      CHECK(spec.type_ids_.at(0) == spec.input_type_ids_.at(1) && spec.type_ids_.at(1) == spec.input_type_ids_.at(0));
      CHECK(spec.parameters_.length() == (target_only ? 20 : 16) && std::memcmp(spec.parameters_.ptr(), "SPJ1", 4) == 0);
      return true;
    }
  }
  for (int64_t i = 0; i < root->get_child_cnt(); ++i) if (has_custom_layout(root->get_child(i), derived, target_only)) return true;
  return false;
}
template <typename Provider>
void run_case(Provider &provider, oceanbase::share::plugin::ObPluginLoader &loader,
    ObArenaAllocator &arena, const char *ordering, const char *limit,
    const std::vector<int> &expected, int batch_size, bool native = false,
    const char *input = "z aa 🙂 bbb a z", bool partition_ranges = false, bool lob_payload = false,
    bool stored_payload = false, bool float_payload = false, bool derived_payload = false,
    const char *join_input = nullptr, int join_mode = 0, const std::vector<int> &right_expected = {})
{
  auto session = std::make_unique<ObSQLSessionInfo>();
  CHECK(session->test_init(1, 1, &arena) == OB_SUCCESS);
  CHECK(session->load_default_sys_variable(false, false) == OB_SUCCESS);
  session->set_inner_session();
  auto schemas = std::make_unique<MockSchemaService>();
  auto manager = std::make_unique<ObSchemaMgr>(); CHECK(manager->init() == OB_SUCCESS);
  ObTableSchema partition_schema, full_partition_schema;
  ObSchemaGetterGuard guard; CHECK(MockSchemaService::bind(guard, *schemas, *manager) == OB_SUCCESS);
  if (partition_ranges) {
    rust_partition_expression_test::build_schema(partition_schema);
    CHECK(MockSchemaService::cache_table(guard, partition_schema) == OB_SUCCESS);
    rust_partition_expression_test::build_schema(full_partition_schema, true);
    CHECK(MockSchemaService::cache_table(guard, full_partition_schema) == OB_SUCCESS);
  }
  ObSchemaChecker checker; CHECK(checker.init(guard) == OB_SUCCESS);
  ObSqlSchemaGuard sql_guard; sql_guard.set_schema_guard(&guard);
  ObSqlCtx sql; sql.session_info_ = session.get(); sql.schema_guard_ = &guard;
  ObPhysicalPlan physical;
  ObExecContext execution(arena); execution.set_my_session(session.get()); execution.set_sql_ctx(&sql);
  plugin_projection_test::InrowOnlyLobService lob_service;
  execution.set_lob_read_service(&lob_service);
  CHECK(execution.create_physical_plan_ctx() == OB_SUCCESS);
  ObSQLSessionInfo::ExecCtxSessionRegister register_execution(*session, &execution);
  auto &factory = *execution.get_expr_factory(); auto &statements = *execution.get_stmt_factory();
  ObResolverParams params;
  params.allocator_ = &arena; params.expr_factory_ = &factory; params.stmt_factory_ = &statements;
  params.query_ctx_ = statements.get_query_ctx(); params.session_info_ = session.get(); params.schema_checker_ = &checker;
  const bool partial_join = join_mode >= 9;
  const bool multi_join = join_mode == 8 || join_mode == 11;
  const bool correlated_join = join_mode == 7 || join_mode == 10 || multi_join;
  const bool join_custom = partial_join ? provider.candidate_subproblem_enabled_ :
      join_input && !provider.candidate_relation_disabled_ && (join_mode < 3 || join_mode == 6 || join_mode == 7 || join_mode == 8);
  const std::string join_key_left = join_mode == 1 ? "NULLIF(CAST(MOD(l.ordinal, 3) AS SIGNED), 0)" :
      join_mode == 5 ? "CONCAT('k', MOD(l.ordinal, 2))" : "CAST(MOD(l.ordinal, 2) AS SIGNED)";
  const std::string join_key_right = join_mode == 1 ? "NULLIF(CAST(MOD(r.ordinal, 3) AS SIGNED), 0)" :
      join_mode == 5 ? "CONCAT('k', MOD(r.ordinal, 2))" : "CAST(MOD(r.ordinal, 2) AS SIGNED)";
  const std::string text = join_mode == 11 ?
      "SELECT /*+ OPT_PARAM('rowsets_max_rows', " + std::to_string(batch_size) +
      ") LEADING(((l m) n) r) USE_NL(m) USE_NL(n) USE_HASH(r) */ l.ordinal, r.ordinal "
      "FROM TABLE(seekdb_rust_words_bytes('a b c d e')) l "
      "JOIN TABLE(seekdb_rust_words_bytes(CONCAT(CAST(l.token AS BINARY), ' x'))) m "
      "ON CAST(MOD(l.ordinal, 2) AS SIGNED) = CAST(MOD(m.ordinal, 2) AS SIGNED) "
      "JOIN TABLE(seekdb_rust_words_bytes(CONCAT(CAST(l.token AS BINARY), ' ', CAST(m.token AS BINARY)))) n "
      "ON CAST(MOD(m.ordinal, 2) AS SIGNED) = CAST(MOD(n.ordinal, 2) AS SIGNED) "
      "LEFT JOIN TABLE(seekdb_rust_words_bytes('u v w')) r ON n.ordinal = r.ordinal "
      "ORDER BY l.ordinal, r.ordinal" : join_mode == 10 ?
      "SELECT /*+ OPT_PARAM('rowsets_max_rows', " + std::to_string(batch_size) +
      ") LEADING((l m) r) USE_NL(m) USE_HASH(r) */ l.ordinal, r.ordinal "
      "FROM TABLE(seekdb_rust_words_bytes('a b c d e')) l "
      "JOIN TABLE(seekdb_rust_words_bytes(CAST(l.token AS BINARY))) m "
      "ON CAST(MOD(l.ordinal, 2) AS SIGNED) = CAST(MOD(m.ordinal, 2) AS SIGNED) "
      "LEFT JOIN TABLE(seekdb_rust_words_bytes('u v w')) r ON m.ordinal = r.ordinal "
      "ORDER BY l.ordinal, r.ordinal" : join_mode == 9 ?
      "SELECT /*+ OPT_PARAM('rowsets_max_rows', " + std::to_string(batch_size) +
      ") LEADING((l m) r) USE_HASH(m) USE_HASH(r) */ l.ordinal, r.ordinal "
      "FROM TABLE(seekdb_rust_words_bytes('a b c')) l "
      "JOIN TABLE(seekdb_rust_words_bytes('x y z')) m "
      "ON CAST(MOD(l.ordinal, 2) AS SIGNED) = CAST(MOD(m.ordinal, 2) AS SIGNED) "
      "LEFT JOIN TABLE(seekdb_rust_words_bytes('u v w')) r ON m.ordinal = r.ordinal "
      "ORDER BY l.ordinal, r.ordinal" : join_mode == 8 ?
      "SELECT /*+ OPT_PARAM('rowsets_max_rows', " + std::to_string(batch_size) +
      ") LEADING((l m) r) USE_NL(m) USE_NL(r) */ l.ordinal, r.ordinal "
      "FROM TABLE(seekdb_rust_words_bytes('" + input + "')) l "
      "JOIN TABLE(seekdb_rust_words_bytes(CONCAT(CAST(l.token AS BINARY), ' x'))) m "
      "ON CAST(MOD(l.ordinal, 2) AS SIGNED) = CAST(MOD(m.ordinal, 2) AS SIGNED) "
      "JOIN TABLE(seekdb_rust_words_bytes(CONCAT(CAST(l.token AS BINARY), ' ', CAST(m.token AS BINARY)))) r "
      "ON CAST(MOD(m.ordinal, 2) AS SIGNED) = CAST(MOD(r.ordinal, 2) AS SIGNED) "
      "ORDER BY l.ordinal, r.ordinal" : join_input ?
      "SELECT /*+ OPT_PARAM('rowsets_max_rows', " + std::to_string(batch_size) +
      ") */ l.ordinal, r.ordinal FROM TABLE(seekdb_rust_words_bytes('" + input +
      "')) l " + (join_mode == 4 ? "LEFT JOIN" : "JOIN") + " TABLE(seekdb_rust_words_bytes(" +
      (join_mode == 7 ? std::string("CAST(l.token AS BINARY)") : std::string("'") + join_input + "'") + ")) r ON " +
      (join_mode == 2 ? join_key_right : join_key_left) + (join_mode == 3 ? " <=> " : " = ") +
      (join_mode == 2 ? join_key_left : join_key_right) + " ORDER BY " +
      (join_mode == 6 ? "CAST(l.token AS BINARY), " : "") + "l.ordinal, r.ordinal" :
      "SELECT /*+ OPT_PARAM('enable_newsort', 'true') OPT_PARAM('rowsets_max_rows', " +
      std::to_string(batch_size) + ") */ " + (provider.upper_case_ == 3 ? "DISTINCT " : "") +
      (provider.sort_case_ == 6 ? "ABS(ordinal) + 10 AS projected" :
       provider.sort_case_ == 7 ? "MAX(ordinal) + 10 AS projected" :
       provider.sort_case_ == 8 ? "ROW_NUMBER() OVER (ORDER BY ordinal) + 10 AS projected" :
       provider.upper_case_ == 1 ? "MAX(ordinal) AS ordinal" : provider.upper_case_ == 2 ?
       "ROW_NUMBER() OVER (ORDER BY ordinal) AS ordinal" : provider.upper_case_ == 3 ?
       "CAST(MOD(ordinal, 3) AS SIGNED) AS ordinal" : "ordinal") +
      (partition_ranges ? ", ordinal + 1000" : "") +
      (provider.sort_payload_ ? ", token AS sort_payload" : "") +
      (lob_payload ? ", REPEAT(CAST(token AS BINARY), 70000) AS payload" : "") +
      (stored_payload ? ", CAST(CAST(token AS BINARY) AS rust_stored_utf8) AS stored_payload" : "") +
      (float_payload ? ", CAST(ordinal AS FLOAT) AS numeric_payload" : "") +
      (derived_payload ? ", ABS(ordinal) AS derived_payload" : "") +
      " FROM TABLE(seekdb_rust_words_bytes('" + input + "')) " +
      (provider.upper_case_ == 1 || provider.sort_case_ == 7 ? "GROUP BY CAST(MOD(ordinal, 3) AS SIGNED) " : "") + "ORDER BY " + ordering + limit;
  params.query_ctx_->set_sql_stmt(ObString(text.size(), text.data()));
  params.query_ctx_->set_sql_stmt_coll_type(session->get_local_collation_connection());
  params.query_ctx_->set_literal_stmt_type(oceanbase::sql::stmt::T_SELECT);
  ObParser parser(arena, session->get_sql_mode()); ParseResult parsed{};
  CHECK(parser.parse(ObString(text.size(), text.data()), parsed) == OB_SUCCESS);
  ObResolver resolver(params); ObStmt *resolved = nullptr;
  const int resolution = resolver.resolve(ObResolver::IS_NOT_PREPARED_STMT, *parsed.result_tree_->children_[0], resolved);
  if (resolution != OB_SUCCESS) std::cerr << "sort resolve=" << resolution << " sql=" << text << std::endl;
  CHECK(resolution == OB_SUCCESS && resolved && resolved->is_select_stmt());
  ObDMLStmt *stmt = static_cast<ObDMLStmt *>(resolved);
  if (stored_payload) {
    // Supply the same stored-column conversion used by DML. The SELECT is
    // parsed/resolved normally; this canonical encoder is inserted here instead
    // of claiming an INSERT/storage test. Optimizer/codegen/execution are real.
    ObColumnRefRawExpr *target = nullptr;
    CHECK(factory.create_raw_expr(T_REF_COLUMN, target) == OB_SUCCESS && target);
    target->set_ref_id(900, 1); target->set_data_type(ObLongTextType);
    PluginExprType type;
    type.stored_ = true; type.physical_type_ = ObLongTextType;
    type.sql_name_ = ObString::make_string("rust_stored_utf8");
    type.logical_id_ = ObString::make_string(rust_stored_type_test::TYPE_ID);
    type.owner_ = ObString::make_string("org.seekdb.rust-text");
    type.format_ = ObString::make_string("org.seekdb.rust-text.stored-utf8.v1"); type.format_version_ = 1;
    CHECK(target->set_plugin_type(type) == OB_SUCCESS);
    auto &value = static_cast<ObSelectStmt *>(stmt)->get_select_item(1).expr_;
    CHECK(PluginTypeEncodeExpr::build(factory, *target, value, session.get()) == OB_SUCCESS);
    CHECK(value->get_plugin_type() && value->get_plugin_type()->stored_);
    // Make the stored value an input to ordering too. Otherwise the optimizer
    // correctly delays this projection until above the custom path, and the
    // query would not exercise stored-column transport at all.
    stmt->get_order_items().at(0).expr_ = value;
    CHECK(PluginTypeValueExpr::prepare_ordering(factory, stmt->get_order_items().at(0).expr_,
        session.get()) == OB_SUCCESS);
  }
  if (partition_ranges) {
    // Use the same pseudo-column builder as PDML planning. This carrier has
    // no value until transmit writes the row's calculated tablet ID.
    ObRawExprResType type; type.set_type(ObIntType);
    type.set_accuracy(ObAccuracy::MAX_ACCURACY[ObIntType]);
    ObOpPseudoColumnRawExpr *carrier = nullptr;
    CHECK(ObRawExprUtils::build_op_pseudo_column_expr(factory, T_PDML_PARTITION_ID,
        "PARTITION_ID", type, carrier) == OB_SUCCESS && carrier);
    CHECK(carrier->formalize(session.get()) == OB_SUCCESS);
    SelectItem item; item.expr_ = carrier;
    CHECK(static_cast<ObSelectStmt *>(stmt)->get_select_items().push_back(item) == OB_SUCCESS);
    ObRawExpr *ddl_carrier = nullptr;
    CHECK(ObRawExprUtils::build_pseudo_ddl_slice_id(factory, *session, ddl_carrier) == OB_SUCCESS && ddl_carrier);
    item.expr_ = ddl_carrier;
    CHECK(static_cast<ObSelectStmt *>(stmt)->get_select_items().push_back(item) == OB_SUCCESS);
    ObRawExpr *calc = nullptr;
    ObRawExpr *ordinal = static_cast<ObSelectStmt *>(stmt)->get_select_items().at(0).expr_;
    CHECK(ObRawExprUtils::build_calc_tablet_id_expr(factory, *session,
        partition_schema.get_table_id(), PARTITION_LEVEL_TWO, ordinal, ordinal, calc) == OB_SUCCESS && calc);
    calc->set_partition_id_calc_type(CALC_IGNORE_SUB_PART);
    item.expr_ = calc;
    CHECK(static_cast<ObSelectStmt *>(stmt)->get_select_items().push_back(item) == OB_SUCCESS);
    ObRawExpr *sub_input = static_cast<ObSelectStmt *>(stmt)->get_select_items().at(1).expr_;
    for (auto mode : {CALC_IGNORE_FIRST_PART, CALC_NORMAL}) {
      CHECK(ObRawExprUtils::build_calc_tablet_id_expr(factory, *session,
          full_partition_schema.get_table_id(), PARTITION_LEVEL_TWO, ordinal, sub_input, calc) == OB_SUCCESS && calc);
      calc->set_partition_id_calc_type(mode);
      item.expr_ = calc;
      CHECK(static_cast<ObSelectStmt *>(stmt)->get_select_items().push_back(item) == OB_SUCCESS);
    }
    const auto &items = static_cast<ObSelectStmt *>(stmt)->get_select_items();
    CHECK(!items.at(5).expr_->same_as(*items.at(6).expr_));
    // Equality controls for all partition result representations. Distinct
    // modes must remain distinct, while equal modes can still share an expr.
    for (auto build : {&ObRawExprUtils::build_calc_part_id_expr,
                       &ObRawExprUtils::build_calc_tablet_id_expr,
                       &ObRawExprUtils::build_calc_partition_tablet_id_expr}) {
      for (auto left_mode : {CALC_NORMAL, CALC_IGNORE_FIRST_PART, CALC_IGNORE_SUB_PART}) {
        for (auto right_mode : {CALC_NORMAL, CALC_IGNORE_FIRST_PART, CALC_IGNORE_SUB_PART}) {
          ObRawExpr *left = nullptr, *right = nullptr;
          CHECK(build(factory, *session, full_partition_schema.get_table_id(), PARTITION_LEVEL_TWO,
              ordinal, sub_input, left) == OB_SUCCESS && left);
          CHECK(build(factory, *session, full_partition_schema.get_table_id(), PARTITION_LEVEL_TWO,
              ordinal, sub_input, right) == OB_SUCCESS && right);
          left->set_partition_id_calc_type(left_mode); right->set_partition_id_calc_type(right_mode);
          CHECK(left->same_as(*right) == (left_mode == right_mode));
          CHECK(right->same_as(*left) == (left_mode == right_mode));
        }
      }
    }
  }
  ObAddr address; CHECK(address.set_ip_addr("127.0.0.1", 2882));
  ObOptStatManager statistics;
  ObTransformerCtx rewrite;
  rewrite.allocator_ = &arena; rewrite.schema_checker_ = &checker; rewrite.session_info_ = session.get();
  rewrite.exec_ctx_ = &execution; rewrite.expr_factory_ = &factory; rewrite.stmt_factory_ = &statements;
  rewrite.opt_stat_mgr_ = &statistics; rewrite.sql_schema_guard_ = &sql_guard;
  rewrite.self_addr_ = &address; rewrite.phy_plan_ = &physical;
  ObTransformerImpl transformer(&rewrite);
  const int rewritten = transformer.transform(stmt);
  if (rewritten != OB_SUCCESS) std::cerr << "sort rewrite=" << rewritten << " sql=" << text << std::endl;
  CHECK(rewritten == OB_SUCCESS);
  ObOptimizerContext context(session.get(), &execution, &sql_guard, &statistics, arena,
      &execution.get_physical_plan_ctx()->get_param_store(), address, params.query_ctx_->get_global_hint(),
      factory, stmt, false, params.query_ctx_);
  ObOptimizer optimizer(context); ObLogPlan *plan = nullptr;
  const int candidate_calls_before = provider.candidate_calls_;
  CHECK(optimizer.optimize(*stmt, plan) == OB_SUCCESS && plan);
  if (correlated_join) {
    // A correlated table function must retain or explicitly transfer its binding owner. Read
    // the real optimized graph, not a separately fabricated parameter list.
    CandidateGraph graph;
    std::vector<ObLogicalOperator *> work{plan->get_plan_root()};
    uint32_t total = 0;
    while (!work.empty()) {
      auto *node = work.back(); work.pop_back(); uint32_t id = 0;
      CHECK(graph.root(node, &id) == SEEKDB_PLUGIN_STATUS_OK);
      if (node->get_type() == log_op_def::LOG_PLUGIN_CUSTOM) {
        const auto &custom = static_cast<const LogPluginCustom &>(*node);
        CHECK(join_custom);
        CHECK(join_mode == 11 ? (custom.input_bindings().count() == 2 ||
            custom.input_bindings().count() == 3 || custom.input_bindings().count() == 5) :
            custom.input_bindings().count() == (multi_join ? 5 : 2));
        for (int64_t i = 0; i < custom.input_bindings().count(); ++i) {
          const auto *p = custom.input_bindings().at(i);
          const auto *s = custom.input_exprs().at(custom.binding_sources().at(i));
          CHECK(p && p->get_ref_expr() == s && bool(p->get_plugin_type()) == bool(s->get_plugin_type()));
          if (s->get_plugin_type()) CHECK(*p->get_plugin_type() == *s->get_plugin_type());
          ++total;
        }
      }
      for (uint32_t role = 1; role <= 3; ++role) {
        uint32_t count = 0;
        CHECK(graph.binding_count(id, role, &count) == SEEKDB_PLUGIN_STATUS_OK);
        for (uint32_t index = 0; index < count; ++index) {
          uint32_t parameter = 0, source = 0;
          CHECK(graph.binding(id, role, index, &parameter, &source) == SEEKDB_PLUGIN_STATUS_OK && parameter != source);
          ObRawExpr *p = nullptr, *s = nullptr;
          CHECK(graph.resolve_expression(parameter, p) == OB_SUCCESS && p->is_exec_param_expr());
          CHECK(graph.resolve_expression(source, s) == OB_SUCCESS && static_cast<ObExecParamRawExpr *>(p)->get_ref_expr() == s);
          CHECK(bool(p->get_plugin_type()) == bool(s->get_plugin_type()));
          if (s->get_plugin_type()) CHECK(*p->get_plugin_type() == *s->get_plugin_type());
          ++total;
        }
      }
      for (int64_t i = 0; i < node->get_num_of_child(); ++i) work.push_back(node->get_child(i));
    }
    CHECK(total == (multi_join ? 5 : 2));
  }
  candidate_graph_test::run(*plan, factory);
  plugin_path_test::subproblem(*plan, provider);
  if (provider.candidate_upper_enabled_) plugin_path_test::upper(*plan, provider);
  {
    auto *original = plan->get_plan_root();
    auto *parent = original->get_parent();
    ObSEArray<CandidatePlan, 1> choices;
    CHECK(choices.push_back(CandidatePlan(original)) == OB_SUCCESS);
    for (int mode = 1; mode <= 5; ++mode) {
      provider.relation_probe_mode_ = mode;
      ObSEArray<CandidatePlan, 4> result;
      CHECK(result.push_back(CandidatePlan(original)) == OB_SUCCESS);
      const int expected = mode <= 2 ? OB_SUCCESS : mode == 3 ? OB_TIMEOUT : OB_INVALID_ARGUMENT;
      CHECK(plan->contribute_plugin_relation_paths(choices, result) == expected);
      CHECK(original->get_parent() == parent);
      CHECK(choices.count() == 1 && choices.at(0).plan_tree_ == original);
      if (mode <= 2) {
        CHECK(result.count() == (mode == 1 ? 1 : 3) && result.at(0).plan_tree_ == original);
        if (mode == 2) {
          CHECK(result.at(1).plan_tree_->get_child(0) == original);
          CHECK(result.at(2).plan_tree_->get_child(0) == result.at(1).plan_tree_);
          CHECK(result.at(1).plan_tree_->get_parent() == nullptr);
        }
        CandidatePlan cheapest;
        CHECK(plan->get_minimal_cost_candidate_core(result, cheapest) == OB_SUCCESS);
        CHECK(cheapest.plan_tree_ == original); // Additions did not force a winner.
      } else CHECK(result.empty());
    }
    CHECK(plan->contribute_plugin_relation_paths(choices, choices) == OB_INVALID_ARGUMENT);
    CHECK(choices.count() == 1 && choices.at(0).plan_tree_ == original);
    provider.relation_probe_mode_ = 0;
  }
  provider.natural_candidate_calls_ += provider.candidate_calls_ - candidate_calls_before;
  if (!provider.candidate_build_enabled_) {
    // A controlled pair proves that the actual ObLogPlan entrypoint honors the
    // Rust decision instead of silently recomputing minimum cost. These two
    // probe nodes are not executed. The generated SQL plan below is executed
    // normally, with the same candidate policy active during optimization.
    ObLogSort cheap(*plan), expensive(*plan);
    cheap.set_cost(2); expensive.set_cost(40);
    ObSEArray<CandidatePlan, 2> choices;
    CHECK(choices.push_back(CandidatePlan(&cheap)) == OB_SUCCESS);
    CHECK(choices.push_back(CandidatePlan(&expensive)) == OB_SUCCESS);
    CandidatePlan selected;
    provider.candidate_enabled_ = false;
    CHECK(plan->get_minimal_cost_candidate(choices, selected) == OB_SUCCESS && selected.plan_tree_ == &cheap);
    provider.candidate_enabled_ = true;
    CHECK(plan->get_minimal_cost_candidate(choices, selected) == OB_SUCCESS && selected.plan_tree_ == &expensive);
    for (int mode = 1; mode <= 4; ++mode) {
      provider.candidate_probe_mode_ = mode;
      selected = CandidatePlan(&expensive);
      const int expected_error = mode <= 2 ? OB_INVALID_ARGUMENT : mode == 3 ? OB_STATE_NOT_MATCH : OB_TIMEOUT;
      CHECK(plan->get_minimal_cost_candidate(choices, selected) == expected_error && !selected.plan_tree_);
    }
    provider.candidate_probe_mode_ = 0;
  }
  if (provider.candidate_build_enabled_) {
    auto *original = plan->get_plan_root();
    auto *parent = original->get_parent();
    ObSEArray<CandidatePlan, 1> choices;
    CHECK(choices.push_back(CandidatePlan(original)) == OB_SUCCESS);
    for (int mode = 5; mode <= (provider.candidate_custom_enabled_ ? 16 : 9); ++mode) {
      provider.candidate_probe_mode_ = mode;
      CandidatePlan result;
      const int expected_error = mode == 5 || mode == 13 ? OB_SUCCESS : mode == 6 ? OB_TIMEOUT :
          mode == 7 ? OB_NOT_SUPPORTED : mode == 8 ? OB_SIZE_OVERFLOW : mode == 12 ? OB_ENTRY_NOT_EXIST : OB_INVALID_ARGUMENT;
      CHECK(plan->get_minimal_cost_candidate(choices, result) == expected_error);
      CHECK(mode == 5 || mode == 13 ? result.plan_tree_ == original : !result.plan_tree_);
      CHECK(original->get_parent() == parent);
    }
    provider.candidate_probe_mode_ = 0;
    char explain[16384]{};
    CHECK(plan->to_string(explain, sizeof(explain)) > 0);
    if (partial_join && join_custom) CHECK(std::strstr(explain, "LEADING(") == nullptr);
    if (join_input || provider.candidate_upper_enabled_) std::cerr << "plugin SQL=" << text << "\n" << explain << std::endl;
    if (join_input && !join_custom) CHECK(std::strstr(explain, "PLUGIN CUSTOM") == nullptr);
    else CHECK(std::strstr(explain, provider.candidate_custom_enabled_ ? "PLUGIN CUSTOM" : "MATERIAL") != nullptr);
  }
  ObCodeGenerator generator(&execution.get_physical_plan_ctx()->get_datum_param_store());
  const int generated = generator.generate(*plan, physical);
  if (generated != OB_SUCCESS) std::cerr << "sort codegen=" << generated << " sql=" << text << std::endl;
  CHECK(generated == OB_SUCCESS && physical.get_batch_size() == batch_size);
  const auto *root = physical.get_root_op_spec(); const auto *sort = find_sort(root);
  if (provider.candidate_layout_enabled_) CHECK(has_custom_layout(root,
      provider.candidate_layout_derived_, provider.candidate_layout_target_only_));
  if (provider.candidate_fragment_enabled_) {
    const auto check_fragment = [&](const auto &self, const ObOpSpec *spec) -> bool {
      if (spec->get_type() == PHY_PLUGIN_CUSTOM) {
        const auto &custom = static_cast<const PluginCustomSpec &>(*spec);
        if (!custom.input_offsets_.empty()) {
          CHECK(custom.get_child_cnt() == 1 && custom.input_offsets_.count() == 2);
          CHECK(custom.input_offsets_.at(0) == 0 && custom.input_offsets_.at(1) == 3);
          return true;
        }
      }
      for (int64_t i = 0; i < spec->get_child_cnt(); ++i)
        if (self(self, spec->get_child(i))) return true;
      return false;
    };
    CHECK(check_fragment(check_fragment, root));
  }
  const auto *rust_sort = find_rust_sort(root);
  if (provider.sort_case_ || (provider.candidate_upper_enabled_ && provider.upper_case_ >= 1 && provider.upper_case_ <= 4)) {
    CHECK(rust_sort && root == rust_sort); // The final ORDER BY is actually replaced.
    // A window's input ordering is a separate native operation, not the ORDER
    // BY being replaced. Keep the old no-SORT assertion for all other cases.
    if (provider.upper_case_ != 2 && provider.sort_case_ != 8) CHECK(!sort);
    CHECK(rust_sort->get_child_cnt() == 1 && rust_sort->explicit_input_ && rust_sort->input_bindings_.empty());
    CHECK(rust_sort->input_offsets_.count() == 2 && rust_sort->input_offsets_.at(0) == 0);
    if (provider.sort_case_ >= 6) {
      for (auto *value : rust_sort->columns_)
        CHECK(value != root->output_.at(0)); // Carry dependencies, not the late SELECT computation.
      for (auto *value : rust_sort->input_columns_) CHECK(value != root->output_.at(0));
    }
    std::cerr << "Rust SSO1 replaces native SORT: case=" << provider.sort_case_ << " batch=" << batch_size << std::endl;
  } else CHECK(sort || rust_sort);
  CHECK(root->output_.count() == (partition_ranges ? 7 : lob_payload || stored_payload || float_payload || derived_payload || join_input || provider.sort_payload_ ? 2 : 1));
  if (join_input) {
    CHECK(right_expected.size() == expected.size());
    const auto check_join = [&](const auto &self, const ObOpSpec *spec) -> bool {
      if (spec->get_type() == PHY_PLUGIN_CUSTOM) {
        const auto &custom = static_cast<const PluginCustomSpec &>(*spec);
        if (join_mode == 11) {
          CHECK(custom.get_child_cnt() == 2 || custom.get_child_cnt() == 3);
          const bool flat = custom.get_child_cnt() == 3;
          CHECK(custom.input_offsets_.count() == custom.get_child_cnt() + 1 && custom.input_offsets_.at(0) == 0);
          CHECK(custom.parameters_.length() == (flat ? 36 : 16) + custom.columns_.count() * 8 &&
              std::memcmp(custom.parameters_.ptr(), flat ? "SJD1" : "SJC1", 4) == 0);
          CHECK(flat ? custom.input_bindings_.count() == 5 :
              (custom.input_bindings_.count() == 2 || custom.input_bindings_.count() == 3));
          CHECK(custom.binding_inputs_.count() == custom.input_bindings_.count() &&
              custom.binding_targets_.count() == custom.input_bindings_.count());
          // A cheaper previously contributed child can remain as a nested
          // custom input. Validate it too; do not require forced flattening.
          for (int64_t i = 0; i < spec->get_child_cnt(); ++i)
            if (has_custom(spec->get_child(i))) CHECK(self(self, spec->get_child(i)));
          return true;
        }
        if (join_mode == 8 && custom.get_child_cnt() == 3) {
          CHECK(custom.input_offsets_.count() == 4 && custom.input_offsets_.at(0) == 0);
          CHECK(custom.parameters_.length() == 36 + custom.columns_.count() * 8 &&
              std::memcmp(custom.parameters_.ptr(), "SJD1", 4) == 0);
          CHECK(custom.input_bindings_.count() == 5 && custom.binding_inputs_.count() == 5 && custom.binding_targets_.count() == 5);
          int inner = 0, outer = 0; bool from_first = false, from_second = false;
          for (int64_t i = 0; i < 5; ++i) {
            if (custom.binding_targets_.at(i) == 1) { CHECK(custom.binding_inputs_.at(i) == 0); ++inner; }
            else {
              CHECK(custom.binding_targets_.at(i) == 2);
              from_first |= custom.binding_inputs_.at(i) == 0;
              from_second |= custom.binding_inputs_.at(i) == 1;
              ++outer;
            }
          }
          CHECK(inner == 2 && outer == 3 && from_first && from_second);
          return true;
        }
        if (custom.get_child_cnt() == 2) {
          CHECK(custom.input_offsets_.count() == 3 && custom.input_offsets_.at(0) == 0);
          CHECK(custom.input_offsets_.at(1) == 2 || (join_mode == 6 && custom.input_offsets_.at(1) == 3));
          CHECK(custom.input_offsets_.at(2) == (correlated_join ? 3 : join_mode == 6 ? 5 : 4));
          CHECK(custom.parameters_.length() == 16 + custom.columns_.count() * 8 &&
              std::memcmp(custom.parameters_.ptr(), correlated_join ? "SJC1" : "SJE1", 4) == 0);
          CHECK(custom.input_bindings_.count() == (correlated_join ? 2 : 0));
          return true;
        }
      }
      for (int64_t i = 0; i < spec->get_child_cnt(); ++i) if (self(self, spec->get_child(i))) return true;
      return false;
    };
    if (join_custom) CHECK(check_join(check_join, root));
    else CHECK(!has_custom(root));
    if (partial_join && join_custom) {
      const auto native_above_custom = [&](const auto &self, const ObOpSpec *spec) -> bool {
        if (spec->get_type() == PHY_HASH_JOIN && has_custom(spec)) return true;
        for (int64_t i = 0; i < spec->get_child_cnt(); ++i)
          if (self(self, spec->get_child(i))) return true;
        return false;
      };
      CHECK(native_above_custom(native_above_custom, root));
    }
  }
  if (lob_payload) CHECK(has_custom_lob(root));
  if (stored_payload) CHECK(has_custom_codec(root));
  if (float_payload) CHECK(has_custom_float(root));
  if (provider.candidate_build_enabled_) {
    if (join_input && !join_custom) CHECK(!has_custom(root));
    else if (provider.candidate_custom_enabled_) CHECK(has_custom(root) && !has_material(root));
    else CHECK(has_material(root) && !has_custom(root));
  }
  if (sort) CHECK(bool(sort->topn_expr_) == (limit[0] != '\0'));
  if (!native) {
    CHECK(!sort->enable_encode_sortkey_opt_ && !sort->enable_pd_topn_filter());
    const auto *key = sort->all_exprs_.at(sort->sort_collations_.at(0).field_idx_);
    const auto *info = dynamic_cast<const PluginTypeValueExtraInfo *>(key->extra_info_);
    if (!info || !info->valid() || info->mode_ != PluginTypeValueExtraInfo::ORDERED) {
      std::cerr << "sort key type=" << key->type_ << " mode=" << (info ? int(info->mode_) : -1)
                << " valid=" << (info && info->valid()) << " sql=" << text << std::endl;
      for (int64_t i = 0; i < stmt->get_order_item_size(); ++i) {
        const auto &item = stmt->get_order_items().at(i);
        std::cerr << "raw ordering type=" << item.expr_->get_expr_type()
                  << " plugin=" << bool(item.expr_->get_plugin_type()) << std::endl;
      }
    }
    CHECK(info && info->valid() && info->mode_ == PluginTypeValueExtraInfo::ORDERED);
  }
  execution.get_physical_plan_ctx()->set_phy_plan(&physical);
  // The fixture bypasses ObExecContext::init_physical_plan_ctx; mirror its
  // parameter reservation before native NLJ clears/sets correlated parameters.
  // Dynamic expression frames alone do not allocate the ObObj parameter store.
  CHECK(execution.get_physical_plan_ctx()->reserve_param_space(physical.get_param_count()) == OB_SUCCESS);
  if (correlated_join) CHECK(physical.get_param_count() >= (multi_join ? 5 : 2));
  CHECK(execution.init_phy_op(physical.get_phy_operator_size()) == OB_SUCCESS);
  CHECK(execution.init_expr_op(physical.get_expr_operator_size()) == OB_SUCCESS);
  CHECK(physical.get_expr_frame_info().pre_alloc_exec_memory(execution) == OB_SUCCESS);
  ObOperator *op = nullptr;
  CHECK(root->create_op_input(execution) == OB_SUCCESS && root->create_operator(execution, op) == OB_SUCCESS && op);
  const int resolves = provider.resolves_, comparisons = provider.comparisons_;
  const int custom_opens = provider.custom_opens_, custom_nexts = provider.custom_nexts_;
  CHECK(op->open() == OB_SUCCESS);
  const bool bound_join = join_custom && correlated_join;
  const auto verify_parameters = [&](bool cleared) {
    int checked = 0;
    std::vector<int64_t> slots;
    const auto visit = [&](const auto &self, const ObOpSpec *node) -> void {
      if (node->get_type() == PHY_PLUGIN_CUSTOM) {
        const auto &custom = static_cast<const PluginCustomSpec &>(*node);
        for (const auto &binding : custom.input_bindings_) {
          CHECK(binding.param_idx_ >= 0 && binding.param_idx_ < physical.get_param_count());
          CHECK(std::find(slots.begin(), slots.end(), binding.param_idx_) == slots.end());
          slots.push_back(binding.param_idx_);
          CHECK(execution.get_physical_plan_ctx()->get_param_store().at(binding.param_idx_).is_null() == cleared);
          ++checked;
        }
      }
      for (int64_t i = 0; i < node->get_child_cnt(); ++i) self(self, node->get_child(i));
    };
    visit(visit, root);
    CHECK(checked == (multi_join ? 5 : 2));
  };
  for (int pass = 0; pass < (bound_join ? 5 : join_custom || rust_sort ? 3 : 2); ++pass) {
    if ((join_custom || rust_sort) && pass == 2 && !expected.empty()) {
      CHECK(op->rescan() == OB_SUCCESS && !provider.custom_input_observer_);
      int reads = 0;
      provider.custom_input_observer_ = [&](const seekdb_plugin_custom_row_v1_t &) { ++reads; };
      class CancelAfterJoinRead final : public ObIExtraStatusCheck {
      public:
        explicit CancelAfterJoinRead(const int &reads) : reads_(reads) {}
        const char *name() const override { return "plugin-join-cancel-after-input"; }
        int check() const override { return reads_ ? OB_TIMEOUT : OB_SUCCESS; }
      private:
        const int &reads_;
      } cancellation(reads);
      {
        ObIExtraStatusCheck::Guard guard(execution, cancellation);
        CHECK(op->get_next_row() == OB_TIMEOUT);
      }
      provider.custom_input_observer_ = {};
      CHECK(reads == 1 && op->rescan() == OB_SUCCESS);
    }
    if (bound_join && pass >= 3) {
      CHECK(op->rescan() == OB_SUCCESS && !provider.custom_binding_observer_);
      verify_parameters(true);
      int bindings = 0;
      provider.custom_binding_observer_ = [&] {
        ++bindings;
        if (!multi_join || bindings >= 2) verify_parameters(false);
        else {
          int active = 0;
          const auto &values = execution.get_physical_plan_ctx()->get_param_store();
          for (int64_t i = 0; i < values.count(); ++i) if (!values.at(i).is_null()) ++active;
          CHECK(active == 2); // Only the first target has been bound yet.
        }
      };
      class CancelAfterBinding final : public ObIExtraStatusCheck {
      public:
        CancelAfterBinding(const int &count, int limit) : count_(count), limit_(limit) {}
        const char *name() const override { return "plugin-join-cancel-after-binding"; }
        int check() const override { return count_ >= limit_ ? OB_TIMEOUT : OB_SUCCESS; }
      private:
        const int &count_;
        int limit_;
      } cancellation(bindings, pass - 2);
      {
        ObIExtraStatusCheck::Guard guard(execution, cancellation);
        CHECK(op->get_next_row() == OB_TIMEOUT);
      }
      provider.custom_binding_observer_ = {};
      CHECK(bindings == pass - 2);
      CHECK(op->rescan() == OB_SUCCESS);
      verify_parameters(true);
    }
    size_t result_index = 0;
    for (int id : expected) {
      const int fetched = op->get_next_row();
      if (fetched != OB_SUCCESS) std::cerr << "sort fetch=" << fetched << " sql=" << text << std::endl;
      CHECK(fetched == OB_SUCCESS);
      ObDatum *value = nullptr;
      CHECK(root->output_.at(0)->eval(op->get_eval_ctx(), value) == OB_SUCCESS && value && value->get_int() == id);
      if (provider.sort_payload_) {
        const char *words[] = {"z", "aa", "🙂", "bbb", "a", "z"};
        CHECK(root->output_.at(1)->eval(op->get_eval_ctx(), value) == OB_SUCCESS && value && !value->is_null());
        CHECK(std::string(value->get_string().ptr(), value->get_string().length()) == words[id - 1]);
      }
      if (join_input) {
        CHECK(root->output_.at(1)->eval(op->get_eval_ctx(), value) == OB_SUCCESS && value && !value->is_null());
        CHECK(value->get_int() == right_expected.at(result_index++));
      }
      if (derived_payload) CHECK(root->output_.at(1)->eval(op->get_eval_ctx(), value) == OB_SUCCESS && value && value->get_int() == id);
      if (float_payload) {
        CHECK(root->output_.at(1)->datum_meta_.type_ == ObFloatType);
        CHECK(root->output_.at(1)->eval(op->get_eval_ctx(), value) == OB_SUCCESS && value && !value->is_null());
        CHECK(value->get_float() == static_cast<float>(id));
      }
      if (stored_payload) {
        const char *words[] = {"z", "aa", "🙂", "bbb", "a", "z"};
        std::string expected_bytes("RUT\1", 4);
        for (const unsigned char *p = reinterpret_cast<const unsigned char *>(words[id - 1]); *p; ++p)
          expected_bytes += static_cast<char>(~*p);
        CHECK(root->output_.at(1)->eval(op->get_eval_ctx(), value) == OB_SUCCESS && value && !value->is_null());
        CHECK(std::string(value->get_string().ptr(), value->get_string().length()) == expected_bytes);
      }
      if (lob_payload) {
        const char *words[] = {"z", "aa", "🙂", "bbb", "a", "z"};
        std::string expected_bytes;
        for (int repeat = 0; repeat < 70000; ++repeat) expected_bytes += words[id - 1];
        const auto &expr = *root->output_.at(1);
        CHECK(expr.eval(op->get_eval_ctx(), value) == OB_SUCCESS && value && !value->is_null());
        ObString bytes; ObArenaAllocator temporary;
        CHECK(ObTextStringHelper::read_real_string_data_with_copy(execution, temporary, *value,
            expr.datum_meta_, expr.obj_meta_.has_lob_header(), bytes) == OB_SUCCESS);
        CHECK(std::string(bytes.ptr(), bytes.length()) == expected_bytes);
      }
      if (partition_ranges) {
        CHECK(root->output_.at(1)->eval(op->get_eval_ctx(), value) == OB_SUCCESS && value && value->get_int() == id + 1000);
      }
    }
    CHECK(op->get_next_row() == OB_ITER_END);
    if (bound_join) verify_parameters(true);
    if (!pass) CHECK(op->rescan() == OB_SUCCESS);
  }
  CHECK(provider.resolves_ == resolves);
  CHECK(native ? provider.comparisons_ == comparisons : provider.comparisons_ > comparisons);
  if (!native) {
    CHECK(op->rescan() == OB_SUCCESS);
    class CancelAfterComparison final : public ObIExtraStatusCheck {
    public:
      explicit CancelAfterComparison(const int &calls) : calls_(calls), before_(calls) {}
      const char *name() const override { return "plugin-sort-cancel"; }
      int check() const override { return calls_ == before_ ? OB_SUCCESS : OB_TIMEOUT; }
    private:
      const int &calls_;
      int before_;
    } cancel(provider.comparisons_);
    const int before = provider.comparisons_;
    { ObIExtraStatusCheck::Guard cancellation(execution, cancel);
      CHECK(op->get_next_row() == OB_TIMEOUT); }
    CHECK(provider.comparisons_ == before + 1);
  }
  CHECK(op->close() == OB_SUCCESS);
  if (provider.candidate_custom_enabled_) {
    if (join_input && !join_custom) CHECK(provider.custom_opens_ == custom_opens && provider.custom_nexts_ == custom_nexts);
    else CHECK(provider.custom_opens_ > custom_opens && provider.custom_nexts_ > custom_nexts);
    CHECK(provider.custom_closes_ == provider.custom_opens_);
    oceanbase::share::plugin::ObPluginStatusSnapshot custom_status;
    CHECK(provider.candidate_loader_->get_status("org.seekdb.rust-candidate", custom_status) == OB_SUCCESS && custom_status.lease_count_ == 0);
  }
  oceanbase::share::plugin::ObPluginStatusSnapshot status;
  CHECK(loader.get_status("org.seekdb.rust-text", status) == OB_SUCCESS && status.lease_count_ == 0);
  if (!provider.candidate_build_enabled_ && !limit[0] && std::strlen(input) < 32 &&
      (std::strcmp(ordering, "token, ordinal") == 0 ||
       std::strcmp(ordering, "seekdb_rust_identity(token), ordinal") == 0 ||
       std::strcmp(ordering, "CAST(token AS BINARY), ordinal") == 0 ||
       std::strcmp(ordering, "token DESC, ordinal DESC") == 0)) {
    rust_px_merge_test::run(*sort, op->get_eval_ctx(), provider, loader, arena, native,
        partition_ranges ? root->output_.at(3) : nullptr,
        partition_ranges ? root->output_.at(2) : nullptr,
        partition_ranges ? &root->output_ : nullptr);
  }
}
template <typename Provider>
void run_built(Provider &provider, oceanbase::share::plugin::ObPluginLoader &loader, ObArenaAllocator &arena)
{
  if (provider.candidate_custom_enabled_) rust_custom_lob_test::run(provider, arena);
  if (provider.candidate_custom_enabled_) rust_custom_numeric_test::run(provider, arena);
  if (provider.candidate_custom_enabled_) rust_custom_projection_test::run(provider, arena);
  if (provider.candidate_custom_enabled_) rust_custom_multi_test::run(provider, arena);
  if (provider.candidate_custom_enabled_) rust_custom_dag_test::run(provider, arena);
  if (provider.candidate_custom_enabled_) rust_custom_sort_test::run(provider, arena);
  using Manager = oceanbase::tmp_file::ObTmpFileManager;
  auto *saved = oceanbase::share::server_service<Manager>();
  MemoryOnlyDirectories directories; CHECK(directories.init() == OB_SUCCESS);
  oceanbase::share::bind_server_service<Manager>(&directories);
  for (int batch : {0, 3}) {
    const int before = provider.natural_candidate_calls_;
    run_case(provider, loader, arena, "token, ordinal", "", {5,1,6,3,2,4}, batch);
    run_case(provider, loader, arena, "token DESC, ordinal DESC", "", {4,2,3,6,1,5}, batch);
    if (provider.candidate_custom_enabled_) {
      provider.candidate_upper_enabled_ = true;
      for (int upper_case : {1, 2, 3, 4}) {
        provider.upper_case_ = upper_case;
        const auto phase = upper_case == 1 ? SEEKDB_PLUGIN_PHASE_GROUP : upper_case == 2 ?
            SEEKDB_PLUGIN_PHASE_WINDOW : upper_case == 3 ? SEEKDB_PLUGIN_PHASE_DISTINCT : SEEKDB_PLUGIN_PHASE_ORDERED;
        const int before = provider.upper_builds_[phase];
        const int ordered = provider.upper_builds_[SEEKDB_PLUGIN_PHASE_ORDERED];
        const std::vector<int> expected = upper_case == 1 ? std::vector<int>{6,5,4} :
            upper_case == 3 ? std::vector<int>{2,1,0} : std::vector<int>{6,5,4,3,2,1};
        run_case(provider, loader, arena, "ordinal DESC", "", expected, batch, true);
        std::cerr << "upper case=" << upper_case << " batch=" << batch << " builds=";
        for (int i = SEEKDB_PLUGIN_PHASE_GROUP; i <= SEEKDB_PLUGIN_PHASE_ORDERED; ++i)
          std::cerr << provider.upper_builds_[i] << ',';
        std::cerr << std::endl;
        CHECK(provider.upper_builds_[phase] > before);
        CHECK(provider.upper_builds_[SEEKDB_PLUGIN_PHASE_ORDERED] > ordered);
      }
      provider.upper_case_ = 0;
      for (int test = 1; test <= 8; ++test) {
        provider.sort_case_ = test; provider.sort_payload_ = test == 4;
        const char *keys = test == 1 ? "ordinal ASC" : test == 2 ?
            "CAST(CASE WHEN ordinal = 2 THEN NULL ELSE MOD(ordinal, 3) END AS SIGNED) ASC, ordinal DESC" :
            test == 3 ? "CAST(CASE WHEN ordinal = 2 THEN NULL ELSE MOD(ordinal, 3) END AS SIGNED) DESC, ordinal DESC" :
            test == 7 ? "MAX(ordinal) DESC" : "ordinal DESC";
        const std::vector<int> ids = test == 1 ? std::vector<int>{1,2,3,4,5,6} : test == 2 ?
            std::vector<int>{2,6,3,4,1,5} : test == 3 ? std::vector<int>{5,4,1,6,3,2} :
            test == 5 ? std::vector<int>{} : test == 7 ? std::vector<int>{16,15,14} :
            test >= 6 ? std::vector<int>{16,15,14,13,12,11} : std::vector<int>{6,5,4,3,2,1};
        run_case(provider, loader, arena, keys, "", ids, batch, true, test == 5 ? "" : "z aa 🙂 bbb a z");
      }
      provider.sort_case_ = 0; provider.sort_payload_ = false;
      run_case(provider, loader, arena, "ordinal DESC", " LIMIT 2", {6,5}, batch, true);
      provider.candidate_upper_enabled_ = false;
    }
    if (provider.candidate_custom_enabled_)
      run_case(provider, loader, arena, "payload, ordinal", "", {5,2,4,1,6,3}, batch, true,
          "z aa 🙂 bbb a z", false, true);
    if (provider.candidate_custom_enabled_)
      run_case(provider, loader, arena, "token, ordinal", "", {5,1,6,3,2,4}, batch, false,
          "z aa 🙂 bbb a z", false, false, true);
    if (provider.candidate_custom_enabled_)
      run_case(provider, loader, arena, "numeric_payload, ordinal", "", {1,2,3,4,5,6}, batch, true,
          "z aa 🙂 bbb a z", false, false, false, true);
    if (provider.candidate_custom_enabled_) {
      const int layouts = provider.candidate_layout_builds_;
      provider.candidate_layout_enabled_ = true;
      // Controlled policy uses only graph/build callbacks; parser, optimizer,
      // allocation/pruning/codegen and Rust execution are the actual SQL path.
      run_case(provider, loader, arena, "token, ordinal", "", {5,1,6,3,2,4}, batch);
      run_case(provider, loader, arena, "CAST(token AS BINARY), ordinal", "", {5,2,4,1,6,3}, batch, true);
      provider.candidate_layout_derived_ = true;
      run_case(provider, loader, arena, "derived_payload, ordinal", "", {1,2,3,4,5,6}, batch, true,
          "z aa 🙂 bbb a z", false, false, false, false, true);
      provider.candidate_layout_derived_ = false;
      // ABS is only a SELECT target here, absent from both ordering keys. The
      // v4 target ID still feeds the real builder/allocation/Rust executor.
      provider.candidate_layout_target_only_ = true;
      run_case(provider, loader, arena, "token, ordinal", "", {5,1,6,3,2,4}, batch, false,
          "z aa 🙂 bbb a z", false, false, false, false, true);
      provider.candidate_fragment_enabled_ = true;
      run_case(provider, loader, arena, "token, ordinal", "", {5,1,6,3,2,4}, batch, false,
          "z aa 🙂 bbb a z", false, false, false, false, true);
      provider.candidate_fragment_enabled_ = false;
      provider.candidate_layout_target_only_ = false;
      CHECK(provider.candidate_layout_builds_ >= layouts + 5);
      provider.candidate_layout_enabled_ = false;
      provider.candidate_join_enabled_ = true;
      const int joins = provider.candidate_join_builds_;
      run_case(provider, loader, arena, "l.ordinal, r.ordinal", "", {1,1,2,2,3,3,4,4,5,5}, batch, true,
          "a b c d e", false, false, false, false, false, "x y z w", 0, {1,3,2,4,1,3,2,4,1,3});
      run_case(provider, loader, arena, "l.ordinal, r.ordinal", "", {1,1,2,4,4,5}, batch, true,
          "a b c d e f", false, false, false, false, false, "x y z w", 1, {1,4,2,1,4,2});
      for (int mode : {2, 3, 4, 5, 6}) {
        const int built = provider.candidate_join_builds_;
        run_case(provider, loader, arena, "l.ordinal, r.ordinal", "", {1,1,2,2,3,3,4,4,5,5}, batch, true,
            "a b c d e", false, false, false, false, false, "x y z w", mode, {1,3,2,4,1,3,2,4,1,3});
        CHECK(provider.candidate_join_builds_ == built + (mode == 2 || mode == 6 ? 1 : 0));
      }
      CHECK(provider.candidate_join_builds_ >= joins + 4);
      const int before_correlated = provider.candidate_join_builds_;
      provider.candidate_relation_disabled_ = true;
      run_case(provider, loader, arena, "l.ordinal, r.ordinal", "", {1,3,5}, batch, true,
          "a b c d e", false, false, false, false, false, "unused", 7, {1,1,1});
      CHECK(provider.candidate_join_builds_ == before_correlated);
      provider.candidate_relation_disabled_ = false;
      run_case(provider, loader, arena, "l.ordinal, r.ordinal", "", {1,3,5}, batch, true,
          "a b c d e", false, false, false, false, false, "unused", 7, {1,1,1});
      CHECK(provider.candidate_join_builds_ == before_correlated + 1);
      const int before_multi = provider.candidate_join_builds_;
      provider.candidate_relation_disabled_ = true;
      run_case(provider, loader, arena, "l.ordinal, r.ordinal", "", {1,2,3,4,5}, batch, true,
          "a b c d e", false, false, false, false, false, "unused", 8, {1,2,1,2,1});
      CHECK(provider.candidate_join_builds_ == before_multi);
      provider.candidate_relation_disabled_ = false;
      run_case(provider, loader, arena, "l.ordinal, r.ordinal", "", {1,2,3,4,5}, batch, true,
          "a b c d e", false, false, false, false, false, "unused", 8, {1,2,1,2,1});
      CHECK(provider.candidate_join_builds_ == before_multi + 1);
      provider.candidate_relation_disabled_ = true;
      const int subproblems = provider.subproblem_builds_;
      run_case(provider, loader, arena, "l.ordinal, r.ordinal", "", {1,1,2,3,3}, batch, true,
          "unused", false, false, false, false, false, "unused", 9, {1,3,2,1,3});
      CHECK(provider.subproblem_builds_ == subproblems);
      provider.candidate_subproblem_enabled_ = true;
      run_case(provider, loader, arena, "l.ordinal, r.ordinal", "", {1,1,2,3,3}, batch, true,
          "unused", false, false, false, false, false, "unused", 9, {1,3,2,1,3});
      CHECK(provider.subproblem_builds_ > subproblems);
      provider.candidate_subproblem_enabled_ = false;
      for (int mode : {10, 11}) {
        const std::vector<int> left = mode == 10 ? std::vector<int>{1,3,5} : std::vector<int>{1,2,3,4,5};
        const std::vector<int> right = mode == 10 ? std::vector<int>{1,1,1} : std::vector<int>{1,2,1,2,1};
        const int built = provider.subproblem_builds_;
        run_case(provider, loader, arena, "l.ordinal, r.ordinal", "", left, batch, true,
            "unused", false, false, false, false, false, "unused", mode, right);
        CHECK(provider.subproblem_builds_ == built);
        provider.candidate_subproblem_enabled_ = true;
        run_case(provider, loader, arena, "l.ordinal, r.ordinal", "", left, batch, true,
            "unused", false, false, false, false, false, "unused", mode, right);
        CHECK(provider.subproblem_builds_ > built);
        provider.candidate_subproblem_enabled_ = false;
      }
      provider.candidate_relation_disabled_ = false;
      provider.candidate_join_enabled_ = false;
    }
    CHECK(provider.natural_candidate_calls_ > before);
  }
  oceanbase::share::bind_server_service<Manager>(saved);
}
template <typename Provider>
void run(Provider &provider, oceanbase::share::plugin::ObPluginLoader &loader, ObArenaAllocator &arena)
{
  rust_dtl_wire_test::MemoryScope dtl_memory;
  using Manager = oceanbase::tmp_file::ObTmpFileManager;
  auto *saved = oceanbase::share::server_service<Manager>();
  MemoryOnlyDirectories directories; CHECK(directories.init() == OB_SUCCESS);
  oceanbase::share::bind_server_service<Manager>(&directories);
  for (int batch : {0, 3}) {
    run_case(provider, loader, arena, "token, ordinal", "", {5,1,6,3,2,4}, batch);
    run_case(provider, loader, arena, "token DESC, ordinal DESC", "", {4,2,3,6,1,5}, batch);
    run_case(provider, loader, arena, "token, ordinal", " LIMIT 3", {5,1,6}, batch);
    run_case(provider, loader, arena, "token, ordinal", " LIMIT 1,3", {1,6,3}, batch);
    run_case(provider, loader, arena, "CASE WHEN ordinal IN (2,5) THEN NULL ELSE token END, ordinal", "", {2,5,1,6,3,4}, batch);
    run_case(provider, loader, arena, "CASE WHEN ordinal IN (2,5) THEN NULL ELSE token END DESC, ordinal", "", {4,3,1,6,2,5}, batch);
    run_case(provider, loader, arena, "seekdb_rust_identity(token), ordinal", "", {5,1,6,3,2,4}, batch);
    run_case(provider, loader, arena, "CAST(token AS BINARY), ordinal", "", {5,2,4,1,6,3}, batch, true);
    // Additional real SQL output frame for partition-RANGE DDL publication;
    // ordinal is independently reused as the supplied tablet-ID input.
    run_case(provider, loader, arena, "token, ordinal", "", {5,1,6,3,2,4}, batch, false, "z aa 🙂 bbb a z", true);
    run_case(provider, loader, arena, "token DESC, ordinal DESC", "", {4,2,3,6,1,5}, batch, false, "z aa 🙂 bbb a z", true);
    run_case(provider, loader, arena, "seekdb_rust_identity(token), ordinal", "", {5,1,6,3,2,4}, batch, false, "z aa 🙂 bbb a z", true);
    run_case(provider, loader, arena, "CAST(token AS BINARY), ordinal", "", {5,2,4,1,6,3}, batch, true, "z aa 🙂 bbb a z", true);
    // More than a small insertion-sort run, with duplicate keys across many
    // child batches. The oracle uses the independently specified key ranks.
    const char *words[] = {"z", "aa", "🙂", "bbb", "a", "z"};
    const int ranks[] = {1, 3, 2, 4, 0, 1};
    std::string input;
    std::vector<int> expected;
    for (int i = 0; i < 1031; ++i) {
      if (i) input += ' ';
      input += words[i % 6]; expected.push_back(i + 1);
    }
    std::stable_sort(expected.begin(), expected.end(), [&](int left, int right) {
      return ranks[(left - 1) % 6] < ranks[(right - 1) % 6];
    });
    run_case(provider, loader, arena, "token, ordinal", "", expected, batch, false, input.c_str());
    expected.resize(31);
    run_case(provider, loader, arena, "token, ordinal", " LIMIT 31", expected, batch, false, input.c_str());
  }
  oceanbase::share::bind_server_service<Manager>(saved);
}
} // namespace rust_sort_plan_test
#endif
