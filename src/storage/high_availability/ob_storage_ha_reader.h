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

#ifndef OCEABASE_STORAGE_HA_MACRO_BLOCK_READER_
#define OCEABASE_STORAGE_HA_MACRO_BLOCK_READER_

#include "storage/meta_mem/ob_tablet_handle.h"
#include "share/ob_define.h"
#include "lib/utility/ob_macro_utils.h"
#include "lib/function/ob_function.h"
#include "ob_storage_ha_struct.h"
#include "storage/blocksstable/ob_block_manager.h"
#include "storage/ob_i_table.h"
#include "storage/ob_storage_rpc.h"
#include "ob_storage_restore_struct.h"
#include "storage/blocksstable/index_block/ob_sstable_sec_meta_iterator.h"
#include "storage/tx_storage/ob_ls_handle.h"

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
      const uint64_t tenant_id,
      const share::ObLSID &ls_id,
      const ObITable::TableKey &table_key,
      const ObCopyMacroRangeInfo &copy_macro_range_info,
      const int64_t data_version,
      const share::SCN backfill_tx_scn);
  int get_next_macro_block(
      blocksstable::ObBufferReader &data,
      obcall::ObCopyMacroBlockHeader &copy_macro_block_header);

private:
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

class ObICopyTabletInfoReader
{
public:
  enum Type {
    TABLET_INFO_OB_READER = 0,
    TABLET_INFO_RESTORE_READER = 1,
    MAX,
  };
  ObICopyTabletInfoReader() {}
  virtual ~ObICopyTabletInfoReader() {}
  virtual int fetch_tablet_info(
      obcall::ObCopyTabletInfo &tablet_info) = 0;
  virtual Type get_type() const = 0;
private:
  DISALLOW_COPY_AND_ASSIGN(ObICopyTabletInfoReader);
};

class ObCopyTabletInfoRestoreReader : public ObICopyTabletInfoReader
{
public:
  ObCopyTabletInfoRestoreReader();
  virtual ~ObCopyTabletInfoRestoreReader();
  virtual int fetch_tablet_info(obcall::ObCopyTabletInfo &tablet_info);
  virtual Type get_type() const { return TABLET_INFO_RESTORE_READER; }
private:
  DISALLOW_COPY_AND_ASSIGN(ObCopyTabletInfoRestoreReader);
};

class ObCopyTabletInfoObProducer
{
public:
  ObCopyTabletInfoObProducer();
  virtual ~ObCopyTabletInfoObProducer();
  int init(
    const uint64_t tenant_id,
    const share::ObLSID &ls_id,
    const common::ObIArray<common::ObTabletID> &tablet_id_array);
  int get_next_tablet_info(obcall::ObCopyTabletInfo &tablet_info);

private:
  bool is_inited_;
  ObArray<common::ObTabletID> tablet_id_array_;
  int64_t tablet_index_;
  ObLSHandle ls_handle_;
  DISALLOW_COPY_AND_ASSIGN(ObCopyTabletInfoObProducer);
};

class ObCopyRemoteSSTableInfoObProducer final
{
public:
  ObCopyRemoteSSTableInfoObProducer();
  ~ObCopyRemoteSSTableInfoObProducer() {}

  int init (
      const common::ObTabletID tablet_id,
      ObLS *ls);
  int get_next_sstable_info(
      obcall::ObCopyTabletSSTableInfo &sstable_info);
  int get_copy_tablet_sstable_header(
      obcall::ObCopyTabletSSTableHeader &copy_header);

  void reset();

private:
  int check_need_copy_sstable_(
      blocksstable::ObSSTable *sstable,
      bool &need_copy_sstable);
  int get_copy_sstable_count_(int64_t &sstable_count);
  int get_tablet_meta_(ObMigrationTabletParam &tablet_meta);

private:
  bool is_inited_;
  share::ObLSID ls_id_;
  common::ObTabletID tablet_id_;
  ObTabletHandle tablet_handle_;
  ObTableStoreIterator iter_;
  DISALLOW_COPY_AND_ASSIGN(ObCopyRemoteSSTableInfoObProducer);
};

