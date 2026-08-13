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

#define USING_LOG_PREFIX STORAGE
#include "ob_standby_restore_tablet_builder.h"
#include "standby/restore/ob_restore_helper.h"
#include "share/ob_structured_event_logger.h"
#include "storage/ob_storage_schema_util.h"
#include "storage/tablet/ob_mds_schema_helper.h"

namespace oceanbase
{
using namespace share;
namespace storage
{

/******************ObStandbyRestoreTabletTableInfoMgr*********************/
ObStandbyRestoreTableInfoMgr::ObStandbyRestoreTabletTableInfoMgr::ObStandbyRestoreTabletTableInfoMgr()
  : is_inited_(false),
    tablet_id_(),
    status_(ObCopyTabletStatus::MAX_STATUS),
    allocator_("StandbyTable", OB_MALLOC_NORMAL_BLOCK_SIZE),
    copy_table_info_array_(OB_MALLOC_NORMAL_BLOCK_SIZE, ModulePageAllocator(allocator_)),
    tablet_meta_()
{
}

ObStandbyRestoreTableInfoMgr::ObStandbyRestoreTabletTableInfoMgr::~ObStandbyRestoreTabletTableInfoMgr()
{
}

int ObStandbyRestoreTableInfoMgr::ObStandbyRestoreTabletTableInfoMgr::init(
    const ObTabletID &tablet_id,
    const storage::ObCopyTabletStatus::STATUS &status,
    const ObMigrationTabletParam &tablet_meta)
{
  int ret = OB_SUCCESS;
  if (is_inited_) {
    ret = OB_INIT_TWICE;
    LOG_WARN("storage ha tablet table info mgr init twice", K(ret), K(tablet_id));
  } else if (!tablet_id.is_valid() || !ObCopyTabletStatus::is_valid(status)
      || (ObCopyTabletStatus::TABLET_EXIST == status && !tablet_meta.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("init storage ha tablet table info mgr get invalid argument", K(ret), K(tablet_id),
        K(status), K(tablet_meta));
  } else if (ObCopyTabletStatus::TABLET_EXIST == status && OB_FAIL(tablet_meta_.assign(tablet_meta))) {
    LOG_WARN("failed to assign tablet meta", K(ret), K(tablet_meta));
  } else {
    tablet_id_ = tablet_id;
    status_ = status;
    is_inited_ = true;
  }
  return ret;
}

int ObStandbyRestoreTableInfoMgr::ObStandbyRestoreTabletTableInfoMgr::get_copy_table_info(
    const ObITable::TableKey &table_key,
    const blocksstable::ObMigrationSSTableParam *&copy_table_info)
{
  int ret = OB_SUCCESS;
  bool found = false;
  copy_table_info  = nullptr;

  if (!is_inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("storage ha tablet table info mgr do not init", K(ret));
  } else if (!table_key.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("get copy table info get invalid argument", K(ret), K(table_key));
  } else {
    for (int64_t i = 0; i < copy_table_info_array_.count() && !found; ++i) {
      const ObMigrationSSTableParam &tmp_copy_table_info = copy_table_info_array_.at(i);
      if (table_key == tmp_copy_table_info.table_key_) {
        copy_table_info = &copy_table_info_array_.at(i);
        found = true;
      }
    }

    if (!found) {
      ret = OB_ENTRY_NOT_EXIST;
      LOG_WARN("failed to get copy table key info", K(ret), K(table_key));
    }
  }
  return ret;
}

int ObStandbyRestoreTableInfoMgr::ObStandbyRestoreTabletTableInfoMgr::add_copy_table_info(
    const blocksstable::ObMigrationSSTableParam &copy_table_info)
{
  int ret = OB_SUCCESS;
  bool is_exist = false;
  bool found = false;

  if (!is_inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("storage ha tablet table info mgr do not init", K(ret));
  } else if (!copy_table_info.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("add copy table key get invalid argument", K(ret), K(copy_table_info));
  } else{
    for (int64_t i = 0; i < copy_table_info_array_.count() && !found; ++i) {
      const ObMigrationSSTableParam &tmp_copy_table_info = copy_table_info_array_.at(i);
      if (copy_table_info.table_key_ == tmp_copy_table_info.table_key_) {
        found = true;
      }
    }

    if (!found) {
      if (OB_FAIL(copy_table_info_array_.push_back(copy_table_info))) {
        LOG_WARN("failed to push copy table key info into array", K(ret), K(copy_table_info));
      }
    }
  }
  return ret;
}

int ObStandbyRestoreTableInfoMgr::ObStandbyRestoreTabletTableInfoMgr::get_table_keys(
    common::ObIArray<ObITable::TableKey> &table_keys)
{
  int ret = OB_SUCCESS;
  table_keys.reset();

  if (!is_inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("storage ha tablet table info mgr do not init", K(ret));
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < copy_table_info_array_.count(); ++i) {
      const ObMigrationSSTableParam &tmp_copy_table_info = copy_table_info_array_.at(i);
      if (OB_FAIL(table_keys.push_back(tmp_copy_table_info.table_key_))) {
        LOG_WARN("failed to push table key into array", K(ret), K(tmp_copy_table_info));
      }
    }
  }
  return ret;
}

int ObStandbyRestoreTableInfoMgr::ObStandbyRestoreTabletTableInfoMgr::check_copy_tablet_exist(bool &is_exist)
{
  int ret = OB_SUCCESS;
  is_exist = false;

  if (!is_inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("storage ha tablet table info mgr do not init", K(ret));
  } else {
    is_exist = ObCopyTabletStatus::TABLET_EXIST == status_;
  }
  return ret;
}

int ObStandbyRestoreTableInfoMgr::ObStandbyRestoreTabletTableInfoMgr::get_tablet_meta(const ObMigrationTabletParam *&tablet_meta)
{
  int ret = OB_SUCCESS;
  tablet_meta = nullptr;
  if (!is_inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("storage ha tablet table info mgr do not init", K(ret));
  } else if (ObCopyTabletStatus::TABLET_EXIST != status_) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("src tablet do not exist, cannot get tablet meta", K(ret), K(status_));
  } else {
    tablet_meta = &tablet_meta_;
  }
  return ret;
}

/******************ObStandbyRestoreTableInfoMgr*********************/
ObStandbyRestoreTableInfoMgr::ObStandbyRestoreTableInfoMgr()
  : is_inited_(false),
    lock_(),
    table_info_mgr_map_()
{
}

ObStandbyRestoreTableInfoMgr::~ObStandbyRestoreTableInfoMgr()
{
  reuse();
}

int ObStandbyRestoreTableInfoMgr::init()
{
  int ret = OB_SUCCESS;
  if (is_inited_) {
    ret = OB_INIT_TWICE;
    LOG_WARN("storage ha table info mgr init twice", K(ret));
  } else if (OB_FAIL(table_info_mgr_map_.create(MAX_BUCEKT_NUM, "StandbyTableMgr"))) {
    LOG_WARN("failed to create tablet table key mgr", K(ret));
  } else {
    is_inited_ = true;
  }
  return ret;
}

int ObStandbyRestoreTableInfoMgr::get_table_info(
    const common::ObTabletID &tablet_id,
    const ObITable::TableKey &table_key,
    const blocksstable::ObMigrationSSTableParam *&copy_table_info)
{
  int ret = OB_SUCCESS;
  copy_table_info = nullptr;
  ObStandbyRestoreTabletTableInfoMgr *tablet_table_info_mgr = nullptr;

  if (!is_inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("storage ha table info mgr do not init", K(ret));
  } else if (!tablet_id.is_valid() || !table_key.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("get table key info get invalid argument", K(ret), K(tablet_id), K(table_key));
  } else {
    common::SpinRLockGuard guard(lock_);
    if (OB_FAIL(table_info_mgr_map_.get_refactored(tablet_id, tablet_table_info_mgr))) {
      LOG_WARN("failed to get tablet table key mgr", K(ret), K(tablet_id));
    } else if (OB_ISNULL(tablet_table_info_mgr)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("tablet table key mgr should not be NULL", K(ret), KP(tablet_table_info_mgr));
    } else if (OB_FAIL(tablet_table_info_mgr->get_copy_table_info(table_key, copy_table_info))) {
      LOG_WARN("failed to get copy table key info", K(ret), K(tablet_id), K(table_key));
    }
  }
  return ret;
}

int ObStandbyRestoreTableInfoMgr::add_table_info(
    const common::ObTabletID &tablet_id,
    const obcall::ObCopyTabletSSTableInfo &sstable_info)
{
  int ret = OB_SUCCESS;
  ObStandbyRestoreTabletTableInfoMgr *tablet_table_info_mgr = nullptr;

  if (!is_inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("storage ha table info mgr do not init", K(ret), K(tablet_id));
  } else if (!tablet_id.is_valid() || !sstable_info.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("add table info get invalid argument", K(ret), K(tablet_id), K(sstable_info));
  } else {
    common::SpinWLockGuard guard(lock_);
    if (OB_FAIL(table_info_mgr_map_.get_refactored(tablet_id, tablet_table_info_mgr))) {
      LOG_WARN("failed to get tablet table info mgr", K(ret), K(tablet_id));
    } else if (OB_FAIL(tablet_table_info_mgr->add_copy_table_info(sstable_info.param_))) {
      LOG_WARN("failed to add copy table key info", K(ret), K(tablet_id), K(sstable_info));
    }
  }
  return ret;
}

void ObStandbyRestoreTableInfoMgr::reuse()
{
  common::SpinWLockGuard guard(lock_);
  if (!table_info_mgr_map_.created()) {
  } else {
    for (TabletTableInfoMgr::iterator iter = table_info_mgr_map_.begin(); iter != table_info_mgr_map_.end(); ++iter) {
      ObStandbyRestoreTabletTableInfoMgr *tablet_table_info_mgr = iter->second;
      tablet_table_info_mgr->~ObStandbyRestoreTabletTableInfoMgr();
      ob_free(tablet_table_info_mgr);
      tablet_table_info_mgr = nullptr;
    }
    table_info_mgr_map_.reuse();
  }
}

int ObStandbyRestoreTableInfoMgr::remove_tablet_table_info(const common::ObTabletID &tablet_id)
{
  int ret = OB_SUCCESS;
  ObStandbyRestoreTabletTableInfoMgr *tablet_table_info_mgr = nullptr;

  if (!is_inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("storage ha table info mgr do not init", K(ret));
  } else if (!tablet_id.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("remove tablet table key mgr get invalid argument", K(ret), K(tablet_id));
  } else {
    common::SpinWLockGuard guard(lock_);
    if (OB_FAIL(table_info_mgr_map_.erase_refactored(tablet_id, &tablet_table_info_mgr))) {
      LOG_WARN("failed to erase tablet table key mgr", K(ret), K(tablet_id));
    } else if (nullptr == tablet_table_info_mgr) {
      //do nothing
    } else {
      tablet_table_info_mgr->~ObStandbyRestoreTabletTableInfoMgr();
      ob_free(tablet_table_info_mgr);
      tablet_table_info_mgr = nullptr;
    }
  }
  return ret;
}

int ObStandbyRestoreTableInfoMgr::get_table_keys(
    const common::ObTabletID &tablet_id,
    common::ObIArray<ObITable::TableKey> &table_keys)
{
  int ret = OB_SUCCESS;
  table_keys.reset();
  ObStandbyRestoreTabletTableInfoMgr *tablet_table_info_mgr = nullptr;

  if (!is_inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("storage ha tablet info mgr do not init", K(ret));
  } else if (!tablet_id.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("get table keys get invalid argument", K(ret), K(tablet_id));
  } else {
    common::SpinRLockGuard guard(lock_);
    if (OB_FAIL(table_info_mgr_map_.get_refactored(tablet_id, tablet_table_info_mgr))) {
      LOG_WARN("failed to get tablet table info mgr", K(ret), K(tablet_id));
    } else if (OB_ISNULL(tablet_table_info_mgr)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("tablet table info mgr should not be NULL", K(ret), K(tablet_id), KP(tablet_table_info_mgr));
    } else if (OB_FAIL(tablet_table_info_mgr->get_table_keys(table_keys))) {
      LOG_WARN("failed to get table keys", K(ret), K(tablet_id));
    }
  }
  return ret;
}

int ObStandbyRestoreTableInfoMgr::init_tablet_info(
    const obcall::ObCopyTabletSSTableHeader &copy_header)
{
  int ret = OB_SUCCESS;
  ObStandbyRestoreTabletTableInfoMgr *tablet_table_info_mgr = nullptr;

  if (!is_inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("storage hs tablet info mgr do not init", K(ret));
  } else if (!copy_header.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("init tablet info get invalid argument", K(ret), K(copy_header));
  } else {
    common::SpinWLockGuard guard(lock_);
    int32_t hash_ret = table_info_mgr_map_.get_refactored(copy_header.tablet_id_, tablet_table_info_mgr);
    if (OB_HASH_NOT_EXIST != hash_ret) {
      ret = hash_ret == OB_SUCCESS ? OB_ERR_UNEXPECTED : hash_ret;
      LOG_WARN("tablet table info mgr already init", K(ret), K(copy_header));
    } else {
      void *buf = NULL;
      tablet_table_info_mgr = nullptr;

      if (FALSE_IT(buf = ob_malloc(sizeof(ObStandbyRestoreTabletTableInfoMgr), "StandbyTablet"))) {
      } else if (OB_ISNULL(buf)) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_WARN("failed to alloc memory", K(ret), KP(buf));
      } else if (FALSE_IT(tablet_table_info_mgr = new (buf) ObStandbyRestoreTabletTableInfoMgr())) {
      } else if (OB_FAIL(tablet_table_info_mgr->init(copy_header.tablet_id_, copy_header.status_, copy_header.tablet_meta_))) {
        LOG_WARN("failed to init tablet table key mgr", K(ret), K(copy_header));
      } else if (OB_FAIL(table_info_mgr_map_.set_refactored(copy_header.tablet_id_, tablet_table_info_mgr))) {
        LOG_WARN("failed to set tablet table key mgr into map", K(ret), K(copy_header));
      }

      if (OB_FAIL(ret)) {
        if (OB_NOT_NULL(tablet_table_info_mgr)) {
          tablet_table_info_mgr->~ObStandbyRestoreTabletTableInfoMgr();
          ob_free(tablet_table_info_mgr);
          tablet_table_info_mgr = nullptr;
        }
      }
    }
  }
  return ret;
}

