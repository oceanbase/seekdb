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

#ifndef OCEANBASE_SHARE_OB_FORK_TABLE_UTIL_H
#define OCEANBASE_SHARE_OB_FORK_TABLE_UTIL_H

#include "common/ob_tablet_id.h"
#include "share/ob_define.h"
#include "lib/container/ob_iarray.h"
#include "lib/utility/ob_print_utils.h"
#include "lib/utility/ob_unify_serialize.h"

namespace oceanbase
{
namespace common
{
class ObMySQLTransaction;
}
namespace rootserver
{
class ObDDLTask;
}
namespace share
{
namespace schema
{
class ObSchemaGetterGuard;
class ObTableSchema;
}

class ObForkTableInfo final
{
  OB_UNIS_VERSION(1);
public:
  ObForkTableInfo() : fork_src_table_id_(OB_INVALID_ID), fork_snapshot_version_(0) {}
  ObForkTableInfo(const uint64_t fork_src_table_id, const int64_t fork_snapshot_version)
    : fork_src_table_id_(fork_src_table_id), fork_snapshot_version_(fork_snapshot_version) {}
  ~ObForkTableInfo() = default;
  void reset() { fork_src_table_id_ = OB_INVALID_ID; fork_snapshot_version_ = 0; }
  bool is_valid() const { return OB_INVALID_ID != fork_src_table_id_ && fork_snapshot_version_ > 0; }
  uint64_t get_fork_src_table_id() const { return fork_src_table_id_; }
  void set_fork_src_table_id(const uint64_t table_id) { fork_src_table_id_ = table_id; }
  int64_t get_fork_snapshot_version() const { return fork_snapshot_version_; }
  void set_fork_snapshot_version(const int64_t version) { fork_snapshot_version_ = version; }
  TO_STRING_KV(K_(fork_src_table_id), K_(fork_snapshot_version));
private:
  uint64_t fork_src_table_id_;
  int64_t fork_snapshot_version_;
};

class ObForkTabletInfo final
{
  OB_UNIS_VERSION(1);
public:
  ObForkTabletInfo() : fork_info_(0), fork_snapshot_version_(0), fork_src_tablet_id_() { }
  ~ObForkTabletInfo() { reset(); }
  void reset() { fork_info_ = 0; fork_snapshot_version_ = 0; fork_src_tablet_id_.reset(); }
  bool is_valid() const { return fork_snapshot_version_ > 0 && fork_src_tablet_id_.is_valid(); }
  bool is_complete() const { return is_complete_; }
  void set_complete() { is_complete_ = 1; }
  void clear_complete() { is_complete_ = 0; }
  int64_t get_fork_snapshot_version() const { return fork_snapshot_version_; }
  void set_fork_snapshot_version(int64_t version) { fork_snapshot_version_ = version; }
  const ObTabletID &get_fork_src_tablet_id() const { return fork_src_tablet_id_; }
  void set_fork_src_tablet_id(const ObTabletID &tablet_id) { fork_src_tablet_id_ = tablet_id; }
  TO_STRING_KV(K_(fork_info), K_(fork_snapshot_version), K_(fork_src_tablet_id));
private:
  union {
    uint32_t fork_info_;
    struct {
      uint32_t is_complete_: 1;
      uint32_t reserved_: 31;
    };
  };
  int64_t fork_snapshot_version_;
  ObTabletID fork_src_tablet_id_;
};

class ObForkTableUtil final
{
public:
  static int collect_tablet_ids_from_table(
      schema::ObSchemaGetterGuard &schema_guard,
      const uint64_t tenant_id,
      const uint64_t table_id,
      common::ObIArray<common::ObTabletID> &tablet_ids);

  static int collect_tablet_ids_from_table(
      share::schema::ObSchemaGetterGuard &schema_guard,
      const uint64_t tenant_id,
      const share::schema::ObTableSchema &table_schema,
      common::ObIArray<common::ObTabletID> &tablet_ids);

  static int collect_index_tablet_ids(
      share::schema::ObSchemaGetterGuard &schema_guard,
      const uint64_t tenant_id,
      const share::schema::ObTableSchema &table_schema,
      common::ObIArray<common::ObTabletID> &tablet_ids);

  static int collect_lob_aux_tablet_ids(
      share::schema::ObSchemaGetterGuard &schema_guard,
      const uint64_t tenant_id,
      const share::schema::ObTableSchema &table_schema,
      common::ObIArray<common::ObTabletID> &tablet_ids);

  static int collect_table_ids_from_table(
      const share::schema::ObTableSchema &table_schema,
      common::ObIArray<uint64_t> &table_ids);

  static int get_tablet_ids(
      const common::ObIArray<share::schema::ObTableSchema> &table_schemas,
      common::ObIArray<common::ObTabletID> &tablet_ids);

  static int obtain_snapshot(
      common::ObMySQLTransaction &trans,
      schema::ObSchemaGetterGuard &schema_guard,
      const share::schema::ObTableSchema &data_table_schema,
      int64_t &new_fetched_snapshot);

  static int release_snapshot(
      rootserver::ObDDLTask* task,
      schema::ObSchemaGetterGuard &schema_guard,
      const uint64_t table_id,
      const int64_t snapshot_version);
};

} // namespace share
} // namespace oceanbase

#endif // OCEANBASE_SHARE_OB_FORK_TABLE_UTIL_H
