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

#ifndef OCEANBASE_STORAGE_DDL_MERGE_HELPER_
#define OCEANBASE_STORAGE_DDL_MERGE_HELPER_

#include "share/scn.h"
#include "storage/meta_mem/ob_tablet_handle.h"
#include "observer/scheduler/ob_dag_scheduler.h"
#include "storage/blocksstable/index_block/ob_index_block_builder.h"
#include "storage/blocksstable/ob_macro_block_struct.h"
#include "storage/ddl/ob_ddl_struct.h"
#include "storage/ddl/ob_tablet_ddl_kv.h"
#include "storage/tablet/ob_tablet.h"
#include "storage/blocksstable/ob_macro_block_struct.h"
#include "storage/ddl/ob_tablet_ddl_kv_mgr.h"
#include "storage/ddl/ob_direct_load_struct.h"
namespace oceanbase
{
namespace storage
{
class ObSNDDLMergeHelperV2;
struct ObDDLSliceRange
{
  ObDDLSliceRange() : start_slice_idx_(-1), end_slice_idx_(-1) {}
  ObDDLSliceRange(const int64_t start_slice_idx, const int64_t end_slice_idx)
    : start_slice_idx_(start_slice_idx), end_slice_idx_(end_slice_idx) {}
  bool is_valid() const { return start_slice_idx_ >= 0 && end_slice_idx_ >= start_slice_idx_; }
  TO_STRING_KV(K(start_slice_idx_), K(end_slice_idx_));

  int64_t start_slice_idx_;
  int64_t end_slice_idx_;
};

class ObIDDLMergeHelper
{
public:
  static int get_merge_helper(ObIAllocator &allocator, 
                              const ObDirectLoadType direct_load_type,
                              ObIDDLMergeHelper *&helper);
  static int get_rec_scn_from_ddl_kvs(ObDDLTabletMergeDagParamV2 &merge_param);
  static int remove_tablet_from_log_handler(const ObTabletID &tablet_id);
public:
/* interface used for prpare_task*/
  ObIDDLMergeHelper() {}
  virtual ~ObIDDLMergeHelper() {}
  /*
  * process_prepare_task will generate the following task for merge single tablet, actions are follwings
  * 1. check majaor & freeze ddl kv
  * 2. calc slice info, defiene number for merge slice task & assemble task
  * 3. generate merge_slice_task & assemble task
  */
  virtual int check_need_merge(ObIDag *dag,
                               ObDDLTabletMergeDagParamV2 &ddl_merge_param,
                               bool &need_merge)
  {
    int ret = OB_SUCCESS;
    need_merge = true;
    return ret;
  }
  virtual int process_prepare_task(ObIDag *dag,
                                   ObDDLTabletMergeDagParamV2 &ddl_merge_param,
                                   ObIArray<ObDDLSliceRange> &slice_ranges) = 0;
  virtual int merge_slice(ObIDag* dag,
                          ObDDLTabletMergeDagParamV2 &merge_param,
                          const int64_t start_slice,
                          const int64_t end_slice)
  { return OB_NOT_SUPPORTED; }
  virtual int assemble_sstable(ObDDLTabletMergeDagParamV2 &param)
  { return OB_NOT_SUPPORTED; }
  virtual int freeze_ddl_kv(ObDDLTabletMergeDagParamV2 &param);

  virtual int prepare_ddl_param(const ObDDLTabletMergeDagParamV2 &merge_param,
                                ObTabletDDLParam &ddl_param);
  virtual int prepare_ddl_param(const ObDDLTabletMergeDagParamV2 &merge_param,
                                const int64_t start_slice_idx,
                                const int64_t end_slice_idx,
                                ObTabletDDLParam &ddl_param);
  virtual int get_rec_scn(ObDDLTabletMergeDagParamV2 &merge_param)
  { return OB_NOT_SUPPORTED; }
protected:
  virtual bool is_supported_direct_load_type(const ObDirectLoadType direct_load_type) 
  { return OB_NOT_SUPPORTED; }
public:
    TO_STRING_KV(KP(this));
};

class ObSNDDLMergeHelperV2: public ObIDDLMergeHelper
{
public:
  ObSNDDLMergeHelperV2() { };
  virtual ~ObSNDDLMergeHelperV2() {}
  int process_prepare_task(ObIDag *dag,
                           ObDDLTabletMergeDagParamV2 &ddl_merge_param,
                           ObIArray<ObDDLSliceRange> &slice_ranges) override;
  int merge_slice(ObIDag* dag,
                  ObDDLTabletMergeDagParamV2 &merge_param,
                  const int64_t start_slice,
                  const int64_t end_slice) override;
  int assemble_sstable(ObDDLTabletMergeDagParamV2 &param) override;

  int get_rec_scn(ObDDLTabletMergeDagParamV2 &merge_param) override;

protected:
  bool is_supported_direct_load_type(const ObDirectLoadType direct_load_type) override ;
  int set_ddl_complete(ObIDag *dag, ObTablet &tablet, ObDDLTabletMergeDagParamV2 &ddl_merge_param);
};

} // namespace storage
} // namespace oceanbase

#endif