int ObStandbyRestoreTableInfoMgr::check_copy_tablet_exist(
    const common::ObTabletID &tablet_id,
    bool &is_exist)
{
  int ret = OB_SUCCESS;
  is_exist = false;
  ObStandbyRestoreTabletTableInfoMgr *tablet_table_info_mgr = nullptr;

  if (!is_inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("storage ha tablet info mgr do not init", K(ret));
  } else if (!tablet_id.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("check copy tablet exist get invalid argument", K(ret), K(tablet_id));
  } else {
    common::SpinRLockGuard guard(lock_);
    if (OB_FAIL(table_info_mgr_map_.get_refactored(tablet_id, tablet_table_info_mgr))) {
      LOG_WARN("failed to get tablet table info mgr", K(ret), K(tablet_id));
    } else if (OB_FAIL(tablet_table_info_mgr->check_copy_tablet_exist(is_exist))) {
      LOG_WARN("failed to check copy tablet exist", K(ret), K(tablet_id));
    }
  }
  return ret;
}

int ObStandbyRestoreTableInfoMgr::check_tablet_table_info_exist(
    const common::ObTabletID &tablet_id, bool &is_exist)
{
  int ret = OB_SUCCESS;
  is_exist = false;
  ObStandbyRestoreTabletTableInfoMgr *tablet_table_info_mgr = nullptr;

  if (!is_inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("storage ha tablet info mgr do not init", K(ret));
  } else if (!tablet_id.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("check copy tablet exist get invalid argument", K(ret), K(tablet_id));
  } else {
    common::SpinRLockGuard guard(lock_);
    if (OB_FAIL(table_info_mgr_map_.get_refactored(tablet_id, tablet_table_info_mgr))) {
      if (OB_HASH_NOT_EXIST == ret) {
        is_exist = false;
        ret = OB_SUCCESS;
      } else {
        LOG_WARN("failed to get tablet table info mgr", K(ret), K(tablet_id));
      }
    } else if (OB_FAIL(tablet_table_info_mgr->check_copy_tablet_exist(is_exist))) {
      LOG_WARN("failed to check copy tablet exist", K(ret), K(tablet_id));
    }
  }
  return ret;
}

