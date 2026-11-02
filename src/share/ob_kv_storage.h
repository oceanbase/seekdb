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

#ifndef OCEANBASE_SHARE_OB_KV_STORAGE_H_
#define OCEANBASE_SHARE_OB_KV_STORAGE_H_

#include "share/storage/ob_sqlite_connection_pool.h"
#include "lib/string/ob_string.h"

namespace oceanbase
{
namespace share
{

// Simple KV storage for tenant info and other simple information
class ObKVStorage
{
public:
  ObKVStorage();
  virtual ~ObKVStorage();

  // Initialize with shared connection pool instance
  int init(ObSQLiteConnectionPool *pool);

  // Get value by key
  int get(const common::ObString &key, common::ObString &value);

  // Set key-value pair
  int set(const common::ObString &key, const common::ObString &value);

  // Delete key
  int del(const common::ObString &key);

  bool is_inited() const { return nullptr != pool_; }

private:
  // Create table if not exists
  int create_table_if_not_exists();

  ObSQLiteConnectionPool *pool_;
  DISALLOW_COPY_AND_ASSIGN(ObKVStorage);
};

} // namespace share
} // namespace oceanbase

#endif // OCEANBASE_SHARE_OB_KV_STORAGE_H_
