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

#ifndef OCEANBASE_SHARE_AUTOINCREMENT_OB_I_TABLET_AUTOINCREMENT_ADMIN_H_
#define OCEANBASE_SHARE_AUTOINCREMENT_OB_I_TABLET_AUTOINCREMENT_ADMIN_H_

namespace oceanbase
{
namespace common
{
template <typename T> class ObIArray;
class ObMySQLTransaction;
class ObTabletID;
}
namespace share
{
struct ObTabletAutoincSeqCopyParam;
namespace schema
{
class ObDatabaseSchema;
class ObSchemaGetterGuard;
class ObSchemaGuardWrapper;
class ObTableSchema;
}

// Administrative operations whose persistence, retry, and MDS details belong
// to Storage. DDL orchestration and transaction ownership remain with callers.
class ObITabletAutoincrementAdmin
{
public:
  virtual ~ObITabletAutoincrementAdmin() {}

  // Registers the sequence copies in trans. The copies become visible only
  // when the caller commits the transaction. Both arrays must be non-empty and
  // have the same number of valid tablet IDs.
  virtual int copy_sequences_for_fork(
      const common::ObIArray<common::ObTabletID> &source_tablet_ids,
      const common::ObIArray<common::ObTabletID> &destination_tablet_ids,
      common::ObMySQLTransaction &trans) = 0;

  // Cache invalidation is deliberately two-phase: collect tablet IDs while
  // the old schema is still readable, then invalidate only after the schema
  // transaction commits. Collection appends to cache_tablet_ids so one plan
  // can cover multiple tables.
  virtual int collect_table_cache_invalidation(
      schema::ObSchemaGetterGuard &schema_guard,
      const schema::ObTableSchema &table_schema,
      common::ObIArray<common::ObTabletID> &cache_tablet_ids) = 0;
  virtual int collect_table_cache_invalidation(
      schema::ObSchemaGuardWrapper &schema_guard,
      const schema::ObTableSchema &table_schema,
      common::ObIArray<common::ObTabletID> &cache_tablet_ids) = 0;
  virtual int collect_database_cache_invalidation(
      const schema::ObDatabaseSchema &database_schema,
      common::ObIArray<common::ObTabletID> &cache_tablet_ids) = 0;
  virtual int invalidate_caches(
      const common::ObIArray<common::ObTabletID> &cache_tablet_ids) = 0;

  // Single-node sequence migration. Rootserver owns retry and result
  // reconciliation; Storage owns the tablet state and local handler.
  virtual int read_migration_sequences(
      const common::ObIArray<ObTabletAutoincSeqCopyParam> &request_params,
      common::ObIArray<ObTabletAutoincSeqCopyParam> &result_params) = 0;
  virtual int write_migration_sequences(
      const common::ObIArray<ObTabletAutoincSeqCopyParam> &request_params,
      common::ObIArray<ObTabletAutoincSeqCopyParam> &result_params) = 0;
};

} // namespace share
} // namespace oceanbase

#endif // OCEANBASE_SHARE_AUTOINCREMENT_OB_I_TABLET_AUTOINCREMENT_ADMIN_H_
