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

#include "ob_checksum_validator.h"
#include "rootserver/freeze/ob_major_merge_progress_checker.h"
#include "storage/compaction/ob_medium_compaction_func.h"
#include "share/ob_structured_event_logger.h"

namespace oceanbase
{
namespace rootserver
{
using namespace common;
using namespace share;
using namespace schema;
using namespace compaction;
///////////////////////////////////////////////////////////////////////////////

int ObChecksumValidator::init(
    const bool is_primary_service,
    ObMySQLProxy &sql_proxy)
{
  int ret = OB_SUCCESS;
  if (IS_INIT) {
    ret = OB_INIT_TWICE;
    LOG_WARN("init twice", KR(ret));
  } else if (OB_FAIL(local_ckm_items_.init(DEFAULT_TABLET_CNT))) {
  } else {
    is_primary_service_ = is_primary_service;
    cur_tablet_ids_.set_attr(ObMemAttr("RSCompTabs"));
    sql_proxy_ = &sql_proxy;
    is_inited_ = true;
  }
  return ret;
}

int ObChecksumValidator::set_basic_info(
    const share::ObFreezeInfo &freeze_info)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!freeze_info.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), K(freeze_info));
  } else if (FALSE_IT(freeze_info_ = freeze_info)) {
  } else if (FALSE_IT(major_merge_start_us_ = ObTimeUtility::fast_current_time())) {
  } else if (OB_FAIL(set_need_validate())) {
  } else {
    statistics_.reset();
  }
  return ret;
}

int ObChecksumValidator::deal_with_special_table_at_last(bool &finish_validate)
{
  int ret = OB_SUCCESS;
  finish_validate = false;
  ObSchemaGetterGuard schema_guard(ObSchemaMgrItem::MOD_RS_MAJOR_CHECK);
  cur_tablet_ids_.reuse();
  if (OB_FAIL(ObMultiVersionSchemaService::get_instance().get_runtime_schema_guard(
    schema_guard, OB_INVALID_VERSION,
    ObMultiVersionSchemaService::RefreshSchemaMode::FORCE_LAZY))) {
  } else if (FALSE_IT(schema_guard_ = &schema_guard)) {
  } else if (OB_FAIL(check_inner_status())) {
  } else if (FALSE_IT(table_id_ = ObChecksumValidator::SPECIAL_TABLE_ID)) {
  } else if (OB_FAIL(get_table_compaction_info(table_id_, table_compaction_info_))) {
  } else if (OB_FAIL(schema_guard_->get_simple_table_schema( table_id_, simple_schema_))) {
  } else if (OB_ISNULL(simple_schema_)) {
    ret = OB_TABLE_NOT_EXIST;
    LOG_WARN("table schema is null", KR(ret), K_(table_id));
  } else if (OB_FAIL(simple_schema_->get_tablet_ids(cur_tablet_ids_))) {
  } else if (OB_UNLIKELY(cur_tablet_ids_.empty())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("fail to get tablet ids of current table schema", KR(ret), K_(table_id),
      K_(cur_tablet_ids));
  }
  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(get_local_tablet_checksum_and_validate(true /*include_larger_than*/))) {
    if (OB_ITEM_NOT_MATCH == ret) {
      (void) uncompact_info_.add_skip_verify_table(table_id_);
      ret = OB_SUCCESS;
    } else {
      LOG_ERROR("fail to validate local tablet checksum", KR(ret), "compaction_scn", get_compaction_scn(), K_(table_id),
        KPC(simple_schema_), K_(cur_tablet_ids));
    }
  } else if (FALSE_IT(table_compaction_info_.set_index_ckm_verified())) {
  } else if (OB_FAIL(validate_standby_checksum())) {
  } else {
    finish_validate = true;
    LOG_INFO("success to deal with special table", KR(ret), K_(table_id), K_(table_compaction_info));
  }
  return ret;
}

// check for every merge round
int ObChecksumValidator::set_need_validate()
{
  int ret = OB_SUCCESS;
  if (is_primary_service_) {
    // Check index checksums on the primary database.
    need_validate_index_ckm_ = true;
    if (OB_FAIL(check_tablet_checksum_sync_finish(true /*force_check*/))) {
    } else {
      // Once the standby checksum is synchronized, validate this merge round.
      // else: write ckm into inner table
      need_validate_standby_ckm_ = standby_ckm_sync_finish_;
    }
  } else { // standby database
    need_validate_index_ckm_ = false;
    need_validate_standby_ckm_ = true;
  }
  return ret;
}

int ObChecksumValidator::get_table_compaction_info(
  const uint64_t table_id, compaction::ObTableCompactionInfo &table_compaction_info)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(table_compaction_map_.get_refactored(table_id, table_compaction_info))) {
    if (OB_HASH_NOT_EXIST == ret) {  // first initialization
      ret = OB_SUCCESS;
      table_compaction_info.reset();
      table_compaction_info.table_id_ = table_id;
    } else {
      LOG_WARN("fail to get val from hashmap", KR(ret), K(table_id));
    }
  }
  return ret;
}

int ObChecksumValidator::check_inner_status()
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("is not init", KR(ret));
  } else if (stop_) {
    ret = OB_CANCELED;
    LOG_WARN("already stop", KR(ret));
  } else if (OB_UNLIKELY(!freeze_info_.is_valid() || nullptr == schema_guard_)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid freeze_info/schema_guard_", KR(ret),
      K_(freeze_info), KP_(schema_guard));
  } else if (OB_FAIL(check_tablet_checksum_sync_finish(false /*force_check*/))) {
  }
  return ret;
}

