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
#include "storage/high_availability/ob_physical_copy_ctx.h"
#include "storage/high_availability/ob_restore_helper.h"

namespace oceanbase
{
namespace storage
{

/******************ObCopyTabletRecordExtraInfo*********************/
ObCopyTabletRecordExtraInfo::ObCopyTabletRecordExtraInfo()
  : cost_time_ms_(0),
    total_data_size_(0),
    write_data_size_(0),
    major_count_(0),
    macro_count_(0),
    major_macro_count_(0),
    reuse_macro_count_(0),
    max_reuse_mgr_size_(0),
    restore_action_(ObTabletRestoreAction::ACTION::RESTORE_NONE)
{ 
}

ObCopyTabletRecordExtraInfo::~ObCopyTabletRecordExtraInfo()
{
}

void ObCopyTabletRecordExtraInfo::reset()
{
  cost_time_ms_ = 0;
  total_data_size_ = 0;
  write_data_size_ = 0;
  major_count_ = 0;
  macro_count_ = 0;
  major_macro_count_ = 0;
  reuse_macro_count_ = 0;
  max_reuse_mgr_size_ = 0; 
  restore_action_ = ObTabletRestoreAction::ACTION::RESTORE_NONE;
}

int ObCopyTabletRecordExtraInfo::update_max_reuse_mgr_size(ObMacroBlockReuseMgr *reuse_mgr)
{
  int ret = OB_SUCCESS;
  int64_t count = 0;

  if (OB_ISNULL(reuse_mgr)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("update max reuse mgr size get invalid argument", K(ret), KP(reuse_mgr));
  } else if (OB_FAIL(reuse_mgr->count(count))) {
    LOG_WARN("failed to count reuse mgr", K(ret), KP(reuse_mgr));
  } else {
    max_reuse_mgr_size_ = MAX(count * reuse_mgr->get_item_size(), max_reuse_mgr_size_);
  }

  return ret;
} 

/******************ObPhysicalCopyCtx*********************/
ObPhysicalCopyCtx::ObPhysicalCopyCtx()
  : lock_(),
    tenant_id_(OB_INVALID_ID),
    ls_id_(),
    tablet_id_(),
    helper_(nullptr),
    ha_dag_(nullptr),
    sstable_index_builder_(nullptr),
    table_key_(),
    macro_block_reuse_mgr_(nullptr),
    total_macro_count_(0),
    reuse_macro_count_(0),
    extra_info_(nullptr),
    allocator_("PhyCopyCtx")
{
}

ObPhysicalCopyCtx::~ObPhysicalCopyCtx()
{
  destroy();
}

void ObPhysicalCopyCtx::destroy()
{
  if (OB_NOT_NULL(helper_)) {
    helper_->destroy();
    helper_ = nullptr;
  }
  allocator_.reset();
}

bool ObPhysicalCopyCtx::is_valid() const
{
  bool bool_ret = false;
  bool_ret = tenant_id_ != OB_INVALID_ID 
             && ls_id_.is_valid() 
             && tablet_id_.is_valid()
             && OB_NOT_NULL(helper_)
             && helper_->is_valid()
             && OB_NOT_NULL(ha_dag_)
             && OB_NOT_NULL(sstable_index_builder_) 
             && table_key_.is_valid() 
             && total_macro_count_ >= 0 
             && reuse_macro_count_ >= 0 
             && OB_NOT_NULL(extra_info_);
  return bool_ret;
}


}
}
