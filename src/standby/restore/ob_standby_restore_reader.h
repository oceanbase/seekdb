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

#ifndef OCEANBASE_STORAGE_STANDBY_RESTORE_READER_
#define OCEANBASE_STORAGE_STANDBY_RESTORE_READER_

#include "storage/meta_mem/ob_tablet_handle.h"
#include "share/ob_define.h"
#include "lib/utility/ob_macro_utils.h"
#include "ob_standby_restore_storage_struct.h"
#include "storage/blocksstable/ob_block_manager.h"
#include "storage/ob_i_table.h"
#include "standby/restore/ob_standby_restore_rpc.h"
#include "ob_storage_restore_struct.h"
#include "storage/blocksstable/index_block/ob_sstable_sec_meta_iterator.h"

namespace oceanbase
{
namespace storage
{
class ObICopyMacroBlockReader
{
public:
  enum Type {
    MACRO_BLOCK_OB_READER = 0,
    MACRO_BLOCK_RESTORE_READER = 1,
    REMOTE_SSTABLE_MACRO_BLOCK_RESTORE_READER = 2,
    DDL_MACRO_BLOCK_RESTORE_READER = 3,
    MAX_READER_TYPE
  };
  struct CopyMacroBlockReadData final
  {
  public:
    CopyMacroBlockReadData();
    ~CopyMacroBlockReadData();
    void reset();
    bool is_valid() const;
    int set_macro_meta(const blocksstable::ObDataMacroBlockMeta& macro_meta, const bool &is_reuse_macro_block);
    int set_macro_data(const blocksstable::ObBufferReader& macro_data, const bool &is_reuse_macro_block);
    void set_macro_block_id(const blocksstable::MacroBlockId &macro_block_id);
    bool is_reuse_macro_block() const { return is_reuse_macro_block_; }
    bool is_macro_data() const { return data_type_ == obcall::ObCopyMacroBlockDataType::MACRO_DATA; }
    bool is_macro_meta() const { return data_type_ == obcall::ObCopyMacroBlockDataType::MACRO_META_ROW; }
  public:
    TO_STRING_KV(K_(data_type), K_(is_reuse_macro_block), K_(macro_data), KPC_(macro_meta));
    obcall::ObCopyMacroBlockDataType data_type_;
    bool is_reuse_macro_block_;
    blocksstable::ObBufferReader macro_data_;
    blocksstable::ObDataMacroBlockMeta *macro_meta_;
    blocksstable::MacroBlockId macro_block_id_;
    common::ObArenaAllocator allocator_;
  };
  // macro block list is set in the init func
  ObICopyMacroBlockReader() {}
  virtual ~ObICopyMacroBlockReader() {}
  virtual int get_next_macro_block(CopyMacroBlockReadData &read_data) = 0;
  virtual Type get_type() const = 0;
  virtual int64_t get_data_size() const = 0;
private:
  DISALLOW_COPY_AND_ASSIGN(ObICopyMacroBlockReader);
};

struct ObCopyMacroBlockHandle final
{
  ObCopyMacroBlockHandle();
  ~ObCopyMacroBlockHandle() = default;
  void reset();
  bool is_valid() const;
  int set_macro_meta(const blocksstable::ObDataMacroBlockMeta &macro_meta);

  bool is_reuse_macro_block_;
  blocksstable::ObStorageObjectHandle read_handle_;
  common::ObArenaAllocator allocator_;
  blocksstable::ObDataMacroBlockMeta *macro_meta_;

  DISALLOW_COPY_AND_ASSIGN(ObCopyMacroBlockHandle);
};

class ObCopyMacroBlockObProducer
{
public:
  ObCopyMacroBlockObProducer();
  virtual ~ObCopyMacroBlockObProducer();

