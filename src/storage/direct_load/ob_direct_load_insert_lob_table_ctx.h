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

#pragma once

#include "storage/direct_load/ob_direct_load_insert_table_ctx.h"

namespace oceanbase
{
namespace storage
{
class ObDirectLoadInsertLobTableContext;
class ObDirectLoadInsertDataTableContext;
class ObDirectLoadInsertDataTabletContext;

class ObDirectLoadInsertLobTabletContext : public ObDirectLoadInsertTabletContext
{
public:
  ObDirectLoadInsertLobTabletContext();
  virtual ~ObDirectLoadInsertLobTabletContext();
  int init(ObDirectLoadInsertLobTableContext *table_ctx,
           ObDirectLoadInsertDataTabletContext *data_tablet_ctx, const share::ObLSID &ls_id,
           const common::ObTabletID &origin_tablet_id, const common::ObTabletID &tablet_id);
  int open() override;
  int close() override;
  void cancel() override;

  //////////////////////// write interface ////////////////////////
public:
  int open_sstable_slice(const blocksstable::ObMacroDataSeq &start_seq,
                         const int64_t slice_idx,
                         int64_t &slice_id,
                         ObDirectLoadMgrAgent &ddl_agent) override;
  int fill_sstable_slice(const int64_t &slice_id, ObIStoreRowIterator &iter,
                         ObDirectLoadMgrAgent &ddl_agent,
                         int64_t &affected_rows) override;
  int fill_sstable_slice(const int64_t &slice_id,
                         const blocksstable::ObBatchDatumRows &datum_rows,
                         ObDirectLoadMgrAgent &ddl_agent) override;
  int close_sstable_slice(const int64_t slice_id,
                          const int64_t slice_idx,
                          ObDirectLoadMgrAgent &ddl_agent) override;
  int get_ddl_agent(ObDirectLoadMgrAgent &ddl_agent) override;
  // Special write lob interface, datum_row is main table data
  int fill_lob_sstable_slice(ObIAllocator &allocator, const int64_t &lob_slice_id,
                             share::ObTabletCacheInterval &pk_interval,
                             blocksstable::ObDatumRow &datum_row,
                             ObDirectLoadMgrAgent &ddl_agent);
  int fill_lob_sstable_slice(ObIAllocator &allocator, const int64_t &lob_slice_id,
                             share::ObTabletCacheInterval &pk_interval,
                             blocksstable::ObBatchDatumRows &datum_rows,
                             ObDirectLoadMgrAgent &ddl_agent);
  const ObLobId &get_min_insert_lob_id() const { return min_insert_lob_id_; }

private:
  int get_pk_interval(uint64_t count, share::ObTabletCacheInterval &pk_interval) override;

  //////////////////////// members ////////////////////////
public:
  INHERIT_TO_STRING_KV("ObDirectLoadInsertTabletContext", ObDirectLoadInsertTabletContext,
                       KP_(data_tablet_ctx), K_(tablet_id_in_lob_id), K_(min_insert_lob_id));

private:
  ObDirectLoadInsertDataTabletContext *data_tablet_ctx_;
  common::ObTabletID tablet_id_in_lob_id_;
  ObLobId min_insert_lob_id_;
};

class ObDirectLoadInsertLobTableContext : public ObDirectLoadInsertTableContext
{
  friend class ObDirectLoadInsertLobTabletContext;

public:
  ObDirectLoadInsertLobTableContext();
  virtual ~ObDirectLoadInsertLobTableContext();
  int init(const ObDirectLoadInsertTableParam &param,
           ObDirectLoadInsertDataTableContext *data_table_ctx,
           const common::ObIArray<table::ObTableLoadLSIdAndPartitionId> &ls_partition_ids,
           const common::ObIArray<table::ObTableLoadLSIdAndPartitionId> &target_ls_partition_ids,
           const common::ObIArray<table::ObTableLoadLSIdAndPartitionId> &data_ls_partition_ids);

  void set_data_table_ctx(ObDirectLoadInsertTableContext *data_table_ctx)
  {
    data_table_ctx_ = data_table_ctx;
  }
  ObDirectLoadInsertTableContext *get_data_table_ctx() { return data_table_ctx_; }

private:
  int create_all_tablet_contexts(
    ObDirectLoadInsertDataTableContext *data_table_ctx,
    const common::ObIArray<table::ObTableLoadLSIdAndPartitionId> &ls_partition_ids,
    const common::ObIArray<table::ObTableLoadLSIdAndPartitionId> &target_ls_partition_ids,
    const common::ObIArray<table::ObTableLoadLSIdAndPartitionId> &data_ls_partition_ids);

private:
  ObDirectLoadInsertTableContext *data_table_ctx_;
};

} // namespace storage
} // namespace oceanbase
