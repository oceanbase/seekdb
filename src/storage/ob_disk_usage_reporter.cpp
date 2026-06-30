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

#include "storage/ob_disk_usage_reporter.h"
#include "observer/omt/ob_multi_tenant.h"  // previously hidden behind a transitive include
#include "share/rc/ob_module_provider.h"


#include "logservice/ob_log_service.h"
#include "storage/meta_store/ob_server_storage_meta_service.h"
#include "storage/meta_store/ob_tenant_storage_meta_service.h"
#include "logservice/ob_server_log_block_mgr.h"

namespace oceanbase
{

using namespace common;
using namespace share;
using namespace logservice;
using namespace tmp_file;
namespace storage
{

ObDiskUsageReportTask::ObDiskUsageReportTask()
    : is_inited_(false),
      sql_proxy_(NULL)
{
  // do nothing
}

int ObDiskUsageReportTask::init(ObMySQLProxy &sql_proxy)
{
  int ret = OB_SUCCESS;
  lib::ObMemAttr mem_attr("DiskReport");
  if (is_inited_) {
    ret = OB_INIT_TWICE;
    STORAGE_LOG(WARN, "init twice", K(ret));
  } else if (OB_FAIL(result_map_.create(OB_MAX_SERVER_TENANT_CNT * 5, SET_USE_500("OB_DISK_REP")))) {
    STORAGE_LOG(WARN, "Failed to create result_map_", K(ret));
  } else {
    sql_proxy_ = &sql_proxy;
    is_inited_ = true;
    STORAGE_LOG(INFO, "ObDistUsageReportTask init successful", K(ret));
  }

  if (IS_NOT_INIT) {
    destroy();
  }
  return ret;
}

void ObDiskUsageReportTask::destroy()
{
  result_map_.destroy();
  is_inited_ = false;
}

int ObDiskUsageReportTask::count_tenant_data()
{
  int ret = OB_SUCCESS;
  common::ObSArray<blocksstable::MacroBlockId> block_list;
  ObDiskUsageReportKey meta_key;
  ObDiskUsageReportKey data_key;
  ObDiskUsageReportKey major_data_key;
  ObDiskUsageReportKey local_data_key;
  ObDiskUsageReportKey quick_restore_remote_key;
  int64_t meta_size = 0;
  int64_t backup_size = 0;
  int64_t data_size = 0;
  int64_t occupy_size = 0;
  int64_t tablet_shared_occupy_size = 0;
  int64_t tablet_local_required_size = 0;

  if (OB_FAIL(share::g_mp->tenant_storage_meta_service()->get_meta_block_list(block_list))) {
    STORAGE_LOG(WARN, "failed to get tenant's meta block list", K(ret));
  } else {
    ObTenantMetaMemMgr *t3m = share::g_mp->tenant_meta_mem_mgr();
    ObLSService *ls_service = share::g_mp->ls_service();
    if (OB_ISNULL(ls_service) || OB_ISNULL(t3m) ) {
      ret = OB_ERR_UNEXPECTED;
      STORAGE_LOG(WARN, "unexpected error!!! ls_service and t3m must not nullptr", K(ret), KP(ls_service), KP(t3m));
    } else {
      ObTenantTabletPtrWithInMemObjIterator tablet_ptr_iter(*t3m);
      ObTabletPointerHandle pointer_handle;
      ObTabletHandle unused_tablet_handle;
      ObTabletMapKey tablet_map_key;
      const ObTabletPointer *tablet_pointer = nullptr;
      ObLSHandle ls_handle;
      common::ObRole ls_role;
      ObLS *ls = NULL;
      bool is_leader = false;

      while (OB_SUCC(ret) && OB_SUCC(tablet_ptr_iter.get_next_tablet_pointer(tablet_map_key, pointer_handle, unused_tablet_handle))) {
        if (OB_UNLIKELY(!pointer_handle.is_valid())) {
          ret = OB_ERR_UNEXPECTED;
          STORAGE_LOG(WARN, "unexpected invalid tablet", K(ret), K(pointer_handle));
        } else if (OB_NOT_NULL(ls) && ls->get_ls_id() == tablet_map_key.ls_id_) {
          // do not need get_ls again
        } else if (OB_FAIL(ls_service->get_ls(tablet_map_key.ls_id_, ls_handle, ObLSGetMod::STORAGE_MOD))) {
          STORAGE_LOG(WARN, "get_ls failed", K(tablet_map_key.ls_id_));
        } else if (OB_ISNULL(ls = ls_handle.get_ls())) {
          ret = OB_ERR_UNEXPECTED;
          STORAGE_LOG(WARN, "unexpected error!!! ls must not nullptr", K(tablet_map_key.ls_id_));
        } else if (OB_FAIL(ls->get_ls_role(ls_role))) {
          STORAGE_LOG(WARN, "fail to get ls_role", K(ret), KPC(ls));
        } 
        
        if (OB_FAIL(ret)) {
        } else if (OB_ISNULL(tablet_pointer = static_cast<const ObTabletPointer*>(pointer_handle.get_resource_ptr()))) {
          ret = OB_ERR_UNEXPECTED;
          STORAGE_LOG(WARN, "failed to cast ptr to ObTabletPointer*", K(ret), K(pointer_handle));
        } else {
          ObTabletResidentInfo tablet_info = tablet_pointer->get_tablet_resident_info(tablet_map_key);
          occupy_size += tablet_info.get_occupy_size();
          data_size += tablet_info.get_required_size();
          meta_size += tablet_info.get_tablet_meta_size();
          backup_size += tablet_info.get_backup_size();
          tablet_local_required_size += tablet_info.get_required_size() + tablet_info.get_tablet_meta_size();
        }
        pointer_handle.reset();
      }
      if (OB_ITER_END == ret || OB_SUCCESS == ret) {
        ret = OB_SUCCESS;
        meta_size += block_list.count() * OB_DEFAULT_MACRO_BLOCK_SIZE;
      }
    }
  }

  if (OB_SUCC(ret)) {
    
    meta_key.file_type_ = ObDiskReportFileType::TENANT_META_DATA;
    
    data_key.file_type_ = ObDiskReportFileType::TENANT_DATA;
    
    major_data_key.file_type_ = ObDiskReportFileType::TENANT_MAJOR_SSTABLE_DATA;
    
    local_data_key.file_type_ = ObDiskReportFileType::TENANT_MAJOR_LOCAL_DATA;
    
    quick_restore_remote_key.file_type_ = ObDiskReportFileType::TENANT_BACKUP_DATA;
    if (OB_FAIL(result_map_.set_refactored(meta_key, std::make_pair(meta_size, meta_size), 1 /* whether allowed to override */))) {
      STORAGE_LOG(WARN, "failed to insert meta info result_map_", K(ret), K(meta_key), K(meta_size));
    } else if (OB_FAIL(result_map_.set_refactored(data_key, std::make_pair(occupy_size, data_size), 1 /* whether allowed to override */))) {
      STORAGE_LOG(WARN, "failed to insert data info result_map_", K(ret), K(data_key), K(occupy_size), K(data_size));
    } else if (OB_FAIL(result_map_.set_refactored(local_data_key,std::make_pair(tablet_local_required_size, tablet_local_required_size), 1 /* whether allowed to override */))) {
      STORAGE_LOG(WARN, "failed to insert data info result_map_", K(ret), K(local_data_key), K(tablet_local_required_size));
    } else if (OB_FAIL(result_map_.set_refactored(quick_restore_remote_key, std::make_pair(backup_size, backup_size), 1 /* whether allowed to override */))) {
      STORAGE_LOG(WARN, "failed to insert backup_size info result_map_", K(ret), K(data_key), K(backup_size));
    }
  }
  return ret;
}

int ObDiskUsageReportTask::get_data_disk_used_size(int64_t &used_size)
{
  int ret = OB_SUCCESS;
  used_size = 0;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    STORAGE_LOG(WARN, "ObDiskUsageReportTask not inited", K(ret));
  } else {
    // compute disk usage on demand instead of relying on periodic timer
    MOD_SCOPE {
      if (OB_FAIL(count_tenant_data())) {
        STORAGE_LOG(WARN, "fail to count tenant data", K(ret));
      } else {
        ObDiskUsageReportKey tmp_key;
        tmp_key.file_type_ = ObDiskReportFileType::TENANT_TMP_DATA;
        
        ObTenantTmpFileManager *tmp_file_manager = share::g_mp->tenant_tmp_file_manager();
        int64_t tmp_occupy_size = 0;
        int64_t tmp_required_size = 0;
        if (OB_NOT_NULL(tmp_file_manager)
            && OB_FAIL(tmp_file_manager->get_tmp_file_disk_usage(tmp_required_size, tmp_occupy_size))) {
          STORAGE_LOG(WARN, "fail to get_tmp_file_disk_usage", K(ret));
        } else if (OB_NOT_NULL(tmp_file_manager)) {
          result_map_.set_refactored(tmp_key, std::make_pair(tmp_occupy_size, tmp_required_size), 1);
        }
      }
    }
    if (OB_SUCC(ret)) {
      // the file_type_ of which data is on data disk is needed
      const int need_cnt = 3;
      const ObDiskReportFileType file_types_need[need_cnt] = {
          ObDiskReportFileType::TENANT_DATA,
          ObDiskReportFileType::TENANT_META_DATA,
          ObDiskReportFileType::TENANT_TMP_DATA
      };
      ObDiskUsageReportKey key;
      
      std::pair<int64_t, int64_t> size = std::make_pair(0, 0);

      for (int64_t i = 0; i < need_cnt && OB_SUCC(ret); i++) {
        key.file_type_ = file_types_need[i];

        if (OB_FAIL(result_map_.get_refactored(key, size)) && OB_HASH_NOT_EXIST != ret) {
          STORAGE_LOG(WARN, "fail to get file type size", K(ret), K(key));
        } else if (OB_HASH_NOT_EXIST == ret) {
          ret = OB_SUCCESS;
        } else {
          used_size += size.second;
        }
      }
    }
  }

  return ret;
}


int ObDiskUsageReportTask::delete_tenant_usage_stat()
{
  int ret = OB_SUCCESS;
  ObDiskUsageReportKey key;
  
  int64_t file_type_num = static_cast<int64_t>(ObDiskReportFileType::TYPE_MAX);

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    STORAGE_LOG(WARN, "ObDiskUsageReportTask not inited", K(ret));
  }

  for (int64_t i = 0; i < file_type_num && OB_SUCC(ret); i++) {
    key.file_type_ = static_cast<ObDiskReportFileType>(i);
    if (OB_FAIL(result_map_.erase_refactored(key)) && OB_HASH_NOT_EXIST != ret) {
      STORAGE_LOG(WARN, "fail to erase", K(ret), K(key));
    } else if (OB_HASH_NOT_EXIST == ret) {
      ret = OB_SUCCESS;
    }
  }

  return ret;
}

} // namespace storage
} // namespace oceanbase