int ObStandbyRestoreTableInfoMgr::get_tablet_meta(
    const common::ObTabletID &tablet_id,
    const ObMigrationTabletParam *&tablet_meta)
{
  int ret = OB_SUCCESS;
  tablet_meta = nullptr;
  ObStandbyRestoreTabletTableInfoMgr *tablet_table_info_mgr = nullptr;

  if (!is_inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("storage ha tablet info mgr do not init", K(ret));
  } else if (!tablet_id.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("check copy tablet exist get invalid argument", K(ret), K(tablet_id));
  } else {
    common::SpinRLockGuard guard(lock_);
    if (OB_FAIL(table_info_mgr_map_.get_refactored(tablet_id, tablet_table_info_mgr))) {
      LOG_WARN("failed to get tablet table info mgr", K(ret), K(tablet_id));
    } else if (OB_FAIL(tablet_table_info_mgr->get_tablet_meta(tablet_meta))) {
      LOG_WARN("failed to get tablet meta", K(ret), K(tablet_id), KP(tablet_meta));
    }
  }
  return ret;
}

/******************ObStandbyRestoreCopySSTableParam*********************/
ObStandbyRestoreCopySSTableParam::ObStandbyRestoreCopySSTableParam()
  : copy_table_key_array_(),
    helper_(nullptr)
{
}


