#include "rootserver/ob_root_service.h"
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

#include "rootserver/freeze/ob_tenant_major_freeze.h"

#include "share/ob_tablet_meta_table_compaction_operator.h"

namespace oceanbase
{
namespace rootserver
{
using namespace common;
using namespace share;

ObTenantMajorFreeze::ObTenantMajorFreeze()
  : is_inited_(false), is_primary_service_(true),
    major_merge_info_mgr_(), major_merge_info_detector_{},
    merge_scheduler_{}, daily_launcher_{}, schema_service_(nullptr)
{
}

ObTenantMajorFreeze::~ObTenantMajorFreeze()
{
}

int ObTenantMajorFreeze::init(
    const bool is_primary_service,
    ObMySQLProxy &sql_proxy,
    ObServerConfig &config,
    share::schema::ObMultiVersionSchemaService &schema_service)
{
  int ret = OB_SUCCESS;
  if (IS_INIT) {
    ret = OB_INIT_TWICE;
    LOG_WARN("init twice", KR(ret));
  } else if (OB_FAIL(major_merge_info_mgr_.init(sql_proxy))) {
    LOG_WARN("fail to init major merge info mgr", KR(ret));
  } else if (OB_FAIL(merge_scheduler_.init(is_primary_service, major_merge_info_mgr_,
             schema_service, config, sql_proxy))) {
    LOG_WARN("fail to init merge_scheduler", KR(ret), K(is_primary_service));
  }  else if (OB_FAIL(major_merge_info_detector_.init(is_primary_service, sql_proxy,
              major_merge_info_mgr_, merge_scheduler_.get_major_scheduler_idling()))) {
    LOG_WARN("fail to init freeze_info_detector", KR(ret), K(is_primary_service));
  } else if (is_primary_service) {
    if (OB_FAIL(daily_launcher_.init(config, sql_proxy, major_merge_info_mgr_))) {
      LOG_WARN("fail to init daily_launcher", KR(ret), K(is_primary_service));
    }
  }
  if (OB_SUCC(ret)) {
    is_primary_service_ = is_primary_service;
    schema_service_ = &schema_service;
    is_inited_ = true;
  }

  return ret;
}

int ObTenantMajorFreeze::start()
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", KR(ret));
  } else if (OB_FAIL(major_merge_info_detector_.start())) {
    LOG_WARN("fail to start freeze_info_detector", KR(ret));
  } else if (OB_FAIL(merge_scheduler_.start())) {
    LOG_WARN("fail to start merge_scheduler", KR(ret));
  } else if (is_primary_service()) {
    if (OB_FAIL(daily_launcher_.start())) {
      LOG_WARN("fail to start daily_launcher", KR(ret), K_(is_primary_service));
    }
  }
  return ret;
}

void ObTenantMajorFreeze::stop()
{
  if (is_primary_service()) {
    LOG_INFO("daily_launcher start to stop", K_(is_primary_service));
    daily_launcher_.stop();
  }
  LOG_INFO("freeze_info_detector start to stop", K_(is_primary_service));
  major_merge_info_detector_.stop();
  LOG_INFO("merge_scheduler start to stop", K_(is_primary_service));
  merge_scheduler_.stop();
}

int ObTenantMajorFreeze::wait()
{
  int ret = OB_SUCCESS;
  if (is_primary_service()) {
    LOG_INFO("daily_launcher start to wait", K_(is_primary_service));
    daily_launcher_.wait();
  }
  LOG_INFO("freeze_info_detector start to wait", K_(is_primary_service));
  major_merge_info_detector_.wait();
  LOG_INFO("merge_scheduler start to wait", K_(is_primary_service));
  merge_scheduler_.wait();
  return ret;
}

int ObTenantMajorFreeze::destroy()
{
  int ret = OB_SUCCESS;
  if (is_primary_service()) {
    LOG_INFO("daily_launcher start to destroy", K_(is_primary_service));
    if (OB_FAIL(daily_launcher_.destroy())) {
      LOG_WARN("fail to destroy daily_launcher", KR(ret), K_(is_primary_service));
    }
  }
  if (OB_SUCC(ret)) {
    LOG_INFO("freeze_info_detector start to destroy", K_(is_primary_service));
    if (OB_FAIL(major_merge_info_detector_.destroy())) {
      LOG_WARN("fail to destroy freeze_info_detector", KR(ret), K_(is_primary_service));
    }
  }
  if (OB_SUCC(ret)) {
    LOG_INFO("merge_scheduler start to destroy", K_(is_primary_service));
    if (OB_FAIL(merge_scheduler_.destroy())) {
      LOG_WARN("fail to destroy merge_scheduler", KR(ret), K_(is_primary_service));
    }
  }
  return ret;
}