void ObChecksumValidator::clear_cached_info()
{
  standby_ckm_sync_finish_ = false;
  freeze_info_.reset();
  major_merge_start_us_ = 0;
  schema_guard_ = nullptr;
  simple_schema_ = nullptr;
  table_compaction_info_.reset();
  cur_tablet_ids_.reuse();
  finish_tablet_ids_.reuse();
  local_ckm_items_.reset();
  last_table_ckm_items_.clear();
}

int ObChecksumValidator::get_tablet_ids(
  const share::schema::ObSimpleTableSchemaV2 &simple_schema)
{
  int ret = OB_SUCCESS;
  cur_tablet_ids_.reuse();
  SMART_VAR(ObArray<ObTabletID>, tablet_ids) {
    if (OB_UNLIKELY(!simple_schema.has_tablet())) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("tablet schema should have tablet", K(ret), K(simple_schema));
    } else if (OB_FAIL(simple_schema.get_tablet_ids(tablet_ids))) {
    } else if (OB_UNLIKELY(tablet_ids.empty())) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("fail to get tablet_ids of current table schema", KR(ret), K(simple_schema));
    } else if (OB_FAIL(cur_tablet_ids_.reserve(tablet_ids.count()))) {
    } else if (OB_FAIL(finish_tablet_ids_.reserve(tablet_ids.count()))) {
    } else if (OB_FAIL(cur_tablet_ids_.assign(tablet_ids))) {
    } else {
#ifdef ERRSIM
        static int64_t enter_cnt = 0;
        if (OB_SUCC(ret) && simple_schema.is_global_index_table()) {
          ret = OB_E(EventTable::EN_GET_TABLET_LS_PAIR_IN_RS) OB_SUCCESS;
          if (OB_FAIL(ret)) {
            if (enter_cnt++ == 0) {
              ret = OB_ITEM_NOT_MATCH;
              STORAGE_LOG(INFO, "ERRSIM EN_GET_TABLET_LS_PAIR_IN_RS", K(ret), K(simple_schema), K_(cur_tablet_ids));
            } else {
              ret = OB_SUCCESS;
            }
          }
        }
#endif
    }
  }
  return ret;
}

int ObChecksumValidator::validate_checksum(
  const uint64_t table_id,
  share::schema::ObSchemaGetterGuard &schema_guard)
{
  int ret = OB_SUCCESS;
  schema_guard_ = &schema_guard;
  if (OB_UNLIKELY(0 == table_id)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(table_id));
  } else if (FALSE_IT(table_id_ = table_id)) {
  } else if (OB_FAIL(get_table_compaction_info(table_id_, table_compaction_info_))) {
  } else if (OB_FAIL(check_inner_status())) {
  } else if (table_compaction_info_.is_verified()
    || table_compaction_info_.can_skip_verifying()) {
    // do nothing
  } else if (tablet_status_map_.empty()) {
    table_compaction_info_.set_uncompacted();
  } else if (OB_FAIL(schema_guard_->get_simple_table_schema( table_id_, simple_schema_))) {
  } else if (OB_UNLIKELY(nullptr == simple_schema_ // table deleted
    || !simple_schema_->has_tablet())) {
    // like VIEW, it does not have tablet, treat it as compaction finished and can skip verifying
     table_compaction_info_.set_can_skip_verifying();
  } else if (OB_FAIL(get_tablet_ids(*simple_schema_))) {
    if (OB_ITEM_NOT_MATCH == ret) {
      ret = OB_SUCCESS;
      table_compaction_info_.set_can_skip_verifying();
      (void) uncompact_info_.add_skip_verify_table(table_id_);
    } else {
      LOG_WARN("failed to get tablet ids", K(ret), KPC_(simple_schema));
    }
  } else if (OB_UNLIKELY(cur_tablet_ids_.empty())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("tablet id array is unexpected empty", KR(ret), KPC_(simple_schema), K_(cur_tablet_ids));
  } else {
    if (OB_FAIL(validate_local_tablet_checksum())) {
    } else if (OB_FAIL(validate_index_checksum())) {
    } else if (OB_FAIL(validate_standby_checksum())) {
    }
    if (OB_FAIL(ret)) {
    } else if (table_compaction_info_.unfinish_index_cnt_ <= 0
      || table_compaction_info_.is_uncompacted()
      || table_compaction_info_.can_skip_verifying()) {
      // not cache index table/uncompacted or skip_verify data table
    } else if (local_ckm_items_.count() > 0) {
      int tmp_ret = OB_SUCCESS;
      last_table_ckm_items_.clear();
      if (OB_TMP_FAIL(last_table_ckm_items_.build(*schema_guard_, *simple_schema_, cur_tablet_ids_, local_ckm_items_))) {
      } else {
      }
    } else {
      last_table_ckm_items_.clear();
    }
  }
  cur_tablet_ids_.reuse(); // need reuse array when get_tablet_ids failed

  if (FAILEDx(table_compaction_map_.set_refactored(table_id_, table_compaction_info_, true /*overwrite*/))) {
    LOG_WARN("fail to set refactored", KR(ret), K_(table_id), K_(table_compaction_info));
  } else {
  }
  // do no clear table_compaction_info_ until validate next table
  local_ckm_items_.reset();
  schema_guard_ = nullptr;
  simple_schema_ = nullptr;
  return ret;
}