bool ObStandbyRestoreCopySSTableParam::is_valid() const
{
  return OB_NOT_NULL(helper_) && helper_->is_valid();
}

int ObStandbyRestoreCopySSTableParam::assign(const ObStandbyRestoreCopySSTableParam &param)
{
  int ret = OB_SUCCESS;
  if (!param.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("storage ha copy sstable param is not valid", K(ret), K(param));
  } else if (OB_FAIL(copy_table_key_array_.assign(param.copy_table_key_array_))) {
    LOG_WARN("failed to assign table key info array", K(ret), K(param));
  } else {
    helper_ = param.helper_;
  }
  return ret;
}

/******************ObStandbyRestoreCopySSTableInfoMgr*********************/
ObStandbyRestoreCopySSTableInfoMgr::ObStandbyRestoreCopySSTableInfoMgr()
  : is_inited_(false),
    param_(),
    allocator_("StandbySSTMgr"),
    macro_range_info_map_(),
    status_(ObCopyTabletStatus::TABLET_EXIST)
{
}

ObStandbyRestoreCopySSTableInfoMgr::~ObStandbyRestoreCopySSTableInfoMgr()
{
  if (!macro_range_info_map_.created()) {
  } else {
    for (CopySSTableMacroRangeInfoMap::iterator iter = macro_range_info_map_.begin();
        iter != macro_range_info_map_.end(); ++iter) {
      ObCopySSTableMacroRangeInfo *sstable_macro_range_info = iter->second;
      sstable_macro_range_info->~ObCopySSTableMacroRangeInfo();
      sstable_macro_range_info = nullptr;
    }
    macro_range_info_map_.reuse();
  }
  allocator_.reset();
}

int ObStandbyRestoreCopySSTableInfoMgr::init(const ObStandbyRestoreCopySSTableParam &param)
{
  int ret = OB_SUCCESS;
  const int64_t MAX_BUECKT_NUM = 128;
  int64_t bucket_num = 0;

  if (is_inited_) {
    ret = OB_INIT_TWICE;
    LOG_WARN("storage ha copy sstable info mgr init twice", K(ret));
  } else if (!param.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("init storage ha copy sstable info mgr get invalid argument", K(ret), K(param));
  } else if (OB_FAIL(param_.assign(param))) {
    LOG_WARN("failed to assign copy sstable info param", K(ret), K(param));
  } else if (FALSE_IT(bucket_num = std::max(MAX_BUECKT_NUM, param_.copy_table_key_array_.count()))) {
  } else if (OB_FAIL(macro_range_info_map_.create(bucket_num, "MacroRangeMap"))) {
    LOG_WARN("failed to create macro range info map", K(ret), K(param_));
  } else if (OB_FAIL(build_sstable_macro_range_info_map_())) {
    LOG_WARN("failed to build sstable macro range info map", K(ret), K(param_));
  } else {
    is_inited_ = true;
  }
  return ret;
}

