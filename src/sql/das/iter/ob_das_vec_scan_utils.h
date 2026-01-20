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

#ifndef OB_DAS_VEC_SCAN_UTILS_H_
#define OB_DAS_VEC_SCAN_UTILS_H_

#include "sql/das/iter/ob_das_iter_define.h"
#include "sql/das/iter/ob_das_scan_iter.h"
#include "sql/das/ob_das_attach_define.h"
#include "sql/das/ob_das_vec_define.h"
#include "sql/engine/expr/ob_expr_vector.h"

namespace oceanbase
{
namespace sql
{
class ObDasVecScanUtils
{
public:
  static const uint64_t MAX_BRUTE_FORCE_SIZE = 1;
  static int set_lookup_key(ObRowkey &rowkey, ObTableScanParam &scan_param, uint64_t table_id);
  static int set_lookup_range(const ObNewRange &look_range, ObTableScanParam &scan_param, uint64_t table_id);
  static void release_scan_param(ObTableScanParam &scan_param);
  static void set_whole_range(ObNewRange &scan_range, common::ObTableID table_id);
  static int get_distance_expr_type(ObExpr &expr, ObEvalCtx &ctx, ObExprVectorDistance::ObVecDisType &dis_type);
  static int get_real_search_vec(common::ObIAllocator &allocator,
                                 ObDASSortRtDef *sort_rtdef,
                                 ObExpr *origin_vec,
                                 ObString &real_search_vec);
  static int init_limit(const ObDASVecAuxScanCtDef *ir_ctdef,
                        ObDASVecAuxScanRtDef *ir_rtdef,
                        const ObDASSortCtDef *sort_ctdef,
                        ObDASSortRtDef *sort_rtdef,
                        common::ObLimitParam &limit_param);
  static int init_sort(const ObDASVecAuxScanCtDef *ir_ctdef,
                       ObDASVecAuxScanRtDef *ir_rtdef,
                       const ObDASSortCtDef *sort_ctdef,
                       ObDASSortRtDef *sort_rtdef,
                       const common::ObLimitParam &limit_param,
                       ObExpr *&search_vec,
                       ObExpr *&distance_calc);
  static int init_sort_of_hybrid_index(ObIAllocator &allocator,
                                       const ObDASVecAuxScanCtDef *ir_ctdef,
                                       const ObDASSortCtDef *sort_ctdef,
                                       ObDASSortRtDef *sort_rtdef,
                                       ObString &hybrid_search_vec,
                                       ObExpr *&distance_calc);
  static int reuse_iter(const share::ObLSID &ls_id,
                        ObDASScanIter *iter,
                        ObTableScanParam &scan_param,
                        const ObTabletID tablet_id);
  static int init_scan_param(const share::ObLSID &ls_id,
                             const common::ObTabletID &tablet_id,
                             const ObDASScanCtDef *ctdef,
                             ObDASScanRtDef *rtdef,
                             transaction::ObTxDesc *tx_desc,
                             transaction::ObTxReadSnapshot *snapshot,
                             ObTableScanParam &scan_param,
                             bool is_get = true,
                             ObIAllocator *allocator = nullptr);
  static int init_vec_aux_scan_param(const share::ObLSID &ls_id,
                                      const common::ObTabletID &tablet_id,
                                      const sql::ObDASScanCtDef *ctdef,
                                      sql::ObDASScanRtDef *rtdef,
                                      transaction::ObTxDesc *tx_desc,
                                      transaction::ObTxReadSnapshot *snapshot,
                                      ObTableScanParam &scan_param,
                                      bool is_get = false,
                                      ObIAllocator *scan_allocator = nullptr);
  static int get_rowkey(ObIAllocator &allocator, const ObDASScanCtDef * ctdef, ObDASScanRtDef *rtdef, ObRowkey *&rowkey);
  
  static int get_distance_threshold_hnsw(ObExpr &expr,
                                         float &similarity_threshold,
                                         float &distance_threshold);
  static int check_ivf_support_similarity_threshold(ObExpr &expr);
};

}  // namespace sql
}  // namespace oceanbase
#endif
