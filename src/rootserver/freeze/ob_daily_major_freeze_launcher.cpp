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

#include "rootserver/freeze/ob_daily_major_freeze_launcher.h"

#include "rootserver/freeze/ob_major_freeze_helper.h"
#include "share/ob_tablet_checksum_operator.h"
#include "observer/ob_srv_network_frame.h"
#include "share/rc/ob_tenant_base.h"
#include "rootserver/freeze/ob_major_merge_info_manager.h"

namespace oceanbase
{
using namespace common;
using namespace share;
using namespace obcall;
using namespace share::schema;
namespace rootserver
{
ObDailyMajorFreezeLauncher::ObDailyMajorFreezeLauncher()
  : is_inited_(false),
    is_paused_(false),
    already_launch_(false),
    sql_proxy_(nullptr),
    config_(nullptr),
    gc_freeze_info_last_timestamp_(0),
    merge_info_mgr_(nullptr),
    last_check_tablet_ckm_us_(0),
    tablet_ckm_gc_compaction_scn_(SCN::invalid_scn()),
    stop_(true)
{
}

ObDailyMajorFreezeLauncher::~ObDailyMajorFreezeLauncher()
{
  (void)destroy();
}

int ObDailyMajorFreezeLauncher::init(
    ObServerConfig &config,
    ObMySQLProxy &proxy,
    ObMajorMergeInfoManager &merge_info_manager)
{
  int ret = OB_SUCCESS;
  if (is_inited_) {
    ret = OB_INIT_TWICE;
    LOG_WARN("init twice", KR(ret));
  } else {
    config_ = &config;
    gc_freeze_info_last_timestamp_ = ObTimeUtility::current_time();
    merge_info_mgr_ = &merge_info_manager;
    last_check_tablet_ckm_us_ = ObTimeUtility::current_time();
    tablet_ckm_gc_compaction_scn_ = SCN::invalid_scn();
    sql_proxy_ = &proxy;
    already_launch_ = false;
    stop_ = false;
    is_inited_ = true;
  }
  return ret;
}

int ObDailyMajorFreezeLauncher::start()
{
  int ret = OB_SUCCESS;
  if (!is_inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObDailyMajorFreezeLauncher not init", KR(ret));
  } else if (OB_FAIL(TG_START(lib::TGDefIDs::MFLaunchTimer))) {
    LOG_WARN("start MFLaunch timer failed", KR(ret));
  } else if (OB_FAIL(TG_SCHEDULE(lib::TGDefIDs::MFLaunchTimer, *this, LAUNCHER_INTERVAL_US, true/*is_repeat*/))) {
    LOG_WARN("schedule MFLaunch timer failed", KR(ret));
  } else {
    stop_ = false;
    LOG_INFO("ObDailyMajorFreezeLauncher start succ");
  }
  return ret;
}

void ObDailyMajorFreezeLauncher::runTimerTask()
{
  int ret = OB_SUCCESS;
  int tmp_ret = OB_SUCCESS;
  if (!is_inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("fail to run, not init", KR(ret));
  } else if (stop_ || is_paused()) {
  } else {
    MOD_SCOPE {
      LOG_INFO("start daily major_freeze_launcher");
      LOG_TRACE("run daily major freeze launcher");

      if (OB_FAIL(try_launch_major_freeze())) {
        LOG_WARN("fail to try_launch_major_freeze", KR(ret));
      }
      if (OB_TMP_FAIL(try_gc_freeze_info())) {
        LOG_WARN("fail to try_gc_freeze_info", KR(tmp_ret));
      }
      if (OB_TMP_FAIL(try_gc_tablet_checksum())) {
        LOG_WARN("fail to try_gc_tablet_checksum", KR(tmp_ret));
      }
      LOG_INFO("daily major_freeze_launcher stopped");
    }
  }
}

void ObDailyMajorFreezeLauncher::stop()
{
  if (!stop_) {
    stop_ = true;
    TG_STOP(lib::TGDefIDs::MFLaunchTimer);
  }
}

void ObDailyMajorFreezeLauncher::wait()
{
  if (is_inited_) {
    TG_WAIT(lib::TGDefIDs::MFLaunchTimer);
  }
}

int ObDailyMajorFreezeLauncher::destroy()
{
  int ret = OB_SUCCESS;
  stop();
  wait();
  TG_DESTROY(lib::TGDefIDs::MFLaunchTimer);
  stop_ = true;
  is_paused_ = false;
  is_inited_ = false;
  sql_proxy_ = nullptr;
  config_ = nullptr;
  merge_info_mgr_ = nullptr;
  tablet_ckm_gc_compaction_scn_.set_invalid();
  return ret;
}

int ObDailyMajorFreezeLauncher::try_launch_major_freeze()
{
  int ret = OB_SUCCESS;

  omt::ObTenantConfigGuard tenant_config(TENANT_CONF());
  if (!is_inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", KR(ret));
  } else if (OB_UNLIKELY(!tenant_config.is_valid())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("tenant config is not valid", KR(ret));
  } else if (tenant_config->major_freeze_duty_time.disable()) {
    LOG_INFO("major_freeze_duty_time is disabled, can not launch major freeze by duty");
  } else {
    const int hour = tenant_config->major_freeze_duty_time.hour();
    const int minute = tenant_config->major_freeze_duty_time.minute();
    time_t cur_time = -1;
    time(&cur_time);
    struct tm human_time;
    struct tm *human_time_ptr = nullptr;
#ifdef _WIN32
    human_time_ptr = (0 == localtime_s(&human_time, &cur_time)) ? &human_time : nullptr;
#else
    human_time_ptr = localtime_r(&cur_time, &human_time);
#endif
    if (nullptr == human_time_ptr) {
      ret = OB_ERR_SYS;
      LOG_WARN("fail to get localtime", KR(ret), K(errno));
    } else if ((human_time_ptr->tm_hour == hour) && (human_time_ptr->tm_min == minute)) {
      if (!already_launch_) {
        const int64_t start_us = ObTimeUtility::current_time();
        const int64_t RETRY_TIME_LIMIT = 2 * 3600 * 1000 * 1000L; // 2h
        do {
          ObMajorFreezeParam param;
          param.freeze_reason_ = MF_DAILY_MERGE;
          if (OB_FAIL(param.add_freeze_info())) {
            LOG_WARN("fail to push_back", KR(ret));
          } else if (OB_FAIL(ObMajorFreezeHelper::major_freeze(param))) {
            if ((OB_TIMEOUT == ret)) {
              ret = OB_EAGAIN; // in order to try launch major freeze again, set ret = OB_EAGAIN here
              LOG_WARN("may be ddl confilict, will try to launch major freeze again", KR(ret), K(param),
                       "sleep_us", MAJOR_FREEZE_RETRY_INTERVAL_US * MAJOR_FREEZE_RETRY_LIMIT);
            } else {
              LOG_WARN("fail to major freeze", K(param), KR(ret));
            }
          } else {
            already_launch_ = true;
            LOG_INFO("launch major freeze by duty time",
                     "duty_time", tenant_config->major_freeze_duty_time);
          }

          // launcher will retry when error code is OB_EAGAIN
          // maybe use a new err code is better(OB_MAJRO_FREEZE_EAGAIN)
          if (OB_EAGAIN == ret) {
            LOG_WARN("leader switch or ddl confilict, will try to launch major freeze again",
              KR(ret), K(param), "sleep_us", MAJOR_FREEZE_RETRY_INTERVAL_US * MAJOR_FREEZE_RETRY_LIMIT);
            int64_t usleep_cnt = 0;
            while (!stop_ && (usleep_cnt < MAJOR_FREEZE_RETRY_LIMIT)) {
              ++usleep_cnt;
              ob_usleep(MAJOR_FREEZE_RETRY_INTERVAL_US);
            }
          }
        } while (!stop_ && (OB_EAGAIN == ret) && ((ObTimeUtility::current_time() - start_us) < RETRY_TIME_LIMIT));
        if (!already_launch_ && !stop_ && (OB_EAGAIN == ret)
            && ((ObTimeUtility::current_time() - start_us) > RETRY_TIME_LIMIT)) {
          LOG_ERROR("daily major freeze is not launched due to ddl conflict, and reaches retry "
                    "time limit", KR(ret), K(start_us), "now", ObTimeUtility::current_time());
        }
      } else {
        LOG_INFO("major_freeze has been already launched, no need to do again");
      }
    } else {
      already_launch_ = false;
    }
  }
  return ret;
}

int ObDailyMajorFreezeLauncher::try_gc_freeze_info()
{
  int ret = OB_SUCCESS;
  int64_t now = ObTimeUtility::current_time();
  if (!is_inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", KR(ret));
  } else if ((now - gc_freeze_info_last_timestamp_) < MODIFY_GC_INTERVAL) {
    // nothing
  } else if (OB_FAIL(merge_info_mgr_->try_gc_freeze_info())) {
    LOG_WARN("fail to gc_freeze_info", KR(ret));
  } else {
    gc_freeze_info_last_timestamp_ = now;
  }
  return ret;
}

int ObDailyMajorFreezeLauncher::try_gc_tablet_checksum()
{
  int ret = OB_SUCCESS;
  // keep 30 days for tablet_checksum whose (tablet_id, ls_id) is (1, 1)
  const int64_t MAX_KEEP_INTERVAL_NS =  30LL * 24 * 60 * 60 * 1000 * 1000 * 1000; // 30 day
  const int64_t MIN_RESERVED_COUNT = 8;
  SCN cur_gts_scn;
  SCN min_keep_compaction_scn;
  int64_t now = ObTimeUtility::current_time();
  const static int64_t BATCH_DELETE_CNT = 2000;
  if (OB_UNLIKELY(!is_inited_ || OB_ISNULL(sql_proxy_) || OB_ISNULL(merge_info_mgr_))) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", KR(ret), K_(is_inited), KP(sql_proxy_), KP(merge_info_mgr_));
  } else {
    SMART_VAR(ObArray<SCN>, all_compaction_scn) {
      // 1. load all distinct compaction_scn, when reach 30 min interval time and no valid
      // tablet_ckm_gc_compaction_scn exists
      if (((now - last_check_tablet_ckm_us_) < TABLET_CKM_CHECK_INTERVAL_US)
          || tablet_ckm_gc_compaction_scn_.is_valid()) {
        // do nothing, so as to decrease the frequency of load all distinct compaction_scn
      } else if (OB_FAIL(ObTabletChecksumOperator::load_all_compaction_scn(*sql_proxy_, all_compaction_scn))) {
        LOG_WARN("fail to load all compaction scn", KR(ret));
      } else {
        last_check_tablet_ckm_us_ = now;
        // 2. check if need gc tablet_checksum
        if (all_compaction_scn.count() > MIN_RESERVED_COUNT) {
          const int64_t compaction_scn_cnt = all_compaction_scn.count();
          tablet_ckm_gc_compaction_scn_ = all_compaction_scn.at(compaction_scn_cnt - MIN_RESERVED_COUNT - 1);
          if (OB_FAIL(merge_info_mgr_->get_gts(cur_gts_scn))) {
            LOG_WARN("fail to get_gts", KR(ret));
          } else {
            min_keep_compaction_scn = SCN::minus(cur_gts_scn, MAX_KEEP_INTERVAL_NS);
            const SCN special_tablet_ckm_gc_compaction_scn = MIN(min_keep_compaction_scn, tablet_ckm_gc_compaction_scn_);
            if (OB_FAIL(ObTabletChecksumOperator::delete_special_tablet_checksum_items(*sql_proxy_, special_tablet_ckm_gc_compaction_scn))) {
              LOG_WARN("fail to delete special tablet checksum items", KR(ret),
                       K(special_tablet_ckm_gc_compaction_scn));
            }
          }
        }
      }

      // 3. gc tablet_checksum if need
      if (OB_SUCC(ret) && tablet_ckm_gc_compaction_scn_.is_valid()) {
        int64_t affected_rows = 0;
        if (OB_FAIL(ObTabletChecksumOperator::delete_tablet_checksum_items(*sql_proxy_,
                    tablet_ckm_gc_compaction_scn_, BATCH_DELETE_CNT, affected_rows))) {
          LOG_WARN("fail to delete tablet checksum items", KR(ret), K_(tablet_ckm_gc_compaction_scn));
        } else if (0 == affected_rows) {
          // already delete all tablet_checksum with comapction_scn <= tablet_ckm_gc_compaction_scn_
          tablet_ckm_gc_compaction_scn_.set_invalid();
        }
      }
    }
  }
  return ret;
}

}//end namespace rootserver
}//end namespace oceanbase