int ObStandbyRestoreCopySSTableInfoMgr::build_sstable_macro_range_info_map_()
{
  int ret = OB_SUCCESS;
  if (!param_.is_valid()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("param should not be invalid", K(ret), K(param_));
  } else if (param_.copy_table_key_array_.empty()) {
    LOG_INFO("tablet do not has any sstable", K(ret), K(param_));
  } else {
    ObCopySSTableMacroRangeInfo sstable_macro_range_info;
    void *buf = nullptr;
    ObCopySSTableMacroRangeInfo *sstable_macro_range_info_ptr = nullptr;
    ObArenaAllocator allocator("CopySStable");
    restore::ObStandbyRestoreHelper *helper = nullptr;
    if (OB_FAIL(param_.helper_->copy_for_task(allocator, helper))) {
      LOG_WARN("failed to copy helper", K(ret), K(param_));
    } else if (OB_ISNULL(helper)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("helper should not be NULL", K(ret), K(param_));
    } else if (OB_FAIL(helper->init_for_sstable_macro_range(param_.copy_table_key_array_))) {
      LOG_WARN("failed to init helper for sstable macro range", K(ret), K(param_));
    }
    while (OB_SUCC(ret)) {
      sstable_macro_range_info.reset();
      buf = nullptr;
      sstable_macro_range_info_ptr = nullptr;
      if (OB_FAIL(helper->fetch_next_sstable_macro_range_info(sstable_macro_range_info))) {
        if (OB_ITER_END == ret) {
          ret = OB_SUCCESS;
          break;
        } else if (OB_TABLET_NOT_EXIST == ret) {
          LOG_INFO("src tablet do not exist", K(param_));
          status_ = ObCopyTabletStatus::TABLET_NOT_EXIST;
          ret = OB_SUCCESS;
          break;
        } else {
          LOG_WARN("failed to get next sstable range info", K(ret), K(param_));
        }
      } else if (FALSE_IT(buf = allocator_.alloc(sizeof(ObCopySSTableMacroRangeInfo)))) {
      } else if (OB_ISNULL(buf)) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_WARN("failed to alloc memory", K(ret), KP(buf));
      } else if (FALSE_IT(sstable_macro_range_info_ptr = new (buf) ObCopySSTableMacroRangeInfo())) {
      } else if (OB_FAIL(sstable_macro_range_info_ptr->assign(sstable_macro_range_info))) {
        LOG_WARN("failed to assign sstable macro range info", K(ret), K(param_));
      } else if (OB_FAIL(macro_range_info_map_.set_refactored(
          sstable_macro_range_info_ptr->copy_table_key_, sstable_macro_range_info_ptr))) {
        LOG_WARN("failed to set sstable macro range info into map", K(ret), K(param_));
      } else {
        sstable_macro_range_info_ptr = nullptr;
      }

      if (nullptr != sstable_macro_range_info_ptr) {
        sstable_macro_range_info_ptr->~ObCopySSTableMacroRangeInfo();
      }
    }
    if (OB_NOT_NULL(helper)) {
      helper->destroy();
      allocator.free(helper);
      helper = nullptr;
    }
  }
  return ret;
}

int ObStandbyRestoreCopySSTableInfoMgr::get_copy_sstable_maro_range_info(
    const ObITable::TableKey &copy_table_key,
    ObCopySSTableMacroRangeInfo &copy_sstable_macro_range_info)
{
  int ret = OB_SUCCESS;
  ObCopySSTableMacroRangeInfo *sstable_macro_range_info_ptr = nullptr;

  if (!is_inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("storage ha copy sstable info mgr do not init", K(ret));
  } else if (!copy_table_key.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("get copy sstable macro range info get invalid argument", K(ret), K(copy_table_key));
  } else if (OB_FAIL(macro_range_info_map_.get_refactored(copy_table_key, sstable_macro_range_info_ptr))) {
    LOG_WARN("failed to get macro range info map", K(ret), K(copy_table_key));
  } else if (OB_ISNULL(sstable_macro_range_info_ptr) || !sstable_macro_range_info_ptr->is_valid()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("sstable macro range info should not be NULL or invalid", K(ret), KPC(sstable_macro_range_info_ptr));
  } else if (OB_FAIL(copy_sstable_macro_range_info.assign(*sstable_macro_range_info_ptr))) {
    LOG_WARN("failed to copy sstable macro range info", K(ret), KPC(sstable_macro_range_info_ptr));
  } else {
    LOG_INFO("succeed get copy sstable macro range info", K(ret), K(copy_table_key), K(copy_sstable_macro_range_info));
  }
  return ret;
}

int ObStandbyRestoreCopySSTableInfoMgr::check_src_tablet_exist(bool &is_exist)
{
  int ret = OB_SUCCESS;
  is_exist = true;
  if (!is_inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("storage ha copy sstable info mgr do not init", K(ret));
  } else {
    is_exist = ObCopyTabletStatus::TABLET_EXIST == status_;
  }
  return ret;
}

/******************ObStandbyRestoreTabletBuilderUtil*********************/
int ObStandbyRestoreTabletBuilderUtil::get_tablet_(
    const common::ObTabletID &tablet_id,
    ObLS *ls,
    ObTabletHandle &tablet_handle)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(ls->get_tablet(tablet_id, tablet_handle))) {
    LOG_WARN("failed to get tablet", K(ret), K(tablet_id), KPC(ls));
  }
  return ret;
}

int ObStandbyRestoreTabletBuilderUtil::build_tablet_with_major_tables(
    ObLS *ls,
    const common::ObTabletID &tablet_id,
    const ObTablesHandleArray &major_tables,
    const ObBuildMajorSSTablesParam &major_sstables_param)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(NULL == ls || !tablet_id.is_valid() || !major_sstables_param.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("get invalid argument", K(ret), KP(ls), K(tablet_id), K(major_sstables_param));
  } else if (OB_FAIL(ObStandbyRestoreTabletBuilderUtil::build_tablet_for_row_store_(
      ls, tablet_id, major_tables, major_sstables_param))) {
    LOG_WARN("failed to build tablet with major tables", K(ret), K(tablet_id), KPC(ls));
  }
  return ret;
}

