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

#include "rootserver/freeze/ob_local_major_freeze.h"

#include "share/ob_tablet_meta_table_compaction_operator.h"
#include "share/schema/ob_schema_getter_guard.h"

namespace oceanbase
{
namespace rootserver
{
using namespace common;
using namespace share;

ObLocalMajorFreeze::ObLocalMajorFreeze()
  : is_inited_(false), is_primary_service_(true),
    major_merge_info_mgr_(), snapshot_gc_scn_renewer_{},
    major_merge_info_detector_{},
    merge_scheduler_{}, daily_launcher_{}, schema_service_(nullptr)
{
}

ObLocalMajorFreeze::~ObLocalMajorFreeze()
{
}

int ObLocalMajorFreeze::init(
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
  } else if (OB_FAIL(snapshot_gc_scn_renewer_.init(
             is_primary_service, major_merge_info_mgr_))) {
  } else if (OB_FAIL(merge_scheduler_.init(is_primary_service, major_merge_info_mgr_,
             schema_service, config, sql_proxy))) {
  }  else if (OB_FAIL(major_merge_info_detector_.init(is_primary_service, sql_proxy,
              major_merge_info_mgr_, snapshot_gc_scn_renewer_,
              merge_scheduler_.get_major_scheduler_idling()))) {
  } else if (is_primary_service) {
    if (OB_FAIL(daily_launcher_.init(config, sql_proxy, major_merge_info_mgr_))) {
    }
  }
  if (OB_SUCC(ret)) {
    is_primary_service_ = is_primary_service;
    schema_service_ = &schema_service;
    is_inited_ = true;
  }

  return ret;
}

int ObLocalMajorFreeze::start(const bool append_mode)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", KR(ret));
  } else {
    set_log_mode_(append_mode);
    if (OB_FAIL(major_merge_info_detector_.start())) {
    } else if (OB_FAIL(merge_scheduler_.start())) {
    } else if (is_primary_service()) {
      if (OB_FAIL(daily_launcher_.start())) {
      }
    }
  }
  return ret;
}

void ObLocalMajorFreeze::stop()
{
  if (is_primary_service()) {
    LOG_INFO("daily_launcher start to stop", K_(is_primary_service));
    daily_launcher_.stop();
  }
  snapshot_gc_scn_renewer_.pause();
  LOG_INFO("freeze_info_detector start to stop", K_(is_primary_service));
  major_merge_info_detector_.stop();
  LOG_INFO("merge_scheduler start to stop", K_(is_primary_service));
  merge_scheduler_.stop();
}

int ObLocalMajorFreeze::wait()
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

int ObLocalMajorFreeze::destroy()
{
  int ret = OB_SUCCESS;
  if (is_primary_service()) {
    LOG_INFO("daily_launcher start to destroy", K_(is_primary_service));
    if (OB_FAIL(daily_launcher_.destroy())) {
    }
  }
  if (OB_SUCC(ret)) {
    LOG_INFO("freeze_info_detector start to destroy", K_(is_primary_service));
    if (OB_FAIL(major_merge_info_detector_.destroy())) {
    }
  }
  if (OB_SUCC(ret)) {
    LOG_INFO("snapshot gc scn renewer start to destroy", K_(is_primary_service));
    if (OB_FAIL(snapshot_gc_scn_renewer_.destroy())) {
    }
  }
  if (OB_SUCC(ret)) {
    LOG_INFO("merge_scheduler start to destroy", K_(is_primary_service));
    if (OB_FAIL(merge_scheduler_.destroy())) {
    }
  }
  return ret;
}

void ObLocalMajorFreeze::pause()
{
  if (is_primary_service()) {
    daily_launcher_.pause();
  }
  snapshot_gc_scn_renewer_.pause();
  major_merge_info_detector_.pause();
  merge_scheduler_.pause();
}

void ObLocalMajorFreeze::resume(const bool append_mode)
{
  major_merge_info_detector_.resume();
  merge_scheduler_.resume();
  set_log_mode_(append_mode);
}

void ObLocalMajorFreeze::set_log_mode_(const bool append_mode)
{
  major_merge_info_detector_.set_replay_mode(!append_mode);
  if (is_primary_service()) {
    if (append_mode) {
      daily_launcher_.resume();
      snapshot_gc_scn_renewer_.resume();
    } else {
      daily_launcher_.pause();
      snapshot_gc_scn_renewer_.pause();
    }
  }
}

int ObLocalMajorFreeze::on_become_primary()
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", KR(ret));
  } else if (!is_primary_service()) {
    ret = OB_NOT_SUPPORTED;
    LOG_WARN("local major freeze is not primary service", KR(ret));
  } else if (major_merge_info_detector_.is_replay_mode()) {
    ret = OB_STATE_NOT_MATCH;
    LOG_WARN("local major freeze is still in replay mode", KR(ret));
  } else if (OB_FAIL(snapshot_gc_scn_renewer_.on_become_primary())) {
  } else if (OB_FAIL(major_merge_info_detector_.signal())) {
  }
  return ret;
}

