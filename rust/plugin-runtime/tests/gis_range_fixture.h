/*
 * Copyright (c) 2026 OceanBase.
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
#include "sql/rewrite/ob_transform_utils.h"
#include "sql/optimizer/ob_optimizer.h"
#include "sql/code_generator/ob_code_generator.h"
#include "sql/das/ob_das_attach_define.h"
#include "sql/resolver/ddl/ob_index_builder_util.h"
#include "sql/optimizer/ob_log_table_scan.h"
#include "sql/optimizer/ob_log_plan.h"
#include "sql/optimizer/ob_optimizer_context.h"
#include "sql/optimizer/stat/ob_opt_stat_manager.h"
#include "share/ob_server_struct.h"

// Real SQL resolution, candidate matching, logical/physical planning, range
// extraction and GIS DSO, with controlled schemas/default statistics. No tablet IO.
static void native_spatial_ranges(GisProvider &provider, GisCatalogContext &catalog)
{
  using namespace oceanbase::share::schema;
  CHECK(ObSysTableChecker::instance().init() == OB_SUCCESS);
  auto &arena = catalog.arena;
  CHECK(catalog.execution.get_stmt_factory() != nullptr && catalog.execution.get_query_ctx() != nullptr);
  class BoundsProvider final : public ObISrsProvider {
  public:
    ObSrsBoundsItem bounds;
    BoundsProvider() { bounds.minX_ = bounds.minY_ = -100; bounds.maxX_ = bounds.maxY_ = 100; }
    int get_tenant_srs_guard(ObSrsCacheGuard &) override { return OB_NOT_SUPPORTED; }
    int get_srs_bounds(uint64_t, const ObSrsItem *, const ObSrsBoundsItem *&out) override {
      out = &bounds; return OB_SUCCESS;
    }
  } srs;
  auto *saved_srs = catalog.execution.get_srs_provider();
  const auto saved_timeout = catalog.execution.get_physical_plan_ctx()->get_timeout_timestamp();
  catalog.execution.set_srs_provider(&srs);
  catalog.execution.get_physical_plan_ctx()->set_timeout_timestamp(INT64_MAX);
  // Keep cached metadata alive for the entire catalog fixture.
  auto *table = new (arena.alloc(sizeof(ObTableSchema))) ObTableSchema(&arena);
  table->set_table_id(311236); table->set_database_id(100); table->set_schema_version(42);
  table->set_tablet_id(311236);
  table->set_table_type(USER_TABLE); table->set_name_case_mode(OB_ORIGIN_AND_INSENSITIVE);
  table->set_charset_type(CHARSET_UTF8MB4); table->set_collation_type(CS_TYPE_UTF8MB4_GENERAL_CI);
  table->set_rowkey_column_num(1); table->set_max_used_column_id(17);
  CHECK(table->set_table_name("gis_range_rows") == OB_SUCCESS);
  ObColumnSchemaV2 geo, key;
  geo.set_table_id(table->get_table_id()); geo.set_column_id(16);
  CHECK(geo.set_column_name("shape") == OB_SUCCESS);
  geo.set_data_type(ObGeometryType); geo.set_collation_type(CS_TYPE_BINARY);
  geo.set_data_length(16 * 1024 * 1024); geo.set_srs_id(0); geo.set_geo_type(ObGeoType::GEOMETRY);
  CHECK(table->add_column(geo) == OB_SUCCESS);
  key.set_table_id(table->get_table_id()); key.set_column_id(17);
  CHECK(key.set_column_name("cell") == OB_SUCCESS);
  key.set_data_type(ObUInt64Type); key.set_rowkey_position(1); key.set_nullable(false);
  CHECK(table->add_column(key) == OB_SUCCESS && table->is_valid());
  // Controlled spatial-index schema for real candidate matching and planning;
  // it is not an index created on a tablet.
  auto *index = new (arena.alloc(sizeof(ObTableSchema))) ObTableSchema(&arena);
  index->set_table_id(311237); index->set_database_id(100); index->set_schema_version(42);
  index->set_tablet_id(311237);
  index->set_table_type(USER_INDEX); index->set_data_table_id(table->get_table_id());
  index->set_index_type(INDEX_TYPE_SPATIAL_LOCAL); index->set_index_status(INDEX_STATUS_AVAILABLE);
  index->set_name_case_mode(OB_ORIGIN_AND_INSENSITIVE);
  index->set_charset_type(table->get_charset_type()); index->set_collation_type(table->get_collation_type());
  ObString index_name;
  CHECK(ObTableSchema::build_index_table_name(arena, table->get_table_id(),
      ObString::make_string("gis_range_rows_spatial"), index_name) == OB_SUCCESS);
  CHECK(index->set_table_name(index_name) == OB_SUCCESS);
  // Use the production DDL helpers, including hidden-column definitions and
  // dependencies in the base table; synthetic index-only columns omit the MBR
  // access expression that physical scan generation requires.
  oceanbase::obcall::ObCreateIndexArg index_arg;
  index_arg.index_type_ = INDEX_TYPE_SPATIAL_LOCAL;
  oceanbase::obcall::ObColumnSortItem sort_column;
  sort_column.column_name_ = ObString::make_string("shape");
  CHECK(index_arg.index_columns_.push_back(sort_column) == OB_SUCCESS);
  ObSEArray<ObColumnSchemaV2 *, 2> generated_columns;
  CHECK(ObIndexBuilderUtil::adjust_expr_index_args(index_arg, *table, arena, generated_columns) == OB_SUCCESS);
  CHECK(generated_columns.count() == 2 && generated_columns.at(0)->get_column_id() == 18
      && generated_columns.at(1)->get_column_id() == 19);
  CHECK(ObIndexBuilderUtil::set_index_table_columns(index_arg, *table, *index) == OB_SUCCESS);
  CHECK(index->is_valid() && index->get_rowkey_column_num() == 3);
  CHECK(table->add_simple_index_info(ObAuxTableMetaInfo(index->get_table_id(), USER_INDEX,
      INDEX_TYPE_SPATIAL_LOCAL)) == OB_SUCCESS);
  CHECK(catalog.manager->add_table(*index) == OB_SUCCESS);
  CHECK(MockSchemaService::cache_table(catalog.guard, *index) == OB_SUCCESS);
  ObSqlSchemaGuard sql_guard; sql_guard.set_schema_guard(&catalog.guard);
  const int added = catalog.manager->add_table(*table);
  if (added != OB_SUCCESS) std::cerr << "native range add table=" << added << std::endl;
  CHECK(added == OB_SUCCESS);
  CHECK(MockSchemaService::cache_table(catalog.guard, *table) == OB_SUCCESS);
  CHECK(ObExprOperatorFactory::get_type_by_name(ObString::make_string("spatial_mbr")) == T_FUN_SYS_SPATIAL_MBR);
  for (const auto *column : generated_columns) {
    bool has_index = false;
    const int status = catalog.checker.check_column_has_index(table->get_table_id(), column->get_column_id(), has_index);
    if (status != OB_SUCCESS) std::cerr << "generated column index check=" << status << std::endl;
    CHECK(status == OB_SUCCESS && has_index);
  }
  auto declaration = [&](const char *name, const char *implementation, uint64_t id) {
    const auto saved_privileges = catalog.session->get_user_priv_set();
    catalog.session->set_user_priv_set(OB_PRIV_SUPER | OB_PRIV_CREATE_ROUTINE);
    const std::string result_type = std::strcmp(implementation, "st_distance") == 0 ? "DOUBLE" : "BIGINT";
    const std::string sql = std::string("CREATE FUNCTION empty_db.") + name +
        "(a GEOMETRY,b GEOMETRY) RETURNS " + result_type + " DETERMINISTIC NO SQL SQL SECURITY INVOKER AS "
        "'org.seekdb.gis','org.seekdb.gis.function." + implementation + "' LANGUAGE C";
    ObParser parser(arena, catalog.session->get_sql_mode()); ParseResult parsed{};
    CHECK(parser.parse(ObString(sql.size(), sql.data()), parsed) == OB_SUCCESS);
    ObCreateFunctionResolver resolver(catalog.params);
    const int resolved = resolver.resolve(*parsed.result_tree_->children_[0]);
    if (resolved != OB_SUCCESS) std::cerr << "native support declaration=" << resolved << " " << sql << std::endl;
    CHECK(resolved == OB_SUCCESS);
    const auto *statement = dynamic_cast<ObCreateRoutineStmt *>(resolver.get_basic_stmt()); CHECK(statement);
    ObRoutineInfo routine(&arena);
    CHECK(routine.assign(statement->get_routine_arg().routine_info_) == OB_SUCCESS);
    routine.set_routine_id(id); routine.set_schema_version(42); routine.set_owner_id(123);
    CHECK(NativeRoutineCreateSlot::assign(catalog.guard, routine) == OB_SUCCESS);
    for (int64_t i = 0; i < routine.get_routine_params().count(); ++i) {
      routine.get_routine_params().at(i)->set_routine_id(id);
      routine.get_routine_params().at(i)->set_schema_version(42);
    }
    CHECK(catalog.overlay->stage(routine) == OB_SUCCESS);
    ObPackedObjPriv execute = 0;
    CHECK(ObPrivPacker::raw_obj_priv_to_packed_info(NO_OPTION, OBJ_PRIV_ID_EXECUTE, execute) == OB_SUCCESS);
    CHECK(MockSchemaService::grant_object(*catalog.manager, id, 123, 123, execute) == OB_SUCCESS);
    catalog.session->set_user_priv_set(saved_privileges);
  };
  declaration("custom_intersection", "st_intersects", 345000);
  declaration("st_intersects", "st_distance", 345001);
  struct Case { const char *predicate; ObDomainOpType relation; };
  for (const Case &test : {
      Case{"st_intersects(shape,point(1,2))", ObDomainOpType::T_GEO_INTERSECTS},
      Case{"st_covers(shape,point(1,2))", ObDomainOpType::T_GEO_COVEREDBY},
      Case{"st_covers(point(1,2),shape)", ObDomainOpType::T_GEO_COVERS},
      Case{"st_within(shape,point(1,2))", ObDomainOpType::T_GEO_COVERS},
      Case{"st_dwithin(shape,point(1,2),1.0)", ObDomainOpType::T_GEO_DWITHIN},
      Case{"_st_equals(shape,point(1,2))", ObDomainOpType::T_GEO_INTERSECTS},
      Case{"empty_db.custom_intersection(shape,point(1,2))", ObDomainOpType::T_GEO_INTERSECTS},
      Case{"empty_db.st_intersects(shape,point(1,2))", ObDomainOpType::T_INVALID}}) {
    const std::string sql = std::string("SELECT cell FROM gis_range_rows WHERE ") + test.predicate;
    ObParser parser(arena, catalog.session->get_sql_mode()); ParseResult parsed{};
    CHECK(parser.parse(ObString(sql.size(), sql.data()), parsed) == OB_SUCCESS);
    ObSelectResolver resolver(catalog.params);
    const int resolved = resolver.resolve(*parsed.result_tree_->children_[0]);
    if (resolved != OB_SUCCESS) std::cerr << "native range resolve=" << resolved << " " << sql << std::endl;
    CHECK(resolved == OB_SUCCESS);
    auto *stmt = resolver.get_select_stmt();
    CHECK(stmt && stmt->get_condition_exprs().count() == 1);
    const auto *condition = stmt->get_condition_exprs().at(0);
    const bool spatial = test.relation != ObDomainOpType::T_INVALID;
    CHECK(condition->is_spatial_expr() == spatial);
    const auto *raw = ObRawExprUtils::skip_inner_added_expr(condition);
    CHECK(raw->is_udf_expr() && raw->get_expr_type() == T_FUN_UDF);
    ObSEArray<ColumnItem, 1> columns;
    for (int64_t i = 0; i < stmt->get_column_items().count(); ++i) {
      const auto &column = stmt->get_column_items().at(i);
      if (column.column_id_ == 17) CHECK(columns.push_back(column) == OB_SUCCESS);
    }
    CHECK(columns.count() == 1);
    ObSEArray<ObColumnRefRawExpr *, 2> filter_columns;
    CHECK(ObTransformUtils::get_simple_filter_column(stmt, stmt->get_condition_exprs().at(0),
        columns.at(0).table_id_, filter_columns) == OB_SUCCESS);
    CHECK(filter_columns.count() == (spatial ? 1 : 0));
    if (spatial) {
      CHECK(filter_columns.at(0)->get_column_id() == 16);
      bool matched = false;
      CHECK(ObTransformUtils::is_match_index(&sql_guard, stmt, filter_columns.at(0), matched) == OB_SUCCESS);
      CHECK(matched);
    }
    // A predicate for another table must not leak into this table's candidates.
    filter_columns.reuse();
    CHECK(ObTransformUtils::get_simple_filter_column(stmt, stmt->get_condition_exprs().at(0),
        columns.at(0).table_id_ + 1, filter_columns) == OB_SUCCESS);
    CHECK(filter_columns.empty());
    ObWrapperAllocator wrapper(arena);
    ColumnIdInfoMapAllocer map_allocator(OB_MALLOC_NORMAL_BLOCK_SIZE, wrapper);
    ColumnIdInfoMap map;
    CHECK(map.create(8, &map_allocator, &wrapper) == OB_SUCCESS);
    ObGeoColumnInfo info{0, 17};
    CHECK(map.set_refactored(16, info) == OB_SUCCESS);
    ObPreRangeGraph graph(arena);
    const int calls = provider.calls_;
    const int extracted = graph.preliminary_extract_query_range(columns, stmt->get_condition_exprs(),
        &catalog.execution, nullptr, nullptr, false, -1, nullptr, &map);
    if (extracted != OB_SUCCESS) std::cerr << "native range extract=" << extracted << " " << sql << std::endl;
    CHECK(extracted == OB_SUCCESS);
    CHECK(provider.calls_ == calls); // Constructing a graph must not execute native constructors.
    const auto *head = graph.get_range_head();
    if (!spatial) {
      CHECK(graph.is_precise_whole_range());
      continue;
    }
    CHECK(head && head->is_domain_node_ && !head->always_true_ && !head->always_false_);
    CHECK(head->domain_extra_.domain_releation_type_ == static_cast<int32_t>(test.relation));
    CHECK(!graph.get_unprecise_range_exprs().empty()); // Never remove the native predicate.
    ObUDFRawExpr copy(arena);
    copy.set_expr_type(raw->get_expr_type());
    CHECK(copy.assign(*raw) == OB_SUCCESS && copy.is_spatial_expr());
    CHECK(copy.get_native_spatial_flags() == static_cast<const ObUDFRawExpr *>(raw)->get_native_spatial_flags());
    ObQueryRangeArray ranges;
    ObSEArray<ObSpatialMBR, 4> mbrs;
    bool single = false;
    const auto casts = ObBasicSessionInfo::create_dtc_params(catalog.session.get());
    const int generated = graph.get_tablet_ranges(arena, catalog.execution, ranges, single, casts, mbrs);
    if (generated != OB_SUCCESS) std::cerr << "native range generation=" << generated << " " << sql << std::endl;
    CHECK(generated == OB_SUCCESS && !ranges.empty() && !mbrs.empty());
    CHECK(provider.calls_ > calls); // POINT is evaluated through the native runtime binding.
    if (std::strcmp(test.predicate, "st_intersects(shape,point(1,2))") == 0) {
      auto *constructor = const_cast<ObUDFRawExpr *>(dynamic_cast<const ObUDFRawExpr *>(raw->get_param_expr(1)));
      CHECK(constructor);
      const auto saved_flags = constructor->get_native_function_flags();
      constructor->set_native_function_flags(0);
      const int before = provider.calls_;
      ObPreRangeGraph unproven(arena);
      CHECK(unproven.preliminary_extract_query_range(columns, stmt->get_condition_exprs(),
          &catalog.execution, nullptr, nullptr, false, -1, nullptr, &map) == OB_SUCCESS);
      CHECK(unproven.is_precise_whole_range() && provider.calls_ == before);
      constructor->set_native_function_flags(saved_flags);
    }
  }
  struct FilterCase { const char *predicate; int64_t columns; };
  for (const FilterCase &test : {
      FilterCase{"st_intersects(shape,shape)", 0},
      FilterCase{"st_dwithin(shape,shape,1.0)", 0},
      FilterCase{"st_intersects(point(1,2),point(3,4))", 0},
      FilterCase{"st_area(shape)", 0},
      FilterCase{"st_intersects(shape,point(1,2)) OR st_covers(point(3,4),shape)", 1},
      FilterCase{"st_intersects(shape,point(1,2)) AND st_within(shape,point(3,4))", 1},
      FilterCase{"st_intersects(shape,point(1,2)) OR empty_db.st_intersects(shape,point(3,4))", 1},
      FilterCase{"cell=7", 1}}) {
    const std::string sql = std::string("SELECT cell FROM gis_range_rows WHERE ") + test.predicate;
    ObParser parser(arena, catalog.session->get_sql_mode()); ParseResult parsed{};
    CHECK(parser.parse(ObString(sql.size(), sql.data()), parsed) == OB_SUCCESS);
    ObSelectResolver resolver(catalog.params);
    CHECK(resolver.resolve(*parsed.result_tree_->children_[0]) == OB_SUCCESS);
    auto *stmt = resolver.get_select_stmt(); CHECK(stmt && stmt->get_table_size() == 1);
    ObSEArray<ObColumnRefRawExpr *, 2> columns;
    const int calls = provider.calls_;
    for (int64_t i = 0; i < stmt->get_condition_exprs().count(); ++i) {
      CHECK(ObTransformUtils::get_simple_filter_column(stmt, stmt->get_condition_exprs().at(i),
          stmt->get_table_item(0)->table_id_, columns) == OB_SUCCESS);
    }
    if (columns.count() != test.columns) std::cerr << "native filter columns=" << columns.count() << " " << sql << std::endl;
    CHECK(columns.count() == test.columns && provider.calls_ == calls);
    if (!columns.empty()) CHECK(columns.at(0)->get_column_id() == (std::strcmp(test.predicate, "cell=7") == 0 ? 17 : 16));
  }
  ObUDFRawExpr same_name(arena);
  same_name.set_expr_type(T_FUN_UDF);
  same_name.set_func_name(ObString::make_string("st_intersects"));
  CHECK(!same_name.is_spatial_expr()); // Spelling does not grant planner semantics.
  struct PlanCase { const char *predicate; bool spatial; };
  for (const PlanCase &test : {
      PlanCase{"st_intersects(shape,point(1,2))", true},
      PlanCase{"st_covers(shape,point(1,2))", true},
      PlanCase{"st_covers(point(1,2),shape)", true},
      PlanCase{"st_within(shape,point(1,2))", true},
      PlanCase{"st_dwithin(shape,point(1,2),1.0)", true},
      PlanCase{"_st_equals(shape,point(1,2))", true},
      PlanCase{"empty_db.custom_intersection(shape,point(1,2))", true},
      PlanCase{"empty_db.st_intersects(shape,point(1,2))", false}}) {
    // No INDEX hint: cost-based selection must choose the spatial access path.
    const std::string sql = std::string("SELECT cell FROM gis_range_rows WHERE ") + test.predicate;
    ObParser parser(arena, catalog.session->get_sql_mode()); ParseResult parsed{};
    CHECK(parser.parse(ObString(sql.size(), sql.data()), parsed) == OB_SUCCESS);
    ObSelectResolver resolver(catalog.params);
    CHECK(resolver.resolve(*parsed.result_tree_->children_[0]) == OB_SUCCESS);
    auto *stmt = resolver.get_select_stmt(); CHECK(stmt);
    CHECK(stmt->get_condition_exprs().count() == 1);
    const auto *original = stmt->get_condition_exprs().at(0);
    ObAddr address; CHECK(address.set_ip_addr("127.0.0.1", 2882));
    const auto saved_address = GCTX.self_addr_seq_;
    GCTX.self_addr_seq_.set_addr_seq(address, 1);
    ObOptStatManager statistics;
    const bool saved_inner = catalog.session->is_inner();
    const auto saved_session_type = catalog.session->get_session_type();
    catalog.session->set_inner_session(); // Use default statistics without a stats SQL backend.
    CHECK(stmt->get_query_ctx() && catalog.execution.get_sql_executor_ctx());
    ObOptimizerContext context(catalog.session.get(), &catalog.execution, &sql_guard, &statistics, arena,
        &catalog.execution.get_physical_plan_ctx()->get_param_store(), address,
        stmt->get_query_ctx()->get_global_hint(), catalog.factory, stmt, false, stmt->get_query_ctx());
    ObOptimizer optimizer(context); ObLogPlan *plan = nullptr;
    const int optimized = optimizer.optimize(*stmt, plan);
    if (optimized != OB_SUCCESS) std::cerr << "native spatial optimize=" << optimized << " " << sql << std::endl;
    CHECK(optimized == OB_SUCCESS && plan && plan->get_plan_root());
    std::vector<ObLogicalOperator *> pending{plan->get_plan_root()};
    int scans = 0;
    bool residual = false;
    while (!pending.empty()) {
      auto *op = pending.back(); pending.pop_back();
      for (int64_t i = 0; i < op->get_filter_exprs().count(); ++i) {
        residual |= op->get_filter_exprs().at(i)->same_as(*original);
      }
      if (op->get_type() == log_op_def::LOG_TABLE_SCAN) {
        auto *scan = static_cast<ObLogTableScan *>(op);
        std::cerr << "native spatial chosen index=" << scan->get_index_table_id()
                  << " spatial=" << scan->is_spatial_index_scan() << " " << test.predicate << std::endl;
        CHECK(scan->get_index_table_id() == (test.spatial ? index->get_table_id() : table->get_table_id()));
        CHECK(scan->is_spatial_index_scan() == test.spatial);
        if (test.spatial) {
          CHECK(scan->get_index_back() && scan->get_pre_range_graph());
          CHECK(!scan->get_pre_range_graph()->is_precise_whole_range());
          CHECK(!scan->get_pre_range_graph()->get_unprecise_range_exprs().empty());
        }
        ++scans;
      }
      for (int64_t i = 0; i < op->get_num_of_child(); ++i) pending.push_back(op->get_child(i));
    }
    CHECK(scans == 1 && residual);
    ObPhysicalPlan physical;
    ObCodeGenerator generator(&catalog.execution.get_physical_plan_ctx()->get_datum_param_store());
    const int generated = generator.generate(*plan, physical);
    if (generated != OB_SUCCESS) std::cerr << "native spatial codegen=" << generated << " " << sql << std::endl;
    CHECK(generated == OB_SUCCESS && physical.get_root_op_spec());
    ObExpr *predicate = nullptr;
    ObSEArray<ObRawExpr *, 1> outputs;
    CHECK(ObStaticEngineExprCG::generate_rt_expr(*original, outputs, predicate) == OB_SUCCESS && predicate);
    std::vector<const ObOpSpec *> specs{physical.get_root_op_spec()};
    std::vector<const ObExpr *> filters;
    auto collect_filters = [&](const ExprFixedArray &expressions) {
      for (auto *expression : expressions) filters.push_back(expression);
    };
    int physical_scans = 0;
    while (!specs.empty()) {
      const auto *spec = specs.back(); specs.pop_back();
      collect_filters(spec->filters_);
      if (const auto *scan = dynamic_cast<const ObTableScanSpec *>(spec)) {
        ++physical_scans;
        const auto &ctdef = scan->tsc_ctdef_;
        const auto &access = ctdef.scan_ctdef_;
        CHECK(access.ref_table_id_ == (test.spatial ? index->get_table_id() : table->get_table_id()));
        CHECK(bool(access.table_param_.is_spatial_index()) == test.spatial);
        collect_filters(access.pd_expr_spec_.pushdown_filters_);
        if (test.spatial) {
          CHECK(access.access_column_ids_.count() == access.pd_expr_spec_.access_exprs_.count());
          bool mbr_access = false;
          for (int64_t i = 0; i < access.access_column_ids_.count(); ++i) {
            if (access.access_column_ids_.at(i) == 19) {
              const auto *mbr = access.pd_expr_spec_.access_exprs_.at(i);
              CHECK(mbr && mbr->type_ == T_REF_COLUMN && mbr->datum_meta_.type_ == ObVarcharType);
              mbr_access = true;
            }
          }
          CHECK(mbr_access);
          const auto *lookup = ctdef.get_lookup_ctdef();
          CHECK(lookup && lookup->ref_table_id_ == table->get_table_id());
          CHECK(!lookup->table_param_.is_spatial_index());
          collect_filters(lookup->pd_expr_spec_.pushdown_filters_);
          const auto *attach = ctdef.attach_spec_.attach_ctdef_;
          CHECK(attach && attach->op_type_ == DAS_OP_TABLE_LOOKUP && attach->children_cnt_ == 2);
          const auto *lookup_tree = static_cast<const ObDASTableLookupCtDef *>(attach);
          CHECK(!lookup_tree->is_global_index_ && lookup_tree->get_lookup_scan_ctdef() == lookup);
          const auto *rowkeys = lookup_tree->get_rowkey_scan_ctdef();
          CHECK(rowkeys && rowkeys->op_type_ == DAS_OP_SORT && rowkeys->children_cnt_ == 1);
          CHECK(rowkeys->children_[0] == &access);
          const auto *sort = static_cast<const ObDASSortCtDef *>(rowkeys);
          CHECK(sort->sort_exprs_.count() == 1 && sort->sort_collations_.count() == 1
              && sort->sort_cmp_funcs_.count() == 1);
          CHECK(sort->sort_exprs_.at(0)->datum_meta_.type_ == ObUInt64Type);
        } else {
          CHECK(!ctdef.get_lookup_ctdef() && !ctdef.attach_spec_.attach_ctdef_);
        }
      }
      for (uint32_t i = 0; i < spec->get_child_cnt(); ++i) specs.push_back(spec->get_child(i));
    }
    CHECK(physical_scans == 1);
    bool physical_residual = false;
    while (!filters.empty()) {
      const auto *filter = filters.back(); filters.pop_back(); CHECK(filter);
      physical_residual |= filter == predicate;
      for (uint32_t i = 0; i < filter->arg_cnt_; ++i) filters.push_back(filter->args_[i]);
    }
    CHECK(physical_residual); // MBR filtering is never a replacement for the exact SQL predicate.
    if (!saved_inner) catalog.session->set_user_session();
    catalog.session->set_session_type(saved_session_type);
    GCTX.self_addr_seq_ = saved_address;
  }
  std::cout << "PASS: native GIS cost-based spatial index selection and physical MBR scan/rowkey sort/lookup, retained exact predicate; controlled schema/default stats, no tablet IO" << std::endl;
  ObRawExpr *changed = nullptr;
  CHECK(catalog.resolve("st_intersects(POINT(1,2),POINT(3,4))", changed) == OB_SUCCESS && changed);
  CHECK(changed->formalize(catalog.session.get()) == OB_SUCCESS);
  provider.native_support_xor_ = SEEKDB_PLUGIN_EXTENSION_FLAG_SPATIAL_INTERSECTS;
  ObRawExprUniqueSet roots(false);
  CHECK(roots.append(changed) == OB_SUCCESS);
  ObStaticEngineExprCG codegen(arena, catalog.session.get(), &catalog.guard, 0, 0);
  ObExprFrameInfo frame(arena);
  CHECK(codegen.generate(roots, frame) == OB_STATE_NOT_MATCH);
  provider.native_support_xor_ = 0;
  catalog.execution.set_srs_provider(saved_srs);
  catalog.execution.get_physical_plan_ctx()->set_timeout_timestamp(saved_timeout);
  std::cout << "PASS: native GIS SELECT predicate resolution, filter-column collection and actual spatial range extraction; residual predicate retained" << std::endl;
}

static void spatial_ranges(GisProvider &provider, GisCatalogContext &catalog)
{
  oceanbase::omt::ObSrsCacheSnapShot snapshot;
  CHECK(snapshot.init() == OB_SUCCESS);
  gis_srs_lookup_test::Row row;
  row.srid = 4326;
  const ObSrsItem *item = nullptr;
  CHECK(snapshot.parse_srs_item(&row, item) == OB_SUCCESS && item);
  CHECK(snapshot.add_srs_item(row.srid, item) == OB_SUCCESS);
  const ObSrsItem *projected = nullptr;
  for (const auto &record : gis_catalog_test::records) {
    if (record.srid == 32631) {
      row.srid = record.srid; row.wkt = record.wkt; row.proj4 = record.proj4;
      CHECK(snapshot.parse_srs_item(&row, projected) == OB_SUCCESS && projected);
      CHECK(snapshot.add_srs_item(row.srid, projected) == OB_SUCCESS);
    }
  }
  CHECK(projected && !projected->is_geographical_srs());
  class SrsProvider final : public ObISrsProvider {
  public:
    ObISrsSnapshot &snapshot;
    ObSrsBoundsItem bounds;
    int failure = OB_SUCCESS;
    explicit SrsProvider(ObISrsSnapshot &s) : snapshot(s) {
      bounds.minX_ = bounds.minY_ = -100; bounds.maxX_ = bounds.maxY_ = 100;
    }
    int get_tenant_srs_guard(ObSrsCacheGuard &guard) override {
      if (failure != OB_SUCCESS) return failure;
      guard.bind(snapshot); return OB_SUCCESS;
    }
    int get_srs_bounds(uint64_t, const ObSrsItem *, const ObSrsBoundsItem *&out) override {
      if (failure != OB_SUCCESS) return failure;
      out = &bounds; return OB_SUCCESS;
    }
  } srs(snapshot);
  using Bytes = std::vector<uint8_t>;
  using Interval = std::pair<uint64_t, uint64_t>;
  auto wkb = [](const Bytes &b) { return ObString(b.size(), reinterpret_cast<const char *>(b.data())); };
  const auto point = gis_spatial_test::geometry(1, {1, 2});
  const auto line = gis_spatial_test::geometry(2, {-50, -20, 30, 40});
  auto empty = gis_spatial_test::geometry(7, {}); gis_spatial_test::append_u32(empty, 0);
  auto run = [&](const Bytes &bytes, ObDomainOpType op, double distance, int expected,
                 bool whole = false, bool null = false, uint32_t srid = 0,
                 const Bytes *second = nullptr) {
    ObArenaAllocator arena;
    ObExecContext execution(arena);
    execution.set_my_session(catalog.session.get()); execution.set_srs_provider(&srs);
    execution.set_lob_read_service(&catalog.lob);
    CHECK(execution.create_physical_plan_ctx() == OB_SUCCESS);
    ObPreRangeGraph graph(arena);
    ObColumnRefRawExpr column;
    column.set_ref_id(100, 16); column.set_data_type(ObUInt64Type);
    ColumnItem column_item; column_item.expr_ = &column;
    ObSEArray<ColumnItem, 1> columns;
    CHECK(columns.push_back(column_item) == OB_SUCCESS);
    CHECK(graph.fill_column_metas(columns) == OB_SUCCESS);
    ObObj values[3];
    if (!null) { values[0].set_string(ObGeometryType, wkb(bytes)); values[0].set_collation_type(CS_TYPE_BINARY); }
    values[1].set_double(distance);
    if (second) { values[2].set_string(ObGeometryType, wkb(*second)); values[2].set_collation_type(CS_TYPE_BINARY); }
    CHECK(graph.get_range_map().expr_final_infos_.init(3) == OB_SUCCESS);
    for (auto &value : values) {
      ObRangeMap::ExprFinalInfo info;
      info.is_const_ = true; info.const_val_ = &value;
      CHECK(graph.get_range_map().expr_final_infos_.push_back(info) == OB_SUCCESS);
    }
    auto node = [&](int64_t id, int64_t value) {
      auto *n = new (arena.alloc(sizeof(ObRangeNode))) ObRangeNode(arena);
      n->column_cnt_ = 1; n->min_offset_ = n->max_offset_ = 0; n->node_id_ = id;
      n->is_domain_node_ = true; n->include_start_ = n->include_end_ = true;
      n->domain_extra_.srid_ = srid; n->domain_extra_.domain_releation_type_ = static_cast<int32_t>(op);
      n->start_keys_ = static_cast<int64_t *>(arena.alloc(sizeof(int64_t)));
      n->end_keys_ = static_cast<int64_t *>(arena.alloc(sizeof(int64_t)));
      n->start_keys_[0] = value; n->end_keys_[0] = 1;
      return n;
    };
    auto *head = node(0, 0);
    if (second) head->or_next_ = node(1, 2);
    graph.set_range_head(head); graph.set_node_count(second ? 2 : 1);
    ObSEArray<ObNewRange *, 16> ranges;
    ObSEArray<ObSpatialMBR, 4> mbrs;
    bool single = true;
    ObDataTypeCastParams cast_params;
    ObRangeGenerator generator(arena, execution, &graph, ranges, single, cast_params, mbrs);
    const int ret = generator.generate_ranges();
    if ((expected == OB_ERROR && ret == OB_SUCCESS) || (expected != OB_ERROR && ret != expected)) {
      std::cerr << "spatial range: op=" << static_cast<int>(op) << " srid=" << srid
                << " distance=" << distance << " ret=" << ret << " expected=" << expected << std::endl;
    }
    if (expected == OB_SUCCESS) CHECK(ret == OB_SUCCESS);
    else if (expected == OB_ERROR) CHECK(ret != OB_SUCCESS);
    else CHECK(ret == expected);
    std::vector<Interval> result;
    if (ret != OB_SUCCESS) {
      CHECK(ranges.empty() && mbrs.empty() && single);
    } else if (whole) {
      CHECK(ranges.count() == 1 && ranges.at(0)->is_whole_range() && mbrs.empty());
    } else {
      CHECK(ranges.count() > 0 && mbrs.count() == 1);
      const auto &mbr = mbrs.at(0);
      CHECK(!mbr.is_empty() && std::isfinite(mbr.x_min_) && std::isfinite(mbr.y_max_));
      for (auto *range : ranges) {
        CHECK(range->table_id_ == 100 && range->border_flag_.inclusive_start() && range->border_flag_.inclusive_end());
        const uint64_t lo = range->start_key_.get_obj_ptr()[0].get_uint64();
        const uint64_t hi = range->end_key_.get_obj_ptr()[0].get_uint64();
        CHECK(lo <= hi); result.emplace_back(lo, hi);
      }
    }
    std::sort(result.begin(), result.end());
    return result;
  };
  const auto intersects = ObDomainOpType::T_GEO_INTERSECTS;
  const auto within = ObDomainOpType::T_GEO_DWITHIN;
  auto point_ranges = run(point, intersects, 0, OB_SUCCESS);
  // Compare actual host range assembly to plugin cell/ancestor metadata.
  ObArenaAllocator arena;
  ObS2Adapter adapter(&arena, false, true);
  CHECK(adapter.init(wkb(point), &srs.bounds) == OB_SUCCESS);
  ObS2Cellids cells, ancestors;
  CHECK(adapter.get_cellids_and_unrepeated_ancestors(cells, ancestors) == OB_SUCCESS);
  std::vector<Interval> expected;
  for (uint64_t cell : ancestors) expected.emplace_back(cell, cell);
  for (uint64_t cell : cells) expected.emplace_back(cell, cell);
  std::sort(expected.begin(), expected.end());
  CHECK(point_ranges == expected);
  auto outside = run(gis_spatial_test::geometry(1, {200, 200}), intersects, 0, OB_SUCCESS);
  CHECK(outside.size() == 1 && outside[0] == Interval(UINT64_MAX, UINT64_MAX));
  run(line, intersects, 0, OB_SUCCESS);
  run(line, ObDomainOpType::T_GEO_COVERS, 0, OB_SUCCESS);
  run(line, ObDomainOpType::T_GEO_COVEREDBY, 0, OB_SUCCESS);
  run(empty, intersects, 0, OB_SUCCESS, true);
  run(point, intersects, 0, OB_SUCCESS, true, true);
  const int buffers_before = provider.buffer_calls_;
  run(point, within, 1, OB_SUCCESS);
  CHECK(provider.buffer_calls_ > buffers_before);
  const auto projected_point = gis_spatial_test::geometry(1, {1, 2}, 32631);
  run(projected_point, within, 1, OB_SUCCESS, false, false, 32631);
  run(projected_point, within, 1, OB_SUCCESS, false, false, UINT32_MAX);
  for (double distance : {NAN, INFINITY, -INFINITY}) run(point, within, distance, OB_INVALID_ARGUMENT);
  run(gis_spatial_test::geometry(1, {1, 2}, 4326), intersects, 0, OB_SUCCESS, false, false, 4326);
  const int buffers_after = provider.buffer_calls_;
  run(gis_spatial_test::geometry(1, {1, 2}, 4326), within, 100, OB_SUCCESS, false, false, 4326);
  CHECK(provider.buffer_calls_ == buffers_after);
  run(gis_spatial_test::geometry(1, {1, 2}, 99999), intersects, 0, OB_ERR_SRS_NOT_FOUND, false, false, 99999);
  run(point, intersects, 0, OB_ERR_WRONG_SRID_FOR_COLUMN, false, false, 4326);
  srs.failure = OB_TIMEOUT;
  run(point, intersects, 0, OB_TIMEOUT);
  run(gis_spatial_test::geometry(1, {1, 2}, 4326), intersects, 0, OB_TIMEOUT, false, false, 4326);
  srs.failure = OB_SUCCESS;
  // Header inspection is only routing; full WKB remains plugin-owned.
  for (uint32_t type : {1u, 7u, 1001u, 1007u}) {
    for (bool little : {false, true}) {
      Bytes header(10, 0); header[4] = 1; header[5] = little;
      for (unsigned i = 0; i < 4; ++i) header[6 + i] = type >> (8 * (little ? i : 3 - i));
      ObGeoType decoded = ObGeoType::GEOMETRY;
      CHECK(ObGeoTypeUtil::get_type_from_wkb(wkb(header), decoded) == OB_SUCCESS);
      CHECK(static_cast<uint32_t>(decoded) == type);
      header[5] = 2;
      CHECK(ObGeoTypeUtil::get_type_from_wkb(wkb(header), decoded) != OB_SUCCESS);
      CHECK(static_cast<uint32_t>(decoded) == type);
    }
  }
  for (size_t size = 0; size < 10; ++size) {
    Bytes truncated(point.begin(), point.begin() + size);
    ObGeoType decoded = ObGeoType::POINT;
    CHECK(ObGeoTypeUtil::get_type_from_wkb(wkb(truncated), decoded) != OB_SUCCESS && decoded == ObGeoType::POINT);
  }
  Bytes bad_type = point; bad_type[6] = 255;
  ObGeoType decoded = ObGeoType::POINT;
  CHECK(ObGeoTypeUtil::get_type_from_wkb(wkb(bad_type), decoded) != OB_SUCCESS && decoded == ObGeoType::POINT);
  // Buffer admission never interprets an absent/mismatched/geographic SRS as
  // planar. Failed emissions must preserve the caller's existing output.
  ObString buffered = ObString::make_string("untouched");
  const ObString original = buffered;
  CHECK(ObGeoTypeUtil::get_buffered_geo(&arena, wkb(projected_point), 1, nullptr, buffered) == OB_INVALID_ARGUMENT);
  CHECK(ObGeoTypeUtil::get_buffered_geo(&arena, wkb(projected_point), 1, item, buffered) == OB_INVALID_ARGUMENT);
  CHECK(buffered.ptr() == original.ptr() && buffered == original);
  {
    double distance = 1;
    seekdb_plugin_execution_value_v1_t args[2]{};
    for (auto &arg : args) arg.struct_size = sizeof(arg);
    args[0].type_id = "org.seekdb.gis.geometry"; args[0].data = point.data(); args[0].data_size = point.size();
    args[1].type_id = "core.type.double";
    args[1].data = reinterpret_cast<const uint8_t *>(&distance); args[1].data_size = sizeof(distance);
    int emissions = 0;
    seekdb_plugin_execution_context_v1_t context{};
    context.struct_size = sizeof(context);
    context.host = reinterpret_cast<seekdb_plugin_host_handle_t *>(&emissions);
    context.emit_result = [](seekdb_plugin_host_handle_t *host,
        const seekdb_plugin_execution_result_v1_t *) -> seekdb_plugin_status_t {
      ++*reinterpret_cast<int *>(host); return SEEKDB_PLUGIN_STATUS_OK;
    };
    auto rejected = [&] {
      CHECK(provider.execute_plugin_function(SEEKDB_PLUGIN_SPATIAL_BUFFER_SERVICE, 1, 0, &context, args, 2) != OB_SUCCESS);
      CHECK(emissions == 0);
    };
    args[1].type_id = "core.type.int64"; rejected(); args[1].type_id = "core.type.double";
    for (auto &arg : args) {
      arg.reserved[0] = 1; rejected(); arg.reserved[0] = 0;
      arg.reserved_bytes[0] = 1; rejected(); arg.reserved_bytes[0] = 0;
    }
  }
  auto malformed = point; malformed.resize(3);
  run(point, intersects, 0, OB_ERROR, false, false, 0, &malformed);
  for (int fault = 1; fault <= 9; ++fault) {
    provider.index_fault_ = fault;
    run(point, intersects, 0, fault == 1 || fault == 3 ? OB_TIMEOUT : OB_ERROR);
    provider.index_fault_ = 0;
    provider.buffer_fault_ = fault;
    run(point, within, 1, fault == 1 || fault == 3 ? OB_TIMEOUT : OB_ERROR);
    CHECK(ObGeoTypeUtil::get_buffered_geo(&arena, wkb(point), 1, nullptr, buffered) != OB_SUCCESS);
    CHECK(buffered.ptr() == original.ptr() && buffered == original);
    provider.buffer_fault_ = 0;
  }
  auto *saved = g_mp; g_mp = nullptr;
  run(point, intersects, 0, OB_NOT_SUPPORTED);
  run(point, within, 1, OB_NOT_SUPPORTED);
  g_mp = saved;
  CHECK(provider.legacy_calls_ == 0);
  std::cout << "PASS: actual spatial range generator, planar/geographic distance, empty/NULL fallback, SRS and failed-result isolation" << std::endl;
}
