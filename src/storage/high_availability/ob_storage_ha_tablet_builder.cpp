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
#include "ob_storage_ha_tablet_builder.h"
#include "storage/high_availability/ob_restore_helper.h"
#include "observer/ob_server_event_history_table_operator.h"
#include "storage/high_availability/ob_storage_ha_utils.h"
#include "storage/ob_storage_schema_util.h"

namespace oceanbase
{
using namespace share;
namespace storage
{

/******************ObStorageHATabletTableInfoMgr*********************/
ObStorageHATableInfoMgr::ObStorageHATabletTableInfoMgr::ObStorageHATabletTableInfoMgr()
  : is_inited_(false),
    tablet_id_(),
    status_(ObCopyTabletStatus::MAX_STATUS),
    allocator_("HATableInfo", OB_MALLOC_NORMAL_BLOCK_SIZE, MTL_ID()),
    copy_table_info_array_(OB_MALLOC_NORMAL_BLOCK_SIZE, ModulePageAllocator(allocator_)),
    tablet_meta_()
{
}

ObStorageHATableInfoMgr::ObStorageHATabletTableInfoMgr::~ObStorageHATabletTableInfoMgr()
{
}

int ObStorageHATableInfoMgr::ObStorageHATabletTableInfoMgr::init(
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

int ObStorageHATableInfoMgr::ObStorageHATabletTableInfoMgr::get_copy_table_info(
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

int ObStorageHATableInfoMgr::ObStorageHATabletTableInfoMgr::add_copy_table_info(
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

int ObStorageHATableInfoMgr::ObStorageHATabletTableInfoMgr::get_table_keys(
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

int ObStorageHATableInfoMgr::ObStorageHATabletTableInfoMgr::check_copy_tablet_exist(bool &is_exist)
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

int ObStorageHATableInfoMgr::ObStorageHATabletTableInfoMgr::get_tablet_meta(const ObMigrationTabletParam *&tablet_meta)
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

/******************ObStorageHATableInfoMgr*********************/
ObStorageHATableInfoMgr::ObStorageHATableInfoMgr()
  : is_inited_(false),
    lock_(),
    table_info_mgr_map_()
{
}

ObStorageHATableInfoMgr::~ObStorageHATableInfoMgr()
{
  reuse();
}

int ObStorageHATableInfoMgr::init()
{
  int ret = OB_SUCCESS;
  if (is_inited_) {
    ret = OB_INIT_TWICE;
    LOG_WARN("storage ha table info mgr init twice", K(ret));
  } else if (OB_FAIL(table_info_mgr_map_.create(MAX_BUCEKT_NUM, "HATableInfoMgr"))) {
    LOG_WARN("failed to create tablet table key mgr", K(ret));
  } else {
    is_inited_ = true;
  }
  return ret;
}

int ObStorageHATableInfoMgr::get_table_info(
    const common::ObTabletID &tablet_id,
    const ObITable::TableKey &table_key,
    const blocksstable::ObMigrationSSTableParam *&copy_table_info)
{
  int ret = OB_SUCCESS;
  copy_table_info = nullptr;
  ObStorageHATabletTableInfoMgr *tablet_table_info_mgr = nullptr;

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

int ObStorageHATableInfoMgr::add_table_info(
    const common::ObTabletID &tablet_id,
    const obcall::ObCopyTabletSSTableInfo &sstable_info)
{
  int ret = OB_SUCCESS;
  ObStorageHATabletTableInfoMgr *tablet_table_info_mgr = nullptr;

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

void ObStorageHATableInfoMgr::reuse()
{
  common::SpinWLockGuard guard(lock_);
  if (!table_info_mgr_map_.created()) {
  } else {
    for (TabletTableInfoMgr::iterator iter = table_info_mgr_map_.begin(); iter != table_info_mgr_map_.end(); ++iter) {
      ObStorageHATabletTableInfoMgr *tablet_table_info_mgr = iter->second;
      tablet_table_info_mgr->~ObStorageHATabletTableInfoMgr();
      mtl_free(tablet_table_info_mgr);
      tablet_table_info_mgr = nullptr;
    }
    table_info_mgr_map_.reuse();
  }
}

int ObStorageHATableInfoMgr::remove_tablet_table_info(const common::ObTabletID &tablet_id)
{
  int ret = OB_SUCCESS;
  ObStorageHATabletTableInfoMgr *tablet_table_info_mgr = nullptr;

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
      tablet_table_info_mgr->~ObStorageHATabletTableInfoMgr();
      mtl_free(tablet_table_info_mgr);
      tablet_table_info_mgr = nullptr;
    }
  }
  return ret;
}

int ObStorageHATableInfoMgr::get_table_keys(
    const common::ObTabletID &tablet_id,
    common::ObIArray<ObITable::TableKey> &table_keys)
{
  int ret = OB_SUCCESS;
  table_keys.reset();
  ObStorageHATabletTableInfoMgr *tablet_table_info_mgr = nullptr;

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

int ObStorageHATableInfoMgr::init_tablet_info(
    const obcall::ObCopyTabletSSTableHeader &copy_header)
{
  int ret = OB_SUCCESS;
  ObStorageHATabletTableInfoMgr *tablet_table_info_mgr = nullptr;

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

      if (FALSE_IT(buf = mtl_malloc(sizeof(ObStorageHATabletTableInfoMgr), "HATabletInfoMgr"))) {
      } else if (OB_ISNULL(buf)) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_WARN("failed to alloc memory", K(ret), KP(buf));
      } else if (FALSE_IT(tablet_table_info_mgr = new (buf) ObStorageHATabletTableInfoMgr())) {
      } else if (OB_FAIL(tablet_table_info_mgr->init(copy_header.tablet_id_, copy_header.status_, copy_header.tablet_meta_))) {
        LOG_WARN("failed to init tablet table key mgr", K(ret), K(copy_header));
      } else if (OB_FAIL(table_info_mgr_map_.set_refactored(copy_header.tablet_id_, tablet_table_info_mgr))) {
        LOG_WARN("failed to set tablet table key mgr into map", K(ret), K(copy_header));
      }

      if (OB_FAIL(ret)) {
        if (OB_NOT_NULL(tablet_table_info_mgr)) {
          tablet_table_info_mgr->~ObStorageHATabletTableInfoMgr();
          mtl_free(tablet_table_info_mgr);
          tablet_table_info_mgr = nullptr;
        }
      }
    }
  }
  return ret;
}

int ObStorageHATableInfoMgr::check_copy_tablet_exist(
    const common::ObTabletID &tablet_id,
    bool &is_exist)
{
  int ret = OB_SUCCESS;
  is_exist = false;
  ObStorageHATabletTableInfoMgr *tablet_table_info_mgr = nullptr;

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

int ObStorageHATableInfoMgr::check_tablet_table_info_exist(
    const common::ObTabletID &tablet_id, bool &is_exist)
{
  int ret = OB_SUCCESS;
  is_exist = false;
  ObStorageHATabletTableInfoMgr *tablet_table_info_mgr = nullptr;

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

int ObStorageHATableInfoMgr::get_tablet_meta(
    const common::ObTabletID &tablet_id,
    const ObMigrationTabletParam *&tablet_meta)
{
  int ret = OB_SUCCESS;
  tablet_meta = nullptr;
  ObStorageHATabletTableInfoMgr *tablet_table_info_mgr = nullptr;

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

/******************ObStorageHACopySSTableParam*********************/
ObStorageHACopySSTableParam::ObStorageHACopySSTableParam()
  : copy_table_key_array_(),
    helper_(nullptr)
{
}


bool ObStorageHACopySSTableParam::is_valid() const
{
  return OB_NOT_NULL(helper_) && helper_->is_valid();
}

int ObStorageHACopySSTableParam::assign(const ObStorageHACopySSTableParam &param)
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

/******************ObStorageHACopySSTableInfoMgr*********************/
ObStorageHACopySSTableInfoMgr::ObStorageHACopySSTableInfoMgr()
  : is_inited_(false),
    param_(),
    allocator_("HACopySSTMgr"),
    macro_range_info_map_(),
    status_(ObCopyTabletStatus::TABLET_EXIST)
{
}

ObStorageHACopySSTableInfoMgr::~ObStorageHACopySSTableInfoMgr()
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

int ObStorageHACopySSTableInfoMgr::init(const ObStorageHACopySSTableParam &param)
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

int ObStorageHACopySSTableInfoMgr::build_sstable_macro_range_info_map_()
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
    restore::ObIRestoreHelper *helper = nullptr;
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

int ObStorageHACopySSTableInfoMgr::get_copy_sstable_maro_range_info(
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

int ObStorageHACopySSTableInfoMgr::check_src_tablet_exist(bool &is_exist)
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

/******************ObStorageHATabletBuilderUtil*********************/
int ObStorageHATabletBuilderUtil::BuildTabletTableExtraParam::assign(const BuildTabletTableExtraParam &other)
{
  int ret = OB_SUCCESS;
  is_leader_restore_ = other.is_leader_restore_;
  table_key_ = other.table_key_;
  start_meta_macro_seq_ = other.start_meta_macro_seq_;

  return ret;
}

bool ObStorageHATabletBuilderUtil::BuildTabletTableExtraParam::is_valid() const
{
  return table_key_.is_valid();
}

void ObStorageHATabletBuilderUtil::BuildTabletTableExtraParam::reset()
{
  is_leader_restore_ = false;
  table_key_.reset();
  start_meta_macro_seq_ = 0;
}

int ObStorageHATabletBuilderUtil::BatchBuildTabletTablesExtraParam::get_extra_table_param(
    const ObITable::TableKey &table_key, 
    bool &is_exist,
    BuildTabletTableExtraParam &out_param) const
{
  int ret = OB_SUCCESS;
  int64_t i = 0;
  
  out_param.reset();
  is_exist = false;
  for (; i < param_array_.count(); i++) {
    const BuildTabletTableExtraParam &param = param_array_.at(i);
    if (param.table_key_ == table_key) {
      break;
    }
  }

  if (i == param_array_.count()) {
    is_exist = false;
  } else if (OB_FAIL(out_param.assign(param_array_[i]))) {
    LOG_WARN("failed to assign extra table param", K(ret));
  } else {
    is_exist = true;
  }

  return ret;
}



int ObStorageHATabletBuilderUtil::BatchBuildTabletTablesExtraParam::add_extra_param(
    const BuildTabletTableExtraParam &extra_param)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(param_array_.push_back(extra_param))) {
    LOG_WARN("failed to push back extra param", K(ret), K(extra_param));
  }
  return ret;
}


int ObStorageHATabletBuilderUtil::get_tablet_(
    const common::ObTabletID &tablet_id,
    ObLS *ls,
    ObTabletHandle &tablet_handle)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(ls->ha_get_tablet(tablet_id, tablet_handle))) {
    LOG_WARN("failed to get tablet", K(ret), K(tablet_id), KPC(ls));
  }
  return ret;
}

int ObStorageHATabletBuilderUtil::build_tablet_with_major_tables(
    ObLS *ls,
    const common::ObTabletID &tablet_id,
    const ObTablesHandleArray &major_tables,
    const ObBuildMajorSSTablesParam &major_sstables_param,
    const bool is_only_replace_major)
{
  int ret = OB_SUCCESS;
  BatchBuildTabletTablesExtraParam empty_extra_param;
  empty_extra_param.need_replace_remote_sstable_ = false;
  empty_extra_param.is_only_replace_major_ = is_only_replace_major;

  if (OB_FAIL(ObStorageHATabletBuilderUtil::build_tablet_with_major_tables(ls,
                                                                           tablet_id,
                                                                           major_tables,
                                                                           major_sstables_param,
                                                                           empty_extra_param))) {
    LOG_WARN("failed to build tablet with major tables", K(ret), KPC(ls), K(tablet_id), K(major_tables));
  }

  return ret;
}

/*   
 *    There may be hybrid type of major sstable in column store replica.
 *
 *    Time (evnet)       F replica         C Rreplica
 *    t1 (init)          MAJOR_V0 
 *    t2 (compaction)    MAJOR_V1
 *                       MAJOR_V0 
 *    t3 (migration)     MAJOR_V1          CO_MAJOR_V1
 *                       MAJOR_V0          MAJOR_V0 
 *    t4 (compaction)    MAJOR_V2          CO_MAJOR_V2
 *                       MAJOR_V1          CO_MAJOR_V1
 *                       MAJOR_V0          MAJOR_V0 
 *    t5 (compaction)    MAJOR_V3          replay slow, network partition..
 *                       MAJOR_V2          CO_MAJOR_V2
 *                       MAJOR_V1          CO_MAJOR_V1
 *                       MAJOR_V0          MAJOR_V0
 *
 *    t6 (ls rebuild)    MAJOR_V3          MAJOR_V3
 *                       MAJOR_V2          CO_MAJOR_V2
 *                       MAJOR_V1          CO_MAJOR_V1
 *                       MAJOR_V0          MAJOR_V0
 */
int ObStorageHATabletBuilderUtil::build_tablet_for_hybrid_store_(
    ObLS *ls,
    const common::ObTabletID &tablet_id,
    const ObTablesHandleArray &hybrid_major_tables,
    const ObBuildMajorSSTablesParam &major_sstables_param,
    const BatchBuildTabletTablesExtraParam &extra_param)
{
  // tablet with alter column group delayed with have major sstable in the front 
  int ret = OB_SUCCESS;
  ObTablesHandleArray row_store_major_tables;
  ObTablesHandleArray column_store_major_tables;
  row_store_major_tables.reset();
  column_store_major_tables.reset();
  int64_t table_idx = 0;
  ObTableHandleV2 table_handle;
  int64_t last_snapshot_version = 0;
  int64_t cur_snapshot_version = 0;
  for (; OB_SUCC(ret) && table_idx < hybrid_major_tables.get_count(); ++table_idx) {
    table_handle.reset();
    if (OB_FAIL(hybrid_major_tables.get_table(table_idx, table_handle))) {
      LOG_WARN("failed to get table", K(ret), K(table_idx), K(hybrid_major_tables));
    } else if (FALSE_IT(cur_snapshot_version = table_handle.get_table()->get_snapshot_version())) {
    } else if (cur_snapshot_version < last_snapshot_version) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("get snapshot version in reverse order", K(ret), K(last_snapshot_version), K(cur_snapshot_version));
    } else if (FALSE_IT(last_snapshot_version = cur_snapshot_version)) {
    } else if (!table_handle.get_table()->is_column_store_sstable()) { // row store
      if (!column_store_major_tables.empty()) {
        if (OB_FAIL(ObStorageHATabletBuilderUtil::build_tablet_for_column_store_(ls, tablet_id, column_store_major_tables, major_sstables_param, extra_param))) {
          LOG_WARN("failed to build tablet with co tables", K(ret), K(tablet_id), K(hybrid_major_tables), K(column_store_major_tables));
        } else {
          column_store_major_tables.reset();
        }
      }
      if (FAILEDx(row_store_major_tables.add_table(table_handle))) {
        LOG_WARN("failed to add row store major table", K(ret), K(table_handle));
      }
    } else { // column store
      if (!row_store_major_tables.empty()) {
        if (OB_FAIL(ObStorageHATabletBuilderUtil::build_tablet_for_row_store_(ls, tablet_id, row_store_major_tables, major_sstables_param, extra_param))) {
          LOG_WARN("failed to build tablet with co tables", K(ret), K(tablet_id), K(hybrid_major_tables), K(row_store_major_tables));
        } else {
          row_store_major_tables.reset();
        }
      }
      if (FAILEDx(column_store_major_tables.add_table(table_handle))) {
        LOG_WARN("failed to add row store major table", K(ret), K(table_handle));
      }
    } 
  }

  if (OB_FAIL(ret)) { 
  } else if (row_store_major_tables.empty() && column_store_major_tables.empty()) {
  } else if (!row_store_major_tables.empty() && !column_store_major_tables.empty()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("only one sstable array could have major tables", K(ret), K(row_store_major_tables), K(column_store_major_tables), K(hybrid_major_tables));
  } else if (!column_store_major_tables.empty()) {
    if (OB_FAIL(ObStorageHATabletBuilderUtil::build_tablet_for_column_store_(ls, tablet_id, column_store_major_tables, major_sstables_param, extra_param))) {
      LOG_WARN("failed to build tablet with co tables", K(ret), K(tablet_id), K(hybrid_major_tables), K(column_store_major_tables));
    }
  } else {
    if (OB_FAIL(ObStorageHATabletBuilderUtil::build_tablet_for_row_store_(ls, tablet_id, row_store_major_tables, major_sstables_param, extra_param))) {
      LOG_WARN("failed to build tablet with co tables", K(ret), K(tablet_id), K(hybrid_major_tables), K(row_store_major_tables));
    }
  }

  return ret;
}

int ObStorageHATabletBuilderUtil::build_tablet_with_major_tables(
    ObLS *ls,
    const common::ObTabletID &tablet_id,
    const ObTablesHandleArray &major_tables,
    const ObBuildMajorSSTablesParam &major_sstables_param,
    const BatchBuildTabletTablesExtraParam &extra_param)
{
  int ret = OB_SUCCESS;
  bool is_hybrid_store = false;

  if (OB_UNLIKELY(NULL == ls || !tablet_id.is_valid() || !major_sstables_param.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("get invalid argument", K(ret), KP(ls), K(tablet_id), K(major_sstables_param));
  } else if (major_sstables_param.storage_schema_.is_row_store()) {
    if (OB_FAIL(ObStorageHATabletBuilderUtil::build_tablet_for_row_store_(ls,
        tablet_id, major_tables, major_sstables_param, extra_param))) {
      LOG_WARN("failed to build tablet with major tables", K(ret), K(tablet_id), KPC(ls));
    }
  } else if (OB_FAIL(check_hybrid_store(major_sstables_param.storage_schema_, major_tables, is_hybrid_store))) {
    LOG_WARN("failed to check hybrid store", K(ret), K(major_sstables_param), K(major_tables));
  } else if (is_hybrid_store) {
    if (OB_FAIL(ObStorageHATabletBuilderUtil::build_tablet_for_hybrid_store_(ls, 
        tablet_id, major_tables, major_sstables_param, extra_param))) {
      LOG_WARN("failed to built tablet with hybrid tables", K(ret), K(tablet_id), KPC(ls));
    }
  } else if (OB_FAIL(ObStorageHATabletBuilderUtil::build_tablet_for_column_store_(ls,
        tablet_id, major_tables, major_sstables_param, extra_param))) {
    LOG_WARN("failed to build tablet with co tables", K(ret), K(tablet_id), KPC(ls));
  }
  return ret;
}

int ObStorageHATabletBuilderUtil::build_tablet_for_row_store_(
    ObLS *ls,
    const common::ObTabletID &tablet_id,
    const ObTablesHandleArray &major_tables,
    const ObBuildMajorSSTablesParam &major_sstables_param,
    const BatchBuildTabletTablesExtraParam &extra_batch_param)
{
  int ret = OB_SUCCESS;
  ObTabletHandle tablet_handle;
  ObTablet *tablet = nullptr;
  ObSEArray<ObITable *, MAX_SSTABLE_CNT_IN_STORAGE> major_table_array;
  int64_t multi_version_start = 0;
  int64_t transfer_seq = 0;

  BuildTabletTableExtraParam extra_param;
  bool exist_extra_param = false;

  if (OB_ISNULL(ls) || !tablet_id.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("build tablet with major tables get invalid argument", K(ret), KP(ls), K(tablet_id));
  } else if (OB_FAIL(get_tablet_(tablet_id, ls, tablet_handle))) {
    LOG_WARN("failed to get tablet", K(ret), K(tablet_id), KPC(ls));
  } else if (FALSE_IT(tablet = tablet_handle.get_obj())) {
  } else if (FALSE_IT(transfer_seq = 0)) {
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
      } else if (OB_FAIL(extra_batch_param.get_extra_table_param(table_ptr->get_key(), exist_extra_param, extra_param))) {
        LOG_WARN("fail to get extra table param", K(ret), K(extra_batch_param), KPC(table_ptr));
      } else if (OB_FAIL(inner_update_tablet_table_store_with_major_(multi_version_start, 
                                                                     major_table_handle,
                                                                     extra_batch_param,
                                                                     ls, 
                                                                     tablet, 
                                                                     major_sstables_param,
                                                                     transfer_seq,
                                                                     extra_param))) {
        LOG_WARN("failed to update tablet table store", K(ret), K(tablet_id), KPC(table_ptr));
      }
    }
  }
  return ret;
}

int ObStorageHATabletBuilderUtil::build_tablet_for_column_store_(
    ObLS *ls,
    const common::ObTabletID &tablet_id,
    const ObTablesHandleArray &major_tables,
    const ObBuildMajorSSTablesParam &major_sstables_param,
    const BatchBuildTabletTablesExtraParam &extra_param)
{
  int ret = OB_SUCCESS;
  ObTabletHandle tablet_handle;
  ObTablet *tablet = nullptr;
  ObTablesHandleArray co_tables;
  int64_t co_table_cnt = 0;
  int64_t multi_version_start = 0;

  if (OB_UNLIKELY(NULL == ls || !tablet_id.is_valid() || major_tables.empty())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("build tablet with major tables get invalid argument", K(ret), KP(ls), K(tablet_id), K(major_tables));
  } else if (OB_UNLIKELY(NULL == major_tables.get_table(0) || !major_tables.get_table(0)->is_column_store_sstable())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("get unexpected table type", K(ret), KPC(major_tables.get_table(0)));
  } else if (OB_FAIL(get_tablet_(tablet_id, ls, tablet_handle))) {
    LOG_WARN("failed to get tablet", K(ret), K(tablet_id), KPC(ls));
  } else if (FALSE_IT(tablet = tablet_handle.get_obj())) {
  } else if (OB_FAIL(calc_multi_version_start_with_major_(major_tables, tablet, multi_version_start))) {
    LOG_WARN("failed to calc multi version start with major", K(ret), KPC(tablet));
  } else if (OB_FAIL(assemble_column_oriented_sstable_(major_tables, co_tables))) {
    LOG_WARN("assemble co tables failed", K(ret), K(major_tables));
  } else if (OB_FAIL(build_tablet_with_co_tables_( //we should assemble flattened cg sstables when updating tablet due to allocator
      ls, tablet, major_sstables_param, multi_version_start, co_tables, extra_param))) {
    LOG_WARN("failed to build tablet with column store tables", K(ret));
  }
  return ret;
}

int ObStorageHATabletBuilderUtil::get_column_store_tables_(
    const ObTablesHandleArray &major_tables,
    ObSEArray<ObITable *, MAX_SSTABLE_CNT_IN_STORAGE> &column_store_tables,
    int64_t &co_table_cnt)
{
  int ret = OB_SUCCESS;
  column_store_tables.reset();
  co_table_cnt = 0;
  ObSEArray<ObITable *, MAX_SSTABLE_CNT_IN_STORAGE> cg_tables;

  ObITable *table = nullptr;
  int64_t full_co_table_cnt = 0; // just for defensive check
  for (int64_t i = 0; OB_SUCC(ret) && i < major_tables.get_count(); ++i) {
    table = major_tables.get_table(i);
    if (OB_UNLIKELY(NULL == table || !table->is_column_store_sstable())) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("get unpected table", K(ret), KPC(table));
    } else if (table->is_co_sstable()) {
      if (OB_FAIL(column_store_tables.push_back(table))) {
        LOG_WARN("failed to add co table", K(ret), KPC(table));
      } else if (static_cast<ObCOSSTableV2 *>(table)->is_inited()) {
        ++full_co_table_cnt;
      }
    } else if (OB_FAIL(cg_tables.push_back(table))) {
      LOG_WARN("failed to add cg table", K(ret), KPC(table));
    }
  }

