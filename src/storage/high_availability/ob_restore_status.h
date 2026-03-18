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

#ifndef OCEANBASE_STORAGE_HA_RESTORE_STATUS_H_
#define OCEANBASE_STORAGE_HA_RESTORE_STATUS_H_

#include "storage/high_availability/ob_storage_ha_struct.h" // ObMigrationStatus

namespace oceanbase
{
namespace storage
{
class ObRestoreStatus final
{
  OB_UNIS_VERSION(1);
public:
  enum Status : uint8_t
  {
    NONE = 0,
    RESTORE_DOING = 1,
    RESTORE_DATA_TABLETS = 2,
    RESTORE_WAIT = 3,
    RESTORE_FAILED = 4,
    RESTORE_STATUS_MAX,
  };
public:
  ObRestoreStatus() : status_(Status::NONE) {}
  ~ObRestoreStatus() = default;
  explicit ObRestoreStatus(const Status &status);
  ObRestoreStatus &operator=(const Status &status) { status_ = status; return *this; }
  bool operator ==(const ObRestoreStatus &other) const { return status_ == other.get_status(); }
  bool operator !=(const ObRestoreStatus &other) const { return status_ != other.get_status(); }
  Status get_status() const { return status_; }
  bool is_valid() const { return status_ >= Status::NONE && status_ < Status::RESTORE_STATUS_MAX; }
  bool is_none() const { return Status::NONE == status_; }
  bool is_restore_doing() const { return Status::RESTORE_DOING == status_; }
  bool can_restore_log() const { return Status::NONE == status_
                                        || (status_ >= Status::RESTORE_DATA_TABLETS && status_ < Status::RESTORE_FAILED); }
  bool is_restore_wait() const { return Status::RESTORE_WAIT == status_; }
  bool is_restore_failed() const { return Status::RESTORE_FAILED == status_; }
  bool check_allow_read() const { return is_none(); }
  bool is_in_restore_status() const { return status_ >= Status::RESTORE_DOING && status_ <= Status::RESTORE_FAILED; }
  bool need_online() const { return is_none() || is_restore_wait(); }
public:
  static int check_can_change_status(
      const ObRestoreStatus &cur_status,
      const ObRestoreStatus &change_status,
      bool &can_change);

  TO_STRING_KV(K(status_));
private:
  Status status_;
};

} // namespace storage
} // namespace oceanbase

#endif // OCEANBASE_STORAGE_HA_RESTORE_STATUS_H_
