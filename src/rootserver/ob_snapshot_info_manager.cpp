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

#define USING_LOG_PREFIX RS
#include "ob_snapshot_info_manager.h"
#include "share/ob_ddl_common.h"
#include "common/ob_timeout_ctx.h"
#include "lib/mysqlclient/ob_mysql_transaction.h"
#include "ob_rs_event_history_table_operator.h"

using namespace oceanbase::common;
using namespace oceanbase::share;
using namespace oceanbase::share::schema;
using namespace oceanbase::palf;
namespace oceanbase
{
namespace rootserver
{
int ObSnapshotInfoManager::init(const ObAddr &self_addr)
{
  int ret = OB_SUCCESS;
  if (!self_addr.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(self_addr));
  } else {
    self_addr_ = self_addr;
  }
  return ret;
}



int ObSnapshotInfoManager::batch_acquire_snapshot(
    common::ObMySQLTransaction &trans,
    share::ObSnapShotType snapshot_type,
    const uint64_t tenant_id,
    const int64_t schema_version,
    const SCN &snapshot_scn,
    const char *comment,
    const common::ObIArray<ObTabletID> &tablet_ids)
{
  int ret = OB_SUCCESS;
  ObSnapshotTableProxy snapshot_proxy;
  ObSnapshotInfo snapshot;
  ObTimeoutCtx timeout_ctx;
  snapshot.snapshot_type_ = snapshot_type;
  snapshot.tenant_id_ = tenant_id;
  snapshot.snapshot_scn_ = snapshot_scn;
  snapshot.schema_version_ = schema_version;
  snapshot.comment_ = comment;
  if (OB_UNLIKELY(!trans.is_started() || !snapshot.is_valid() || tablet_ids.count() <= 0)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), "trans_started", trans.is_started(), K(snapshot), K(tablet_ids.count()));
  } else {
    int64_t rpc_timeout = 0;
    int64_t trx_timeout = 0;
    if (OB_FAIL(ObDDLUtil::get_ddl_rpc_timeout(tablet_ids.count(), rpc_timeout))) {
      LOG_WARN("get ddl rpc timeout failed", K(ret), K(tablet_ids.count()));
    } else if (OB_FAIL(ObDDLUtil::get_ddl_tx_timeout(tablet_ids.count(), trx_timeout))) {
      LOG_WARN("get ddl tx timeout failed", K(ret), K(tablet_ids.count()));
    } else if (OB_FAIL(timeout_ctx.set_trx_timeout_us(trx_timeout))) {
      LOG_WARN("set trx timeout failed", K(ret), K(trx_timeout));
    } else if (OB_FAIL(timeout_ctx.set_timeout(rpc_timeout))) {
      LOG_WARN("set timeout failed", K(ret), K(rpc_timeout));
    } else if (OB_FAIL(snapshot_proxy.batch_add_snapshot(trans, snapshot_type,
        tenant_id, schema_version, snapshot.snapshot_scn_, comment, tablet_ids))) {
      LOG_WARN("batch add snapshot failed", K(ret));
    }
    ROOTSERVICE_EVENT_ADD("snapshot", "batch_acquire_snapshot", K(ret), K(snapshot), "rs_addr", self_addr_);
  }
  
  return ret;
}


int ObSnapshotInfoManager::batch_release_snapshot_in_trans(
    common::ObMySQLTransaction &trans,
    share::ObSnapShotType snapshot_type,
    const uint64_t tenant_id,
    const int64_t schema_version,
    const SCN &snapshot_scn,
    const common::ObIArray<ObTabletID> &tablet_ids)
{
  int ret = OB_SUCCESS;
  ObSnapshotTableProxy snapshot_proxy;
  ObSnapshotInfo snapshot;
  snapshot.snapshot_type_ = snapshot_type;
  snapshot.tenant_id_ = tenant_id;
  snapshot.snapshot_scn_ = snapshot_scn;
  snapshot.schema_version_ = schema_version;
  if (OB_UNLIKELY(tablet_ids.count() <= 0)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(tablet_ids.count()));
  } else if (OB_FAIL(snapshot_proxy.batch_remove_snapshots(trans,
                                                           snapshot_type,
                                                           tenant_id,
                                                           schema_version,
                                                           snapshot.snapshot_scn_,
                                                           tablet_ids))) {
    LOG_WARN("fail to batch remove snapshots", K(ret));
  }
  ROOTSERVICE_EVENT_ADD("snapshot", "batch_release_snapshot", K(ret), K(snapshot), "rs_addr", self_addr_);
  return ret;
}



int ObSnapshotInfoManager::check_restore_point(common::ObMySQLProxy &proxy,
                                               const uint64_t tenant_id,
                                               const int64_t table_id,
                                               bool &is_exist)
{
  int ret = OB_SUCCESS;
  is_exist = false;
  ObSnapshotTableProxy snapshot_proxy;
  if (OB_FAIL(snapshot_proxy.check_snapshot_exist(proxy, tenant_id, table_id,
      share::SNAPSHOT_FOR_RESTORE_POINT, is_exist))) {
    LOG_WARN("fail to check snapshot exist", K(ret), K(tenant_id), K(table_id));
  }
  return ret;
}


} //end rootserver
} //end oceanbase