int ObStandbyRestoreTabletBuilderUtil::build_tablet_for_row_store_(
    ObLS *ls,
    const common::ObTabletID &tablet_id,
    const ObTablesHandleArray &major_tables,
    const ObBuildMajorSSTablesParam &major_sstables_param)
{
  int ret = OB_SUCCESS;
  ObTabletHandle tablet_handle;
  ObTablet *tablet = nullptr;
  ObSEArray<ObITable *, MAX_SSTABLE_CNT_IN_STORAGE> major_table_array;
  int64_t multi_version_start = 0;

  if (OB_ISNULL(ls) || !tablet_id.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("build tablet with major tables get invalid argument", K(ret), KP(ls), K(tablet_id));
  } else if (OB_FAIL(get_tablet_(tablet_id, ls, tablet_handle))) {
    LOG_WARN("failed to get tablet", K(ret), K(tablet_id), KPC(ls));
  } else if (FALSE_IT(tablet = tablet_handle.get_obj())) {
  } else if (OB_FAIL(calc_multi_version_start_with_major_(major_tables, tablet, multi_version_start))) {
    LOG_WARN("failed to calc multi version start with major", K(ret), KPC(tablet));
  } else if (OB_FAIL(major_tables.get_tables(major_table_array))) {
    LOG_WARN("failed to get tables", K(ret));
  } else if (OB_FAIL(ObTableStoreUtil::sort_major_tables(major_table_array))) {
    LOG_WARN("failed to sort mjaor tables", K(ret));
  } else {
    ObTableHandleV2 major_table_handle;
    for (int64_t i = 0; OB_SUCC(ret) && i < major_table_array.count(); ++i) {
      major_table_handle.reset();
      ObITable *table_ptr = major_table_array.at(i);
      if (OB_ISNULL(table_ptr)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("table ptr should not be null", K(ret), KP(table_ptr));
      } else if (!table_ptr->is_major_sstable()) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("table ptr is not major", K(ret), KPC(table_ptr));
      } else if (OB_FAIL(major_tables.get_table(table_ptr->get_key(), major_table_handle))) {
        LOG_WARN("fail to get table handle from array by table key", K(ret), KPC(table_ptr), K(major_tables));
      } else if (OB_FAIL(inner_update_tablet_table_store_with_major_(
                     multi_version_start, major_table_handle, ls, tablet, major_sstables_param))) {
        LOG_WARN("failed to update tablet table store", K(ret), K(tablet_id), KPC(table_ptr));
      }
    }
  }
  return ret;
}

int ObStandbyRestoreTabletBuilderUtil::calc_multi_version_start_with_major_(
    const ObTablesHandleArray &major_tables,
    ObTablet *tablet,
    int64_t &multi_version_start)
{
  int ret = OB_SUCCESS;
  multi_version_start = 0;
  int64_t tmp_multi_version_start = INT64_MAX;
  ObTabletMemberWrapper<ObTabletTableStore> table_store_wrapper;
  if (OB_ISNULL(tablet)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("calc multi version start with major get invalid argument", K(ret), KP(tablet));
  } else if (OB_FAIL(tablet->fetch_table_store(table_store_wrapper))) {
    LOG_WARN("fail to fetch table store", K(ret));
  } else {
    const ObSSTableArray &local_major_tables = table_store_wrapper.get_member()->get_major_sstables();
    for (int64_t i = 0; OB_SUCC(ret) && i < local_major_tables.count(); ++i) {
      const ObITable *table = local_major_tables.at(i);
      if (OB_ISNULL(table)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("table should not be NULL", K(ret), KP(table), KPC(tablet));
      } else {
        tmp_multi_version_start = std::min(tmp_multi_version_start, table->get_snapshot_version());
      }
    }

    for (int64_t i = 0; OB_SUCC(ret) && i < major_tables.get_count(); ++i) {
      const ObITable *table = major_tables.get_table(i);
      if (OB_ISNULL(table)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("table should not be NULL", K(ret), KP(table), KPC(tablet));
      } else {
        tmp_multi_version_start = std::min(tmp_multi_version_start, table->get_snapshot_version());
      }
    }

    if (OB_SUCC(ret)) {
      if (INT64_MAX == tmp_multi_version_start) {
        //do nothing
      } else {
        multi_version_start = tmp_multi_version_start;
      }
    }
  }
  return ret;
}

int ObStandbyRestoreTabletBuilderUtil::inner_update_tablet_table_store_with_major_(
    const int64_t multi_version_start,
    const ObTableHandleV2 &table_handle,
    ObLS *ls,
    ObTablet *tablet,
    const ObBuildMajorSSTablesParam &major_sstables_param)
{
  int ret = OB_SUCCESS;
  ObTabletHandle tablet_handle;
  ObArenaAllocator allocator;
  ObStorageSchema *tablet_storage_schema = nullptr;
  if (multi_version_start < 0 || OB_ISNULL(tablet) || OB_ISNULL(ls) || !table_handle.is_valid()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("table ptr should not be null", K(ret), K(multi_version_start), KP(tablet), K(table_handle), KP(ls));
  } else if (OB_FAIL(tablet->load_storage_schema(allocator, tablet_storage_schema))) {
    LOG_WARN("fail to load storage schema failed", K(ret));
  } else {
    const ObTabletID &tablet_id = tablet->get_tablet_meta().tablet_id_;
    const ObITable *table = table_handle.get_table();
    const int64_t update_snapshot_version = table->get_key().get_snapshot_version();
    const int64_t update_multi_version_start = multi_version_start;
    ObUpdateTableStoreParam param(
                            update_snapshot_version,
                            update_multi_version_start,
                            &major_sstables_param.storage_schema_,
                            static_cast<const blocksstable::ObSSTable *>(table),
                            true/*allow_duplicate_sstable*/);
    if (OB_FAIL(param.init_with_compaction_info(
            ObCompactionTableStoreParam(
              compaction::ObMergeType::MEDIUM_MERGE/*merge_type*/,
              SCN::min_scn()/*clog_checkpoint_scn*/,
              true/*need_report*/,
              major_sstables_param.has_truncate_info_)))) {
      LOG_WARN("failed to init with compaction info", KR(ret));
    } else if (tablet_storage_schema->get_schema_version() < major_sstables_param.storage_schema_.get_schema_version()) {
      SERVER_EVENT_ADD("standby_restore", "schema_change_need_merge_tablet_meta",
          "tenant_id", OB_SERVER_RUNTIME_ID,
          "tablet_id", tablet_id.id(),
          "old_schema_version", tablet_storage_schema->get_schema_version(),
          "new_schema_version", major_sstables_param.storage_schema_.get_schema_version());
    }
#ifdef ERRSIM
    SERVER_EVENT_ADD("standby_restore", "update_major_tablet_table_store",
        "tablet_id", tablet_id.id(),
        "old_multi_version_start", tablet->get_multi_version_start(),
        "new_multi_version_start", update_multi_version_start,
        "old_snapshot_version", tablet->get_snapshot_version(),
        "new_snapshot_version", table->get_key().get_snapshot_version(),
        "has_truncate_info", major_sstables_param.has_truncate_info_);
#endif


    if (FAILEDx(ls->update_tablet_table_store(tablet_id, param, tablet_handle))) {
      LOG_WARN("failed to build ha tablet new table store", K(ret), KPC(tablet), K(param));
    } else {
      LOG_INFO("succeed to build ha tablet new table store", K(ret), KPC(tablet), K(param), K(tablet_id));
    }
  }
  ObTabletObjLoadHelper::free(allocator, tablet_storage_schema);
  return ret;
}

