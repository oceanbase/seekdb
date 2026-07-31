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

#define USING_LOG_PREFIX RS_COMPACTION

#include "rootserver/freeze/ob_major_freeze_coordinator_adapter.h"
#include "lib/container/ob_array.h"
#include "rootserver/freeze/ob_major_freeze_helper.h"
#include "rootserver/freeze/ob_major_freeze_service.h"
#include "rootserver/freeze/ob_major_freeze_util.h"
#include "share/ob_server_struct.h"
#include "share/tablet/ob_tablet_info.h"

namespace oceanbase
{
namespace rootserver
{

using common::ObArray;
using data_plane::ObMajorMergeTabletDiagnostic;
using share::ObTabletRuntimeInfo;

ObMajorFreezeCoordinatorAdapter::ObMajorFreezeCoordinatorAdapter()
  : primary_service_(nullptr),
    restore_service_(nullptr)
{
}

int ObMajorFreezeCoordinatorAdapter::init(
    ObPrimaryMajorFreezeService &primary_service,
    ObRestoreMajorFreezeService &restore_service)
{
  int ret = OB_SUCCESS;
  if (OB_NOT_NULL(primary_service_) || OB_NOT_NULL(restore_service_)) {
    ret = OB_INIT_TWICE;
    LOG_WARN("major freeze coordinator adapter is already initialized",
             KR(ret));
  } else {
    primary_service_ = &primary_service;
    restore_service_ = &restore_service;
  }
  return ret;
}

void ObMajorFreezeCoordinatorAdapter::reset()
{
  primary_service_ = nullptr;
  restore_service_ = nullptr;
}

int ObMajorFreezeCoordinatorAdapter::get_frozen_scn(
    share::SCN &frozen_scn) const
{
  return ObMajorFreezeHelper::get_frozen_scn(frozen_scn);
}

int ObMajorFreezeCoordinatorAdapter::trigger_memstore_pressure_major_freeze()
{
  int ret = OB_SUCCESS;
  ObMajorFreezeParam param;
  param.freeze_reason_ = MF_MAJOR_COMPACT_TRIGGER;
  if (OB_FAIL(ObMajorFreezeHelper::major_freeze(param))) {
    LOG_WARN("failed to trigger memstore-pressure major freeze",
             KR(ret), K(param));
  }
  return ret;
}

int ObMajorFreezeCoordinatorAdapter::collect_major_merge_diagnostics(
    bool &need_diagnose,
    bool &is_paused,
    common::ObIArray<ObMajorMergeTabletDiagnostic> &uncompacted_tablets,
    common::ObIArray<uint64_t> &uncompacted_table_ids) const
{
  int ret = OB_SUCCESS;
  need_diagnose = false;
  is_paused = true;
  uncompacted_tablets.reset();
  uncompacted_table_ids.reset();
  if (OB_ISNULL(primary_service_) || OB_ISNULL(restore_service_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("major freeze coordinator adapter is not initialized", KR(ret));
  } else {
    ObMajorFreezeService *service = nullptr;
    bool is_primary_service = true;
    if (OB_FAIL(ObMajorFreezeUtil::get_major_freeze_service(
            primary_service_, restore_service_, service,
            is_primary_service))) {
      if (OB_LEADER_NOT_EXIST == ret) {
        ret = OB_SUCCESS;
        LOG_INFO("skip major merge diagnostics while freeze leader switches");
      } else {
        LOG_WARN("failed to select major freeze service", KR(ret));
      }
    } else if (OB_ISNULL(service)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("selected major freeze service is null", KR(ret));
    } else {
      need_diagnose = true;
      is_paused = service->is_paused();
      if (!is_paused) {
        ObArray<ObTabletRuntimeInfo> tablets;
        ObArray<uint64_t> table_ids;
        if (OB_FAIL(service->get_uncompacted_tablets(tablets, table_ids))) {
          if (OB_LEADER_NOT_EXIST == ret) {
            ret = OB_SUCCESS;
            need_diagnose = false;
            is_paused = true;
            LOG_INFO("skip major merge diagnostics after freeze leader changed");
          } else {
            LOG_WARN("failed to collect uncompacted tablets", KR(ret));
          }
        }
        for (int64_t i = 0; OB_SUCC(ret) && i < tablets.count(); ++i) {
          const ObTabletRuntimeInfo &tablet = tablets.at(i);
          ObMajorMergeTabletDiagnostic diagnostic;
          diagnostic.tablet_id_ = tablet.get_tablet_id();
          diagnostic.server_ = GCTX.self_addr();
          diagnostic.snapshot_version_ = tablet.get_snapshot_version();
          diagnostic.report_scn_ = tablet.get_report_scn();
          diagnostic.checksum_error_ =
              ObTabletRuntimeInfo::SCN_STATUS_ERROR == tablet.get_status();
          if (OB_FAIL(uncompacted_tablets.push_back(diagnostic))) {
            LOG_WARN("failed to append major merge tablet diagnostic",
                     KR(ret), K(tablet));
          }
        }
        for (int64_t i = 0; OB_SUCC(ret) && i < table_ids.count(); ++i) {
          if (OB_FAIL(uncompacted_table_ids.push_back(table_ids.at(i)))) {
            LOG_WARN("failed to append uncompacted table id",
                     KR(ret), K(i));
          }
        }
      }
    }
  }
  return ret;
}

} // namespace rootserver
} // namespace oceanbase