int ObChecksumValidator::validate_local_tablet_checksum()
{
  int ret = OB_SUCCESS;
  if (table_compaction_info_.is_uncompacted()) {
    if (OB_UNLIKELY(nullptr == simple_schema_ || !simple_schema_->has_tablet())) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("tablet schema should have tablet", K(ret), KPC_(simple_schema));
    } else {
      if (OB_FAIL(update_table_compaction_info_by_tablet())) {
      } else if (table_compaction_info_.is_compacted()) {
        // Verify the local checksum after the tablet finishes compaction.
        if (OB_FAIL(get_local_tablet_checksum_and_validate(false /*include_larger_than*/))) {
          if (OB_ITEM_NOT_MATCH == ret) {
            ret = OB_SUCCESS;
            table_compaction_info_.set_can_skip_verifying();
          } else {
            LOG_ERROR("fail to validate local tablet checksum", KR(ret), "compaction_scn", get_compaction_scn(), K_(table_compaction_info));
          }
        }
      }
    }
  }
  return ret;
}

int ObChecksumValidator::update_table_compaction_info_by_tablet()
{
  int ret = OB_SUCCESS;
  // iterate all tablets to check 'compacted/finished status' or not.
  int64_t idx = 0;
  const int64_t end_idx = cur_tablet_ids_.count();
  for ( ; OB_SUCC(ret) && (idx < end_idx); ++idx) {
    const ObTabletID &tablet_id = cur_tablet_ids_.at(idx);
    ObTabletCompactionStatusEnum tablet_status = ObTabletCompactionStatusEnum::INITIAL;
    if (OB_FAIL(tablet_status_map_.get_refactored(tablet_id, tablet_status))) {
      // if tablet not finish compaction, it won't be added into this map
      if (OB_HASH_NOT_EXIST == ret) {
        ret = OB_SUCCESS;
        table_compaction_info_.set_uncompacted();
        (void) uncompact_info_.add_tablet(tablet_id);
#ifdef ERRSIM
        ret = OB_E(EventTable::EN_SKIP_INDEX_MAJOR) ret;
        if (OB_FAIL(ret)) {
          ret = OB_SUCCESS;
          if (tablet_id.id() > ObTabletID::MIN_USER_TABLET_ID) {
            LOG_INFO("ERRSIM EN_SKIP_INDEX_MAJOR", K(ret), K(tablet_id));
            table_compaction_info_.set_can_skip_verifying();
          }
        }
#endif
        break;
      } else {
        LOG_WARN("fail to get tablet compaction status from map", KR(ret), K(idx));
      }
    } else if (ObTabletCompactionStatusEnum::INITIAL == tablet_status) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("invalid tablet status", KR(ret), K(tablet_status));
    } else if (ObTabletCompactionStatusEnum::CAN_SKIP_VERIFYING == tablet_status) {
      table_compaction_info_.set_can_skip_verifying();
      break;
    }
  } // end of for
  if (OB_SUCC(ret)) {
    if (idx == end_idx) { // loop finish
      table_compaction_info_.tablet_cnt_ = cur_tablet_ids_.count();
      table_compaction_info_.set_compacted();
    }
  }

  return ret;
}

int ObChecksumValidator::get_local_tablet_checksum_and_validate(const bool include_larger_than)
{
  int ret = OB_SUCCESS;
  FREEZE_TIME_GUARD;
  if (OB_FAIL(get_local_ckm(include_larger_than))) {
  } else if (OB_UNLIKELY(local_ckm_items_.get_tablet_cnt() != cur_tablet_ids_.count())) {
    ret = OB_ITEM_NOT_MATCH;
    local_ckm_items_.reset();
    (void) uncompact_info_.add_skip_verify_table(table_id_);
    LOG_TRACE("checksum count is not equal to tablet id count", KR(ret),
      K_(cur_tablet_ids), "compaction_scn", get_compaction_scn(), K_(table_compaction_info), K(local_ckm_items_));
  }
  return ret;
}


///////////////////////////////////////////////////////////////////////////////
/* Physical Standby Checksum Validator Section */
int ObChecksumValidator::validate_standby_checksum()
{
  int ret = OB_SUCCESS;

  if (stop_) {
    ret = OB_CANCELED;
    LOG_WARN("already stop", KR(ret));
  } else if (table_compaction_info_.is_index_ckm_verified()) {
    if (need_validate_standby_ckm_) {
      if (standby_ckm_sync_finish_ && OB_FAIL(validate_local_and_tablet_checksum())) {
        LOG_ERROR("fail to validate physical standby checksum", KR(ret), K_(stop),
                 "compaction_scn", get_compaction_scn(), K_(table_id));
      }
    } else { // The primary writes the checksum for its standby consumer.
      if (OB_FAIL(try_update_tablet_checksum_items())) {
      }
    }
    ret = OB_ITEM_NOT_MATCH == ret ? OB_SUCCESS : ret; // clear errno
    if (OB_SUCC(ret)) {
      if (OB_FAIL(push_finish_tablet_ids_with_update(table_id_, cur_tablet_ids_))) {
      } else {
        table_compaction_info_.set_verified();
      }
    }
  } else {
    // do nothing. index validator should already wrote ckm and updated report_scn
  }
  return ret;
}