int ObStandbyRestoreTabletBuilderUtil::build_table_with_minor_tables(
    const BatchBuildMinorSSTablesParam &param)
{
  int ret = OB_SUCCESS;
  ObTabletHandle tablet_handle;
  ObTablet *tablet = nullptr;
  ObTablesHandleArray sstables;
  const bool is_replace_remote = ObTabletRestoreAction::is_restore_replace_remote_sstable(param.restore_action_);
  bool need_tablet_meta_merge = true;
  // When we want to place the minor tables on the source side in the local table store,
  // whatever from backup or other observer, tablet meta merge action is necessary,
  // except for the following one cases.
  if (is_replace_remote) {
    // Tablet meta merge happened when restore remote sstable, no need for this time.
    need_tablet_meta_merge = false;
  }

  if (!param.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("build tablet with major tables get invalid argument", K(ret), K(param));
  } else if (ObTabletRestoreAction::is_restore_major(param.restore_action_)) {
    //do nothing
  } else {
    if (OB_FAIL(append_sstable_array_(sstables, param.mds_tables_))) {
      LOG_WARN("failed to append mds tables handle into array", K(ret), K(param));
    } else if (OB_FAIL(append_sstable_array_(sstables, param.minor_tables_))) {
      LOG_WARN("failed to append minor tables handle into array", K(ret), K(param));
    } else if (OB_FAIL(append_sstable_array_(sstables, param.ddl_tables_))) {
      LOG_WARN("failed to append ddl tables handle", K(ret), K(param));
    }

    if (OB_FAIL(ret)) {
    } else if (OB_FAIL(get_tablet_(param.tablet_id_, param.ls_, tablet_handle))) {
      LOG_WARN("failed to get tablet", K(ret), K(param));
    } else if (FALSE_IT(tablet = tablet_handle.get_obj())) {
    } else if (OB_FAIL(inner_update_tablet_table_store_with_minor_(param, tablet, need_tablet_meta_merge,
        sstables, is_replace_remote))) {
      LOG_WARN("failed to update tablet table store with minor", K(ret));
    }
  }
  return ret;
}

