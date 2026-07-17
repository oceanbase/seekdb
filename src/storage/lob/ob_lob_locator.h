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

#ifndef OB_STORAGE_LOB_LOCATOR_H
#define OB_STORAGE_LOB_LOCATOR_H

#include "storage/access/ob_table_access_param.h"
#include "storage/access/ob_dml_param.h"

namespace oceanbase
{
namespace storage
{

class ObLobLocatorHelper
{
public:
  ObLobLocatorHelper();
  virtual ~ObLobLocatorHelper();
  void reuse() {
    locator_allocator_.reuse();
    rowid_objs_.reuse();
    rowkey_str_.reset();
  }
  int init(const ObTableScanParam &scan_param,
           const ObStoreCtx &ctx,
           const share::ObLSID &ls_id,
           const int64_t snapshot_version);
  int init(const uint64_t table_id,
           const uint64_t tablet_id,
           const ObStoreCtx &ctx,
           const share::ObLSID &ls_id,
           const int64_t snapshot_version);
  int fill_lob_locator(blocksstable::ObDatumRow &row, bool is_projected_row,
                       const ObTableAccessParam &access_param);
  int fill_lob_locator_v2(blocksstable::ObDatumRow &row,
                          const ObTableAccessContext &access_ctx,
                          const ObTableAccessParam &access_param);
  int fill_lob_locator_v2(common::ObDatum &datum,
                          const ObColumnParam &col_param,
                          const ObTableIterParam &iter_param,
                          const ObTableAccessContext &access_ctx);
  int fuse_mem_lob_header(ObObj &def_obj, uint64_t col_id, bool is_systable);
  void update_lob_locator_ctx(uint64_t table_id, uint64_t tablet_id, int64_t tx_id) {
    table_id_ = table_id;
    tablet_id_ = tablet_id;
    tx_id_ = tx_id;
  }
  OB_INLINE bool is_valid() const { return is_inited_; }
  OB_INLINE bool enable_lob_locator_v2() const { return enable_locator_v2_; }
  TO_STRING_KV(K_(table_id), K_(ls_id), K_(tx_id), K_(snapshot_version), K_(tx_read_snapshot),
   K_(enable_locator_v2), K_(is_inited), K_(scan_flag), K_(tx_seq_base), K_(is_access_index));
private:
  static const int64_t DEFAULT_LOCATOR_OBJ_ARRAY_SIZE = 8;
  static const int64_t LOB_FORCE_INROW_SIZE = 64 * 1024L; // 64K
  int init_rowid_version(const share::schema::ObTableSchema &table_schema);
  int build_lob_locatorv2(ObLobLocatorV2 &locator,
                          const common::ObString &payload,
                          const uint64_t column_id,
                          const common::ObString &rowid_str,
                          const ObTableAccessContext &access_ctx,
                          ObCollationType cs_type,
                          bool is_simple,
                          bool is_systable);
  bool can_skip_build_mem_lob_locator(const common::ObString &payload);
private:
  uint64_t table_id_;
  uint64_t tablet_id_;
  int64_t ls_id_;
  int64_t tx_id_;
  int64_t snapshot_version_;
  transaction::ObTxReadSnapshot tx_read_snapshot_;
  share::SCN fb_snapshot_;
  common::ObSEArray<common::ObObj, DEFAULT_LOCATOR_OBJ_ARRAY_SIZE> rowid_objs_;
  common::ObArenaAllocator locator_allocator_;
  ObString rowkey_str_; // for default values
  bool enable_locator_v2_;
  bool is_inited_;
  ObQueryFlag scan_flag_;
  int64_t tx_seq_base_;
  bool is_access_index_;
};

} // namespace storage
} // namespace oceanbase
#endif