  int init(
      const share::ObLSID &ls_id,
      const ObITable::TableKey &table_key,
      const ObCopyMacroRangeInfo &copy_macro_range_info,
      const int64_t data_version,
      const share::SCN backfill_tx_scn);
  int get_next_macro_block(
      blocksstable::ObBufferReader &data,
      obcall::ObCopyMacroBlockHeader &copy_macro_block_header);

private:
  int get_read_info_(
      const blocksstable::ObDataMacroBlockMeta &macro_meta,
      blocksstable::ObStorageObjectReadInfo &read_info);
  int prefetch_();

private:
  static const int64_t MAX_PREFETCH_MACRO_BLOCK_NUM = 2;
  bool is_inited_;
  ObCopyMacroRangeInfo copy_macro_range_info_;
  int64_t data_version_;
  int64_t macro_idx_;
  ObCopyMacroBlockHandle copy_macro_block_handle_[MAX_PREFETCH_MACRO_BLOCK_NUM];
  int64_t handle_idx_;
  int64_t prefetch_meta_time_;
  common::ObArenaAllocator tablet_allocator_;
  ObTabletHandle tablet_handle_;
  ObTableHandleV2 sstable_handle_;
  const ObSSTable *sstable_;
  ObDatumRange datum_range_;
  common::ObArenaAllocator allocator_;
  ObSSTableSecMetaIterator second_meta_iterator_;
  common::ObArenaAllocator io_allocator_;
  char *io_buf_[MAX_PREFETCH_MACRO_BLOCK_NUM];
  ObSelfBufferWriter meta_row_buf_; // buffer for macro meta row (ObDatumRow)
  DISALLOW_COPY_AND_ASSIGN(ObCopyMacroBlockObProducer);
};

class ObCopyTabletInfoObProducer
{
public:
  ObCopyTabletInfoObProducer();
  virtual ~ObCopyTabletInfoObProducer();
  int init(
    const share::ObLSID &ls_id,
    const common::ObIArray<common::ObTabletID> &tablet_id_array);
  int get_next_tablet_info(obcall::ObCopyTabletInfo &tablet_info);

private:
  bool is_inited_;
  ObArray<common::ObTabletID> tablet_id_array_;
  int64_t tablet_index_;
  ObLS *ls_;
  DISALLOW_COPY_AND_ASSIGN(ObCopyTabletInfoObProducer);
};

class ObCopyTabletsSSTableInfoObProducer
{
public:
  ObCopyTabletsSSTableInfoObProducer();
  virtual ~ObCopyTabletsSSTableInfoObProducer();
  int init(
      const share::ObLSID &ls_id,
      const common::ObIArray<obcall::ObCopyTabletSSTableInfoArg> &tablet_sstable_info_array);
  int get_next_tablet_sstable_info(
      obcall::ObCopyTabletSSTableInfoArg &arg);

private:
  bool is_inited_;
  ObLS *ls_;
  common::ObArray<obcall::ObCopyTabletSSTableInfoArg> tablet_sstable_info_array_;
  int64_t tablet_index_;
};

class ObCopySSTableInfoObProducer
{
public:
  ObCopySSTableInfoObProducer();
  virtual ~ObCopySSTableInfoObProducer() {}
  int init(const obcall::ObCopyTabletSSTableInfoArg &tablet_sstable_info, ObLS *ls);
  int get_next_sstable_info(obcall::ObCopyTabletSSTableInfo &sstable_info);
  int get_copy_tablet_sstable_header(obcall::ObCopyTabletSSTableHeader &copy_header);
private:
  int check_need_copy_sstable_(
      blocksstable::ObSSTable *sstable,
      bool &need_copy_sstable);
  int get_copy_sstable_count_(int64_t &sstable_count);
  int get_tablet_meta_(ObMigrationTabletParam &tablet_meta);
  int fake_deleted_tablet_meta_(ObMigrationTabletParam &tablet_meta);

private:
  bool is_inited_;
  share::ObLSID ls_id_;
  obcall::ObCopyTabletSSTableInfoArg tablet_sstable_info_;
  ObTabletHandle tablet_handle_;
  ObTableStoreIterator iter_;
  storage::ObCopyTabletStatus::STATUS status_;
  DISALLOW_COPY_AND_ASSIGN(ObCopySSTableInfoObProducer);
};

class ObICopySSTableMacroInfoReader
{
public:
  enum Type {
    COPY_SSTABLE_MACRO_INFO_OB_READER = 0,
    COPY_SSTABLE_MACRO_INFO_RESTORE_READER = 1,
    MAX_TYPE
  };
  ObICopySSTableMacroInfoReader() {}
  virtual ~ObICopySSTableMacroInfoReader() {}
  virtual int get_next_sstable_range_info(
      ObCopySSTableMacroRangeInfo &sstable_macro_range_info) = 0;
  virtual Type get_type() const = 0;
private:
  DISALLOW_COPY_AND_ASSIGN(ObICopySSTableMacroInfoReader);
};

class ObCopySSTableMacroObProducer
{
public:
  ObCopySSTableMacroObProducer();
  virtual ~ObCopySSTableMacroObProducer() {}