int ObStandbyRestoreTabletBuilderUtil::inner_update_tablet_table_store_with_minor_(
    const BatchBuildMinorSSTablesParam &param,
    ObTablet *tablet,
    const bool &need_tablet_meta_merge,
    const ObTablesHandleArray &tables_handle,
    const bool is_replace_remote)
{
  int ret = OB_SUCCESS;
  UNUSEDx(need_tablet_meta_merge, is_replace_remote);

  if (!param.is_valid() || OB_ISNULL(tablet)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("inner update tablet table store with minor get invalid argument", K(ret), K(param), KP(tablet));
  } else {
    const ObTabletID &tablet_id = tablet->get_tablet_meta().tablet_id_;
    auto install_copied_sstable = [&](const ObITable *table) -> int {
      int install_ret = OB_SUCCESS;
      ObTabletHandle current_tablet_handle;
      const blocksstable::ObSSTable *sstable = nullptr;
      compaction::ObMergeType merge_type = compaction::MINI_MERGE;
      if (OB_ISNULL(table) || !table->is_sstable()) {
        install_ret = OB_ERR_UNEXPECTED;
        LOG_WARN("invalid copied table", K(install_ret), K(tables_handle));
      } else if (table->is_mds_sstable()) {
        merge_type = compaction::MDS_MINI_MERGE;
      }
      if (OB_SUCCESS != install_ret) {
      } else if (FALSE_IT(sstable = static_cast<const blocksstable::ObSSTable *>(table))) {
      } else if (OB_SUCCESS != (install_ret = param.ls_->get_tablet(tablet_id, current_tablet_handle))) {
        LOG_WARN("failed to reload tablet before installing copied sstable", K(install_ret), K(tablet_id));
      } else if (OB_ISNULL(current_tablet_handle.get_obj())) {
        install_ret = OB_ERR_UNEXPECTED;
        LOG_WARN("tablet is null before installing copied sstable", K(install_ret), K(tablet_id));
      } else {
        ObTablet *current_tablet = current_tablet_handle.get_obj();
        const ObStorageSchema *storage_schema = table->is_mds_sstable()
            ? ObMdsSchemaHelper::get_instance().get_storage_schema()
            : &param.src_tablet_meta_->storage_schema_;
        ObUpdateTableStoreParam update_param(
            current_tablet->get_snapshot_version(),
            current_tablet->get_multi_version_start(),
            storage_schema,
            sstable,
            true /* allow_duplicate_sstable */);
        const SCN checkpoint_scn = table->is_minor_sstable() || table->is_mds_sstable()
            ? table->get_end_scn() : SCN::min_scn();
        if (OB_SUCCESS != (install_ret = update_param.init_with_compaction_info(ObCompactionTableStoreParam(
                merge_type, checkpoint_scn, false /* need_report */, false /* has_truncate_info */)))) {
          LOG_WARN("failed to init copied sstable update", K(install_ret), KPC(table));
        } else {
          if (table->is_ddl_sstable()) {
            const ObTabletMeta &source_meta = param.src_tablet_meta_->tablet_meta_;
            update_param.ddl_info_.keep_old_ddl_sstable_ = true;
            update_param.ddl_info_.ddl_start_scn_ = source_meta.ddl_start_scn_;
            update_param.ddl_info_.ddl_snapshot_version_ = source_meta.ddl_snapshot_version_;
            update_param.ddl_info_.ddl_checkpoint_scn_ = source_meta.ddl_checkpoint_scn_;
            update_param.ddl_info_.ddl_execution_id_ = source_meta.ddl_execution_id_;
            update_param.ddl_info_.data_format_version_ = source_meta.ddl_data_format_version_;
            update_param.ddl_info_.ddl_commit_scn_ = source_meta.ddl_commit_scn_;
          }
          if (OB_SUCCESS != (install_ret = param.ls_->update_tablet_table_store(
                  tablet_id, update_param, current_tablet_handle))) {
            LOG_WARN("failed to install copied sstable", K(install_ret), K(tablet_id), KPC(table));
          }
        }
      }
      return install_ret;
    };

    for (int64_t i = 0; OB_SUCC(ret) && i < tables_handle.get_count(); ++i) {
      const ObITable *table = tables_handle.get_table(i);
      if (OB_ISNULL(table) || !table->is_sstable()
          || (!table->is_mds_sstable() && !table->is_minor_sstable() && !table->is_ddl_sstable())) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("invalid copied minor table set", K(ret), K(i), K(tables_handle));
      }
    }

    for (int64_t i = 0; OB_SUCC(ret) && i < tables_handle.get_count(); ++i) {
      const ObITable *table = tables_handle.get_table(i);
      if (table->is_mds_sstable() && OB_FAIL(install_copied_sstable(table))) {
        LOG_WARN("failed to install copied mds sstable", K(ret), K(i), KPC(table));
      }
    }

    // The tablet already carries the source's final clog checkpoint. Installing
    // minors from the tail keeps every intermediate table store readable while
    // the remaining contiguous prefix is filled in.
    for (int64_t i = tables_handle.get_count() - 1; OB_SUCC(ret) && i >= 0; --i) {
      const ObITable *table = tables_handle.get_table(i);
      if (table->is_minor_sstable() && OB_FAIL(install_copied_sstable(table))) {
        LOG_WARN("failed to install copied minor sstable", K(ret), K(i), KPC(table));
      }
    }

    for (int64_t i = 0; OB_SUCC(ret) && i < tables_handle.get_count(); ++i) {
      const ObITable *table = tables_handle.get_table(i);
      if (table->is_ddl_sstable() && OB_FAIL(install_copied_sstable(table))) {
        LOG_WARN("failed to install copied ddl sstable", K(ret), K(i), KPC(table));
      }
    }
  }
  return ret;
}

int ObStandbyRestoreTabletBuilderUtil::append_sstable_array_(
    ObTablesHandleArray &dest_array, const ObTablesHandleArray &src_array)
{
  int ret = OB_SUCCESS;
  ObTableHandleV2 table_handle;
  for (int64_t i = 0; OB_SUCC(ret) && i < src_array.get_count(); ++i) {
    table_handle.reset();
    if (OB_FAIL(src_array.get_table(i, table_handle))) {
      LOG_WARN("failed to get table", K(ret), K(i), K(src_array));
    } else if (OB_FAIL(dest_array.add_table(table_handle))) {
      LOG_WARN("failed to add table", K(ret), K(table_handle));
    }
  }
  return ret;
}

ObStandbyRestoreTabletBuilderUtil::BatchBuildMinorSSTablesParam::BatchBuildMinorSSTablesParam()
  : ls_(nullptr),
    tablet_id_(),
    src_tablet_meta_(nullptr),
    mds_tables_(),
    minor_tables_(),
    ddl_tables_(),
    restore_action_(ObTabletRestoreAction::MAX),
    release_mds_scn_()
{
}

bool ObStandbyRestoreTabletBuilderUtil::BatchBuildMinorSSTablesParam::is_valid() const
{
  return OB_NOT_NULL(ls_)
      && tablet_id_.is_valid()
      && OB_NOT_NULL(src_tablet_meta_)
      && src_tablet_meta_->is_valid()
      && ObTabletRestoreAction::is_valid(restore_action_)
      && release_mds_scn_.is_valid();
}


int ObStandbyRestoreTabletBuilderUtil::BatchBuildMinorSSTablesParam::assign_sstables(
    ObTablesHandleArray &mds_tables,
    ObTablesHandleArray &minor_tables,
    ObTablesHandleArray &ddl_tables)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(mds_tables_.assign(mds_tables))) {
    LOG_WARN("failed to assign mds tables", K(ret), K(mds_tables));
  } else if (OB_FAIL(minor_tables_.assign(minor_tables))) {
    LOG_WARN("failed to assign minor tables", K(ret), K(minor_tables));
  } else if (OB_FAIL(ddl_tables_.assign(ddl_tables))) {
    LOG_WARN("failed to assign ddl tables", K(ret), K(ddl_tables));
  }
  return ret;
}

}
}
