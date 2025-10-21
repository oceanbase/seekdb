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

#include "storage/direct_load/ob_direct_load_i_table.h"
#include "storage/direct_load/ob_direct_load_multiple_datum_rowkey.h"
#include "storage/direct_load/ob_direct_load_rowkey_iterator.h"
#include "storage/direct_load/ob_direct_load_sstable_index_block_iterator.h"
#include "storage/direct_load/ob_direct_load_sstable_index_entry_iterator.h"
#include "storage/direct_load/ob_direct_load_tmp_file.h"

namespace oceanbase
{
namespace storage
{
class ObDirectLoadTableDataDesc;
class ObDirectLoadMultipleDatumRange;
class ObDirectLoadMultipleSSTableScanner;
class ObDirectLoadMultipleSSTableIndexBlockMetaIterator;

struct ObDirectLoadMultipleSSTableFragment
{
public:
  ObDirectLoadMultipleSSTableFragment();
  ~ObDirectLoadMultipleSSTableFragment();
  int assign(const ObDirectLoadMultipleSSTableFragment &other);
  TO_STRING_KV(K_(index_block_count),
               K_(data_block_count),
               K_(rowkey_block_count),
               K_(index_file_size),
               K_(data_file_size),
               K_(rowkey_file_size),
               K_(row_count),
               K_(rowkey_count),
               K_(max_data_block_size),
               K_(rowkey_file_handle),
               K_(index_file_handle),
               K_(data_file_handle));
public:
  int64_t index_block_count_;
  int64_t data_block_count_;
  int64_t rowkey_block_count_;
  int64_t index_file_size_;
  int64_t data_file_size_;
  int64_t rowkey_file_size_;
  int64_t row_count_;
  int64_t rowkey_count_;
  int64_t max_data_block_size_;
  ObDirectLoadTmpFileHandle index_file_handle_;
  ObDirectLoadTmpFileHandle data_file_handle_;
  ObDirectLoadTmpFileHandle rowkey_file_handle_;
};

struct ObDirectLoadMultipleSSTableCreateParam
{
public:
  ObDirectLoadMultipleSSTableCreateParam();
  ~ObDirectLoadMultipleSSTableCreateParam();
  bool is_valid() const;
  TO_STRING_KV(K_(tablet_id),
               K_(rowkey_column_num),
               K_(column_count),
               K_(index_block_size),
               K_(data_block_size),
               K_(index_block_count),
               K_(data_block_count),
               K_(row_count),
               K_(rowkey_count),
               K_(max_data_block_size),
               K_(start_key),
               K_(end_key),
               K_(fragments));
public:
  common::ObTabletID tablet_id_;
  int64_t rowkey_column_num_;
  int64_t column_count_;
  int64_t index_block_size_;
  int64_t data_block_size_;
  int64_t rowkey_block_size_;
  int64_t index_block_count_;
  int64_t data_block_count_;
  int64_t rowkey_block_count_;
  int64_t row_count_;
  int64_t rowkey_count_;
  int64_t max_data_block_size_;
  ObDirectLoadMultipleDatumRowkey start_key_;
  ObDirectLoadMultipleDatumRowkey end_key_;
  common::ObArray<ObDirectLoadMultipleSSTableFragment> fragments_;
};

struct ObDirectLoadMultipleSSTableMeta
{
public:
  ObDirectLoadMultipleSSTableMeta();
  ~ObDirectLoadMultipleSSTableMeta();
  void reset();
  TO_STRING_KV(K_(rowkey_column_num),
               K_(column_count),
               K_(index_block_size),
               K_(data_block_size),
               K_(rowkey_block_size),
               K_(index_block_count),
               K_(data_block_count),
               K_(rowkey_block_count),
               K_(row_count),
               K_(rowkey_count),
               K_(max_data_block_size));
public:
  int64_t rowkey_column_num_;
  int64_t column_count_;
  int64_t index_block_size_;
  int64_t data_block_size_;
  int64_t rowkey_block_size_;
  int64_t index_block_count_;
  int64_t data_block_count_; // same as index entry count
  int64_t rowkey_block_count_;
  int64_t row_count_;
  int64_t rowkey_count_;
  int64_t max_data_block_size_;
};

class ObDirectLoadMultipleSSTable : public ObDirectLoadITable
{
public:
  typedef ObDirectLoadMultipleSSTableFragment Fragment;
  typedef ObDirectLoadSSTableIndexBlockIterator<ObDirectLoadMultipleSSTable> IndexBlockIterator;
  typedef ObDirectLoadSSTableIndexEntryIterator<ObDirectLoadMultipleSSTable> IndexEntryIterator;
  ObDirectLoadMultipleSSTable();
  virtual ~ObDirectLoadMultipleSSTable();
  void reset();
  int init(const ObDirectLoadMultipleSSTableCreateParam &param);
  const common::ObTabletID &get_tablet_id() const override { return tablet_id_; }
  int64_t get_row_count() const override { return meta_.row_count_; }
  bool is_valid() const override { return is_inited_; }
  bool is_empty() const { return 0 == meta_.row_count_; }
  const ObDirectLoadMultipleSSTableMeta &get_meta() const { return meta_; }
  const ObDirectLoadMultipleDatumRowkey &get_start_key() const { return start_key_; }
  const ObDirectLoadMultipleDatumRowkey &get_end_key() const { return end_key_; }
  const common::ObIArray<ObDirectLoadMultipleSSTableFragment> &get_fragments() const
  {
    return fragments_;
  }

