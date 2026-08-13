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

#ifndef OCEANBASE_STORAGE_DDL_OB_I_DIRECT_LOAD_MGR_H
#define OCEANBASE_STORAGE_DDL_OB_I_DIRECT_LOAD_MGR_H

#include "lib/lock/ob_mutex.h"
#include "lib/lock/ob_bucket_lock.h"
#include "common/ob_tablet_id.h"
#include "share/scn.h"
#include "storage/ob_i_table.h"
#include "storage/ddl/ob_direct_load_struct.h"
#include "src/share/ob_ddl_common.h"

namespace oceanbase
{
namespace storage
{

class ObInsertMonitor;
class ObIStoreRowIterator;
class ObTabletDirectLoadBuildCtx;

enum class ObDirectLoadMgrRole
{
  INVALID_TYPE = 0,
  DATA_TABLET_TYPE = 1,
  LOB_TABLET_TYPE = 2,
  MAX_TYPE
};

/*
 *  base_direct_load_mgr has two interfaces
 *  1. v1 interface,
       * global unique param, the sub class will be managed by the ObDirectLoadMgr,
       * rely on different execution id to distinguish from different retry tasks
       * lob hanlde will be bind to to the data direct load mg
      +-------+    +-------------------+    +-----------------+
      | Agent | -> | data_direct_load  | -> | lob_direct_load |
      +-------+    +-------------------+    +-----------------+

    2. v2 interface
       v2 interface uses idempotency logic,
       * Local parameters are owned by the concrete direct-load manager.
       * rely on idempodency logic, data will be the same in different retry tasks
       * lob handle & data handle are seperated, direct_load_mgr_agent can directly operate on both of them
      +-------+    +-------------------+
      |       | -> | data_direct_load  |
      |       |    +-------------------+
      | Agent |
      |       |    +-------------------+
      |       | -> | lob_direct_load   |
      +-------+    +-------------------+
 */

class ObBaseTabletDirectLoadMgr
{
public:
  ObBaseTabletDirectLoadMgr();
  virtual ~ObBaseTabletDirectLoadMgr();
  virtual bool is_valid() = 0;
  TO_STRING_KV(K_(table_key), K_(data_format_version), K_(direct_load_type),
               K_(tablet_id));
public: /* some basic methods */
  void inc_ref() { ATOMIC_INC(&ref_cnt_); };
  int64_t dec_ref() { return ATOMIC_SAF(&ref_cnt_, 1); }
  int64_t get_ref() { return ATOMIC_LOAD(&ref_cnt_); }
  virtual int cancel() = 0;
  virtual int update_max_lob_id(const int64_t lob_id) { return OB_NOT_SUPPORTED; };
public:
  /* --------- direct_load_mgr interface  v1 ---------*/
  virtual int update(ObBaseTabletDirectLoadMgr *lob_tablet_mgr, const ObTabletDirectLoadInsertParam &build_param)
  { return OB_NOT_SUPPORTED; }
  virtual int open(const int64_t current_execution_id, share::SCN &start_scn)
  { return OB_NOT_SUPPORTED; }
  virtual int close(const int64_t current_execution_id, const share::SCN &start_scn)
  { return OB_NOT_SUPPORTED; }
  virtual int open_sstable_slice(const bool is_data_tablet_process_for_lob,
                                 const blocksstable::ObMacroDataSeq &start_seq,
                                const ObDirectLoadSliceInfo &slice_info)
  { return OB_NOT_SUPPORTED; }
  virtual int fill_sstable_slice(const ObDirectLoadSliceInfo &slice_info,
                                 const share::SCN &start_scn,
                                 ObIStoreRowIterator *iter,
                                 int64_t &affected_rows,
                                 ObInsertMonitor *insert_monitor = NULL)
  { return OB_NOT_SUPPORTED; }
  virtual int fill_sstable_slice(
      const ObDirectLoadSliceInfo &slice_info,
      const share::SCN &start_scn,
      const blocksstable::ObBatchDatumRows &datum_rows,
      ObInsertMonitor *insert_monitor = NULL)
  { return OB_NOT_SUPPORTED; }
  virtual int fill_lob_sstable_slice(ObIAllocator &allocator,
                                     const ObDirectLoadSliceInfo &slice_info,
                                     const share::SCN &start_scn,
                                     share::ObTabletCacheInterval &pk_interval,
                                     blocksstable::ObDatumRow &datum_row)
  { return OB_NOT_SUPPORTED; }
  virtual int fill_lob_sstable_slice(ObIAllocator &allocator,
                                     const ObDirectLoadSliceInfo &slice_info,
                                     const share::SCN &start_scn,
                                     share::ObTabletCacheInterval &pk_interval,
                                     blocksstable::ObBatchDatumRows &datum_rows)
  {return OB_NOT_SUPPORTED; }
  virtual int close_sstable_slice(const bool is_data_tablet_process_for_lob,
                                  const ObDirectLoadSliceInfo &slice_info,
                                  const share::SCN &start_scn,
                                  const int64_t execution_id,
                                  ObInsertMonitor *insert_monitor,
                                  blocksstable::ObMacroDataSeq &next_seq)
  { return OB_NOT_SUPPORTED; }
public: /* --------- direct_load_mgr interface  v2 ---------*/
  virtual int init_v2(const ObTabletDirectLoadInsertParam &build_param,
                      const int64_t execution_id,
                      const ObDirectLoadMgrRole role)
  { return OB_NOT_SUPPORTED; }
  virtual int prepare_index_builder() { return OB_NOT_SUPPORTED; }
  virtual int fill_sstable_slice_v2(const ObDirectLoadSliceInfo &slice_info,
                                    ObIStoreRowIterator *iter,
                                    ObDirectLoadSliceWriter &slice_writer,
                                    blocksstable::ObMacroDataSeq &next_seq,
                                    ObInsertMonitor *insert_monitor,
                                    int64_t &affected_rows)
  { return OB_NOT_SUPPORTED; }
  virtual int fill_sstable_slice_v2(const ObDirectLoadSliceInfo &slice_info,
                                    const ObBatchDatumRows &datum_rows,
                                    ObDirectLoadSliceWriter &slice_writer,
                                    ObInsertMonitor *insert_monitorr)
  { return OB_NOT_SUPPORTED; }
  virtual int fill_lob_sstable_slice_row_v2(ObIAllocator &allocator,
                                            const ObDirectLoadSliceInfo &slice_info,
                                            share::ObTabletCacheInterval &pk_interval,
                                            blocksstable::ObDatumRow &datum_row,
                                            ObDirectLoadSliceWriter &slice_writer,
                                            ObTabletDirectLoadMgrHandle &data_direct_load_handle)
  { return OB_NOT_SUPPORTED; }
  virtual int fill_lob_sstable_slice_row_v2(ObIAllocator &allocator,
                                            const ObDirectLoadSliceInfo &slice_info,
                                            share::ObTabletCacheInterval &pk_interval,
                                            ObDirectLoadSliceWriter &slice_writer,
                                            ObTabletDirectLoadMgrHandle &data_direct_load_handle,
                                            blocksstable::ObBatchDatumRows &datum_row)
  { return OB_NOT_SUPPORTED; }
  virtual int close_sstable_slice_v2(const ObDirectLoadSliceInfo &slice_info,
                                     ObDirectLoadSliceWriter &slice_writer,
                                     blocksstable::ObMacroDataSeq &next_seq,
                                     ObInsertMonitor *insert_monitor,
                                     bool &is_all_sliced_finished)
  { return OB_NOT_SUPPORTED; }
  virtual int close() { return OB_NOT_SUPPORTED; }

public:
  /* get basic info */
  inline  ObITable::TableKey get_table_key() const { return table_key_; }
  inline uint64_t get_data_format_version() const { return data_format_version_; }
  inline ObDirectLoadType get_direct_load_type() const { return direct_load_type_; }
  inline ObTabletID get_tablet_id() const { return tablet_id_; }
  /* Build metadata exposed to direct-load implementations. */
  virtual int64_t get_ddl_task_id() const = 0;
  virtual ObWholeDataStoreDesc &get_data_block_desc() = 0;
  virtual ObTabletDirectLoadInsertParam &get_build_param() = 0;
  virtual int64_t get_task_cnt() = 0;
  virtual const ObIArray<ObColumnSchemaItem> &get_column_info() const = 0;
  virtual bool get_micro_index_clustered() = 0;

protected:
  /* basic info */
  ObTabletID tablet_id_;
  ObITable::TableKey table_key_;
  uint64_t data_format_version_;
  ObDirectLoadType direct_load_type_;
  /* concurrent control param */
  int64_t ref_cnt_;
  common::ObLatch lock_;
  common::ObThreadCond cond_;
};

}// namespace storage
}// namespace oceanbase

#endif//OCEANBASE_STORAGE_DDL_OB_I_DIRECT_LOAD_MGR_H
