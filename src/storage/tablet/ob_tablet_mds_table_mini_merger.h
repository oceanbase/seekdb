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

#ifndef OCEANBASE_STORAGE_TABLET_OB_MDS_TABLE_MINI_MERGER_H_
#define OCEANBASE_STORAGE_TABLET_OB_MDS_TABLE_MINI_MERGER_H_

#include "share/scn.h"
#include "storage/blocksstable/ob_macro_block_writer.h"
#include "storage/compaction/ob_tablet_merge_ctx.h"
#include "storage/blocksstable/ob_row_queue.h"

namespace oceanbase
{
namespace compaction
{
class ObTabletMergeCtx;
}

namespace storage
{

class ObMdsMergeMultiVersionRowStore
{
public:
  ObMdsMergeMultiVersionRowStore();
  ~ObMdsMergeMultiVersionRowStore() = default;
public:
  int init(const ObDataStoreDesc &data_store_desc, blocksstable::ObMacroBlockWriter &macro_writer);
  int finish();
  int put_row_into_queue(const blocksstable::ObDatumRow &row);
private:
  int put_same_rowkey_row_into_queue(const blocksstable::ObDatumRow &row, const ObDatumRow &last_row_in_qu);
  int dump_shadow_row();
  int dump_row_queue();
private:
  // compare row key
  const ObDataStoreDesc *data_store_desc_;
  blocksstable::ObMacroBlockWriter *macro_writer_;
  common::ObArenaAllocator row_queue_allocator_;
  blocksstable::ObDatumRow shadow_row_;
  blocksstable::ObDatumRowkey cur_key_;
  blocksstable::ObDatumRowkey last_key_;

  ObRowQueue row_queue_;
  bool is_inited_;
};

class ObMdsMiniMergeOperator
{
public:
  ObMdsMiniMergeOperator();
  virtual ~ObMdsMiniMergeOperator() = default;
public:
  virtual int init(
      const ObDataStoreDesc &data_store_desc,
      blocksstable::ObMacroBlockWriter &macro_writer);
  virtual int finish() { return row_store_.finish(); }
  virtual bool for_flush() = 0;
  virtual int operator()(const mds::MdsDumpKV &kv) = 0;
protected:
  bool is_inited_;
  ObMdsMergeMultiVersionRowStore row_store_;
  common::ObArenaAllocator cur_allocator_;
  blocksstable::ObDatumRow cur_row_;
};

class ObTabletDumpMds2MiniOperator : public ObMdsMiniMergeOperator
{
public:
  ObTabletDumpMds2MiniOperator() = default;
  virtual ~ObTabletDumpMds2MiniOperator() = default;
protected:
  virtual bool for_flush() override { return true; }
  virtual int operator()(const mds::MdsDumpKV &kv) override;
};

// last_rowkey.invalid | cur_row is First
// last_rowkey same as cur_row | cur_row ...
// last_rowkey not same as cur row | cur_row is First | last_row is Last

class ObCrossLSMdsMiniMergeOperator : public ObMdsMiniMergeOperator
{
public:
  explicit ObCrossLSMdsMiniMergeOperator(const share::SCN &scan_end_scn);
  virtual ~ObCrossLSMdsMiniMergeOperator() = default;
protected:
  virtual bool for_flush() override { return false; }
  virtual int operator()(const mds::MdsDumpKV &kv) override;
private:
  share::SCN scan_end_scn_;
};

// to query all medium mds info, and dump them to minor sstable.
class ObTabletDumpMediumMds2MiniOperator : public ObMdsMiniMergeOperator
{
public:
  ObTabletDumpMediumMds2MiniOperator() = default;
  virtual ~ObTabletDumpMediumMds2MiniOperator() = default;
  virtual int operator()(const mds::MdsDumpKV &kv) override;
protected:
  virtual bool for_flush() override { return true; }
};
 

class ObMdsTableMiniMerger
{
public:
  ObMdsTableMiniMerger();
  ~ObMdsTableMiniMerger() { reset(); }
  void reset();

  int init(compaction::ObTabletMergeCtx &ctx, ObMdsMiniMergeOperator &op);
  int generate_mds_mini_sstable(common::ObArenaAllocator &allocator, ObTableHandleV2 &table_handle);

  TO_STRING_KV(K_(is_inited), KPC_(ctx), K_(data_desc), K_(macro_writer), K_(sstable_builder));

private:
  ObArenaAllocator allocator_;
  ObWholeDataStoreDesc data_desc_;
  blocksstable::ObMacroBlockWriter macro_writer_;
  ObSSTableIndexBuilder sstable_builder_;
  compaction::ObTabletMergeCtx *ctx_;
  const ObStorageSchema *storage_schema_;
  bool is_inited_;
};

class ObMdsDataCompatHelper
{
public:
  static int generate_mds_mini_sstable(
      const ObMigrationTabletParam &mig_param,
      common::ObArenaAllocator &allocator,
      ObTableHandleV2 &table_handle);
  static int generate_mds_mini_sstable(
      const ObTablet &tablet,
      common::ObArenaAllocator &allocator,
      ObTableHandleV2 &table_handle,
      bool &has_tablet_status);
};


} // namespace storage
} // namespace oceanbase


#endif /* OCEANBASE_STORAGE_TABLET_OB_MDS_TABLE_MINI_MERGER_H_ */
