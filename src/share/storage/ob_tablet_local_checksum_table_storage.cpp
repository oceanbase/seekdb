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

#define USING_LOG_PREFIX SHARE

// local storage for local tablet checksums.

#include "share/storage/ob_tablet_local_checksum_table_storage.h"
#include "share/storage/local_store.h"
#include "lib/oblog/ob_log.h"
#include "lib/utility/ob_print_utils.h"
#include "share/ob_tablet_local_checksum_operator.h"

namespace oceanbase
{
namespace share
{

ObTabletLocalChecksumTableStorage::ObTabletLocalChecksumTableStorage()
  : store_(nullptr)
{
}

ObTabletLocalChecksumTableStorage::~ObTabletLocalChecksumTableStorage()
{
}

int ObTabletLocalChecksumTableStorage::init(LocalStore *store)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(store_ = store)) {
    ret = OB_INVALID_ARGUMENT;
  } else if (!store_->is_inited()) {
    ret = OB_NOT_INIT;
  }
  if (OB_FAIL(ret)) {
    store_ = nullptr;
  }
  return ret;
}

int ObTabletLocalChecksumTableStorage::batch_get(
    const ObIArray<common::ObTabletID> &tablet_ids,
    const SCN &compaction_scn,
    ObLocalTabletChecksumArray &items,
    const bool include_larger_than)
{
  int ret = OB_SUCCESS;
  items.reset();
  if (!is_inited()) {
    ret = OB_NOT_INIT;
  } else if (tablet_ids.empty()) {
    // do nothing
  } else {
    auto request = LocalRequest::select(tablet_checksum::schema(), {tablet_checksum::TabletId, tablet_checksum::CompactionScn, tablet_checksum::RowCount, tablet_checksum::DataChecksum, tablet_checksum::ColumnChecksums, tablet_checksum::BColumnChecksums, tablet_checksum::DataChecksumType})
        .in(tablet_checksum::TabletId, tablet_ids).order(tablet_checksum::TabletId);
    for (int64_t i = 0; OB_SUCC(ret) && i < tablet_ids.count(); ++i) {
      if (!tablet_ids.at(i).is_valid()) { ret = OB_INVALID_ARGUMENT; }
    }
    if (compaction_scn.is_valid()) {
      const auto scn = static_cast<int64_t>(compaction_scn.get_val_for_inner_table_field());
      if (include_larger_than) { request.ge(tablet_checksum::CompactionScn, scn); }
      else { request.eq(tablet_checksum::CompactionScn, scn); }
    }

    if (OB_SUCC(ret)) {
      auto row_processor = [&](LocalRowReader &reader) -> int {
        ObTabletLocalChecksumItem item;
        int64_t tablet_id_val = reader.get_int64();
        uint64_t compaction_scn_val = reader.get_int64();
        int64_t row_count = reader.get_int64();
        int64_t data_checksum = reader.get_int64();
        int column_checksums_len = 0;
        int b_column_checksums_len = 0;
        const char *column_checksums_str = reader.get_text(&column_checksums_len);
        const void *b_column_checksums_blob = reader.get_blob(&b_column_checksums_len);
        int64_t data_checksum_type = reader.get_int64();
        UNUSED(column_checksums_str);
        if (!is_valid_data_checksum_type(static_cast<ObDataChecksumType>(data_checksum_type))) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("invalid local checksum type", K(ret), K(data_checksum_type), K(tablet_id_val));
        } else if (OB_FAIL(item.compaction_scn_.convert_for_inner_table_field(compaction_scn_val))) {
        } else if (OB_ISNULL(b_column_checksums_blob) || b_column_checksums_len <= 0) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("local column checksum metadata is empty", K(ret), K(tablet_id_val));
        } else {
          item.tablet_id_ = ObTabletID(tablet_id_val);
          item.row_count_ = row_count;
          item.data_checksum_ = data_checksum;
          item.data_checksum_type_ = static_cast<ObDataChecksumType>(data_checksum_type);
          common::ObString b_column_checksums_obstr(b_column_checksums_len, static_cast<const char *>(b_column_checksums_blob));
          if (OB_FAIL(item.column_meta_.set_with_str(item.data_checksum_type_, b_column_checksums_obstr))) {
          }
        }

        if (OB_SUCC(ret) && OB_UNLIKELY(!item.is_valid())) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("invalid local checksum item", K(ret), K(item));
        } else if (OB_SUCC(ret) && OB_FAIL(items.push_back(item))) {
        }
        return ret;
      };

      if (OB_FAIL(store_->query(request, row_processor))) {
        if (OB_ENTRY_NOT_EXIST != ret) {
        } else {
          ret = OB_SUCCESS; // No rows is acceptable
        }
      }
    }
  }
  return ret;
}

int ObTabletLocalChecksumTableStorage::get_row_count(
    const common::ObTabletID &tablet_id,
    int64_t &row_count)
{
  int ret = OB_SUCCESS;
  row_count = 0;
  if (!is_inited()) {
    ret = OB_NOT_INIT;
  } else {
    const auto select_request = LocalRequest::select(tablet_checksum::schema(), {tablet_checksum::RowCount})
        .eq(tablet_checksum::TabletId, tablet_id.id());

    auto row_processor = [&](LocalRowReader &reader) -> int {
      int64_t value = reader.get_int64();
      if (value > 0) {
        row_count = value;
      }
      return OB_SUCCESS;
    };

    if (OB_FAIL(store_->query(select_request, row_processor))) {
      if (OB_ENTRY_NOT_EXIST != ret) {
      } else {
        ret = OB_SUCCESS; // No rows is acceptable
      }
    }
  }
  return ret;
}

} // namespace share
} // namespace oceanbase
