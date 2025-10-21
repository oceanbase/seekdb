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

#ifndef OCEANBASE_SQL_RESOLVER_MV_OB_MV_PROVIDER_H_
#define OCEANBASE_SQL_RESOLVER_MV_OB_MV_PROVIDER_H_
#include "share/scn.h"
#include "lib/string/ob_string.h"
#include "lib/hash_func/ob_hash_func.h"
#include "lib/container/ob_se_array.h"
#include "sql/resolver/expr/ob_raw_expr.h"
#include "sql/resolver/mv/ob_mv_checker.h"
#include "sql/resolver/mv/ob_mv_printer.h"

namespace oceanbase
{
namespace sql
{

class ObMVProvider
{
public:
  explicit ObMVProvider(int64_t tenant_id, uint64_t mview_id)
    : inner_alloc_("MVProvider", OB_MALLOC_NORMAL_BLOCK_SIZE, tenant_id, ObCtxIds::DEFAULT_CTX_ID),
      mview_id_(mview_id),
      inited_(false),
      refreshable_type_(OB_MV_REFRESH_INVALID),
      operators_(&inner_alloc_),
      dependency_infos_(&inner_alloc_),
      fast_refreshable_note_()
    {}
  ~ObMVProvider() {}
  static int check_mv_refreshable(const uint64_t tenant_id,
                                  const uint64_t mview_id,
                                  ObSQLSessionInfo *session_info,
                                  ObSchemaGetterGuard *schema_guard,
                                  bool &can_fast_refresh,
                                  FastRefreshableNotes &note);
  static int get_real_time_mv_expand_view(const uint64_t tenant_id,
                                          const uint64_t mview_id,
                                          ObSQLSessionInfo *session_info,
                                          ObSchemaGetterGuard *schema_guard,
                                          ObIAllocator &alloc,
                                          ObString &expand_view,
                                          bool &is_major_refresh_mview);
  static int get_complete_refresh_mview_str(const ObTableSchema &mv_schema,
                                            ObSQLSessionInfo &session_info,
                                            ObSchemaGetterGuard &schema_guard,
                                            const share::SCN *mv_refresh_scn,
                                            const share::SCN *table_refresh_scn,
                                            ObIAllocator &str_alloc,
                                            ObString &mview_str);
  int get_mlog_mv_refresh_infos(ObSQLSessionInfo *session_info,
                                ObSchemaGetterGuard *schema_guard,
                                const share::SCN &last_refresh_scn,
                                const share::SCN &refresh_scn,
                                const share::SCN *mv_last_refresh_scn,
                                const share::SCN *mv_refresh_scn,
                                ObIArray<ObDependencyInfo> &dep_infos,
                                bool &can_fast_refresh,
                                const ObIArray<ObString> *&operators);
  int get_major_refresh_operators(ObSQLSessionInfo *session_info,
                                  ObSchemaGetterGuard *schema_guard,
                                  const share::SCN &last_refresh_scn,
                                  const share::SCN &refresh_scn,
                                  const int64_t part_idx,
                                  const int64_t sub_part_idx,
                                  const ObNewRange &range,
                                  const ObIArray<ObString> *&operators);
  OB_INLINE const common::ObIArray<ObString> &get_operators() const
  { return operators_; }
  OB_INLINE const ObSqlString &get_error_str() const  { return fast_refreshable_note_.error_; }
  static int check_mview_dep_session_vars(const ObTableSchema &mv_schema,
                                          const ObSQLSessionInfo &session,
                                          const bool gen_error,
                                          bool &is_vars_matched);
  OB_INLINE bool is_major_refresh_mview()
  { return ObMVRefreshableType::OB_MV_FAST_REFRESH_MAJOR_REFRESH_MJV == refreshable_type_; }
  static int transform_mv_def_stmt(ObDMLStmt *&mv_def_stmt,
                                   ObIAllocator *allocator,
                                   ObSchemaChecker *schema_checker,
                                   ObSQLSessionInfo *session_info,
                                   ObRawExprFactory *expr_factory,
                                   ObStmtFactory *stmt_factory);
private:
  int init_mv_provider(ObSQLSessionInfo *session_info,
                       ObSchemaGetterGuard *schema_guard,
                       ObMVPrinterRefreshInfo *refresh_info,
                       const bool check_refreshable_only);
  int check_mv_column_type(const ObTableSchema *mv_schema, const ObSelectStmt *view_stmt,
                           ObSQLSessionInfo &session);
  int check_mv_column_type(const ObColumnSchemaV2 &org_column, const ObColumnSchemaV2 &cur_column);
  int check_column_type_and_accuracy(const ObColumnSchemaV2 &org_column,
                                     const ObColumnSchemaV2 &cur_column,
                                     bool &is_match);
  static int generate_mv_stmt(ObIAllocator &alloc,
                              ObStmtFactory &stmt_factory,
                              ObRawExprFactory &expr_factory,
                              ObSchemaChecker &schema_checker,
                              ObSQLSessionInfo &session_info,
                              const ObTableSchema &mv_schema,
                              ObSelectStmt *&view_stmt);
  int pre_process_view_stmt(ObSelectStmt &view_stmt);
  int print_mv_operators(ObMVPrinterCtx &mv_printer_ctx,
                         ObMVChecker &checker,
                         const ObTableSchema &mv_schema,
                         const ObTableSchema &mv_container_schema,
                         const ObSelectStmt &mv_def_stmt,
                         ObIArray<ObString> &operators);
  static int get_trans_rule_set(const ObDMLStmt *mv_def_stmt,
                                uint64_t &rule_set);
  TO_STRING_KV(K_(mview_id), K_(inited), K_(refreshable_type),
                K_(operators), K_(dependency_infos));

private:
  common::ObArenaAllocator inner_alloc_;
  const uint64_t mview_id_;
  bool inited_;
  ObMVRefreshableType refreshable_type_;
  common::ObFixedArray<ObString, common::ObIAllocator> operators_;  // refresh or real time access operator for mv
  common::ObFixedArray<ObDependencyInfo, common::ObIAllocator> dependency_infos_;
  FastRefreshableNotes fast_refreshable_note_;
};

}
}

#endif
