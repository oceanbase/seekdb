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
 
#pragma once

#include "lib/container/ob_iarray.h"
#include "lib/compress/ob_compress_util.h"
#include "src/storage/direct_load/ob_direct_load_struct.h"

namespace oceanbase
{
namespace share
{
namespace schema
{
class ObSchemaGetterGuard;
class ObTableSchema;
}
}
namespace observer
{
class ObTableLoadExecCtx;
class ObTableLoadInstance;
}
namespace common
{
class ObTabletID;
}
namespace storage
{
struct ObDirectLoadLevel;
}

namespace sql
{
class ObExecContext;
class ObPhysicalPlan;
class ObSqlCtx;

class ObTableDirectInsertCtx
{
public:
  ObTableDirectInsertCtx()
    : load_exec_ctx_(nullptr),
      table_load_instance_(nullptr),
      is_inited_(false),
      is_direct_(false),
      is_online_gather_statistics_(false),
      online_sample_percent_(1.),
      force_inc_direct_write_(false) {}
  ~ObTableDirectInsertCtx();
  TO_STRING_KV(K_(is_inited), K_(is_direct), K_(is_online_gather_statistics),
               K_(force_inc_direct_write));

public:
  int init(sql::ObExecContext *exec_ctx,
           sql::ObPhysicalPlan &phy_plan,
           const uint64_t table_id,
           const int64_t parallel,
           const bool is_incremental,
           const bool enable_inc_replace,
           const bool is_insert_overwrite,
           const double online_sample_percent);
  int commit();
  int finish();
  void destroy();

  bool get_is_direct() const { return is_direct_; }
  void set_is_direct(bool is_direct) { is_direct_ = is_direct; }
  bool get_is_online_gather_statistics() const {
    return is_online_gather_statistics_;
  }
  bool get_force_inc_direct_write() const { return force_inc_direct_write_; }

  void set_is_online_gather_statistics(const bool is_online_gather_statistics) {
    is_online_gather_statistics_ = is_online_gather_statistics;
  }

  void set_online_sample_percent(double online_sample_percent) {
    online_sample_percent_ = online_sample_percent;
  }

  void set_force_inc_direct_write(const bool force_inc_direct_write) {
    force_inc_direct_write_ = force_inc_direct_write;
  }

  double get_online_sample_percent() const {
    return online_sample_percent_;
  }

private:
  int get_partition_level_tablet_ids(const sql::ObPhysicalPlan &phy_plan,
                                     const share::schema::ObTableSchema *table_schema,
                                     common::ObIArray<common::ObTabletID> &tablet_ids);
private:
  observer::ObTableLoadExecCtx *load_exec_ctx_;
  observer::ObTableLoadInstance *table_load_instance_;
  bool is_inited_;
  bool is_direct_; //indict whether the plan is direct load plan including insert into append and load data direct
  bool is_online_gather_statistics_;
  double online_sample_percent_;
  bool force_inc_direct_write_;
};
} // namespace observer
} // namespace oceanbase
