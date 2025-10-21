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

#ifndef OB_STORAGE_OB_COLUMN_ORIENTED_MERGER_H_
#define OB_STORAGE_OB_COLUMN_ORIENTED_MERGER_H_

#include "storage/compaction/ob_partition_merger.h"
#include "storage/blocksstable/ob_macro_block_writer.h"
#include "storage/compaction/ob_partition_merge_iter.h"
#include "storage/blocksstable/ob_sstable.h"
#include "ob_co_merge_dag.h"
#include "ob_co_merge_writer.h"
#include "storage/column_store/ob_column_oriented_sstable.h"
#include "storage/column_store/ob_co_merge_ctx.h"

namespace oceanbase
{
namespace storage
{
class ObCOSSTableV2;
}

namespace compaction
{
class ObCOMinorSSTableMergeHelper : public ObPartitionMergeHelper
{
public:
  ObCOMinorSSTableMergeHelper(
      const ObITableReadInfo &read_info,
      const int64_t co_major_snapshot_version,
      ObIAllocator &allocator)
    : ObPartitionMergeHelper(read_info, allocator),
      co_major_snapshot_version_(co_major_snapshot_version)
  {}
  virtual ~ObCOMinorSSTableMergeHelper() {}
  virtual ObPartitionMergeIter *alloc_merge_iter(const ObMergeParameter &merge_param, const ObITable *table) override
  {
    UNUSEDx(merge_param, table);
    return alloc_helper<ObPartitionRowMergeIter> (allocator_, allocator_);
  }
  virtual OB_INLINE bool is_co_major_helper() const { return true; }

private:
  const int64_t co_major_snapshot_version_;
};

typedef oceanbase::common::ObSEArray<oceanbase::compaction::ObCOMergeWriter *, DEFAULT_CG_MERGE_BATCH_SIZE, common::ObIAllocator&> MERGEWRITERS;

class ObCOMerger : public ObMerger
{
public:
  ObCOMerger(
    compaction::ObLocalArena &allocator,
    const ObStaticMergeParam &static_param,
    const uint32_t start_cg_idx,
    const uint32_t end_cg_idx,
    const bool only_use_row_table = false);
	virtual ~ObCOMerger() { reset(); };
  virtual void reset();
  virtual int merge_partition(ObBasicTabletMergeCtx &ctx, const int64_t idx) override;
  void set_merge_progress(ObPartitionMergeProgress *merge_progress) { merge_progress_ = merge_progress; }
  INHERIT_TO_STRING_KV("ObCOMerger", ObMerger, KPC_(row_store_iter), K_(merge_writers), K_(start_cg_idx), K_(end_cg_idx))
protected:
  int move_iter_next(ObPartitionMergeIter &iter);
  int compare(const blocksstable::ObDatumRow &left, ObPartitionMergeIter &row_store_iter_, int64_t &cmp_ret);
  virtual int inner_close() override;
private:
  virtual int inner_prepare_merge(ObBasicTabletMergeCtx &ctx, const int64_t idx) override;
  int init_base_merge_iter(ObSSTable *sstable);
  int init_cg_writers(ObSSTable *sstable);
  bool is_empty_table(const ObSSTable &sstable) const;
  int build_mergelog(const blocksstable::ObDatumRow &row, ObMergeLog &merge_log, bool &need_replay, bool &row_store_iter_need_move);
  int replay_merglog(const ObMergeLog &merge_log, const blocksstable::ObDatumRow &row);
  int alloc_writers(
      const blocksstable::ObDatumRow &default_row,
      const common::ObIArray<ObStorageColumnGroupSchema> &cg_array,
      ObTabletMergeInfo **merge_infos,
      ObSSTable &sstable);
  int alloc_single_writer(
      const blocksstable::ObDatumRow &default_row,
      const common::ObIArray<ObStorageColumnGroupSchema> &cg_array,
      ObTabletMergeInfo **merge_infos,
      ObSSTable &sstable);
  int alloc_row_writers(
      const blocksstable::ObDatumRow &default_row,
      const common::ObIArray<ObStorageColumnGroupSchema> &cg_array,
      ObTabletMergeInfo **merge_infos,
      ObSSTable &sstable);
  int write_residual_data();
private:
  ObPartitionMergeIter *row_store_iter_;
  ObPartitionMergeProgress *merge_progress_;
  MERGEWRITERS merge_writers_;
  ObSEArray<storage::ObSSTableWrapper, 16, common::ObIAllocator&> cg_wrappers_;
  ObPartitionMergeLoserTreeCmp *cmp_;
  const uint32_t start_cg_idx_;
  const uint32_t end_cg_idx_;
  bool only_use_row_table_;
};


} //compaction
} //oceanbase


#endif
