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

#define USING_LOG_PREFIX STORAGE

#include "storage/ob_tablet_restore_state.h"
#include "lib/utility/serialization.h"

namespace oceanbase
{
namespace storage
{

bool ObTabletRestoreStatus::is_valid(const STATUS status)
{
  return FULL <= status && status < RESTORE_STATUS_MAX;
}

int ObTabletRestoreStatus::check_can_change_status(
    const STATUS cur_status,
    const STATUS change_status,
    bool &can_change)
{
  int ret = OB_SUCCESS;
  can_change = false;

  if (!is_valid(cur_status) || !is_valid(change_status)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid restore status", K(ret), K(cur_status), K(change_status));
  } else {
    switch (cur_status) {
      case PENDING:
        can_change = UNDEFINED == change_status || EMPTY == change_status || PENDING == change_status;
        break;
      case UNDEFINED:
        can_change = PENDING == change_status || UNDEFINED == change_status;
        break;
      case EMPTY:
        can_change = MINOR_AND_MAJOR_META == change_status || REMOTE == change_status
            || FULL == change_status || EMPTY == change_status || UNDEFINED == change_status;
        break;
      case MINOR_AND_MAJOR_META:
        can_change = FULL == change_status || MINOR_AND_MAJOR_META == change_status;
        break;
      case REMOTE:
        can_change = FULL == change_status || REMOTE == change_status;
        break;
      case FULL:
        can_change = FULL == change_status || EMPTY == change_status;
        break;
      default:
        ret = OB_INVALID_ARGUMENT;
        LOG_ERROR("invalid restore status", K(ret), K(cur_status));
        break;
    }
  }
  return ret;
}

ObTabletRestoreState::ObTabletRestoreState()
  : status_(ObTabletRestoreStatus::RESTORE_STATUS_MAX)
{
}

bool ObTabletRestoreState::is_valid() const
{
  return ObTabletRestoreStatus::is_valid(status_);
}

int ObTabletRestoreState::serialize(char *buf, const int64_t len, int64_t &pos) const
{
  return serialization::encode_i8(buf, len, pos, static_cast<int8_t>(status_));
}

int ObTabletRestoreState::deserialize(const char *buf, const int64_t len, int64_t &pos)
{
  int ret = OB_SUCCESS;
  int8_t status = 0;
  if (OB_FAIL(serialization::decode_i8(buf, len, pos, &status))) {
    LOG_WARN("failed to deserialize restore state", K(ret), K(len), K(pos));
  } else if (!ObTabletRestoreStatus::is_valid(static_cast<ObTabletRestoreStatus::STATUS>(status))) {
    ret = OB_INVALID_DATA;
    LOG_WARN("invalid serialized restore state", K(ret), K(status));
  } else {
    status_ = static_cast<ObTabletRestoreStatus::STATUS>(status);
  }
  return ret;
}

int64_t ObTabletRestoreState::get_serialize_size() const
{
  return serialization::encoded_length_i8(static_cast<int8_t>(status_));
}

void ObTabletRestoreState::reset()
{
  status_ = ObTabletRestoreStatus::RESTORE_STATUS_MAX;
}

int ObTabletRestoreState::init_status()
{
  status_ = ObTabletRestoreStatus::FULL;
  return OB_SUCCESS;
}

int ObTabletRestoreState::set_restore_status(const ObTabletRestoreStatus::STATUS status)
{
  int ret = OB_SUCCESS;
  if (!ObTabletRestoreStatus::is_valid(status)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid restore status", K(ret), K(status));
  } else {
    status_ = status;
  }
  return ret;
}

int ObTabletRestoreState::get_restore_status(ObTabletRestoreStatus::STATUS &status) const
{
  int ret = OB_SUCCESS;
  if (!is_valid()) {
    ret = OB_NOT_INIT;
    LOG_WARN("restore state is not initialized", K(ret), KPC(this));
  } else {
    status = status_;
  }
  return ret;
}

} // namespace storage
} // namespace oceanbase
