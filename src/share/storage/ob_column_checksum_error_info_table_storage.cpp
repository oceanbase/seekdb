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

#include "share/storage/ob_column_checksum_error_info_table_storage.h"
#include "lib/oblog/ob_log.h"
#include "share/ob_column_checksum_error_operator.h"
#include "share/storage/local_store.h"

namespace oceanbase
{
namespace share
{

ObColumnChecksumErrorInfoTableStorage::ObColumnChecksumErrorInfoTableStorage()
  : store_(nullptr)
{
}

ObColumnChecksumErrorInfoTableStorage::~ObColumnChecksumErrorInfoTableStorage()
{
}

int ObColumnChecksumErrorInfoTableStorage::init(LocalStore *store)
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

int ObColumnChecksumErrorInfoTableStorage::insert(const ObColumnChecksumErrorInfo &error_info)
{
  int ret = OB_SUCCESS;
  if (!is_inited()) {
    ret = OB_NOT_INIT;
  } else {
    const auto insert_request = LocalRequest::insert(checksum_error::schema())
        .set(checksum_error::FrozenScn, error_info.frozen_scn_.get_val_for_inner_table_field())
        .set(checksum_error::IndexType, error_info.is_global_index_ ? 1 : 0)
        .set(checksum_error::DataTableId, error_info.data_table_id_)
        .set(checksum_error::IndexTableId, error_info.index_table_id_)
        .set(checksum_error::DataTabletId, error_info.data_tablet_id_.id())
        .set(checksum_error::IndexTabletId, error_info.index_tablet_id_.id())
        .set(checksum_error::ColumnId, error_info.column_id_)
        .set(checksum_error::DataColumnChecksum, error_info.data_column_checksum_)
        .set(checksum_error::IndexColumnChecksum, error_info.index_column_checksum_);

    ret = store_->execute(insert_request);
  }
  return ret;
}

int ObColumnChecksumErrorInfoTableStorage::insert_all(const ObIArray<ObColumnChecksumErrorInfo> &error_infos)
{
  int ret = OB_SUCCESS;
  if (!is_inited()) {
    ret = OB_NOT_INIT;
  } else if (error_infos.empty()) {
    // do nothing
  } else {
    ret = store_->write([&](LocalTransaction &tx) -> int {
      for (int64_t i = 0; OB_SUCC(ret) && i < error_infos.count(); ++i) {
        const ObColumnChecksumErrorInfo &error_info = error_infos.at(i);
        const auto insert_request = LocalRequest::insert(checksum_error::schema())
            .set(checksum_error::FrozenScn, error_info.frozen_scn_.get_val_for_inner_table_field())
            .set(checksum_error::IndexType, error_info.is_global_index_ ? 1 : 0)
            .set(checksum_error::DataTableId, error_info.data_table_id_)
            .set(checksum_error::IndexTableId, error_info.index_table_id_)
            .set(checksum_error::DataTabletId, error_info.data_tablet_id_.id())
            .set(checksum_error::IndexTabletId, error_info.index_tablet_id_.id())
            .set(checksum_error::ColumnId, error_info.column_id_)
            .set(checksum_error::DataColumnChecksum, error_info.data_column_checksum_)
            .set(checksum_error::IndexColumnChecksum, error_info.index_column_checksum_);

        ret = tx.execute(insert_request);
      }

      return ret;
    });
  }
  return ret;
}

int ObColumnChecksumErrorInfoTableStorage::get(
    const SCN &frozen_scn,
    const bool is_global_index,
    const int64_t data_table_id,
    const int64_t index_table_id,
    const common::ObTabletID &data_tablet_id,
    const common::ObTabletID &index_tablet_id,
    ObIArray<ObColumnChecksumErrorInfo> &error_infos)
{
  int ret = OB_SUCCESS;
  error_infos.reset();
  if (!is_inited()) {
    ret = OB_NOT_INIT;
  } else {
    const auto select_request = LocalRequest::select(checksum_error::schema(), {checksum_error::FrozenScn, checksum_error::IndexType, checksum_error::DataTableId, checksum_error::IndexTableId, checksum_error::DataTabletId, checksum_error::IndexTabletId, checksum_error::ColumnId, checksum_error::DataColumnChecksum, checksum_error::IndexColumnChecksum})
        .eq(checksum_error::FrozenScn, frozen_scn.get_val_for_inner_table_field())
        .eq(checksum_error::IndexType, is_global_index ? 1 : 0)
        .eq(checksum_error::DataTableId, data_table_id)
        .eq(checksum_error::IndexTableId, index_table_id)
        .eq(checksum_error::DataTabletId, data_tablet_id.id())
        .eq(checksum_error::IndexTabletId, index_tablet_id.id())
        .order(checksum_error::ColumnId);

    auto row_processor = [&](LocalRowReader &reader) -> int {
      ObColumnChecksumErrorInfo error_info;
      uint64_t frozen_scn_val = reader.get_int64();
      int64_t index_type = reader.get_int64();
      int64_t data_table_id_val = reader.get_int64();
      int64_t index_table_id_val = reader.get_int64();
      int64_t data_tablet_id_val = reader.get_int64();
      int64_t index_tablet_id_val = reader.get_int64();
      int64_t column_id_val = reader.get_int64();
      int64_t data_column_checksum_val = reader.get_int64();
      int64_t index_column_checksum_val = reader.get_int64();

      error_info.frozen_scn_.convert_for_inner_table_field(frozen_scn_val);
      error_info.is_global_index_ = (index_type != 0);
      error_info.data_table_id_ = data_table_id_val;
      error_info.index_table_id_ = index_table_id_val;
      error_info.data_tablet_id_ = ObTabletID(data_tablet_id_val);
      error_info.index_tablet_id_ = ObTabletID(index_tablet_id_val);
      error_info.column_id_ = column_id_val;
      error_info.data_column_checksum_ = data_column_checksum_val;
      error_info.index_column_checksum_ = index_column_checksum_val;

      if (OB_FAIL(error_infos.push_back(error_info))) {
      }
      return ret;
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

int ObColumnChecksumErrorInfoTableStorage::delete_expired(
    const SCN &frozen_scn_before,
    int64_t limit)
{
  int ret = OB_SUCCESS;
  if (!is_inited()) {
    ret = OB_NOT_INIT;
  } else {
    const auto request = LocalRequest::erase(checksum_error::schema())
        .lt(checksum_error::FrozenScn, static_cast<int64_t>(frozen_scn_before.get_val_for_inner_table_field())).order(checksum_error::FrozenScn).take(limit);
    ret = store_->execute(request);
  }
  return ret;
}

} // namespace share
} // namespace oceanbase