int ObChecksumValidator::batch_write_tablet_ckm()
{
  int ret = OB_SUCCESS;
  if (finish_tablet_ckm_array_.empty()) {
  } else if (!is_primary_service_) {
    // only primary major_freeze_service need to write tablet checksum
  } else {
    const int64_t IMMEDIATE_RETRY_CNT = 5;
    int64_t fail_count = 0;
    int64_t sleep_time_us = 200 * 1000; // 200 ms
    while (OB_SUCC(ret) && !stop_
          && (fail_count < IMMEDIATE_RETRY_CNT)) {
      if (OB_SUCC(ObTabletChecksumOperator::update_tablet_checksum_items(
          *sql_proxy_, finish_tablet_ckm_array_))) {
        ++statistics_.write_ckm_sql_cnt_;
        break;
      } else {
        ++fail_count;
        LOG_ERROR("fail to write tablet checksum items", KR(ret), K(fail_count), K(sleep_time_us));
        ob_throttle_usleep(sleep_time_us, ret, get_compaction_scn_val());
        sleep_time_us *= 2;
        ret = OB_SUCCESS;
      }
    } // end of while
    if (OB_SUCC(ret)) {
      finish_tablet_ckm_array_.reuse();
    }
  }
  return ret;
}

int ObChecksumValidator::batch_update_report_scn()
{
  int ret = OB_SUCCESS;
  if (finish_tablet_ids_.empty()) {
  } else if (OB_FAIL(ObTabletMetaTableCompactionOperator::batch_update_report_scn(
          GCTX.meta_db_pool_,
          get_compaction_scn_val(),
          finish_tablet_ids_,
          ObTabletRuntimeInfo::ScnStatus::SCN_STATUS_ERROR /*except_status*/))) {
  } else {
    ++statistics_.update_report_scn_sql_cnt_;
    LOG_INFO("success to batch update report_scn", KR(ret),
             "table_cnt", finish_tablet_ids_.count());
    finish_tablet_ids_.reuse();
  }
  return ret;
}

int ObChecksumValidator::check_tablet_checksum_sync_finish(const bool force_check)
{
  int ret = OB_SUCCESS;
  bool is_exist = false;
  // need check inner table:
  // 1) force check when first init
  // 2) ckm not sync finish in standby service
  if (!force_check && (is_primary_service_ || standby_ckm_sync_finish_)) {
  } else if (OB_FAIL(ObTabletChecksumOperator::is_first_tablet_checksum_exist(*sql_proxy_, get_compaction_scn(), is_exist))) {
  } else if (is_exist) {
    standby_ckm_sync_finish_ = true;
  } else if (is_primary_service_) {
    standby_ckm_sync_finish_ = false;
  } else {
    standby_ckm_sync_finish_ = check_waiting_tablet_checksum_timeout();
    if (TC_REACH_TIME_INTERVAL(PRINT_STANDBY_CHECKSUM_LOG_INTERVAL)) {
      LOG_ERROR("can not check physical standby checksum until the first tablet checksum exists",
             "compaction_scn", get_compaction_scn(), K_(major_merge_start_us),
             "fast_current_time_us", ObTimeUtil::fast_current_time(), K(is_exist), K_(is_primary_service));
    }
  }
  return ret;
}

int ObChecksumValidator::validate_local_and_tablet_checksum()
{
  int ret = OB_SUCCESS;
  SMART_VAR(ObArray<ObTabletChecksumItem>, tablet_checksum_items) {
    FREEZE_TIME_GUARD;
    if (local_ckm_items_.empty() && OB_FAIL(get_local_ckm())) {
      LOG_ERROR("fail to batch get local tablet checksum items", KR(ret), "compaction_scn", get_compaction_scn());
    } else if (OB_FAIL(ObTabletChecksumOperator::load_tablet_checksum_items(*sql_proxy_,
                        cur_tablet_ids_, get_compaction_scn(), tablet_checksum_items))) {
    } else if (local_ckm_items_.empty() || tablet_checksum_items.empty()
        || local_ckm_items_.get_tablet_cnt() != tablet_checksum_items.count()) {
      ret = OB_ITEM_NOT_MATCH;
      (void) uncompact_info_.add_skip_verify_table(table_id_);
      table_compaction_info_.set_verified();
      LOG_WARN("fail to get checksum items", KR(ret), "compaction_scn", get_compaction_scn(),
        K(local_ckm_items_), K(tablet_checksum_items));
    } else if (OB_FAIL(check_column_checksum(local_ckm_items_, tablet_checksum_items))) {
      if (OB_CHECKSUM_ERROR == ret) {
        LOG_ERROR("ERROR! ERROR! ERROR! checksum error in cross-cluster checksum", KR(ret), "compaction_scn", get_compaction_scn());
      } else {
        LOG_ERROR("fail to check cross-cluster checksum", KR(ret),
          "compaction_scn", get_compaction_scn());
      }
    }
  }
  return ret;
}

