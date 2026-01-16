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

#define USING_LOG_PREFIX SHARE

#include "share/ob_ddl_common.h"
#include "share/ob_ddl_sim_point.h"
#include "share/ob_server_struct.h"
#include "share/schema/ob_multi_version_schema_service.h"
#include "share/schema/ob_schema_service.h"
#include "share/ob_fork_table_util.h"
#include "rootserver/ob_root_service.h"
#include "rootserver/ddl_task/ob_ddl_task.h"
#include "lib/mysqlclient/ob_mysql_transaction.h"

using namespace oceanbase;
using namespace oceanbase::share;
using namespace oceanbase::common;
using namespace oceanbase::share::schema;

int ObForkTableUtil::collect_tablet_ids_from_table(
    schema::ObSchemaGetterGuard &schema_guard,
    const uint64_t tenant_id,
    const uint64_t table_id,
    common::ObIArray<common::ObTabletID> &tablet_ids)
{
  int ret = OB_SUCCESS;
  const ObTableSchema *table_schema = nullptr;
  tablet_ids.reset();

  if (OB_FAIL(schema_guard.get_table_schema(tenant_id, table_id, table_schema))) {
    LOG_WARN("fail to get table schema", K(ret), K(table_id));
  } else if (OB_ISNULL(table_schema)) {
    ret = OB_TABLE_NOT_EXIST;
    LOG_WARN("table not exist", K(ret), K(table_id));
  } else if (OB_FAIL(ObForkTableUtil::collect_tablet_ids_from_table(schema_guard, tenant_id, *table_schema, tablet_ids))) {
    LOG_WARN("fail to collect tablet ids", K(ret), K(table_id));
  }

  return ret;
}

int ObForkTableUtil::collect_tablet_ids_from_table(
    ObSchemaGetterGuard &schema_guard,
    const uint64_t tenant_id,
    const ObTableSchema &table_schema,
    common::ObIArray<common::ObTabletID> &tablet_ids)
{
  int ret = OB_SUCCESS;
  tablet_ids.reset();

  if (OB_UNLIKELY(OB_INVALID_ID == tenant_id)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid tenant_id", K(ret), K(tenant_id));
  } else {
    // 1. Get main table tablet IDs
    ObSEArray<ObTabletID, 4> main_tablet_ids;
    if (OB_FAIL(table_schema.get_tablet_ids(main_tablet_ids))) {
      LOG_WARN("failed to get tablet ids", K(ret), K(table_schema.get_table_id()));
    } else {
      for (int64_t i = 0; OB_SUCC(ret) && i < main_tablet_ids.count(); ++i) {
        if (OB_FAIL(tablet_ids.push_back(main_tablet_ids.at(i)))) {
          LOG_WARN("failed to push back main tablet id", K(ret));
        }
      }
    }

    // 2. Get all index table tablet IDs (including domain index auxiliary tables)
    if (OB_FAIL(ret)) {
    } else if (OB_FAIL(collect_index_tablet_ids(schema_guard, tenant_id, table_schema, tablet_ids))) {
      LOG_WARN("failed to collect index tablet ids", K(ret));
    }

    // 3. Get LOB auxiliary table tablet IDs
    if (OB_FAIL(ret)) {
    } else if (OB_FAIL(collect_lob_aux_tablet_ids(schema_guard, tenant_id, table_schema, tablet_ids))) {
      LOG_WARN("failed to collect LOB aux tablet ids", K(ret));
    }
  }
  return ret;
}

int ObForkTableUtil::collect_index_tablet_ids(
    ObSchemaGetterGuard &schema_guard,
    const uint64_t tenant_id,
    const ObTableSchema &table_schema,
    common::ObIArray<common::ObTabletID> &tablet_ids)
{
  int ret = OB_SUCCESS;
  ObArray<ObAuxTableMetaInfo> simple_index_infos;

  if (OB_FAIL(table_schema.get_simple_index_infos(simple_index_infos))) {
    LOG_WARN("get simple index infos failed", K(ret));
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < simple_index_infos.count(); ++i) {
      const ObTableSchema *index_schema = nullptr;
      const uint64_t index_table_id = simple_index_infos.at(i).table_id_;

      if (OB_FAIL(schema_guard.get_table_schema(tenant_id, index_table_id, index_schema))) {
        LOG_WARN("get index table schema failed", K(ret), K(tenant_id), K(index_table_id));
      } else if (OB_ISNULL(index_schema)) {
        ret = OB_TABLE_NOT_EXIST;
        LOG_WARN("index table schema is null", K(ret), K(tenant_id), K(index_table_id));
      } else {
        ObSEArray<ObTabletID, 4> index_tablet_ids;
        if (OB_FAIL(index_schema->get_tablet_ids(index_tablet_ids))) {
          LOG_WARN("failed to get index tablet ids", K(ret), K(index_table_id));
        } else {
          for (int64_t j = 0; OB_SUCC(ret) && j < index_tablet_ids.count(); ++j) {
            if (OB_FAIL(tablet_ids.push_back(index_tablet_ids.at(j)))) {
              LOG_WARN("failed to push back index tablet id", K(ret));
            }
          }
        }
      }
    }
  }
  return ret;
}

