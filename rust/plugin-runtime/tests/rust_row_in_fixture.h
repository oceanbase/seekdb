// Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.
#ifndef SEEKDB_TEST_RUST_ROW_IN_FIXTURE_H_
#define SEEKDB_TEST_RUST_ROW_IN_FIXTURE_H_
#include "sql/rewrite/ob_transform_pre_process.h"
namespace rust_row_in_test {
using namespace oceanbase::common;
using namespace oceanbase::sql;
using namespace oceanbase::share::plugin;

template <typename Provider>
void run(Provider &provider, ObPluginLoader &loader, ObArenaAllocator &arena,
         ObRawExprFactory &factory, ObSQLSessionInfo &session)
{
  std::string maximum = "(NULL,1)";
  for (uint32_t i = 1; i < SEEKDB_PLUGIN_MAX_ARGUMENTS - 1; ++i) maximum += ",(NULL,1)";
  const std::string oversized = maximum + ",(NULL,1)";
  struct Case {
    const char *left, *list; int result, comparisons, functions, casts;
    int status = OB_SUCCESS, bind_status = OB_SUCCESS;
    bool custom = true;
  };
  for (bool negate : {false, true}) for (const Case &test : {
      Case{"(seekdb_rust_text('z'),1)", "(CAST('a' AS rust_utf8),1),(CAST('z' AS rust_utf8),1),(CAST(X'FF' AS rust_utf8),1)", 1,2,1,2},
      Case{"(seekdb_rust_text('z'),1)", "(CAST('z' AS rust_utf8),1),(CAST(X'FF' AS rust_utf8),1)", 1,1,1,1},
      Case{"(seekdb_rust_text('z'),1)", "(CAST('z' AS rust_utf8),2),(CAST('z' AS rust_utf8),3)", 0,2,1,2},
      Case{"(CAST(NULL AS rust_utf8),1)", "(CAST('z' AS rust_utf8),2)", 0,0,0,1},
      Case{"(CAST(NULL AS rust_utf8),1)", "(NULL,1)", -1,0,0,0},
      Case{"(CAST(NULL AS rust_utf8),1)", "(CAST('z' AS rust_utf8),2),(CAST('z' AS rust_utf8),1)", -1,0,0,2},
      Case{"(seekdb_rust_text('z'),NULL)", "(CAST('a' AS rust_utf8),NULL)", 0,1,1,1},
      Case{"(seekdb_rust_text('z'),NULL)", "(CAST('z' AS rust_utf8),1)", -1,1,1,1},
      Case{"(seekdb_rust_text('z'),1)", "(NULL,1),(CAST('z' AS rust_utf8),1)", 1,1,1,1},
      Case{"(seekdb_rust_text('z'),1)", "(NULL,1),(CAST('a' AS rust_utf8),1)", -1,1,1,1},
      Case{"(1,seekdb_rust_text('z'))", "(2,CAST(X'FF' AS rust_utf8)),(1,CAST('z' AS rust_utf8))", 1,1,1,1},
      Case{"(1,seekdb_rust_text('z'))", "(2,CAST(X'FF' AS rust_utf8)),(3,CAST('z' AS rust_utf8))", 0,0,0,0},
      Case{"(1,seekdb_rust_text('z'))", "(1,CAST(X'FF' AS rust_utf8)),(1,CAST('z' AS rust_utf8))", 0,0,1,1,OB_INVALID_ARGUMENT},
      Case{"(seekdb_rust_text('z'),1)", "('a',1),(CAST('z' AS rust_utf8),1)", 1,0,1,3},
      Case{"('z',1)", "(seekdb_rust_text('a'),1),(seekdb_rust_text('z'),1)", 1,0,2,2},
      Case{"(seekdb_rust_text('z'),1)", "(7,1)", 0,0,0,0,OB_SUCCESS,OB_ERR_INVALID_TYPE_FOR_OP},
      Case{"(seekdb_rust_text('z'),1)", "(CAST('z' AS rust_utf8),1,2)", 0,0,0,0,OB_SUCCESS,OB_ERR_INVALID_COLUMN_NUM},
      Case{"(CAST('a\\0b' AS rust_utf8),1)", "(CAST('aaa' AS rust_utf8),1),(CAST('a\\0b' AS rust_utf8),1)", 1,2,0,3},
      Case{"(seekdb_rust_text('中🙂'),1)", "(CAST('中' AS rust_utf8),1),(CAST('中🙂' AS rust_utf8),1)", 1,2,1,2},
      Case{"(seekdb_rust_text('z'),1)", "(CAST('z' AS rust_utf8),1)", 1,1,1,1},
      Case{"(CAST(NULL AS rust_utf8),1)", maximum.c_str(), -1,0,0,0},
      Case{"(CAST(NULL AS rust_utf8),1)", oversized.c_str(), 0,0,0,0,OB_SUCCESS,OB_SIZE_OVERFLOW},
      Case{"((seekdb_rust_text('z'),1),2)", "((CAST('a' AS rust_utf8),1),2),((CAST('z' AS rust_utf8),1),2),((CAST(X'FF' AS rust_utf8),1),2)", 1,2,1,2},
      Case{"((seekdb_rust_text('z'),1),2)", "((NULL,1),2),((CAST('z' AS rust_utf8),1),2)", 1,1,1,1},
      Case{"((seekdb_rust_text('z'),1),2)", "((NULL,1),2),((CAST('a' AS rust_utf8),1),2)", -1,1,1,1},
      Case{"((CAST(NULL AS rust_utf8),1),2)", "((NULL,1),3)", 0,0,0,0},
      Case{"((CAST(NULL AS rust_utf8),1),2)", "((NULL,1),2)", -1,0,0,0},
      Case{"(1,(seekdb_rust_text('z'),2))", "(2,(CAST(X'FF' AS rust_utf8),2)),(1,(CAST('z' AS rust_utf8),2))", 1,1,1,1},
      Case{"(((1,2),3),seekdb_rust_text('z'))", "(((1,2),4),CAST(X'FF' AS rust_utf8)),(((1,2),5),CAST('z' AS rust_utf8))", 0,0,0,0},
      Case{"(1,(seekdb_rust_text('z'),2))", "(1,(CAST(X'FF' AS rust_utf8),2))", 0,0,1,1,OB_INVALID_ARGUMENT},
      Case{"((seekdb_rust_text('z'),1),2)", "(('a',1),2),((CAST('z' AS rust_utf8),1),2)", 1,0,1,3},
      Case{"((seekdb_rust_text('z'),1),2)", "((CAST('z' AS rust_utf8),1),2),(CAST('z' AS rust_utf8),(1,2))", 0,0,0,0,OB_SUCCESS,OB_ERR_INVALID_COLUMN_NUM},
      Case{"((seekdb_rust_text('z'),1),2)", "((7,1),2)", 0,0,0,0,OB_SUCCESS,OB_ERR_INVALID_TYPE_FOR_OP},
      Case{"((1,2),3)", "((1,2),3),((1,2),4)", 0,0,0,0,OB_SUCCESS,OB_ERR_INVALID_COLUMN_NUM,false},
      Case{"(1,2)", "(1,2),(3,4)", 1,0,0,0,OB_SUCCESS,OB_SUCCESS,false},
      Case{"(NULL,1)", oversized.c_str(), -1,0,0,0,OB_SUCCESS,OB_SUCCESS,false}}) {
    const std::string sql = std::string(test.left) + (negate ? " NOT IN (" : " IN (") + test.list + ")";
    ObSqlCtx sql_context; sql_context.session_info_ = &session;
    ObExecContext execution(arena); execution.set_my_session(&session); execution.set_sql_ctx(&sql_context);
    CHECK(execution.create_physical_plan_ctx() == OB_SUCCESS);
    ObSQLSessionInfo::ExecCtxSessionRegister register_execution(session, &execution);
    const ParseNode *node = nullptr;
    CHECK(ObRawExprUtils::parse_expr_node_from_str(ObString(sql.size(), sql.data()), session.get_charsets4parser(),
        arena, node, session.get_sql_mode()) == OB_SUCCESS && node);
    ObSEArray<ObQualifiedName, 1> columns;
    ObSEArray<ObVarInfo, 1> variables;
    ObSEArray<ObAggFunRawExpr *, 1> aggregates;
    ObSEArray<ObWinFunRawExpr *, 1> windows;
    ObSEArray<ObSubQueryInfo, 1> subqueries;
    ObSEArray<ObUDFInfo, 1> udfs;
    ObSEArray<ObOpRawExpr *, 1> operators;
    ObRawExpr *raw = nullptr;
    const int comparisons = provider.comparisons_, functions = provider.functions_, casts = provider.casts_;
    int bound = ObRawExprUtils::build_raw_expr(factory, session, *node, raw, columns,
        variables, aggregates, windows, subqueries, udfs, operators);
    if (bound == OB_SUCCESS && raw) bound = raw->formalize(&session);
    if (bound != test.bind_status) std::cerr << "Rust row IN bind=" << bound << " sql=" << sql << std::endl;
    CHECK(bound == test.bind_status);
    CHECK(provider.comparisons_ == comparisons && provider.functions_ == functions && provider.casts_ == casts);
    if (bound != OB_SUCCESS) {
      ObSQLSessionInfo::ExecCtxSessionRegister unregister_execution(session, nullptr);
      continue;
    }
    CHECK(raw && columns.empty() && !raw->get_plugin_type());
    if (test.custom) CHECK(raw->get_expr_type() == (negate ? T_OP_NE : T_OP_EQ) && raw->has_flag(CNT_STATE_FUNC));
    const int resolves = provider.resolves_;
    ObRawExpr *copy = nullptr;
    CHECK(ObRawExprCopier::copy_expr(factory, raw, copy) == OB_SUCCESS && copy);
    CHECK(copy->deduce_type(&session) == OB_SUCCESS && provider.resolves_ == resolves);
    bool transformed = false;
    CHECK(ObTransformPreProcess::transform_expr(factory, session, copy, transformed) == OB_SUCCESS);
    ObStaticEngineExprCG generator(arena, &session, nullptr, 0, 0);
    ObRawExprUniqueSet roots(false); CHECK(roots.append(copy) == OB_SUCCESS);
    ObExprFrameInfo frame(arena);
    const int generated = generator.generate(roots, frame);
    if (generated != OB_SUCCESS) std::cerr << "Rust row IN CG=" << generated << " sql=" << sql << std::endl;
    CHECK(generated == OB_SUCCESS);
    CHECK(execution.init_expr_op(frame.need_ctx_cnt_) == OB_SUCCESS);
    CHECK(frame.pre_alloc_exec_memory(execution) == OB_SUCCESS);
    ObEvalCtx eval(execution); ObExpr *root = nullptr;
    ObSEArray<ObRawExpr *, 1> outputs;
    CHECK(ObStaticEngineExprCG::generate_rt_expr(*copy, outputs, root) == OB_SUCCESS && root);
    ObDatum *result = nullptr;
    const int evaluated = root->eval(eval, result);
    if (evaluated != test.status) std::cerr << "Rust row IN eval=" << evaluated << " sql=" << sql << std::endl;
    CHECK(evaluated == test.status);
    if (evaluated == OB_SUCCESS) {
      CHECK(result);
      if (test.result < 0) CHECK(result->is_null());
      else CHECK(!result->is_null() && result->get_int() == (negate ? !test.result : test.result));
    }
    if (provider.comparisons_ != comparisons + test.comparisons || provider.functions_ != functions + test.functions ||
        provider.casts_ != casts + test.casts) std::cerr << "Rust row IN calls=" << provider.comparisons_ - comparisons << ","
          << provider.functions_ - functions << "," << provider.casts_ - casts << " sql=" << sql << std::endl;
    CHECK(provider.comparisons_ == comparisons + test.comparisons && provider.functions_ == functions + test.functions &&
        provider.casts_ == casts + test.casts && provider.resolves_ == resolves);
    ObPluginStatusSnapshot module;
    CHECK(loader.get_status("org.seekdb.rust-text", module) == OB_SUCCESS && module.lease_count_ == 0);
    ObSQLSessionInfo::ExecCtxSessionRegister unregister_execution(session, nullptr);
  }
}
} // namespace rust_row_in_test
#endif
