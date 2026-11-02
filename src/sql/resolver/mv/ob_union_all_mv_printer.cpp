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

#define USING_LOG_PREFIX SQL_RESV
#include "sql/resolver/mv/ob_union_all_mv_printer.h"
#include "sql/optimizer/ob_optimizer_util.h"
#include "sql/resolver/mv/ob_simple_mav_printer.h"
#include "sql/resolver/mv/ob_simple_mjv_printer.h"
#include "sql/resolver/mv/ob_simple_join_mav_printer.h"

namespace oceanbase
{
using namespace common;
namespace sql
{

int ObUnionAllMVPrinter::gen_refresh_dmls(ObIArray<ObDMLStmt*> &dml_stmts)
{
  int ret = OB_SUCCESS;
  dml_stmts.reuse();
  ObSEArray<ObDMLStmt*, 8> cur_dml_stmts;
  for (int64_t i = 0; OB_SUCC(ret) && i < child_refresh_types_.count(); ++i) {
    cur_dml_stmts.reuse();
    if (OB_ISNULL(mv_def_stmt_.get_set_query(i))) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("unexpected null", K(ret), K(i));
    } else if (OB_FAIL(gen_child_refresh_dmls(child_refresh_types_.at(i),
                                              *mv_def_stmt_.get_set_query(i),
                                              cur_dml_stmts))) {
      LOG_WARN("failed to get child refresh dmls", K(ret), K(i), K(child_refresh_types_.at(i)),
                                          KPC(mv_def_stmt_.get_set_query(i)));
    } else if (OB_FAIL(append(dml_stmts, cur_dml_stmts))) {
      LOG_WARN("failed to append dml stmts", K(ret));
    }
  }
  return ret;
}

int ObUnionAllMVPrinter::gen_real_time_view(ObSelectStmt *&sel_stmt)
{
  int ret = OB_SUCCESS;
  sel_stmt = NULL;
  return OB_NOT_SUPPORTED;
}
    
int ObUnionAllMVPrinter::gen_child_refresh_dmls(const ObMVRefreshableType refresh_type,
                                                const ObSelectStmt &child_sel_stmt,
                                                ObIArray<ObDMLStmt*> &dml_stmts)
{
  int ret = OB_SUCCESS;
  dml_stmts.reuse();
  if (OB_ISNULL(mlog_tables_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected null", K(ret), K(mlog_tables_));
  } else {
    ctx_.marker_idx_ = marker_idx_;
    switch (refresh_type) {
      case OB_MV_FAST_REFRESH_SIMPLE_MAV: {
        ObSimpleMAVPrinter printer(ctx_, mv_schema_, mv_container_schema_, child_sel_stmt, *mlog_tables_, expand_aggrs_);
        if (OB_FAIL(printer.gen_child_refresh_dmls_for_union_all(marker_idx_, dml_stmts))) {
          LOG_WARN("failed to gen child refresh dmls for union all", K(ret));
        }
        break;
      }
      case OB_MV_FAST_REFRESH_SIMPLE_MJV: {
        ObSimpleMJVPrinter printer(ctx_, mv_schema_, mv_container_schema_, child_sel_stmt, *mlog_tables_);
        if (OB_FAIL(printer.gen_child_refresh_dmls_for_union_all(marker_idx_, dml_stmts))) {
          LOG_WARN("failed to gen child refresh dmls for union all", K(ret));
        }
        break;
      }
      case OB_MV_FAST_REFRESH_SIMPLE_JOIN_MAV: {
        ObSimpleJoinMAVPrinter printer(ctx_, mv_schema_, mv_container_schema_, child_sel_stmt, *mlog_tables_, expand_aggrs_);
        if (OB_FAIL(printer.gen_child_refresh_dmls_for_union_all(marker_idx_, dml_stmts))) {
          LOG_WARN("failed to gen child refresh dmls for union all", K(ret));
        }
        break;
      }
      default:  {
        ret = OB_NOT_SUPPORTED;
        LOG_WARN("not supported child refresh type for union all", K(ret), K(refresh_type));
        break;
      }
    }
    ctx_.marker_idx_ = OB_INVALID_INDEX;
  }
  return ret;
}

}//end of namespace sql
}//end of namespace oceanbase
