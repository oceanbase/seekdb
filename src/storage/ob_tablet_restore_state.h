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

#ifndef OCEANBASE_STORAGE_TABLET_RESTORE_STATE_
#define OCEANBASE_STORAGE_TABLET_RESTORE_STATE_

#include <stdint.h>
#include "lib/utility/ob_print_utils.h"

namespace oceanbase
{
namespace storage
{

class ObTabletRestoreStatus final
{
public:
  enum STATUS : uint8_t
  {
    FULL = 0,
    EMPTY = 1,
    MINOR_AND_MAJOR_META = 2,
    PENDING = 3,
    UNDEFINED = 4,
    REMOTE = 5,
    RESTORE_STATUS_MAX
  };

  static bool is_valid(const STATUS status);
  static bool is_full(const STATUS status) { return FULL == status; }
  static bool is_empty(const STATUS status) { return EMPTY == status; }
  static bool is_minor_and_major_meta(const STATUS status) { return MINOR_AND_MAJOR_META == status; }
  static bool is_pending(const STATUS status) { return PENDING == status; }
  static bool is_undefined(const STATUS status) { return UNDEFINED == status; }
  static bool is_remote(const STATUS status) { return REMOTE == status; }
  static int check_can_change_status(
      const STATUS cur_status,
      const STATUS change_status,
      bool &can_change);
};

class ObTabletRestoreState final
{
public:
  ObTabletRestoreState();
  ~ObTabletRestoreState() = default;

  bool is_valid() const;
  int serialize(char *buf, const int64_t len, int64_t &pos) const;
  int deserialize(const char *buf, const int64_t len, int64_t &pos);
  int64_t get_serialize_size() const;
  void reset();
  int init_status();

  bool is_restore_status_full() const { return ObTabletRestoreStatus::is_full(status_); }
  bool is_restore_status_pending() const { return ObTabletRestoreStatus::is_pending(status_); }
  bool is_restore_status_undefined() const { return ObTabletRestoreStatus::is_undefined(status_); }
  bool is_restore_status_empty() const { return ObTabletRestoreStatus::is_empty(status_); }
  bool is_restore_status_minor_and_major_meta() const
  {
    return ObTabletRestoreStatus::is_minor_and_major_meta(status_);
  }
  bool is_restore_status_remote() const { return ObTabletRestoreStatus::is_remote(status_); }
  bool check_allow_read() const
  {
    return is_restore_status_full() || is_restore_status_remote();
  }

  int set_restore_status(const ObTabletRestoreStatus::STATUS status);
  int get_restore_status(ObTabletRestoreStatus::STATUS &status) const;
  int64_t get_state_value() const { return static_cast<int64_t>(status_); }

  TO_STRING_KV(K_(status));

private:
  ObTabletRestoreStatus::STATUS status_;
};

} // namespace storage
} // namespace oceanbase

#endif