int ObChecksumValidator::check_column_checksum(
    const ObLocalTabletChecksumArray &local_tablet_checksum_items,
    const ObArray<ObTabletChecksumItem> &tablet_checksum_items)
{
  int ret = OB_SUCCESS;
  const ObTabletLocalChecksumItem *local_item = NULL;
  for (int64_t i = 0; OB_SUCC(ret) && (i < tablet_checksum_items.count()); ++i) {
    const ObTabletChecksumItem &tablet_ckm_item = tablet_checksum_items.at(i);
    if (OB_FAIL(local_tablet_checksum_items.get(tablet_ckm_item.get_tablet_id(), local_item))) {
      if (OB_ENTRY_NOT_EXIST == ret) {
        ret = OB_SUCCESS;
      }
    } else {
      if (OB_FAIL(tablet_ckm_item.verify_tablet_column_checksum(*local_item))) {
        if (OB_CHECKSUM_ERROR == ret) {
          LOG_DBA_ERROR(OB_CHECKSUM_ERROR, "msg", "ERROR! ERROR! ERROR! checksum error in "
                        "cross-cluster checksum", K(tablet_ckm_item), KPC(local_item));
        } else {
          LOG_WARN("unexpected error in cross-cluster checksum", KR(ret),
                   K(tablet_ckm_item), KPC(local_item));
        }
      }
    }
  } // end of for
  return ret;
}

bool ObChecksumValidator::check_waiting_tablet_checksum_timeout() const
{

  const int64_t total_wait_time_us = (ObTimeUtil::fast_current_time() - major_merge_start_us_);
  const bool is_timeout = (total_wait_time_us > MAX_TABLET_CHECKSUM_WAIT_TIME_US);
  if (is_timeout) {
    LOG_WARN_RET(OB_TIMEOUT, "check waiting tablet checksum timeout", K_(major_merge_start_us), K(total_wait_time_us));
  }
  return is_timeout;
}

int ObChecksumValidator::try_update_tablet_checksum_items()
{
  int ret = OB_SUCCESS;
  const bool include_lager_than = (table_id_ == SPECIAL_TABLE_ID ? true : false);
  if (local_ckm_items_.empty() && OB_FAIL(get_local_ckm(include_lager_than))) {
    LOG_ERROR("fail to batch get local tablet checksum items", KR(ret),  "compaction_scn", get_compaction_scn());
  } else if (local_ckm_items_.get_tablet_cnt() < cur_tablet_ids_.count()) {
    ret = OB_ITEM_NOT_MATCH;
    (void) uncompact_info_.add_skip_verify_table(table_id_);
    LOG_WARN("fail to get local tablet checksum items", KR(ret),  "compaction_scn", get_compaction_scn(),
      K_(cur_tablet_ids), K(local_ckm_items_));
  } else if (OB_FAIL(push_tablet_ckm_items_with_update(local_ckm_items_.get_array()))) {
  }
  return ret;
}

int ObChecksumValidator::push_finish_tablet_ids_with_update(
  const uint64_t table_id,
  const common::ObIArray<common::ObTabletID> &tablet_ids)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(finish_tablet_ids_.push_back(tablet_ids))) {
  } else {
    bool need_update_report_scn = (finish_tablet_ids_.count() >= MAX_BATCH_INSERT_COUNT)
      || table_id == SPECIAL_TABLE_ID;
#ifdef ERRSIM
    need_update_report_scn = true;
#endif
    if (need_update_report_scn) {
      int64_t tmp_ret = OB_SUCCESS;
      if (OB_TMP_FAIL(batch_update_report_scn())) {
      }
    }
  }
  return ret;
}

int ObChecksumValidator::push_tablet_ckm_items_with_update(
  const ObIArray<ObTabletLocalChecksumItem> &local_ckm_items)
{
  int ret = OB_SUCCESS;
  ObTabletChecksumItem tmp_checksum_item;
  for (int64_t i = 0; !stop_ && OB_SUCC(ret) && (i < local_ckm_items.count()); ++i) {
    const ObTabletLocalChecksumItem &curr_local_item = local_ckm_items.at(i);
    if (OB_UNLIKELY(!curr_local_item.is_key_valid())) {
      ret = OB_ERR_UNEXPECTED;
      LOG_ERROR("local tablet checksum is not valid", KR(ret),
               K(curr_local_item));
    } else if (OB_FAIL(tmp_checksum_item.assign(curr_local_item))) {
    } else if (OB_FAIL(finish_tablet_ckm_array_.push_back(tmp_checksum_item))) {
    }
  } // end of for
  if (OB_SUCC(ret)
      && (finish_tablet_ckm_array_.count() >= MAX_BATCH_INSERT_COUNT || table_id_ == SPECIAL_TABLE_ID)) {
    (void) batch_write_tablet_ckm();
  }
  return ret;
}

