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
#include "share/rc/ob_server_runtime.h"


#include "logservice/ob_log_service.h"
#include "storage/tx_storage/ob_ls_service.h"
#include "storage/meta_store/ob_server_storage_meta_service.h"
#include "storage/meta_store/ob_local_storage_meta_service.h"
#include "storage/tmp_file/ob_tmp_file_manager.h"
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
  } else if (OB_FAIL(result_map_.create(
      static_cast<int64_t>(ObDiskReportFileType::TYPE_MAX) * 5, lib::ObMemAttr("OB_DISK_REP")))) {
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

int ObDiskUsageReportTask::count_server_data()
{
  int ret = OB_SUCCESS;
  common::ObSArray<blocksstable::MacroBlockId> block_list;
  ObDiskUsageReportKey meta_key;
  ObDiskUsageReportKey data_key;
  int64_t meta_size = 0;
  int64_t data_size = 0;
  int64_t occupy_size = 0;

  if (OB_FAIL(::oceanbase::share::server_service<::oceanbase::storage::ObLocalStorageMetaService>()->get_meta_block_list(block_list))) {
    STORAGE_LOG(WARN, "failed to get the server meta block list", K(ret));
  } else {
    ObStorageMetaMemMgr *t3m = ::oceanbase::share::server_service<::oceanbase::storage::ObStorageMetaMemMgr>();
    ObLSService *ls_service = ::oceanbase::share::server_service<::oceanbase::storage::ObLSService>();
    if (OB_ISNULL(ls_service) || OB_ISNULL(t3m) ) {
      ret = OB_ERR_UNEXPECTED;
      STORAGE_LOG(WARN, "tenant meta memory manager must not be null", K(ret), KP(t3m));
    } else {
      ObTabletPtrWithInMemObjIterator tablet_ptr_iter(*t3m);
      ObTabletPointerHandle pointer_handle;
      ObTabletHandle unused_tablet_handle;
      ObTabletMapKey tablet_map_key;
      const ObTabletPointer *tablet_pointer = nullptr;
      while (OB_SUCC(ret) && OB_SUCC(tablet_ptr_iter.get_next_tablet_pointer(tablet_map_key, pointer_handle, unused_tablet_handle))) {
        if (OB_UNLIKELY(!pointer_handle.is_valid())) {
          ret = OB_ERR_UNEXPECTED;
          STORAGE_LOG(WARN, "unexpected invalid tablet", K(ret), K(pointer_handle));
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
    
    meta_key.file_type_ = ObDiskReportFileType::LOCAL_STORAGE_META_DATA;
    
    data_key.file_type_ = ObDiskReportFileType::SERVER_DATA;
    
    if (OB_FAIL(result_map_.set_refactored(meta_key, std::make_pair(meta_size, meta_size), 1 /* whether allowed to override */))) {
      STORAGE_LOG(WARN, "failed to insert meta info result_map_", K(ret), K(meta_key), K(meta_size));
    } else if (OB_FAIL(result_map_.set_refactored(data_key, std::make_pair(occupy_size, data_size), 1 /* whether allowed to override */))) {
      STORAGE_LOG(WARN, "failed to insert data info result_map_", K(ret), K(data_key), K(occupy_size), K(data_size));
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
    SERVER_MODULE_SCOPE {
      if (OB_FAIL(count_server_data())) {
        STORAGE_LOG(WARN, "failed to count server data", K(ret));
      } else {
        ObDiskUsageReportKey tmp_key;
        tmp_key.file_type_ = ObDiskReportFileType::SERVER_TMP_DATA;
        
        ObTmpFileManager *tmp_file_manager = ::oceanbase::share::server_service<::oceanbase::tmp_file::ObTmpFileManager>();
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
          ObDiskReportFileType::SERVER_DATA,
          ObDiskReportFileType::LOCAL_STORAGE_META_DATA,
          ObDiskReportFileType::SERVER_TMP_DATA
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


int ObDiskUsageReportTask::delete_usage_stat()
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
