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
#include "lib/net/ob_addr.h" // ObAddr

namespace oceanbase
{
namespace share
{
class ObTabletReplicaFilter;

enum class ObDataChecksumType : uint8_t
{
  DATA_CHECKSUM_NORMAL = 0,
  DATA_CHECKSUM_NORMAL_WITH_NORMAL_COLUMN = 1, // with hex column checksums
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


class ObTabletReplica
{
public:
  enum ScnStatus
  {
    SCN_STATUS_IDLE = 0,
    SCN_STATUS_ERROR,
    SCN_STATUS_MAX
  };

  ObTabletReplica();
  virtual ~ObTabletReplica();
  void reset();
  inline bool is_valid() const
  {
    return tablet_id_.is_valid_with_tenant()
        && server_.is_valid()
        && snapshot_version_ >= 0
        && data_size_ >= 0
        && required_size_ >= 0
        && report_scn_ >= 0
        && is_status_valid(status_);
  }
  inline bool primary_keys_are_valid() const
  {
    return tablet_id_.is_valid_with_tenant()
        && server_.is_valid();
  }
  int assign(const ObTabletReplica &other);
  
  inline const common::ObTabletID &get_tablet_id() const { return tablet_id_; }
  inline const common::ObAddr &get_server() const { return server_; }
  inline int64_t get_snapshot_version() const { return snapshot_version_; }
  inline int64_t get_data_size() const { return data_size_; }
  inline int64_t get_required_size() const { return required_size_; }
  inline int64_t get_report_scn() const { return report_scn_; }
  inline ScnStatus get_status() const { return status_; }
  int init(
      const common::ObTabletID &tablet_id,
      const common::ObAddr &server,
      const int64_t snapshot_version,
      const int64_t data_size,
      const int64_t required_size,
      const int64_t report_scn,
      const ScnStatus status);
  void fake_for_diagnose(const common::ObTabletID &tablet_id);
  bool is_equal_for_report(const ObTabletReplica &other) const;
  static bool is_status_valid(const ScnStatus status)
  {
    return status >= SCN_STATUS_IDLE && status < SCN_STATUS_MAX;
  }
  TO_STRING_KV(
      K_(tablet_id),
      K_(server),
      K_(snapshot_version),
      K_(data_size),
      K_(required_size),
      K_(report_scn),
      K_(status));
private:
  common::ObTabletID tablet_id_;
  common::ObAddr server_;
  int64_t snapshot_version_;
  int64_t data_size_; // load balancing releated
  int64_t required_size_; // load balancing releated
  // below: tablet level member for compaction
  int64_t report_scn_;
  ScnStatus status_;
};

class ObTabletInfo
{
public:
  ObTabletInfo();
  explicit ObTabletInfo(const common::ObTabletID &tablet_id);
  explicit ObTabletInfo(
      const common::ObTabletID &tablet_id,
      const ObTabletReplica &replica);
  virtual ~ObTabletInfo();
  void reset();
  inline bool is_valid() const
  {
    return true
        && tablet_id_.is_valid_with_tenant()
        && has_replica_
        && replica_.is_valid();
  }
  int assign(const ObTabletInfo &other);
  
  inline const common::ObTabletID &get_tablet_id() const { return tablet_id_; }
  inline bool has_replica() const { return has_replica_; }
  inline const ObTabletReplica &get_replica() const { return replica_; }
  int64_t replica_count() const { return has_replica_ ? 1 : 0; }
  int init_empty(const common::ObTabletID &tablet_id);
  int init(const common::ObTabletID &tablet_id,
           const ObTabletReplica &replica);
  int init_by_replica(const ObTabletReplica &replica);
  int set_replica(const ObTabletReplica &replica);
  bool is_self_replica(const ObTabletReplica &replica) const;
  int filter(const ObTabletReplicaFilter &filter);
  TO_STRING_KV(K_(tablet_id), K_(has_replica), K_(replica));
private:
  common::ObTabletID tablet_id_;
  bool has_replica_;
  ObTabletReplica replica_;
  DISALLOW_COPY_AND_ASSIGN(ObTabletInfo);
};

class ObTabletTablePair
{
public:
  ObTabletTablePair();
  ObTabletTablePair(const common::ObTabletID &tablet_id, const uint64_t table_id);
  ~ObTabletTablePair();

  void reset();
  int init(const ObTabletID &tablet_id, const uint64_t table_id);
  int assign(const ObTabletTablePair &other);
  bool is_valid() const;
  const ObTabletID &get_tablet_id() const { return tablet_id_; }
  uint64_t get_table_id() const { return table_id_; }
  TO_STRING_KV(K_(tablet_id), K_(table_id));
private:
  common::ObTabletID tablet_id_;
  uint64_t table_id_;
};

} // end namespace share
} // end namespace oceanbase
#endif