///////////////////////////////////////////////////////////////////////////////
/* Data Table - Index Table Checksum Validator Section */
int ObChecksumValidator::validate_index_checksum() {
  int ret = OB_SUCCESS;
  if (stop_) {
    ret = OB_CANCELED;
    LOG_WARN("already stop", KR(ret));
  } else if (OB_ISNULL(simple_schema_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("table schema is unexpected null", K(ret), KPC_(simple_schema));
  } else if (!need_validate_index_ckm_) { // no need to validate data-index checksum
    table_compaction_info_.set_index_ckm_verified();
  } else if (simple_schema_->is_index_table()) { // for index table, do not check status
    bool should_handle_index_table = true;
    // only for case : check special index table first
#ifdef ERRSIM
    if (EN_SPECIAL_INDEX_TABLE_VERIFY && !simple_schema_->should_not_validate_data_index_ckm()) { 
      should_handle_index_table = false;
    }
#endif
    if (should_handle_index_table && !table_compaction_info_.finish_idx_verified() && OB_FAIL(handle_index_table(*simple_schema_))) {
      LOG_WARN("fail to handle index table", KR(ret), KPC_(simple_schema));
    }
#ifdef ERRSIM
    if (EN_SPECIAL_INDEX_TABLE_VERIFY && simple_schema_->should_not_validate_data_index_ckm()) { 
      SERVER_EVENT_ADD("storage_engine", "special_index_table_verify",
        "index_table_id", simple_schema_->get_table_id(),
        "data_table_id", simple_schema_->get_data_table_id());
    }
#endif
  } else if (table_compaction_info_.need_check_fts_) {
    LOG_INFO("check fts for data table", KR(ret), K_(table_compaction_info));
  } else if (table_compaction_info_.is_compacted()) { // for data table, check status
    if (0 == table_compaction_info_.unfinish_index_cnt_) { // no unfinish index
      table_compaction_info_.set_index_ckm_verified();
    }
  }
  return ret;
}

int ObChecksumValidator::handle_index_table(
  const share::schema::ObSimpleTableSchemaV2 &index_simple_schema)
{
  int ret = OB_SUCCESS;
  const uint64_t index_table_id = index_simple_schema.get_table_id();
  const uint64_t data_table_id = index_simple_schema.get_data_table_id();
  ObTableCompactionInfo &index_compaction_info = table_compaction_info_; // cur table is index
  ObTableCompactionInfo data_compaction_info;
  if (OB_FAIL(get_table_compaction_info(data_table_id, data_compaction_info))) {
  } else if (!index_simple_schema.can_read_index()) {
    // for index table can not read, directly mark it as VERIFIED
    // do not check compaction_scn and validate checksum of can not read
    // index's tablets. although update_all_tablets_report_scn will update
    // its report_scn. the storage layer may schedule major compaction and
    // increase compaction_scn of this index's tablets later.
    index_compaction_info.set_can_skip_verifying();
  } else if (data_compaction_info.is_index_ckm_verified() || data_compaction_info.is_verified()) {
    // if a data table finished verification, then create index on this data table.
    // we should skip verification for this index table, cuz the data table may already
    // launched another medium compaction.
    LOG_INFO("index table is not verified while data table is already verified, skip"
            " verification for this index table", K(index_table_id), K(data_table_id),
            K(index_compaction_info), K(data_compaction_info));
    if (index_compaction_info.finish_compaction()) {
      index_compaction_info.set_index_ckm_verified();
    }
  } else if (fts_group_array_.need_check_fts() && index_simple_schema.is_fts_or_multivalue_index()) {
    LOG_INFO("skip fts or multivalue index", KR(ret), K(index_table_id), K(index_compaction_info));
  } else {
      if (index_compaction_info.is_compacted() && data_compaction_info.is_compacted()) {
#ifdef ERRSIM
        if (OB_SUCC(ret)) {
          ret = OB_E(EventTable::EN_MEDIUM_VERIFY_GROUP_SKIP_SET_VERIFY) OB_SUCCESS;
          if (OB_FAIL(ret)) {
            if (!is_inner_table(index_table_id)) {
              ret = OB_EAGAIN;
              STORAGE_LOG(INFO, "ERRSIM EN_MEDIUM_VERIFY_GROUP_SKIP_SET_VERIFY failed", K(ret));
            } else {
              ret = OB_SUCCESS;
            }
            return ret;
          }
        }
#endif
      // set it to false, if succ to handle_table_can_not_verify
      // both tables' all tablets finished compaction, validate column
      // checksum if need_validate()
      if (OB_UNLIKELY(index_simple_schema.should_not_validate_data_index_ckm())) {
        // do nothing
        // spatial index column is different from data table column
        index_compaction_info.set_index_ckm_verified();
      } else if (1 == data_compaction_info.unfinish_index_cnt_ || last_table_ckm_items_.is_inited()) {
        // only one index
        if (OB_FAIL(verify_table_index(index_simple_schema, data_compaction_info, index_compaction_info))) {
        }
      } else if (OB_FAIL(idx_ckm_validate_array_.push_back(ObIndexCkmValidatePair(data_table_id, index_table_id)))) {
      }
    } else if (index_compaction_info.can_skip_verifying()
      || data_compaction_info.can_skip_verifying()) {
        // if one of them can skip verifying, that means we don't need to
        // execute index checksum verification. Mark index table as
        // INDEX_CKM_VERIFIED directly.
      index_compaction_info.set_index_ckm_verified();
    }
  }
  // deal with data table
  if (OB_SUCC(ret) && index_compaction_info.finish_idx_verified() && !data_compaction_info.finish_idx_verified()) {
    if (index_simple_schema.should_not_validate_data_index_ckm()) { // special index table not count in unfinish index cnt
      if (0 == data_compaction_info.unfinish_index_cnt_) {
        data_compaction_info.set_index_ckm_verified();
      }
    } else {
      if ((0 == (--data_compaction_info.unfinish_index_cnt_)) && !data_compaction_info.need_check_fts_) {
        data_compaction_info.set_index_ckm_verified();
      }
    }
    // add for defend, unfinish_index_cnt_ of data table should not be less than 0
    if (data_compaction_info.unfinish_index_cnt_ < 0) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("unfinish index cnt is unexpected", KR(ret), K(data_compaction_info));
    } else if (OB_FAIL(table_compaction_map_.set_refactored(
            data_compaction_info.table_id_, data_compaction_info,
            true /*overwrite*/))) {
    }
  }
  return ret;
}