bool ObLocalMajorFreeze::is_paused() const
{
  return major_merge_info_detector_.is_paused() || merge_scheduler_.is_paused();
}

int ObLocalMajorFreeze::set_freeze_info(const ObMajorFreezeReason freeze_reason)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", KR(ret));
  } else if (OB_FAIL(major_merge_info_mgr_.set_freeze_info(freeze_reason))) {
  }
  return ret;
}

int ObLocalMajorFreeze::launch_major_freeze(const ObMajorFreezeReason freeze_reason)
{
  int ret = OB_SUCCESS;
  LOG_INFO("launch_major_freeze");
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", KR(ret));
  } else if (OB_FAIL(check_runtime_status())) {
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
  } else if (OB_FAIL(major_merge_info_detector_.signal())) {
  }
  return ret;
}

int ObLocalMajorFreeze::suspend_merge()
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", KR(ret));
  } else if (merge_scheduler_.is_paused()) {
    ret = OB_LEADER_NOT_EXIST;
    LOG_WARN("leader may switch", KR(ret));
  } else if (OB_FAIL(major_merge_info_mgr_.get_global_merge_mgr().try_reload())) {
  } else if (OB_FAIL(major_merge_info_mgr_.get_global_merge_mgr().suspend_merge())) {
  }
  return ret;
}

int ObLocalMajorFreeze::resume_merge()
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", KR(ret));
  } else if (merge_scheduler_.is_paused()) {
    ret = OB_LEADER_NOT_EXIST;
    LOG_WARN("leader may switch", KR(ret));
  } else if (OB_FAIL(major_merge_info_mgr_.get_global_merge_mgr().try_reload())) {
  } else if (OB_FAIL(major_merge_info_mgr_.get_global_merge_mgr().resume_merge())) {
  }
  return ret;
}

int ObLocalMajorFreeze::clear_merge_error()
{
  int ret = OB_SUCCESS;
  const ObGlobalMergeInfo::ObMergeErrorType error_type = ObGlobalMergeInfo::NONE_ERROR;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", KR(ret));
  } else if (merge_scheduler_.is_paused()) {
    ret = OB_LEADER_NOT_EXIST;
    LOG_WARN("leader may switch", KR(ret));
  } else if (OB_FAIL(major_merge_info_mgr_.get_global_merge_mgr().try_reload())) {
  } else {
    if (OB_FAIL(ObTabletMetaTableCompactionOperator::batch_update_status(
        GCTX.meta_db_pool_))) {
    }

    if (FAILEDx(major_merge_info_mgr_.get_global_merge_mgr().set_merge_status(error_type))) {
      LOG_WARN("fail to set merge error", KR(ret), K(error_type));
    }
  }
  return ret;
}

int ObLocalMajorFreeze::get_uncompacted_tablets(
    ObArray<ObTabletRuntimeInfo> &uncompacted_tablets,
    ObArray<uint64_t> &uncompacted_table_ids) const
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", KR(ret));
  } else {
    if (OB_FAIL(merge_scheduler_.get_uncompacted_tablets(uncompacted_tablets, uncompacted_table_ids))) {
    }
  }
  return ret;
}

int ObLocalMajorFreeze::check_runtime_status() const
{
  int ret = OB_SUCCESS;
  share::schema::ObSchemaGetterGuard schema_guard;
  const share::schema::ObSimpleServerRuntimeSchema *runtime_schema = nullptr;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", KR(ret));
  } else if (OB_FAIL(schema_service_->get_runtime_schema_guard(schema_guard))) {
  } else if (OB_FAIL(schema_guard.get_server_runtime_info(runtime_schema))) {
  } else if ((nullptr == runtime_schema) || !runtime_schema->is_normal()) {
    ret = OB_INNER_STAT_ERROR;
    LOG_WARN("database runtime is not normal", KR(ret), KPC(runtime_schema));
  }
  return ret;
}

int ObLocalMajorFreeze::check_freeze_info()
{
  int ret = OB_SUCCESS;
  SCN latest_frozen_scn;
  SCN global_last_merged_scn;
  ObGlobalMergeInfo::MergeStatus global_merge_status = ObGlobalMergeInfo::MERGE_STATUS_MAX;

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", KR(ret));
  } else if (OB_FAIL(major_merge_info_mgr_.get_local_latest_frozen_scn(latest_frozen_scn))) {
  } else {
    ObGlobalMergeManager &global_merge_mgr = major_merge_info_mgr_.get_global_merge_mgr();
    if (OB_FAIL(global_merge_mgr.try_reload())) {
    } else if (OB_FAIL(global_merge_mgr.get_global_last_merged_scn(global_last_merged_scn))) {
    } else if (OB_FAIL(global_merge_mgr.get_global_merge_status(global_merge_status))) {
    } else {
      // check pending freeze_info
      if (latest_frozen_scn > global_last_merged_scn) {
        if (global_merge_status == ObGlobalMergeInfo::MERGE_STATUS_IDLE) {
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
