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
#ifndef OCEANBASE_SHARE_STORAGE_OB_SERVER_EVENT_HISTORY_TABLE_STORAGE_H_
#define OCEANBASE_SHARE_STORAGE_OB_SERVER_EVENT_HISTORY_TABLE_STORAGE_H_

#include "lib/ob_define.h"
#include "lib/container/ob_iarray.h"
#include "lib/net/ob_addr.h"
#include "share/storage/ob_sqlite_connection_pool.h"
#include <functional>

namespace oceanbase
{
namespace share
{

enum class ObEventHistoryType
{
  SERVER = 0,
  ROOTSERVICE = 1,
  CLUSTER = 2,
  TENANT = 3
};

struct ObServerEventHistoryEntry
{
  int64_t gmt_create_;
  ObEventHistoryType event_type_;
  common::ObAddr svr_addr_;  // For server events, or rs_svr_addr for rootservice events
  common::ObString module_;
  common::ObString event_;
  common::ObString name1_;
  common::ObString value1_;
  common::ObString name2_;
  common::ObString value2_;
  common::ObString name3_;
  common::ObString value3_;
  common::ObString name4_;
  common::ObString value4_;
  common::ObString name5_;
  common::ObString value5_;
  common::ObString name6_;
  common::ObString value6_;
  common::ObString extra_info_;

  ObServerEventHistoryEntry()
    : gmt_create_(0),
      event_type_(ObEventHistoryType::SERVER),
      svr_addr_(),
      module_(),
      event_(),
      name1_(),
      value1_(),
      name2_(),
      value2_(),
      name3_(),
      value3_(),
      name4_(),
      value4_(),
      name5_(),
      value5_(),
      name6_(),
      value6_(),
      extra_info_()
  {}

  void reset() {
    gmt_create_ = 0;
    event_type_ = ObEventHistoryType::SERVER;
    svr_addr_.reset();
    module_.reset();
    event_.reset();
    name1_.reset();
    value1_.reset();
    name2_.reset();
    value2_.reset();
    name3_.reset();
    value3_.reset();
    name4_.reset();
    value4_.reset();
    name5_.reset();
    value5_.reset();
    name6_.reset();
    value6_.reset();
    extra_info_.reset();
  }
  TO_STRING_EMPTY();
};

class ObServerEventHistoryTableStorage
{
public:
  ObServerEventHistoryTableStorage();
  virtual ~ObServerEventHistoryTableStorage();

  int init(ObSQLiteConnectionPool *pool);
  bool is_inited() const { return nullptr != pool_; }

  int insert(const ObServerEventHistoryEntry &entry);
  int insert_all(const ObIArray<ObServerEventHistoryEntry> &entries);
  int delete_expired(int64_t gmt_create_before, int64_t limit);

private:
  int create_table_if_not_exists();

  ObSQLiteConnectionPool *pool_;
  DISALLOW_COPY_AND_ASSIGN(ObServerEventHistoryTableStorage);
};

} // namespace share
} // namespace oceanbase

#endif // OCEANBASE_SHARE_STORAGE_OB_SERVER_EVENT_HISTORY_TABLE_STORAGE_H_