void ObTenantMajorFreeze::pause()
{
  if (is_primary_service()) {
    daily_launcher_.pause();
  }
  major_merge_info_detector_.pause();
  merge_scheduler_.pause();
}

void ObTenantMajorFreeze::resume()
{
  if (is_primary_service()) {
    daily_launcher_.resume();
  }
  major_merge_info_detector_.resume();
  merge_scheduler_.resume();
}

int ObTenantMajorFreeze::on_become_primary()
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", KR(ret));
  } else if (!is_primary_service()) {
    ret = OB_NOT_SUPPORTED;
    LOG_WARN("tenant major freeze is not primary service", KR(ret));
  } else if (OB_FAIL(major_merge_info_detector_.on_become_primary())) {
    LOG_WARN("fail to activate snapshot gc detector for primary", KR(ret));
  }
  return ret;
}

bool ObTenantMajorFreeze::is_paused() const
{
  bool is_paused = (major_merge_info_detector_.is_paused() || merge_scheduler_.is_paused());
  if (is_primary_service()) {
    is_paused = (is_paused || daily_launcher_.is_paused());
  }
  return is_paused;
}

int ObTenantMajorFreeze::set_freeze_info(const ObMajorFreezeReason freeze_reason)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", KR(ret));
  } else if (OB_FAIL(major_merge_info_mgr_.set_freeze_info(freeze_reason))) {
    LOG_WARN("fail to set_freeze_info", KR(ret));
  }
  return ret;
}

int ObTenantMajorFreeze::launch_major_freeze(const ObMajorFreezeReason freeze_reason)
{
  int ret = OB_SUCCESS;
  LOG_INFO("launch_major_freeze");
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", KR(ret));
  } else if (OB_FAIL(check_tenant_status())) {
    LOG_WARN("fail to check tenant status", KR(ret));
  } else if (!GCONF.enable_major_freeze) {
    ret = OB_MAJOR_FREEZE_NOT_ALLOW;
    LOG_WARN("enable_major_freeze is off, refuse to to major_freeze",
             KR(ret));
  } else if (merge_scheduler_.is_paused()) {
    ret = OB_LEADER_NOT_EXIST;
    LOG_WARN("leader may switch", KR(ret));
  } else if (OB_FAIL(check_freeze_info())) {
    LOG_ERROR("fail to check freeze info", KR(ret));
    if ((OB_MAJOR_FREEZE_NOT_FINISHED == ret) || (OB_FROZEN_INFO_ALREADY_EXIST == ret)) {
      LOG_INFO("should not launch major freeze again", KR(ret));
    } else {
      LOG_ERROR("fail to check freeze info", KR(ret));
    }
  } else if (OB_FAIL(set_freeze_info(freeze_reason))) {
    LOG_WARN("fail to set_freeze_info", KR(ret));
  } else if (OB_FAIL(major_merge_info_detector_.signal())) {
    LOG_WARN("fail to signal", KR(ret));
  }
  return ret;
}

int ObTenantMajorFreeze::suspend_merge()
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", KR(ret));
  } else if (merge_scheduler_.is_paused()) {
    ret = OB_LEADER_NOT_EXIST;
    LOG_WARN("leader may switch", KR(ret));
  } else if (OB_FAIL(major_merge_info_mgr_.get_zone_merge_mgr().try_reload())) {
    LOG_WARN("fail to try reload zone_merge_mgr", KR(ret));
  } else if (OB_FAIL(major_merge_info_mgr_.get_zone_merge_mgr().suspend_merge())) {
    LOG_WARN("fail to suspend merge", KR(ret));
  }
  return ret;
}

int ObTenantMajorFreeze::resume_merge()
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", KR(ret));
  } else if (merge_scheduler_.is_paused()) {
    ret = OB_LEADER_NOT_EXIST;
    LOG_WARN("leader may switch", KR(ret));
  } else if (OB_FAIL(major_merge_info_mgr_.get_zone_merge_mgr().try_reload())) {
    LOG_WARN("fail to try reload zone_merge_mgr", KR(ret));
  } else if (OB_FAIL(major_merge_info_mgr_.get_zone_merge_mgr().resume_merge())) {
    LOG_WARN("fail to resume merge", KR(ret));
  }
  return ret;
}

