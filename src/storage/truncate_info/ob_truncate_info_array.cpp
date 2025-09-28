//Copyright (c) 2024 OceanBase
// OceanBase is licensed under Mulan PubL v2.
// You can use this software according to the terms and conditions of the Mulan PubL v2.
// You may obtain a copy of Mulan PubL v2 at:
//          http://license.coscl.org.cn/MulanPubL-2.0
// THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
// EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
// MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
// See the Mulan PubL v2 for more details.
#define USING_LOG_PREFIX STORAGE
#include "storage/truncate_info/ob_truncate_info_array.h"
#include "storage/tablet/ob_tablet_obj_load_helper.h"
#include "storage/multi_data_source/adapter_define/mds_dump_node.h"
#include "share/compaction/ob_compaction_info_param.h"
namespace oceanbase
{
using namespace share;
namespace storage
{
/*
 * ObTruncateInfoArray
 * */

ObTruncateInfoArray::ObTruncateInfoArray()
  : truncate_info_array_(),
    allocator_(nullptr),
    src_(TRUN_SRC_MAX),
    is_inited_(false)
{
}

ObTruncateInfoArray::~ObTruncateInfoArray()
{
  reset();
}

int ObTruncateInfoArray::init_for_first_creation(ObIAllocator &allocator)
{
  int ret = OB_SUCCESS;
  if (IS_INIT) {
    ret = OB_INIT_TWICE;
    LOG_WARN("init twice", K(ret));
  } else {
    allocator_ = &allocator;
    src_ = TRUN_SRC_MDS;
    is_inited_ = true;
  }
  return ret;
}

int ObTruncateInfoArray::init_with_kv_cache_array(
  ObIAllocator &allocator,
  const ObArrayWrap<ObTruncateInfo> &input_array)
{
  int ret = OB_SUCCESS;
  if (IS_INIT) {
    ret = OB_INIT_TWICE;
    LOG_WARN("init twice", K(ret));
  } else if (OB_UNLIKELY(input_array.empty())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument to init truncate info array", KR(ret), K(input_array));
  } else {
    allocator_ = &allocator;
    src_ = TRUN_SRC_KV_CACHE;
    is_inited_ = true;
    for (int64_t idx = 0; OB_SUCC(ret) && idx < input_array.count(); ++idx) {
      if (OB_FAIL(append_with_deep_copy(input_array.at(idx)))) {
        LOG_WARN("failed to append", KR(ret), K(idx), K(input_array.at(idx)));
      }
    }
    if (OB_FAIL(ret)) {
      reset();
    }
  }
  return ret;
}

int ObTruncateInfoArray::append_with_deep_copy(const ObTruncateInfo &truncate_info)
{
  int ret = OB_SUCCESS;
  ObTruncateInfo *info = nullptr;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("not inited", K(ret), K_(is_inited));
  } else if (OB_FAIL(ObTabletObjLoadHelper::alloc_and_new(*allocator_, info))) {
    LOG_WARN("failed to alloc and new", K(ret));
  } else if (OB_FAIL(info->assign(*allocator_, truncate_info))) {
    LOG_WARN("failed to copy truncate info", K(ret), K(truncate_info));
  } else {
    ret = inner_append_and_sort(*info);
  }

  if (OB_FAIL(ret)) {
    ObTabletObjLoadHelper::free(*allocator_, info);
  }

  return ret;
}

int ObTruncateInfoArray::append_ptr(ObTruncateInfo &truncate_info)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("not inited", K(ret), K_(is_inited));
  } else {
    ret = inner_append_and_sort(truncate_info);
  }
  return ret;
}

int ObTruncateInfoArray::inner_append_and_sort(ObTruncateInfo &info)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(truncate_info_array_.push_back(&info))) {
    LOG_WARN("failed to push back to array", K(ret));
  } else {
    lib::ob_sort(truncate_info_array_.begin(), truncate_info_array_.end(), compare);
  }
  return ret;
}


bool ObTruncateInfoArray::compare(
    const ObTruncateInfo *lhs,
    const ObTruncateInfo *rhs)
{
  bool bret = true;
  if (lhs->commit_version_ == rhs->commit_version_) {
    bret = lhs->key_ < rhs->key_;
  } else {
    bret = lhs->commit_version_ < rhs->commit_version_;
  }
  return bret;
}

void ObTruncateInfoArray::reset_list()
{
  if (OB_NOT_NULL(allocator_)) {
    for (int64_t idx = 0; idx < count(); ++idx) {
      ObTabletObjLoadHelper::free(*allocator_, truncate_info_array_.at(idx));
    }
    truncate_info_array_.reset();
    allocator_ = nullptr;
  }
}

void ObTruncateInfoArray::reset()
{
  reset_list();
  is_inited_ = false;
}







} // namespace storage
} // namespace oceanbase
