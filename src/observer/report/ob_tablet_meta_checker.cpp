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

#include "observer/report/ob_tablet_meta_checker.h"
#include "share/rc/ob_server_runtime.h"
#include "storage/tablet/ob_tablet_iterator.h" // ObLSTabletIterator
#include "share/tablet/ob_tablet_table_iterator.h" // ObTenantTabletTableIterator
#include "storage/ls/ob_ls.h"
#include "storage/tx_storage/ob_ls_service.h" // ObLSService
#include "share/ob_tablet_local_checksum_operator.h" // ObTabletLocalChecksumItem
#include "observer/report/ob_tablet_table_updater.h" // ObTabletTableUpdater
#include "observer/ob_service.h" // ObService

namespace oceanbase
{
using namespace share;
using namespace common;

namespace observer
{

ObTabletMetaTableCheckTask::ObTabletMetaTableCheckTask(
    ObTabletMetaChecker &checker)
    : checker_(checker)
{
}

void ObTabletMetaTableCheckTask::runTimerTask()
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(checker_.check_tablet_table())) {
  }
  if (OB_FAIL(checker_.schedule_tablet_meta_check_task())) {
    // overwrite ret
  }
}

ObTabletMetaChecker::ObTabletMetaChecker()
    : inited_(false),
      stopped_(true),
      tablet_checker_timer_(),
      tt_operator_(NULL),
      tablet_meta_check_task_(*this)
{
}

int ObTabletMetaChecker::module_init(ObTabletMetaChecker *&checker)
{

  ObTabletTableOperator *tt_operator = GCTX.tablet_operator_;
  return checker->init(tt_operator);
}

int ObTabletMetaChecker::init(
    share::ObTabletTableOperator *tt_operator)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(inited_)) {
    ret = OB_INIT_TWICE;
  } else if (OB_ISNULL(tt_operator)) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(tablet_checker_timer_.init(
      "TbMetaCh", common::ObMemAttr("TbMetaCh")))) {
  } else {
    tt_operator_ = tt_operator;
    inited_ = true;
  }
  return ret;
}

int ObTabletMetaChecker::start()
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!inited_)) {
    ret = OB_NOT_INIT;
  } else {
    stopped_ = false;
    if (OB_FAIL(schedule_tablet_meta_check_task())) {
    } else {
      LOG_INFO("ObTabletMetaChecker start success");
    }
  }
  return ret;
}

void ObTabletMetaChecker::stop()
{
  if (OB_LIKELY(inited_)) {
    stopped_ = true;
    tablet_checker_timer_.stop();
    LOG_INFO("ObTabletMetaChecker stop finished");
  }
}

void ObTabletMetaChecker::wait()
{
  if (OB_LIKELY(inited_)) {
    tablet_checker_timer_.wait();
    LOG_INFO("ObTabletMetaChecker wait finished");
  }
}

void ObTabletMetaChecker::destroy()
{
  if (OB_LIKELY(inited_)) {
    tt_operator_ = nullptr;
    inited_ = false;
    stopped_ = true;
    tablet_checker_timer_.destroy();
    LOG_INFO("ObTabletMetaChecker destroy finished");
  }
}

int ObTabletMetaChecker::check_tablet_table()
{
  int ret = OB_SUCCESS;
  int64_t stale_row_count = 0;
  int64_t missing_or_changed_row_count = 0;
  if (OB_UNLIKELY(!inited_)) {
    ret = OB_NOT_INIT;
  } else {
    const int64_t start_time = ObTimeUtility::current_time();
    ObTabletMetaRowMap tablet_meta_row_map;
    if (OB_FAIL(build_tablet_meta_row_map_(tablet_meta_row_map))) {
    } else if (OB_FAIL(check_stale_tablet_meta_rows_(tablet_meta_row_map, stale_row_count))) {
    } else if (OB_FAIL(check_missing_or_changed_tablet_meta_rows_(
        tablet_meta_row_map, missing_or_changed_row_count))) {
    } else if (stale_row_count != 0 || missing_or_changed_row_count != 0) {
      LOG_INFO("checker found and corrected stale or missing tablet meta rows",
        KR(ret), K(stale_row_count), K(missing_or_changed_row_count));
    }
    LOG_TRACE("finish checking tablet table", KR(ret),
        K(stale_row_count), K(missing_or_changed_row_count),
        K(start_time), "cost_time", ObTimeUtility::current_time() - start_time);
  }
  return ret;
}

