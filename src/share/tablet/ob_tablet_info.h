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

#ifndef OCEANBASE_SHARE_OB_TABLET_INFO
#define OCEANBASE_SHARE_OB_TABLET_INFO

#include "common/ob_tablet_id.h" // ObTabletID

namespace oceanbase
{
namespace share
{
enum class ObDataChecksumType : uint8_t
{
  DATA_CHECKSUM_NORMAL = 0,
  DATA_CHECKSUM_NORMAL_WITH_NORMAL_COLUMN = 1,
  DATA_CHECKSUM_MAX
};

inline bool is_valid_data_checksum_type(const ObDataChecksumType &type)
{
  return type >= ObDataChecksumType::DATA_CHECKSUM_NORMAL
      && type < ObDataChecksumType::DATA_CHECKSUM_MAX;
}

inline bool is_normal_column_checksum_type(const ObDataChecksumType &type)
{
  return type == ObDataChecksumType::DATA_CHECKSUM_NORMAL_WITH_NORMAL_COLUMN;
}


class ObTabletRuntimeInfo
{
public:
  enum ScnStatus
  {
    SCN_STATUS_IDLE = 0,
    SCN_STATUS_ERROR,
    SCN_STATUS_MAX
  };

  ObTabletRuntimeInfo();
  virtual ~ObTabletRuntimeInfo();
  void reset();
  bool is_valid() const
  {
    return tablet_id_.is_valid()
        && snapshot_version_ >= 0
        && data_size_ >= 0
        && required_size_ >= 0
        && report_scn_ >= 0
        && create_transaction_id_ > 0
        && physical_create_version_ > 0 && physical_create_version_ < INT64_MAX
        && storage_layout_id_ != 0 && storage_layout_id_ != common::OB_INVALID_ID
        && is_status_valid(status_);
  }
  inline bool primary_keys_are_valid() const
  {
    return tablet_id_.is_valid();
  }
  int assign(const ObTabletRuntimeInfo &other);
  
  inline const common::ObTabletID &get_tablet_id() const { return tablet_id_; }
  inline int64_t get_snapshot_version() const { return snapshot_version_; }
  inline int64_t get_data_size() const { return data_size_; }
  inline int64_t get_required_size() const { return required_size_; }
  inline int64_t get_report_scn() const { return report_scn_; }
  inline ScnStatus get_status() const { return status_; }
  int64_t get_create_transaction_id() const { return create_transaction_id_; }
  int64_t get_physical_create_version() const { return physical_create_version_; }
  uint64_t get_storage_layout_id() const { return storage_layout_id_; }
  int init(
      const common::ObTabletID &tablet_id,
      const int64_t snapshot_version,
      const int64_t data_size,
      const int64_t required_size,
      const int64_t report_scn,
      const ScnStatus status,
      const int64_t create_transaction_id,
      const int64_t physical_create_version,
      const uint64_t storage_layout_id);
  void fake_for_diagnose(const common::ObTabletID &tablet_id);
  static bool is_status_valid(const ScnStatus status)
  {
    return status >= SCN_STATUS_IDLE && status < SCN_STATUS_MAX;
  }
  TO_STRING_KV(
      K_(tablet_id),
      K_(snapshot_version),
      K_(data_size),
      K_(required_size),
      K_(report_scn),
      K_(status), K_(create_transaction_id), K_(physical_create_version), K_(storage_layout_id));

private:
  common::ObTabletID tablet_id_;
  int64_t snapshot_version_;
  int64_t data_size_;
  int64_t required_size_;
  int64_t report_scn_;
  ScnStatus status_;
  int64_t create_transaction_id_;
  int64_t physical_create_version_;
  uint64_t storage_layout_id_;
};

class ObTabletTablePair
{
public:
  ObTabletTablePair();
  ObTabletTablePair(const common::ObTabletID &tablet_id, const uint64_t table_id);
  ~ObTabletTablePair();

  void reset();
  int init(const common::ObTabletID &tablet_id, const uint64_t table_id);
  int assign(const ObTabletTablePair &other);
  bool is_valid() const;
  const common::ObTabletID &get_tablet_id() const { return tablet_id_; }
  uint64_t get_table_id() const { return table_id_; }
  TO_STRING_KV(K_(tablet_id), K_(table_id));

private:
  common::ObTabletID tablet_id_;
  uint64_t table_id_;
};

} // end namespace share
} // end namespace oceanbase
#endif