int ObTenantMajorFreeze::clear_merge_error()
{
  int ret = OB_SUCCESS;
  const ObZoneMergeInfo::ObMergeErrorType error_type = ObZoneMergeInfo::ObMergeErrorType::NONE_ERROR;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", KR(ret));
  } else if (merge_scheduler_.is_paused()) {
    ret = OB_LEADER_NOT_EXIST;
    LOG_WARN("leader may switch", KR(ret));
  } else if (OB_FAIL(major_merge_info_mgr_.get_zone_merge_mgr().try_reload())) {
    LOG_WARN("fail to try reload zone_merge_mgr", KR(ret));
  } else {
    if (!GCTX.is_shared_storage_mode()
            && OB_FAIL(ObTabletMetaTableCompactionOperator::batch_update_status())) {
      LOG_WARN("fail to batch update status", KR(ret));
    } else if (GCTX.is_shared_storage_mode()) {
    }

    if (FAILEDx(major_merge_info_mgr_.get_zone_merge_mgr().set_merge_status(error_type))) {
      LOG_WARN("fail to set merge error", KR(ret), K(error_type));
    }
  }
  return ret;
}

int ObTenantMajorFreeze::get_uncompacted_tablets(
    ObArray<ObTabletReplica> &uncompacted_tablets,
    ObArray<uint64_t> &uncompacted_table_ids) const
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", KR(ret));
  } else {
    if (OB_FAIL(merge_scheduler_.get_uncompacted_tablets(uncompacted_tablets, uncompacted_table_ids))) {
      LOG_WARN("fail to get uncompacted tablets", KR(ret));
    }
  }
  return ret;
}

int ObTenantMajorFreeze::check_tenant_status() const
{
  int ret = OB_SUCCESS;
  share::schema::ObSchemaGetterGuard schema_guard;
  const share::schema::ObSimpleTenantSchema *tenant_schema = nullptr;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", KR(ret));
  } else if (OB_FAIL(schema_service_->get_tenant_schema_guard(schema_guard))) {
    LOG_WARN("fail to get schema guard", KR(ret));
  } else if (OB_FAIL(schema_guard.get_tenant_info(tenant_schema))) {
    LOG_WARN("fail to get simple tenant schema", KR(ret));
  } else if ((nullptr == tenant_schema) || !tenant_schema->is_normal()) {
    ret = OB_INNER_STAT_ERROR;
    LOG_WARN("tenant is not normal status", KR(ret), KPC(tenant_schema));
  }
  return ret;
}

int ObTenantMajorFreeze::check_freeze_info()
{
  int ret = OB_SUCCESS;
  SCN latest_frozen_scn;
  SCN global_last_merged_scn;
  ObZoneMergeInfo::MergeStatus global_merge_status = ObZoneMergeInfo::MergeStatus::MERGE_STATUS_MAX;

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", KR(ret));
  } else if (OB_FAIL(major_merge_info_mgr_.get_local_latest_frozen_scn(latest_frozen_scn))) {
    LOG_WARN("fail to get local latest frozen_scn", KR(ret));
  } else {
    ObZoneMergeManager &zone_merge_mgr = major_merge_info_mgr_.get_zone_merge_mgr();
    if (OB_FAIL(zone_merge_mgr.try_reload())) {
      LOG_WARN("fail to try_reload zone_merge_info", KR(ret));
    } else if (OB_FAIL(zone_merge_mgr.get_global_last_merged_scn(global_last_merged_scn))) {
      LOG_WARN("fail to get global_last_merged_scn", KR(ret));
    } else if (OB_FAIL(zone_merge_mgr.get_global_merge_status(global_merge_status))) {
      LOG_WARN("fail to get_global_merge_status", KR(ret));
    } else {
      // check pending freeze_info
      if (latest_frozen_scn > global_last_merged_scn) {
        if (global_merge_status == ObZoneMergeInfo::MergeStatus::MERGE_STATUS_IDLE) {
          ret = OB_FROZEN_INFO_ALREADY_EXIST;
        } else {
          ret = OB_MAJOR_FREEZE_NOT_FINISHED;
        }
        LOG_ERROR("cannot do major freeze now, need wait current major_freeze finish", KR(ret),
                K(global_last_merged_scn), K(latest_frozen_scn));
      } else if (merge_scheduler_.is_paused()) {
        ret = OB_LEADER_NOT_EXIST;
        LOG_WARN("leader may switch", KR(ret));
      }
    }
  }
  return ret;
}

} // end namespace rootserver
} // end namespace oceanbase
