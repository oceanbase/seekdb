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

#include "storage/multi_data_source/ob_tablet_mds_merge_ctx.h"

#include "storage/tablet/ob_mds_schema_helper.h"
#include "storage/multi_data_source/ob_mds_minor_compaction_filter.h"

#define USING_LOG_PREFIX MDS

using namespace oceanbase::common;
using namespace oceanbase::share;
using namespace oceanbase::compaction;

namespace oceanbase
{
namespace storage
{
ObTabletMdsMinorMergeCtx::ObTabletMdsMinorMergeCtx(ObTabletMergeDagParam &param, ObArenaAllocator &allocator)
    : ObTabletExeMergeCtx(param, allocator)
{
}

int ObTabletMdsMinorMergeCtx::prepare_schema()
{
  int ret = OB_SUCCESS;
  static_param_.schema_ = ObMdsSchemaHelper::get_instance().get_storage_schema();
  return ret;
}

int ObTabletMdsMinorMergeCtx::prepare_index_tree()
{
  return ObBasicTabletMergeCtx::build_index_tree(
      merge_info_,
      ObMdsSchemaHelper::get_instance().get_rowkey_read_info());
}

void ObTabletMdsMinorMergeCtx::free_schema()
{
  static_param_.schema_ = nullptr;
}

int ObTabletMdsMinorMergeCtx::get_merge_tables(ObGetMergeTablesResult &get_merge_table_result)
{
  int ret = OB_SUCCESS;
  void *buf = nullptr;
  if (OB_FAIL(get_tables_by_key(get_merge_table_result))) {
  } else if (OB_ISNULL(buf = mem_ctx_.alloc(sizeof(ObMdsMinorFilter)))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("fail to alloc memory", K(ret), "size", sizeof(ObMdsMinorFilter));
  } else {
    // prepare mds compaction filter
    ObMdsMinorFilter *compaction_filter = new (buf) ObMdsMinorFilter();
    const int64_t last_major_snapshot = get_tablet()->get_last_major_snapshot_version();
    const int64_t multi_version_start = get_tablet()->get_multi_version_start();
    int tmp_ret = OB_SUCCESS;
    if (OB_TMP_FAIL(compaction_filter->init(last_major_snapshot, multi_version_start))) {
    } else {
      filter_ctx_.compaction_filter_ = compaction_filter;
      FLOG_INFO("success to init mds compaction filter", K(tmp_ret), K(last_major_snapshot), K(multi_version_start));
    }

    if (OB_TMP_FAIL(tmp_ret)) {
      if (OB_NOT_NULL(buf)) {
        mem_ctx_.free(buf);
        buf = nullptr;
      }
    }
  }
  return ret;
}


int ObTabletMdsMinorMergeCtx::update_tablet(ObTabletHandle &new_tablet_handle)
{
  int ret = OB_SUCCESS;
  const ObSSTable *sstable = nullptr;
  if (OB_FAIL(create_sstable(sstable))) {
  } else {
    ObUpdateTableStoreParam mds_param(static_param_.version_range_.snapshot_version_,
                                      1/*multi_version_start*/,
                                      ObMdsSchemaHelper::get_instance().get_storage_schema(),
                                      sstable,
                                      false/*allow_duplicate_sstable*/);
    if (OB_FAIL(mds_param.init_with_compaction_info(
      ObCompactionTableStoreParam(get_merge_type(), sstable->get_end_scn()/*clog_checkpoint_scn*/, false/*need_report*/, false/*has_truncate_info*/)))) {
    } else if (OB_FAIL(get_ls()->update_tablet_table_store(get_tablet_id(), mds_param, new_tablet_handle))) {
      LOG_ERROR("failed to update tablet table store", K(ret), K(mds_param), K(new_tablet_handle));
      CTX_SET_DIAGNOSE_LOCATION(*this);
    } else {
      LOG_INFO("success to update tablet table store with mds table", K(sstable), K(new_tablet_handle));
      time_guard_click(ObStorageCompactionTimeGuard::UPDATE_TABLET);
    }
  }
  return ret;
}

} // namespace storage
} // namespace oceanbase
