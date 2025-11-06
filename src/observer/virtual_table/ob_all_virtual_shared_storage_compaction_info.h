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

#ifndef SRC_OBSERVER_VIRTUAL_TABLE_OB_ALL_VIRTUAL_SHARED_STORAGE_COMPACTION_INFO_H_
#define SRC_OBSERVER_VIRTUAL_TABLE_OB_ALL_VIRTUAL_SHARED_STORAGE_COMPACTION_INFO_H_

#include "share/ob_virtual_table_scanner_iterator.h"
#include "observer/omt/ob_multi_tenant_operator.h"
#ifdef OB_BUILD_SHARED_STORAGE
#include "share/compaction/ob_shared_storage_compaction_util.h"
#include "storage/compaction/ob_basic_compaction_obj_mgr.h"
#include "storage/compaction/ob_ls_compaction_obj_mgr.h"
#endif
namespace oceanbase
{
namespace observer
{
class ObAllVirtualSharedStorageCompactionInfo
    : public common::ObVirtualTableScannerIterator,
      public omt::ObMultiTenantOperator {
#ifndef OB_BUILD_SHARED_STORAGE
public:
  ObAllVirtualSharedStorageCompactionInfo() {}
  virtual ~ObAllVirtualSharedStorageCompactionInfo() {}
  int init(ObIAllocator *allocator, common::ObAddr addr)
  { return OB_SUCCESS; }
  int inner_get_next_row(common::ObNewRow *&row) { return OB_ITER_END; }
  int process_curr_tenant(common::ObNewRow *&row) { return OB_ITER_END; }
  virtual void release_last_tenant() override {}
#else
  enum COLUMN_ID_LIST
  {
    SVR_IP = common::OB_APP_MIN_COLUMN_ID,
    SVR_PORT,
    TENANT_ID,
    LS_ID,
    TABLET_ID,
    OBJ_TYPE,
    LAST_REFRESH_TIME,
    INFO,
  };
public:
  ObAllVirtualSharedStorageCompactionInfo();
  virtual ~ObAllVirtualSharedStorageCompactionInfo();
  int init(ObIAllocator *allocator, common::ObAddr addr);
  virtual int inner_get_next_row(common::ObNewRow *&row) override;
  virtual void reset() override;
  virtual int process_curr_tenant(common::ObNewRow *&row) override;
  virtual void release_last_tenant() override;
private:
  enum LoopState : uint8_t {
    COMPACTION_SVRS = 0,
    LS_COMPACTION_STATUS,
    LS_SVR_COMPACTION_STATUS,
    LOOP_SVR_TABLET_STATUS,
    COMPACTION_REPORT,
    LS_COMPACTION_TALBET_LIST,
    LOOP_TABLET_LIST_STATUS,
    STATE_MAX,
  };
  static const int64_t LS_ID_ARRAY_CNT = 10;
  static const int64_t TABLET_ID_ARRAY_CNT = 2000;
  int prepare_ls_ids();
  int prepare_tablet_ids();
  int get_info(compaction::ObVirtualTableInfo &info);
  int fill_tablet_info(compaction::ObVirtualTableInfo &info);
  int fill_compaction_report(compaction::ObVirtualTableInfo &info);
  int fill_ls_compaction_list_info(compaction::ObVirtualTableInfo &info);
  common::ObAddr addr_;
  char ip_buf_[common::OB_IP_STR_BUFF];
  common::ObSEArray<share::ObLSID, LS_ID_ARRAY_CNT> ls_ids_;
  common::ObSEArray<ObTabletID, TABLET_ID_ARRAY_CNT> tablet_ids_;
  compaction::ObBasicObjHandle<compaction::ObLSObj> obj_handle_;
  int64_t ls_idx_;
  int64_t tablet_idx_;
  LoopState loop_state_;
  compaction::ObVirtualTableInfo info_;
  ObIAllocator *allocator_;
  bool is_inited_;
#endif
private:
  DISALLOW_COPY_AND_ASSIGN(ObAllVirtualSharedStorageCompactionInfo);
};
}
}
#endif /* SRC_OBSERVER_VIRTUAL_TABLE_OB_ALL_VIRTUAL_SHARED_STORAGE_COMPACTION_INFO_H_ */
