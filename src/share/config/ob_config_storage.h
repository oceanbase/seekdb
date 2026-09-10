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

#ifndef OCEANBASE_SHARE_CONFIG_OB_CONFIG_STORAGE_H_
#define OCEANBASE_SHARE_CONFIG_OB_CONFIG_STORAGE_H_

#include "share/config/ob_system_config.h"
#include "share/config/ob_system_config_key.h"
#include "share/config/ob_system_config_value.h"
#include "share/storage/ob_sqlite_connection_pool.h"

struct sqlite3_stmt;
namespace oceanbase
{
namespace common
{

class ObConfigStorage
{
public:
  ObConfigStorage();
  virtual ~ObConfigStorage();

  // Initialize with shared connection pool instance
  int init(share::ObSQLiteConnectionPool *pool);

  int load_all_configs(ObSystemConfig &system_config);
  int get_config_value(const char *name, ObString &value, common::ObIAllocator &allocator);
  int upsert_config(
      const char *name,
      const char *data_type,
      const char *value,
      const char *info,
      const char *section,
      const char *scope,
      const char *source,
      const char *edit_level);

  bool is_inited() const { return nullptr != pool_; }

private:
  // Create table if not exists
  int create_table_if_not_exists();

  share::ObSQLiteConnectionPool *pool_;
  DISALLOW_COPY_AND_ASSIGN(ObConfigStorage);
};

} // namespace common
} // namespace oceanbase

#endif // OCEANBASE_SHARE_CONFIG_OB_CONFIG_STORAGE_H_