  if (OB_FAIL(ret)) {
  } else if (column_store_tables.empty() || (full_co_table_cnt < column_store_tables.count() && cg_tables.empty())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("get unexpected table count", K(ret), K(full_co_table_cnt),
        K(column_store_tables.count()), K(cg_tables.count()), K(major_tables));
  } else if (FALSE_IT(co_table_cnt = column_store_tables.count())) {
  } else if (OB_FAIL(ObTableStoreUtil::sort_column_store_tables(column_store_tables))) {
    LOG_WARN("failed to sort co tables", K(ret));
  } else if (OB_FAIL(ObTableStoreUtil::sort_column_store_tables(cg_tables))) {
    LOG_WARN("failed to sort cg tables", K(ret));
  } else if (OB_FAIL(append(column_store_tables, cg_tables))) {
    LOG_WARN("failed to append cg tables", K(ret));
  }
  return ret;
}

int ObStorageHATabletBuilderUtil::assemble_column_oriented_sstable_(
    const ObTablesHandleArray &mixed_tables,
    ObTablesHandleArray &co_tables)
{
  int ret = OB_SUCCESS;
  co_tables.reset();
  ObSEArray<ObITable *, MAX_SSTABLE_CNT_IN_STORAGE> column_store_tables;
  int64_t co_table_cnt = 0;
  if (OB_FAIL(get_column_store_tables_(mixed_tables, column_store_tables, co_table_cnt))) {
    LOG_WARN("failed to get column store tables", K(ret));
  }

  ObSEArray<ObITable *, MAX_SSTABLE_CNT_IN_STORAGE> cur_cg_tables;
  int64_t start_cg_idx = co_table_cnt;

  // [CO_1, CO_N, CG_1_1, CG_1_2, ..., CG_N_1, CG_N_2]
  for (int64_t co_idx = 0; OB_SUCC(ret) && co_idx < co_table_cnt; ++co_idx) {
    ObCOSSTableV2 *co_sstable = static_cast<ObCOSSTableV2 *>(column_store_tables.at(co_idx));
    const int64_t co_snapshot_version = co_sstable->get_snapshot_version();
    cur_cg_tables.reset();

    if (co_sstable->is_inited()) {
      LOG_INFO("co sstable is inited", K(co_idx), K(co_table_cnt), K(start_cg_idx), KPC(co_sstable));
      // co sstable no need to fill cg tables
    } else {
      for (int64_t cg_idx = start_cg_idx; OB_SUCC(ret) && cg_idx < column_store_tables.count(); ++cg_idx) {
        ObITable *cg_table = column_store_tables.at(cg_idx);
        if (co_snapshot_version != cg_table->get_snapshot_version()) {
          if (cur_cg_tables.empty()) {
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("co table mismatch cg table!", K(ret), K(co_idx), K(co_table_cnt), K(start_cg_idx), K(cg_idx),
                K(co_snapshot_version), KPC(cg_table), K(column_store_tables));
          } else {
            start_cg_idx += cur_cg_tables.count();
          }
          break;
        } else if (OB_FAIL(cur_cg_tables.push_back(cg_table))) {
          LOG_WARN("failed to add cg table", K(ret), KPC(cg_table));
        }
      }
      if (OB_SUCC(ret)) {
        if (OB_FAIL(co_sstable->fill_cg_sstables(cur_cg_tables))) {
          LOG_WARN("failed to fill cg tables", K(ret), KPC(co_sstable));
        }
      }
    }

    ObTableHandleV2 co_table_handle;
    if (OB_FAIL(ret)) {
    } else if (OB_FAIL(mixed_tables.get_table(co_sstable->get_key(), co_table_handle))) {
      LOG_WARN("fail to get table handle from array by table key", K(ret), KPC(co_sstable), K(mixed_tables));
    } else if (OB_FAIL(co_tables.add_table(co_table_handle))) {
      LOG_WARN("failed to add table", K(ret), K(co_table_handle));
    }
  }
  return ret;
}

int ObStorageHATabletBuilderUtil::build_tablet_with_co_tables_(
    ObLS *ls,
    ObTablet *tablet,
    const ObBuildMajorSSTablesParam &major_sstables_param,
    const int64_t multi_version_start,
    const ObTablesHandleArray &co_tables,
    const BatchBuildTabletTablesExtraParam &extra_batch_param)
{
  int ret = OB_SUCCESS;
  int64_t transfer_seq = 0;

  BuildTabletTableExtraParam extra_param;
  bool exist_extra_param = false;

  for (int64_t co_idx = 0; OB_SUCC(ret) && co_idx < co_tables.get_count(); ++co_idx) {
    ObTableHandleV2 major_table_handle;
    if (OB_FAIL(co_tables.get_table(co_idx, major_table_handle))) {
      LOG_WARN("get co table handle failed", K(ret), K(co_idx));
    } else if (OB_FAIL(extra_batch_param.get_extra_table_param(major_table_handle.get_table()->get_key(), exist_extra_param, extra_param))) {
      LOG_WARN("fail to get extra table param", K(ret), K(extra_batch_param), "major_sstable", PC(major_table_handle.get_table()));
    } else if (OB_FAIL(inner_update_tablet_table_store_with_major_(multi_version_start,
                                                                   major_table_handle, 
                                                                   extra_batch_param,
                                                                   ls, 
                                                                   tablet, 
                                                                   major_sstables_param,
                                                                   transfer_seq,
                                                                   extra_param))) {
      LOG_WARN("failed to update tablet table store", K(ret), KPC(tablet), "major_sstable", PC(major_table_handle.get_table()), K(extra_param));
    }
  }
  return ret;
}

int ObStorageHATabletBuilderUtil::calc_multi_version_start_with_major_(
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

int ObStorageHATabletBuilderUtil::inner_update_tablet_table_store_with_major_(
    const int64_t multi_version_start,
    const ObTableHandleV2 &table_handle,
    const BatchBuildTabletTablesExtraParam &batch_extra_param,
    ObLS *ls,
    ObTablet *tablet,
    const ObBuildMajorSSTablesParam &major_sstables_param,
    const int64_t transfer_seq,
    const BuildTabletTableExtraParam &table_extra_param)
{
  int ret = OB_SUCCESS;
  ObTabletHandle tablet_handle;
  SCN tablet_snapshot_version;
  ObTenantMetaMemMgr *meta_mem_mgr = nullptr;
  ObArenaAllocator allocator;
  ObStorageSchema *tablet_storage_schema = nullptr;
  if (multi_version_start < 0 || OB_ISNULL(tablet) || OB_ISNULL(ls) || !table_handle.is_valid()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("table ptr should not be null", K(ret), K(multi_version_start), KP(tablet), K(table_handle), KP(ls));
  } else if (OB_ISNULL(meta_mem_mgr = MTL(ObTenantMetaMemMgr *))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("failed to get meta mem mgr from MTL", K(ret));
  } else if (OB_FAIL(tablet->get_snapshot_version(tablet_snapshot_version))) {
    LOG_WARN("failed to get_snapshot_version", K(ret));
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
                            ls->get_rebuild_seq(),
                            static_cast<const blocksstable::ObSSTable *>(table),
                            true/*allow_duplicate_sstable*/);
    if (OB_FAIL(param.init_with_ha_info(
            ObHATableStoreParam(batch_extra_param.need_replace_remote_sstable_,
                                batch_extra_param.is_only_replace_major_)))) {
      LOG_WARN("failed to init with ha info", KR(ret));
    } else if (OB_FAIL(param.init_with_compaction_info(
            ObCompactionTableStoreParam(
              compaction::ObMergeType::MEDIUM_MERGE/*merge_type*/,
              SCN::min_scn()/*clog_checkpoint_scn*/,
              true/*need_report*/,
              major_sstables_param.has_truncate_info_)))) {
      LOG_WARN("failed to init with compaction info", KR(ret));
    } else if (tablet_storage_schema->get_schema_version() < major_sstables_param.storage_schema_.get_schema_version()) {
      SERVER_EVENT_ADD("storage_ha", "schema_change_need_merge_tablet_meta",
                      "tenant_id", MTL_ID(),
                      "tablet_id", tablet_id.id(),
                      "old_schema_version", tablet_storage_schema->get_schema_version(),
                      "new_schema_version", major_sstables_param.storage_schema_.get_schema_version());
    }
#ifdef ERRSIM
    SERVER_EVENT_ADD("storage_ha", "update_major_tablet_table_store",
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

int ObStorageHATabletBuilderUtil::build_table_with_minor_tables(
    const BatchBuildMinorSSTablesParam &param)
{
  int ret = OB_SUCCESS;
  ObTabletHandle tablet_handle;
  ObTablet *tablet = nullptr;
  ObTablesHandleArray sstables;
  ObTablesHandleArray ddl_co_tables;

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
    } else if (!param.ddl_tables_.empty() && param.ddl_tables_.get_table(0)->is_column_store_sstable()) {
      if (OB_FAIL(assemble_column_oriented_sstable_(param.ddl_tables_, ddl_co_tables))) {
        LOG_WARN("assemble co tables failed", K(ret), K(param));
      } else if (OB_FAIL(append_sstable_array_(sstables, ddl_co_tables))) {
        LOG_WARN("failed to append ddl tables handle", K(ret), K(ddl_co_tables));
      }
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

int ObStorageHATabletBuilderUtil::inner_update_tablet_table_store_with_minor_(
    const BatchBuildMinorSSTablesParam &param,
    ObTablet *tablet,
    const bool &need_tablet_meta_merge,
    const ObTablesHandleArray &tables_handle,
    const bool is_replace_remote)
{
  int ret = OB_SUCCESS;
  ObBatchUpdateTableStoreParam update_table_store_param;

  if (!param.is_valid() || OB_ISNULL(tablet)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("inner update tablet table store with minor get invalid argument", K(ret), K(param), KP(tablet));
  } else {
    const ObTabletID &tablet_id = tablet->get_tablet_meta().tablet_id_;
    update_table_store_param.tablet_meta_ = need_tablet_meta_merge ? param.src_tablet_meta_ : nullptr;
    update_table_store_param.rebuild_seq_ = param.ls_->get_rebuild_seq();
    update_table_store_param.need_replace_remote_sstable_ = is_replace_remote;
    update_table_store_param.release_mds_scn_ = param.release_mds_scn_;

    if (OB_FAIL(update_table_store_param.tables_handle_.assign(tables_handle))) {
      LOG_WARN("failed to assign tables handle", K(ret), K(tables_handle));
    } else if (OB_FAIL(param.ls_->build_tablet_with_batch_tables(tablet_id, update_table_store_param))) {
      LOG_WARN("failed to build ha tablet new table store", K(ret), K(tablet_id), KPC(tablet), K(param), K(update_table_store_param));
    }
  }
  return ret;
}

int ObStorageHATabletBuilderUtil::check_remote_logical_sstable_exist(
    ObTablet *tablet,
    bool &is_exist)
{
  int ret = OB_SUCCESS;
  is_exist = false;
  ObTabletMemberWrapper<ObTabletTableStore> table_store_wrapper;

  if (OB_ISNULL(tablet)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("check remote logical sstable exist get invalid argument", K(ret), KP(tablet));
  } else if (OB_FAIL(tablet->fetch_table_store(table_store_wrapper))) {
    LOG_WARN("fail to fetch table store", K(ret));
  } else {
    const ObSSTableArray &minor_sstables = table_store_wrapper.get_member()->get_minor_sstables();
    for (int64_t i = 0; OB_SUCC(ret) && i < minor_sstables.count(); ++i) {
      const ObITable *table = minor_sstables.at(i);
      if (OB_ISNULL(table)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("minor sstable should not be NULL", K(ret), KP(table));
      } else if (table->is_remote_logical_minor_sstable()) {
        is_exist = true;
        break;
      }
    }
  }
  return ret;
}

int ObStorageHATabletBuilderUtil::append_sstable_array_(
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

int ObStorageHATabletBuilderUtil::check_hybrid_store(
    const ObStorageSchema &storage_schema,
    const ObTablesHandleArray &major_tables,
    bool &is_hybrid_store)
{
  int ret = OB_SUCCESS;
  is_hybrid_store  = false;
  if (storage_schema.is_row_store()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("storage schema is row store, should not check hybrid store", K(ret), K(storage_schema));
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < major_tables.get_count(); ++i) {
      const ObITable *table = major_tables.get_table(i);
      if (OB_ISNULL(table) || !table->is_major_sstable()) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("invalid null major table", K(ret), K(i), KPC(table), K(major_tables));
      } else if (!table->is_column_store_sstable()) {
        is_hybrid_store = true;
        break;
      }
    }
  }
  return ret;
}

ObStorageHATabletBuilderUtil::BatchBuildMinorSSTablesParam::BatchBuildMinorSSTablesParam()
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

bool ObStorageHATabletBuilderUtil::BatchBuildMinorSSTablesParam::is_valid() const
{
  return OB_NOT_NULL(ls_)
      && tablet_id_.is_valid()
      && OB_NOT_NULL(src_tablet_meta_) 
      && src_tablet_meta_->is_valid()
      && ObTabletRestoreAction::is_valid(restore_action_)
      && release_mds_scn_.is_valid();
}


int ObStorageHATabletBuilderUtil::BatchBuildMinorSSTablesParam::assign_sstables(
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