  IndexBlockIterator index_block_begin();
  IndexBlockIterator index_block_end();
  IndexEntryIterator index_entry_begin();
  IndexEntryIterator index_entry_end();
  // Iterate over all data rows in the range
  int scan(const ObDirectLoadTableDataDesc &table_data_desc,
           const ObDirectLoadMultipleDatumRange &range,
           const blocksstable::ObStorageDatumUtils *datum_utils,
           common::ObIAllocator &allocator,
           ObDirectLoadMultipleSSTableScanner *&scanner);
  // Iterate all index block information
  // Iterate the index block information of the specified tablet
  // Iterate all index block corresponding endkey
  int scan_whole_index_block_endkey(const ObDirectLoadTableDataDesc &table_data_desc,
                                    common::ObIAllocator &allocator,
                                    ObIDirectLoadMultipleDatumRowkeyIterator *&rowkey_iter);
  // only for not multiple mode
  int scan_whole_index_block_endkey(const ObDirectLoadTableDataDesc &table_data_desc,
                                    common::ObIAllocator &allocator,
                                    ObIDirectLoadDatumRowkeyIterator *&rowkey_iter);
  // Iterate all rowkey
  int scan_whole_rowkey(const ObDirectLoadTableDataDesc &table_data_desc,
                        common::ObIAllocator &allocator,
                        ObIDirectLoadMultipleDatumRowkeyIterator *&rowkey_iter);
  // only for not multiple mode
  int scan_whole_rowkey(const ObDirectLoadTableDataDesc &table_data_desc,
                        common::ObIAllocator &allocator,
                        ObIDirectLoadDatumRowkeyIterator *&rowkey_iter);

  TO_STRING_KV(K_(meta), K_(start_key), K_(end_key), K_(fragments));
private:
  common::ObArenaAllocator allocator_;
  common::ObTabletID tablet_id_; // invalid in multiple mode
  ObDirectLoadMultipleSSTableMeta meta_;
  ObDirectLoadMultipleDatumRowkey start_key_;
  ObDirectLoadMultipleDatumRowkey end_key_;
  common::ObArray<ObDirectLoadMultipleSSTableFragment> fragments_;
  bool is_inited_;
};

} // namespace storage
} // namespace oceanbase