int ObChecksumValidator::verify_table_index(
    const share::schema::ObSimpleTableSchemaV2 &index_simple_schema,
    compaction::ObTableCompactionInfo &data_compaction_info,
    compaction::ObTableCompactionInfo &index_compaction_info)
{
  int ret = OB_SUCCESS;
  FREEZE_TIME_GUARD;
  const uint64_t index_table_id = index_simple_schema.get_table_id();
  const uint64_t data_table_id = index_simple_schema.get_data_table_id();
  if (local_ckm_items_.empty() && OB_FAIL(get_local_ckm())) {
    LOG_ERROR("fail to batch get local tablet checksum items", KR(ret),  "compaction_scn", get_compaction_scn());
  } else if (local_ckm_items_.get_tablet_cnt() < cur_tablet_ids_.count()) {
    ret = OB_ITEM_NOT_MATCH;
    (void) uncompact_info_.add_skip_verify_table(table_id_);
    LOG_WARN("fail to get local tablet checksum items", KR(ret),  "compaction_scn", get_compaction_scn(),
      K_(cur_tablet_ids), K(local_ckm_items_));
  } else {
    ObTableCkmItems data_table_ckm{};
    ObTableCkmItems *data_table_ckm_ptr = nullptr;
    ObTableCkmItems index_table_ckm{};
    if (last_table_ckm_items_.is_inited()) { // use cached data table ckm
      if (OB_UNLIKELY(last_table_ckm_items_.get_table_id() != data_table_id)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("cached last table ckm items is invalid", KR(ret), K(data_table_id), K_(last_table_ckm_items));
      } else {
        data_table_ckm_ptr = &last_table_ckm_items_;
        ++statistics_.use_cached_ckm_cnt_;
      }
    }
    if (nullptr != data_table_ckm_ptr || OB_FAIL(ret)) {
    } else if (FALSE_IT(data_table_ckm_ptr = &data_table_ckm)) {
    } else if (OB_FAIL(data_table_ckm.build(data_table_id, get_compaction_scn(),
                                     *schema_guard_))) {
    } else {
      ++statistics_.query_ckm_sql_cnt_;
    }
    if (FAILEDx(index_table_ckm.build(*schema_guard_, index_simple_schema, cur_tablet_ids_,
                                      local_ckm_items_))) {
      LOG_WARN("failed to assign checksum items", K(ret), K(local_ckm_items_));
    } else {
      const bool is_global_index = index_simple_schema.is_global_index_table();
      if (OB_FAIL(ObTableCkmItems::validate_ckm_func[is_global_index](
          freeze_info_,
          *sql_proxy_,
          *data_table_ckm_ptr,
          index_table_ckm))) {
      }
    }
  }
  if (OB_FAIL(ret)) {
    if (OB_ITEM_NOT_MATCH == ret) {
      (void) uncompact_info_.add_skip_verify_table(table_id_);
      index_compaction_info.set_can_skip_verifying();
      ret = OB_SUCCESS; // clear errno
    }
  } else {
    index_compaction_info.set_index_ckm_verified();
  }
  return ret;
}

int ObChecksumValidator::get_local_ckm(const bool include_larger_than/* = false*/)
{
  int ret = OB_SUCCESS;
  ++statistics_.query_ckm_sql_cnt_;
  return ObTabletLocalChecksumOperator::batch_get(
      cur_tablet_ids_, get_compaction_scn(),
      local_ckm_items_, include_larger_than);
}

/***************************************** FTS Checksum Section ******************************************/

int ObChecksumValidator::build_ckm_item_for_fts(const int64_t table_id,
                                                ObTableCkmItems &ckm_item,
                                                ObIArray<int64_t> &finish_table_ids)
{
  int ret = OB_SUCCESS;
  bool skip_verify = false;
  ObTableCompactionInfo table_compaction_info;
  if (OB_FAIL(get_table_compaction_info(table_id, table_compaction_info))) {
  } else if (OB_UNLIKELY(!table_compaction_info.is_compacted())) {
    LOG_WARN("exist special status table", KR(ret), K(table_compaction_info));
    skip_verify = true;
  } else if (OB_FAIL(ckm_item.build(table_id, get_compaction_scn(),
                                    *schema_guard_))) {
    if (OB_TABLE_NOT_EXIST == ret || OB_STATE_NOT_MATCH == ret || OB_ITEM_NOT_MATCH == ret) {
      skip_verify = true;
      ret = OB_SUCCESS;
    } else {
      LOG_ERROR("fail to prepare schema checksum items", KR(ret), K(table_id));
    }
  } else if (OB_FAIL(finish_table_ids.push_back(table_id))) {
  } else {
    ckm_item.set_is_fts_index(true);
  }

  if (OB_FAIL(ret) || !skip_verify) {
  } else if (OB_FAIL(finish_verify_fts_ckm(table_id))) {
  } else {
    LOG_INFO("skip verify fts ckm", KR(ret), K(table_id));
  }
  return ret;
}

int ObChecksumValidator::finish_verify_fts_ckm(const int64_t table_id)
{
  int ret = OB_SUCCESS;
  ObTableCompactionInfo table_compaction_info;
  if (OB_FAIL(get_table_compaction_info(table_id, table_compaction_info))) {
  } else if (FALSE_IT(table_compaction_info.need_check_fts_ = false)) {
  } else if (table_compaction_info.unfinish_index_cnt_ <= 0) {
    // for data table, may exist other index
    table_compaction_info.set_index_ckm_verified();
  }
  if (FAILEDx(table_compaction_map_.set_refactored(table_id, table_compaction_info, true /*overwrite*/))) {
    LOG_WARN("fail to set refactored", KR(ret), K(table_id), K(table_compaction_info));
  }
  return ret;
}

