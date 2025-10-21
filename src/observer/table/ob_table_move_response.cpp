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

#define USING_LOG_PREFIX SERVER
#include "ob_table_move_response.h"
#include "share/location_cache/ob_location_service.h"

using namespace oceanbase::observer;
using namespace oceanbase::common;
using namespace oceanbase::share;
using namespace oceanbase::table;
////////////////////////////////////////////////////////////////

int ObTableMoveResponseSender::get_replica(const uint64_t table_id,
                                           const ObTabletID &tablet_id,
                                           table::ObTableMoveReplicaInfo &replica)
{
  int ret = OB_SUCCESS;
  bool is_cache_hit = false;
  int64_t expire_renew_time = INT64_MAX; // For the get interface, a maximum value needs to be passed, indicating that the latest location cache should be retrieved and the old one invalidated
  share::ObLSID ls_id;
  share::ObLSLocation ls_loc;
  share::ObLSReplicaLocation replica_loc;

  if (OB_FAIL(GCTX.location_service_->get(MTL_ID(), tablet_id, expire_renew_time, is_cache_hit, ls_id))) {
    LOG_WARN("fail to get partition", K(ret), K(table_id), K(tablet_id));
  } else if (OB_FAIL(GCTX.location_service_->get(GCONF.cluster_id, MTL_ID(), ls_id, expire_renew_time, is_cache_hit, ls_loc))) {
    LOG_WARN("fail get partition", K(ret), K(table_id), K(tablet_id));
  } else if (OB_FAIL(ls_loc.get_leader(replica_loc))) {
    LOG_WARN("fail to get strong leader replica", K(ret));
  } else {
    replica.server_ = replica_loc.get_server();
    replica.role_ = replica_loc.get_role();
    replica.replica_type_ = replica_loc.get_replica_type();
    replica.part_renew_time_ = ls_loc.get_renew_time();
  }

  return ret;
}

int ObTableMoveResponseSender::init(const uint64_t table_id,
                                    const ObTabletID &tablet_id,
                                    share::schema::ObMultiVersionSchemaService &schema_service)
{
  int ret = OB_SUCCESS;
  ObTableMoveReplicaInfo &replica = result_.get_replica_info();
  share::schema::ObSchemaGetterGuard schema_guard;
  const share::schema::ObTableSchema *table_schema = nullptr;

  if (OB_FAIL(schema_service.get_tenant_schema_guard(MTL_ID(), schema_guard))) {
    LOG_WARN("fail to get schema guard", K(ret));
  } else if (OB_FAIL(schema_guard.get_table_schema(MTL_ID(), table_id, table_schema))) {
    LOG_WARN("fail to get table schema", K(table_id), K(ret));
  } else if (OB_ISNULL(table_schema)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("NULL ptr", K(ret), K(table_id));
  } else {
    ObTabletID tmp_tablet_id = tablet_id;
    if (!table_schema->is_partitioned_table()) {
      tmp_tablet_id = table_schema->get_tablet_id();
    } 
    
    if (OB_FAIL(get_replica(table_id, tmp_tablet_id, replica))) {
      LOG_WARN("fail to get partition info", K(ret), K(table_id), K(tmp_tablet_id));
    } else {
      replica.set_table_id(table_id);
      replica.set_schema_version(table_schema->get_schema_version());
      replica.set_tablet_id(tmp_tablet_id);

      // set move pcode
      response_sender_.set_pcode(obrpc::OB_TABLE_API_MOVE);
      LOG_DEBUG("move response init successfully", K(replica));
    }
  }

  return ret;
}
