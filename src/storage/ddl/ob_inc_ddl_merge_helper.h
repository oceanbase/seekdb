/**
 * Copyright (c) 2021 OceanBase
 * OceanBase CE is licensed under Mulan PubL v2.
 * You can use this software according to the terms and conditions of the Mulan PubL v2.
 * You may obtain a copy of Mulan PubL v2 at:
 *          http://license.coscl.org.cn/MulanPubL-2.0
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
 * EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
 * MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PubL v2 for more details.
 */

#ifndef OCEANBASE_STORAGE_INC_DDL_MERGE_HELPER_
#define OCEANBASE_STORAGE_INC_DDL_MERGE_HELPER_

#include "share/scn.h"
#include "storage/meta_mem/ob_tablet_handle.h"
#include "share/scheduler/ob_tenant_dag_scheduler.h"
#include "storage/blocksstable/index_block/ob_index_block_builder.h"
#include "storage/blocksstable/ob_macro_block_struct.h"
#include "storage/ddl/ob_ddl_struct.h"
#include "storage/ddl/ob_tablet_ddl_kv.h"
#include "storage/tablet/ob_tablet.h"
#include "storage/blocksstable/ob_macro_block_struct.h"
#include "storage/ddl/ob_tablet_ddl_kv_mgr.h"
#include "storage/ddl/ob_direct_load_struct.h"
#include "storage/ddl/ob_ddl_merge_helper.h"
namespace oceanbase
{
namespace storage
{
class ObIncMinDDLMergeHelper: public ObIDDLMergeHelper
{
public:
  int process_prepare_task(ObIDag *dag,
                           ObDDLTabletMergeDagParamV2 &ddl_merge_param,
                           ObIArray<ObTuple<int64_t, int64_t, int64_t>> &cg_slices) override;
  int merge_cg_slice(ObIDag* dag,
                     ObDDLTabletMergeDagParamV2 &merge_param,
                     const int64_t cg_idx,
                     const int64_t start_slice,
                     const int64_t end_slice) override;
  int assemble_sstable(ObDDLTabletMergeDagParamV2 &param) override;
  int get_rec_scn(ObDDLTabletMergeDagParamV2 &merge_param) override;
private:
  bool is_supported_direct_load_type(const ObDirectLoadType direct_load_type) override
  {
    return ObDirectLoadType::DIRECT_LOAD_INCREMENTAL == direct_load_type;
  }
};

} // namespace storage
} // namespace oceanbase

#endif