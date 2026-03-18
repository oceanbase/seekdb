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

#ifndef OCEANBASE_SHARE_STORAGE_OB_RESERVED_SNAPSHOT_TABLE_STORAGE_H_
#define OCEANBASE_SHARE_STORAGE_OB_RESERVED_SNAPSHOT_TABLE_STORAGE_H_

#include "share/storage/ob_sqlite_connection_pool.h"
#include "lib/container/ob_iarray.h"
#include "lib/net/ob_addr.h"

namespace oceanbase
{
namespace share
{

// Reserved snapshot entry structure
struct ObReservedSnapshotEntry
{
  int64_t tenant_id_;
  uint64_t snapshot_type_;
  common::ObAddr svr_addr_;
  uint64_t create_time_;
  uint64_t snapshot_version_;
  uint64_t status_;

  ObReservedSnapshotEntry()
    : tenant_id_(0),
      snapshot_type_(0),
      svr_addr_(),
      create_time_(0),
      snapshot_version_(0),
      status_(0)
  {}

  TO_STRING_EMPTY();

  void reset()
  {
    tenant_id_ = 0;
    snapshot_type_ = 0;
    svr_addr_.reset();
    create_time_ = 0;
    snapshot_version_ = 0;
    status_ = 0;
  }
};

class ObReservedSnapshotTableStorage
{
public:
  ObReservedSnapshotTableStorage();
  virtual ~ObReservedSnapshotTableStorage();

  // Initialize with shared connection pool instance
  int init(ObSQLiteConnectionPool *pool);

  bool is_inited() const { return nullptr != pool_; }

  // Insert or update multiple snapshot entries (used for batch insert/update)
  int insert_or_update(
      const uint64_t tenant_id,
      const ObIArray<ObReservedSnapshotEntry> &entries);

  // Update status for all entries of a tenant on a server
  int update_status(
      const uint64_t tenant_id,
      const common::ObAddr &svr_addr,
      const uint64_t status);

  // Query one snapshot entry
  int get(
      const uint64_t tenant_id,
      const uint64_t snapshot_type,
      const common::ObAddr &svr_addr,
      ObReservedSnapshotEntry &entry);

  // Query all snapshot entries for a tenant
  int get_all(
      const uint64_t tenant_id,
      ObIArray<ObReservedSnapshotEntry> &entries);

  // Delete expired entries for a tenant on a server
  int delete_expired(
      const uint64_t tenant_id,
      const common::ObAddr &svr_addr);

private:
  // Create table if not exists
  int create_table_if_not_exists();

  ObSQLiteConnectionPool *pool_;
};

} // namespace share
} // namespace oceanbase

#endif // OCEANBASE_SHARE_STORAGE_OB_RESERVED_SNAPSHOT_TABLE_STORAGE_H_
