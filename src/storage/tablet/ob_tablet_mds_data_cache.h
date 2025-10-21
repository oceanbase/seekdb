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

#ifndef OCEANBASE_STORAGE_OB_TABLET_MDS_DATA_CACHE
#define OCEANBASE_STORAGE_OB_TABLET_MDS_DATA_CACHE

#include <stdint.h>
#include "storage/tablet/ob_tablet_status.h"

namespace oceanbase
{
namespace storage
{
class ObTabletCreateDeleteMdsUserData;
class ObTabletBindingMdsUserData;

// Only for cache the most frequent case, Nomal tablet status
class ObTabletStatusCache final
{
public:
  ObTabletStatusCache();
  ObTabletStatusCache(const ObTabletStatusCache&) = delete;
  ObTabletStatusCache &operator=(const ObTabletStatusCache&) = delete;
public:

  void set_value(const ObTabletCreateDeleteMdsUserData &user_data);

  ObTabletStatus get_tablet_status() const { return tablet_status_; }
  int64_t get_create_commit_version() const { return create_commit_version_; }
  int64_t get_delete_commit_version() const { return delete_commit_version_; }

  void reset();

  bool is_valid() const { return tablet_status_.is_valid(); }

public:
  TO_STRING_KV(K_(tablet_status),
               K_(create_commit_version),
               K_(delete_commit_version));
private:
  ObTabletStatus tablet_status_;
  int64_t create_commit_version_;
  int64_t delete_commit_version_;
};


class ObDDLInfoCache final
{
public:
  ObDDLInfoCache();
  ObDDLInfoCache(const ObDDLInfoCache&) = delete;
  ObDDLInfoCache &operator=(const ObDDLInfoCache&) = delete;
public:

  void set_value(const ObTabletBindingMdsUserData &user_data);

  bool is_redefined() const { return redefined_; }
  int64_t get_schema_version() const { return schema_version_; }
  int64_t get_snapshot_version() const { return snapshot_version_; }

  void reset();

  bool is_valid() const
  {
    return INT64_MAX != schema_version_
        && INT64_MAX != snapshot_version_;
  }

public:
  TO_STRING_KV(K_(redefined),
               K_(schema_version),
               K_(snapshot_version));
private:
  bool redefined_;
  int64_t schema_version_;
  int64_t snapshot_version_;
};

class ObTruncateInfoCache final
{
public:
  ObTruncateInfoCache()
    : newest_commit_version_(INT64_MAX),
      newest_schema_version_(INT64_MAX),
      cnt_(0),
      replay_seq_(0)
  {}
  ~ObTruncateInfoCache() { reset(); }
  bool is_valid() const
  {
    return INT64_MAX != newest_commit_version_ && INT64_MAX != newest_schema_version_ && cnt_ >= 0;
  }
  int64_t newest_commit_version() const { return newest_commit_version_; }
  int64_t newest_schema_version() const { return newest_schema_version_; }
  int64_t replay_seq() const { return replay_seq_; }
  int64_t count() const { return cnt_; }
  bool is_empty() const
  {
    return 0 == cnt_;
  }
  void replay_truncate_info()
  {
    newest_commit_version_ = INT64_MAX;
    newest_schema_version_ = INT64_MAX;
    cnt_ = 0;
    replay_seq_++;
  }
  void set_empty() { set_value(0, 0, 0); }
  void set_value(
    const int64_t newest_commit_version,
    const int64_t newest_schema_version,
    const uint32_t cnt)
  {
    newest_commit_version_ = newest_commit_version;
    newest_schema_version_ = newest_schema_version;
    cnt_ = cnt;
  }
  TO_STRING_KV(K_(newest_commit_version), K_(newest_schema_version), K_(cnt), K_(replay_seq));
private:
  void reset()
  {
    newest_commit_version_ = INT64_MAX;
    newest_schema_version_ = INT64_MAX;
    cnt_ = 0;
    replay_seq_ = 0;
  }
private:
  int64_t newest_commit_version_;
  int64_t newest_schema_version_;
  uint32_t cnt_;
  uint8_t replay_seq_;
};

} // namespace storage
} // namespace oceanbase

#endif // OCEANBASE_STORAGE_OB_TABLET_MDS_DATA_CACHE