class ObICopySSTableInfoReader
{
public:
  enum Type {
    COPY_SSTABLE_INFO_OB_READER = 0,
    COPY_SSTABLE_INFO_RESTORE_READER = 1,
    MAX_TYPE
  };
  ObICopySSTableInfoReader() {}
  virtual ~ObICopySSTableInfoReader() {}
  virtual int get_next_sstable_info(
      obcall::ObCopyTabletSSTableInfo &sstable_info) = 0;
  virtual int get_next_tablet_sstable_header(
      obcall::ObCopyTabletSSTableHeader &copy_header) = 0;
  virtual Type get_type() const = 0;
private:
  DISALLOW_COPY_AND_ASSIGN(ObICopySSTableInfoReader);
};

class ObCopySSTableInfoRestoreReader : public ObICopySSTableInfoReader
{
public:
  ObCopySSTableInfoRestoreReader();
  virtual ~ObCopySSTableInfoRestoreReader() {}
  virtual int get_next_sstable_info(
      obcall::ObCopyTabletSSTableInfo &sstable_info);
  virtual int get_next_tablet_sstable_header(
      obcall::ObCopyTabletSSTableHeader &copy_header);
  virtual Type get_type() const { return COPY_SSTABLE_INFO_RESTORE_READER; }
private:
  DISALLOW_COPY_AND_ASSIGN(ObCopySSTableInfoRestoreReader);
};

class ObCopyTabletsSSTableInfoObProducer
{
public:
  ObCopyTabletsSSTableInfoObProducer();
  virtual ~ObCopyTabletsSSTableInfoObProducer();
  int init(
      const uint64_t tenant_id,
      const share::ObLSID &ls_id,
      const common::ObIArray<obcall::ObCopyTabletSSTableInfoArg> &tablet_sstable_info_array);
  int get_next_tablet_sstable_info(
      obcall::ObCopyTabletSSTableInfoArg &arg);

private:
  bool is_inited_;
  ObLSHandle ls_handle_;
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
      const uint64_t tenant_id,
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
  ObLSHandle ls_handle_;
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
      const uint64_t tenant_id,
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

class ObDDLCopySSTableMacroRangeObProducer : public ObICopySSTableMacroRangeObProducer
{
public:
  ObDDLCopySSTableMacroRangeObProducer();
  virtual ~ObDDLCopySSTableMacroRangeObProducer() { iterator_.reset(); }
  int init(
      const uint64_t tenant_id,
      const share::ObLSID &ls_id,
      const common::ObTabletID &tablet_id,
      const obcall::ObCopySSTableMacroRangeInfoHeader &header,
      const int64_t macro_range_max_marco_count);
  virtual int get_next_macro_range_info(ObCopyMacroRangeInfo &macro_range_info);
  virtual Type get_type() const { return COPY_DDL_SSTABLE_MACRO_RANGE_INFO_OB_PRODUCER; }

public:
  static const int64_t MACRO_RANGE_MAX_MACRO_COUNT = 128;
  static const int64_t SINGLE_MACRO_ID_FIXED_LENGTH = sizeof(MacroBlockId);
  static const int64_t MAX_BUF_SIZE = OB_MAX_ROWKEY_COLUMN_NUMBER * SINGLE_MACRO_ID_FIXED_LENGTH;
private:
  bool is_inited_;
  ObITable::TableKey table_key_;
  int64_t macro_range_count_;
  int64_t macro_range_index_;
  int64_t macro_range_max_marco_count_;
  common::ObArenaAllocator allocator_;
  ObTabletHandle tablet_handle_;
  ObTableHandleV2 table_handle_;
  ObSSTableMetaHandle meta_handle_;
  ObMacroIdIterator iterator_;
  char buf_[MAX_BUF_SIZE];
  DISALLOW_COPY_AND_ASSIGN(ObDDLCopySSTableMacroRangeObProducer);
};

}
}
#endif