#define VALIDATE_CKM(data_ckm, index_ckm)                                      \
  if (OB_FAIL(ret) || !data_ckm.is_inited() || !index_ckm.is_inited()) {       \
  } else if (OB_FAIL(ObTableCkmItems::validate_ckm_func[0](                    \
                 freeze_info_, *sql_proxy_, data_ckm, index_ckm))) {           \
    LOG_ERROR("failed to validate ckm func", KR(ret), K(data_ckm),             \
              K(index_ckm));                                                   \
  }

int ObChecksumValidator::handle_fts_checksum(
  share::schema::ObSchemaGetterGuard &schema_guard,
  const ObFTSGroupArray &fts_group_array)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(fts_group_array.count() <= 0)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), K(fts_group_array));
  } else {
    schema_guard_ = &schema_guard;
    ObSEArray<int64_t, 16> finish_table_ids;
    finish_table_ids.set_attr(ObMemAttr("FTS_CKM_VER"));
    for (int64_t arr_idx = 0; OB_SUCC(ret) && arr_idx < fts_group_array.count(); ++arr_idx) {
      const ObFTSGroup &fts_group = fts_group_array.at(arr_idx);
      if (OB_FAIL(validate_rowkey_doc_indexs(fts_group, finish_table_ids))) {
      }
      for (int64_t idx = 0; OB_SUCC(ret) && idx < fts_group.count(); ++idx) {
        if (OB_FAIL(validate_fts_indexs(fts_group.at(idx), finish_table_ids))) {
        } else {
          LOG_INFO("validate index info", K(ret), K(fts_group), K(idx), K(fts_group.at(idx)), K(finish_table_ids));
        }
      } // for of fts_group
    } // for of fts_group_array
    for (int64_t idx = 0; OB_SUCC(ret) && idx < finish_table_ids.count(); ++idx) {
      if (OB_FAIL(finish_verify_fts_ckm(finish_table_ids.at(idx)))) {
      }
    } // for
    schema_guard_ = NULL;
  }

  return ret;
}

int ObChecksumValidator::validate_rowkey_doc_indexs(const ObFTSGroup &fts_group, ObIArray<int64_t> &finish_table_ids)
{
  int ret = OB_SUCCESS;
  ObArenaAllocator tmp_allocator(lib::ObMemAttr("ckmvfydoc"));
  ObTableCkmItems* ckm_item[3];
  for (int64_t i = 0; OB_SUCC(ret) && i < 3; ++i) {
    void *buf = nullptr;
    if (OB_ISNULL(buf = tmp_allocator.alloc(sizeof(ObTableCkmItems)))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("failed to alloc mem for table ckm items", K(ret));
    } else {
      ObTableCkmItems *ptr = new (buf) ObTableCkmItems();
      ckm_item[i] = ptr;
    }
  }

  if (FAILEDx(build_ckm_item_for_fts(fts_group.data_table_id_, *ckm_item[0], finish_table_ids))) {
    LOG_WARN_RET(ret, "failed to build ckm", K(fts_group.data_table_id_));
  } else if (OB_FAIL(build_ckm_item_for_fts(fts_group.rowkey_doc_index_id_, *ckm_item[1], finish_table_ids))) {
    LOG_WARN_RET(ret, "failed to build ckm", K(fts_group.rowkey_doc_index_id_));
  } else if (OB_FAIL(build_ckm_item_for_fts(fts_group.doc_rowkey_index_id_, *ckm_item[2], finish_table_ids))) {
    LOG_WARN_RET(ret, "failed to build ckm", K(fts_group.doc_rowkey_index_id_));
  }
  // all fts index is local index now
  VALIDATE_CKM((*ckm_item[0]), (*ckm_item[1]));
  VALIDATE_CKM((*ckm_item[1]), (*ckm_item[2]));

  for (int64_t i = 0; i < 3; ++i) {
    ObTableCkmItems *ptr = ckm_item[i];
    if (OB_NOT_NULL(ptr)) {
      ptr->~ObTableCkmItems();
      tmp_allocator.free(ptr);
      ckm_item[i] = nullptr;
    }
  }
  return ret;
}

int ObChecksumValidator::validate_fts_indexs(const ObFTSIndexInfo &index_info, ObIArray<int64_t> &finish_table_ids)
{
  int ret = OB_SUCCESS;
  ObTableCkmItems ckm_item[2];
  if (OB_FAIL(build_ckm_item_for_fts(index_info.fts_index_id_, ckm_item[0], finish_table_ids))) {
    LOG_WARN_RET(ret, "failed to build ckm", K(index_info.fts_index_id_));
  } else if (OB_FAIL(build_ckm_item_for_fts(index_info.doc_word_index_id_, ckm_item[1], finish_table_ids))) {
    LOG_WARN_RET(ret, "failed to build ckm", K(index_info.doc_word_index_id_));
  }
  VALIDATE_CKM(ckm_item[0], ckm_item[1]);
  return ret;
}
#undef VALIDATE_CKM

} // end namespace rootserver
} // end namespace oceanbase