  int init(
      const share::ObLSID & ls_id,
      const common::ObTabletID &tablet_id,
      const common::ObIArray<ObITable::TableKey> &copy_table_key_array,
      const int64_t macro_range_max_marco_count);

  int get_next_sstable_macro_range_info(obcall::ObCopySSTableMacroRangeInfoHeader &header);
private:
  int get_next_sstable_macro_range_info_(
      obcall::ObCopySSTableMacroRangeInfoHeader &header);
private:
  bool is_inited_;
  common::ObArray<ObITable::TableKey> copy_table_key_array_;
  int64_t sstable_index_;
  bool is_sstable_iter_init_;
  ObLS *ls_;
  ObTabletHandle tablet_handle_;
  int64_t macro_range_max_marco_count_;
  DISALLOW_COPY_AND_ASSIGN(ObCopySSTableMacroObProducer);
};

class ObICopySSTableMacroRangeObProducer
{
public:
  enum Type {
    COPY_SSTABLE_MACRO_RANGE_INFO_OB_PRODUCER = 0,
    COPY_DDL_SSTABLE_MACRO_RANGE_INFO_OB_PRODUCER = 1,
    MAX_TYPE
  };
  ObICopySSTableMacroRangeObProducer() {}
  virtual ~ObICopySSTableMacroRangeObProducer() {}
  virtual int get_next_macro_range_info(
      ObCopyMacroRangeInfo &macro_range_info) = 0;
  virtual Type get_type() const = 0;
private:
  DISALLOW_COPY_AND_ASSIGN(ObICopySSTableMacroRangeObProducer);
};

class ObCopySSTableMacroRangeObProducer : public ObICopySSTableMacroRangeObProducer
{
public:
  ObCopySSTableMacroRangeObProducer();
  virtual ~ObCopySSTableMacroRangeObProducer() { second_meta_iterator_.reset(); }
  int init(
      const share::ObLSID &ls_id,
      const common::ObTabletID &tablet_id,
      const obcall::ObCopySSTableMacroRangeInfoHeader &header,
      const int64_t macro_range_max_marco_count);
  virtual int get_next_macro_range_info(ObCopyMacroRangeInfo &macro_range_info);
  virtual Type get_type() const { return COPY_SSTABLE_MACRO_RANGE_INFO_OB_PRODUCER; }

private:
  bool is_inited_;
  ObITable::TableKey table_key_;
  int64_t macro_range_count_;
  int64_t macro_range_index_;
  int64_t macro_range_max_marco_count_;
  ObTabletHandle tablet_handle_;
  ObTableHandleV2 table_handle_;
  ObDatumRange datum_range_;
  common::ObArenaAllocator allocator_;
  ObSSTableSecMetaIterator second_meta_iterator_;
  DISALLOW_COPY_AND_ASSIGN(ObCopySSTableMacroRangeObProducer);
};

}
}
#endif // OCEANBASE_STORAGE_STANDBY_RESTORE_READER_