int ObForkTableUtil::collect_lob_aux_tablet_ids(
    ObSchemaGetterGuard &schema_guard,
    const uint64_t tenant_id,
    const ObTableSchema &table_schema,
    common::ObIArray<common::ObTabletID> &tablet_ids)
{
  int ret = OB_SUCCESS;
  const uint64_t lob_meta_tid = table_schema.get_aux_lob_meta_tid();
  const uint64_t lob_piece_tid = table_schema.get_aux_lob_piece_tid();

  if (OB_INVALID_ID != lob_meta_tid) {
    const ObTableSchema *lob_meta_schema = nullptr;
    if (OB_FAIL(schema_guard.get_table_schema(tenant_id, lob_meta_tid, lob_meta_schema))) {
      LOG_WARN("get LOB meta table schema failed", K(ret), K(lob_meta_tid));
    } else if (OB_ISNULL(lob_meta_schema)) {
      ret = OB_TABLE_NOT_EXIST;
      LOG_WARN("LOB meta table schema is null", K(ret), K(lob_meta_tid));
    } else {
      ObSEArray<ObTabletID, 4> lob_meta_tablet_ids;
      if (OB_FAIL(lob_meta_schema->get_tablet_ids(lob_meta_tablet_ids))) {
        LOG_WARN("failed to get LOB meta tablet ids", K(ret));
      } else {
        for (int64_t j = 0; OB_SUCC(ret) && j < lob_meta_tablet_ids.count(); ++j) {
          if (OB_FAIL(tablet_ids.push_back(lob_meta_tablet_ids.at(j)))) {
            LOG_WARN("failed to push back LOB meta tablet id", K(ret));
          }
        }
      }
    }
  }

  if (OB_SUCC(ret) && OB_INVALID_ID != lob_piece_tid) {
    const ObTableSchema *lob_piece_schema = nullptr;
    if (OB_FAIL(schema_guard.get_table_schema(tenant_id, lob_piece_tid, lob_piece_schema))) {
      LOG_WARN("get LOB piece table schema failed", K(ret), K(lob_piece_tid));
    } else if (OB_ISNULL(lob_piece_schema)) {
      ret = OB_TABLE_NOT_EXIST;
      LOG_WARN("LOB piece table schema is null", K(ret), K(lob_piece_tid));
    } else {
      ObSEArray<ObTabletID, 4> lob_piece_tablet_ids;
      if (OB_FAIL(lob_piece_schema->get_tablet_ids(lob_piece_tablet_ids))) {
        LOG_WARN("failed to get LOB piece tablet ids", K(ret));
      } else {
        for (int64_t j = 0; OB_SUCC(ret) && j < lob_piece_tablet_ids.count(); ++j) {
          if (OB_FAIL(tablet_ids.push_back(lob_piece_tablet_ids.at(j)))) {
            LOG_WARN("failed to push back LOB piece tablet id", K(ret));
          }
        }
      }
    }
  }
  return ret;
}

int ObForkTableUtil::collect_table_ids_from_table(
    const ObTableSchema &table_schema,
    common::ObIArray<uint64_t> &table_ids)
{
  int ret = OB_SUCCESS;
  table_ids.reset();

  // 1. main table
  const uint64_t main_table_id = table_schema.get_table_id();
  if (OB_FAIL(table_ids.push_back(main_table_id))) {
    LOG_WARN("fail to push back main table id", K(ret), K(main_table_id));
  }

  // 2. index table
  if (OB_SUCC(ret)) {
    ObArray<ObAuxTableMetaInfo> simple_index_infos;
    if (OB_FAIL(table_schema.get_simple_index_infos(simple_index_infos))) {
      LOG_WARN("fail to get simple index infos", K(ret), K(table_schema));
    } else {
      for (int64_t i = 0; OB_SUCC(ret) && i < simple_index_infos.count(); ++i) {
        const uint64_t index_table_id = simple_index_infos.at(i).table_id_;
        if (OB_INVALID_ID != index_table_id && OB_FAIL(table_ids.push_back(index_table_id))) {
          LOG_WARN("fail to push back index table id", K(ret), K(index_table_id));
        }
      }
    }
  }

  // 3. lob aux tables
  if (OB_SUCC(ret)) {
    const uint64_t lob_meta_table_id = table_schema.get_aux_lob_meta_tid();
    const uint64_t lob_piece_table_id = table_schema.get_aux_lob_piece_tid();

    if (OB_INVALID_ID != lob_meta_table_id && OB_FAIL(table_ids.push_back(lob_meta_table_id))) {
      LOG_WARN("fail to push back LOB meta table id", K(ret), K(lob_meta_table_id));
    } else if (OB_INVALID_ID != lob_piece_table_id && OB_FAIL(table_ids.push_back(lob_piece_table_id))) {
      LOG_WARN("fail to push back LOB piece table id", K(ret), K(lob_piece_table_id));
    }
  }

  return ret;
}