int ObTabletMetaChecker::schedule_tablet_meta_check_task()
{
  int ret = OB_SUCCESS;
  const int64_t CHECK_INTERVAL = GCONF.tablet_meta_table_check_interval;
  if (OB_UNLIKELY(!inited_)) {
    ret = OB_NOT_INIT;
  } else if (OB_UNLIKELY(stopped_)) {
    ret = OB_CANCELED;
  } else if (OB_FAIL(tablet_checker_timer_.schedule(
      tablet_meta_check_task_,
      CHECK_INTERVAL,
      false/*repeat*/))) {
  } else {
    LOG_TRACE("schedule tablet meta check task success");
  }
  return ret;
}

int ObTabletMetaChecker::build_tablet_meta_row_map_(ObTabletMetaRowMap &tablet_meta_row_map)
{
  int ret = OB_SUCCESS;
  ObTabletMetaTableIterator tt_iter;
  if (OB_UNLIKELY(!inited_) || OB_ISNULL(tt_operator_)) {
    ret = OB_NOT_INIT;
  } else if (OB_UNLIKELY(stopped_)) {
    ret = OB_CANCELED;
  } else if (OB_FAIL(tablet_meta_row_map.create(
      hash::cal_next_prime(TABLET_META_ROW_MAP_BUCKET_NUM),
      "TabletCheckMap",
      ObModIds::OB_HASH_NODE))) {
  } else if (OB_FAIL(tt_iter.init(*tt_operator_))) {
  } else {
    ObTabletRuntimeInfo tablet_info;
    while (OB_SUCC(ret)) {
      tablet_info.reset();
      if (OB_UNLIKELY(stopped_)) {
        ret = OB_CANCELED;
      } else if (OB_FAIL(tt_iter.next(tablet_info))) {
        if (OB_ITER_END != ret) {
        }
      } else if (OB_UNLIKELY(!tablet_info.is_valid())) {
        ret = OB_ERR_UNEXPECTED;
      } else {
        if (OB_FAIL(tablet_meta_row_map.set_refactored(
            tablet_info.get_tablet_id(), tablet_info))) {
        }
      }
    } // end while
    if (OB_ITER_END == ret) {
      ret = OB_SUCCESS;
    }
  }
  return ret;
}

int ObTabletMetaChecker::check_stale_tablet_meta_rows_(
    ObTabletMetaRowMap &tablet_meta_row_map,
    int64_t &stale_row_count)
{
  int ret = OB_SUCCESS;
  stale_row_count = 0;
  if (OB_UNLIKELY(!inited_)) {
    ret = OB_NOT_INIT;
  } else if (OB_UNLIKELY(stopped_)) {
    ret = OB_CANCELED;
  } else if (OB_ISNULL(::oceanbase::share::server_service<::oceanbase::observer::ObService>())) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    bool not_exist = false;
    FOREACH_X(it, tablet_meta_row_map, OB_SUCC(ret)) {
      const ObTabletID &tablet_id = it->first;
      if (OB_UNLIKELY(stopped_)) {
        ret = OB_CANCELED;
      } else if (OB_FAIL(check_tablet_not_exist_in_local_(tablet_id, not_exist))) {
      } else if (not_exist) {
        ++stale_row_count;
        if (OB_FAIL(share::server_service<ObTabletRuntimeMetaUpdater>()->submit_update_task(tablet_id))) {
        } else {
          LOG_INFO("add async task to remove stale tablet meta row",
              "tablet_meta_row", it->second);
        }
      }
    } // end for
  }
  return ret;
}

int ObTabletMetaChecker::check_tablet_not_exist_in_local_(
    const ObTabletID &tablet_id,
    bool &not_exist)
{
  int ret = OB_SUCCESS;
  ObLS *ls = nullptr;
  ObTabletHandle tablet_handle;
  not_exist = false;
  if (OB_UNLIKELY(!inited_)) {
    ret = OB_NOT_INIT;
  } else if (OB_UNLIKELY(!tablet_id.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
  } else if (tablet_id.is_reserved_tablet()) {
    // skip reserved tablet
  } else if (OB_FAIL(::oceanbase::share::server_service<::oceanbase::storage::ObLSService>()->get_ls(
      ls))) {
  } else if (OB_ISNULL(ls->get_tablet_svr())) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(ls->get_tablet_svr()->get_tablet(
          tablet_id,
          tablet_handle,
          ObTabletCommon::DEFAULT_GET_TABLET_DURATION_10_S,
          ObMDSGetTabletMode::READ_WITHOUT_CHECK))) {
    if (OB_TABLET_NOT_EXIST == ret || OB_ENTRY_NOT_EXIST == ret) {
      ret = OB_SUCCESS;
      not_exist = true;
    } else {
    }
  }
  return ret;
}