int ObForkTableUtil::get_tablet_ids(
    const common::ObIArray<share::schema::ObTableSchema> &table_schemas,
    common::ObIArray<common::ObTabletID> &tablet_ids)
{
  int ret = OB_SUCCESS;
  tablet_ids.reset();
  for (int64_t i = 0; OB_SUCC(ret) && i < table_schemas.count(); ++i) {
    const share::schema::ObTableSchema &schema = table_schemas.at(i);
    if (OB_FAIL(schema.get_tablet_ids(tablet_ids))) {
      LOG_WARN("failed to get tablet ids from table schema", KR(ret), K(schema));
    }
  }
  return ret;
}

int ObForkTableUtil::obtain_snapshot(
    common::ObMySQLTransaction &trans,
    schema::ObSchemaGetterGuard &schema_guard,
    const ObTableSchema &data_table_schema,
    int64_t &new_fetched_snapshot)
{
  int ret = OB_SUCCESS;
  const uint64_t tenant_id = data_table_schema.get_tenant_id();
  const int64_t schema_version = data_table_schema.get_schema_version();
  rootserver::ObDDLService &ddl_service = GCTX.root_service_->get_ddl_service();
  new_fetched_snapshot = 0;
  ObSEArray<ObTabletID, 4> tablet_ids;
  SCN snapshot_scn;
  if (OB_FAIL(ObDDLUtil::calc_snapshot_with_gts(new_fetched_snapshot, tenant_id))) {
    LOG_WARN("fail to calc snapshot with gts", K(ret), K(new_fetched_snapshot));
  } else if (new_fetched_snapshot <= 0) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("the snapshot is not valid", K(ret), K(new_fetched_snapshot));
  } else if (OB_FAIL(snapshot_scn.convert_for_tx(new_fetched_snapshot))) {
    LOG_WARN("failed to convert snapshot", K(ret), K(new_fetched_snapshot));
  } else if (OB_FAIL(ObForkTableUtil::collect_tablet_ids_from_table(schema_guard, tenant_id, data_table_schema, tablet_ids))) {
    LOG_WARN("fail to collect tablet ids", K(ret), K(data_table_schema));
  } else if (OB_FAIL(ddl_service.get_snapshot_mgr().batch_acquire_snapshot(
            trans, SNAPSHOT_FOR_DDL, tenant_id, data_table_schema.get_schema_version(), snapshot_scn, nullptr, tablet_ids))) {
    LOG_WARN("batch acquire snapshot failed", K(ret), K(tablet_ids));
  } else {
    LOG_INFO("hold snapshot finished", K(snapshot_scn), K(schema_version), "tablet_cnt", tablet_ids.count());
    LOG_DEBUG("hold snapshot detail", K(snapshot_scn), K(schema_version), K(tablet_ids));
  }
  return ret;
}

int ObForkTableUtil::release_snapshot(
    rootserver::ObDDLTask* task,
    schema::ObSchemaGetterGuard &schema_guard,
    const uint64_t table_id,
    const int64_t snapshot_version)
{
  int ret = OB_SUCCESS;
  ObSEArray<ObTabletID, 2> tablet_ids;
  if (OB_ISNULL(task)) {
    ret = OB_BAD_NULL_ERROR;
    LOG_WARN("invalid argument", K(ret));
  } else if (!task->is_inited()) {
    ret = OB_NOT_INIT;
    LOG_WARN("args have not been inited", K(ret), K(task->get_task_type()));
  } else {
    uint64_t tenant_id = task->get_src_tenant_id();
    int64_t schema_version = task->get_src_schema_version();
    if (OB_FAIL(DDL_SIM(tenant_id, task->get_task_id(), DDL_TASK_RELEASE_SNAPSHOT_FAILED))) {
      LOG_WARN("ddl sim failure", K(ret), K(tenant_id), K(task->get_task_id()));
    } else if (OB_FAIL(ObForkTableUtil::collect_tablet_ids_from_table(schema_guard, tenant_id, table_id, tablet_ids))) {
      LOG_WARN("failed to get tablet ids", K(ret), K(tenant_id), K(table_id));
    } else if (OB_FAIL(task->batch_release_snapshot(snapshot_version, tablet_ids))) {
      LOG_WARN("failed to release snapshot", K(ret));
    }
    task->add_event_info("release snapshot finish");
    LOG_INFO("release snapshot finished", K(snapshot_version), K(table_id), K(schema_version),
        "tablet_cnt", tablet_ids.count(), "ddl_event_info", ObDDLEventInfo());
    LOG_DEBUG("release snapshot detail", K(snapshot_version), K(table_id), K(schema_version), K(tablet_ids));
  }
  return ret;
}

OB_SERIALIZE_MEMBER(ObForkTableInfo, fork_src_table_id_, fork_snapshot_version_);
OB_SERIALIZE_MEMBER(ObForkTabletInfo, fork_info_, fork_snapshot_version_, fork_src_tablet_id_);