int ObTabletMetaChecker::check_missing_or_changed_tablet_meta_rows_(
    ObTabletMetaRowMap &tablet_meta_row_map,
    int64_t &missing_or_changed_row_count)
{
  int ret = OB_SUCCESS;
  missing_or_changed_row_count = 0;
  ObLS *ls = nullptr;
  if (OB_UNLIKELY(!inited_)) {
    ret = OB_NOT_INIT;
  } else if (OB_UNLIKELY(stopped_)) {
    ret = OB_CANCELED;
  } else if (OB_ISNULL(::oceanbase::share::server_service<::oceanbase::observer::ObService>())) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_ISNULL(::oceanbase::share::server_service<::oceanbase::storage::ObLSService>())) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(::oceanbase::share::server_service<::oceanbase::storage::ObLSService>()->get_ls(ls))) {
    LOG_WARN("failed to get single log stream", KR(ret));
  } else {
    ObLSTabletIterator tablet_iter(ObMDSGetTabletMode::READ_ALL_COMMITED);
    if (OB_UNLIKELY(stopped_)) {
      ret = OB_CANCELED;
    } else if (OB_ISNULL(ls->get_tablet_svr())) {
      ret = OB_ERR_UNEXPECTED;
    } else if (OB_FAIL(ls->get_tablet_svr()->build_tablet_iter(tablet_iter))) {
    } else {
      ObTabletHandle tablet_handle;
      ObTabletID tablet_id;
      ObTabletRuntimeInfo local_tablet_meta_row;
      ObTabletRuntimeInfo tablet_meta_row;
      share::ObTabletLocalChecksumItem tablet_checksum;
      while (OB_SUCC(ret)) {
        if (OB_FAIL(tablet_iter.get_next_tablet(tablet_handle))) {
          if (OB_UNLIKELY(OB_ITER_END != ret)) {
          }
        } else if (OB_UNLIKELY(!tablet_handle.is_valid())) {
          ret = OB_ERR_UNEXPECTED;
        } else if (FALSE_IT(tablet_id = tablet_handle.get_obj()->get_tablet_meta().tablet_id_)) {
        } else if (tablet_id.is_reserved_tablet()) {
          continue;
        } else if (OB_FAIL(tablet_meta_row_map.get_refactored(tablet_id, tablet_meta_row))) {
          if (OB_HASH_NOT_EXIST == ret) { // not exist in table while exist in local
            ret = OB_SUCCESS;
            if (OB_FAIL(share::server_service<ObTabletRuntimeMetaUpdater>()->submit_update_task(tablet_id))) {
            } else {
              ++missing_or_changed_row_count;
              LOG_INFO("add missing tablet meta row success",
                  KR(ret), K(tablet_id));
            }
          } else {
          }
        } else if (OB_FAIL(::oceanbase::share::server_service<::oceanbase::observer::ObService>()->fill_tablet_runtime_info(tablet_id,
            local_tablet_meta_row,
            tablet_checksum))) {
          if (OB_EAGAIN == ret) {
            ret = OB_SUCCESS; // do not affect report of other tablets
          } else {
          }
        } else if (tablet_meta_row.get_tablet_id() == local_tablet_meta_row.get_tablet_id()
            && tablet_meta_row.get_snapshot_version() == local_tablet_meta_row.get_snapshot_version()
            && tablet_meta_row.get_data_size() == local_tablet_meta_row.get_data_size()
            && tablet_meta_row.get_required_size() == local_tablet_meta_row.get_required_size()) {
          continue;
        } else { // not equal
          if (OB_FAIL(share::server_service<ObTabletRuntimeMetaUpdater>()->submit_update_task(tablet_id))) {
          } else {
            ++missing_or_changed_row_count;
            LOG_INFO("modify tablet meta row success", KR(ret), K(local_tablet_meta_row), K(tablet_meta_row));
          }
        }
      } // end while for tablet_iter
      if (OB_ITER_END == ret) {
        ret = OB_SUCCESS;
      }
    }
  }
  return ret;
}


} // end namespace observer
} // end namespace oceanbase
