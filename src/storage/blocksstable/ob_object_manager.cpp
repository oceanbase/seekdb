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

#include "share/ob_io_device_helper.h"
#include "ob_object_manager.h"
#include "storage/meta_store/ob_local_storage_meta_service.h"

namespace oceanbase
{
namespace blocksstable
{
// ============================ ObStorageObjectOpt ======================================//

int64_t ObStorageObjectOpt::to_string(char *buf, const int64_t buf_len) const
{
  int ret = OB_SUCCESS;
  int64_t pos = 0;
  const char *type_name = "UNKNOWN";
  switch (object_type_) {
  case ObStorageObjectType::DATA_MACRO:
    type_name = "DATA_MACRO";
    break;
  case ObStorageObjectType::META_MACRO:
    type_name = "META_MACRO";
    break;
  default:
    break;
  }
  if (OB_FAIL(databuff_printf(buf, buf_len, pos, "object_type=%s", type_name))) {
  }
  return pos;
}

//================================ ObObjectManager =====================================//

ObObjectManager &ObObjectManager::get_instance()
{
  static ObObjectManager instance_;
  return instance_;
}

ObObjectManager::ObObjectManager()
  : is_inited_(false),
    macro_object_size_(0),
    resize_file_lock_()
{
}

ObObjectManager::~ObObjectManager()
{
}

int ObObjectManager::init(const int64_t macro_object_size)
{
  int ret = OB_SERVER_BLOCK_MGR.init(&LOCAL_DEVICE_INSTANCE, macro_object_size);
  if (OB_SUCC(ret)) {
    macro_object_size_ = macro_object_size;
    is_inited_ = true;
    LOG_INFO("succeed to init object mgr", K(macro_object_size_));
  }
  return ret;
}

int ObObjectManager::start(const int64_t reserved_size)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
  } else if (OB_UNLIKELY(reserved_size < 0)) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    ret = OB_SERVER_BLOCK_MGR.start(reserved_size);
  }
  return ret;
}

void ObObjectManager::stop()
{
  OB_SERVER_BLOCK_MGR.stop();
}

void ObObjectManager::wait()
{
  OB_SERVER_BLOCK_MGR.wait();
}

void ObObjectManager::destroy()
{
  OB_SERVER_BLOCK_MGR.destroy();
}

int ObObjectManager::alloc_object(const ObStorageObjectOpt &opt, ObStorageObjectHandle &object_handle)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
  } else if (ObStorageObjectType::DATA_MACRO != opt.object_type_
      && ObStorageObjectType::META_MACRO != opt.object_type_) {
    ret = OB_NOT_SUPPORTED;
  } else if (OB_FAIL(OB_SERVER_BLOCK_MGR.alloc_object(object_handle))) {
  }
  return ret;
}

int ObObjectManager::async_read_object(
    const ObStorageObjectReadInfo &read_info,
    ObStorageObjectHandle &object_handle)
{
  return object_handle.async_read(read_info);
}

int ObObjectManager::async_write_object(
    const ObStorageObjectOpt &opt,
    const ObStorageObjectWriteInfo &write_info,
    ObStorageObjectHandle &object_handle)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!write_info.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(OB_STORAGE_OBJECT_MGR.alloc_object(opt, object_handle))) {
  } else if (OB_FAIL(object_handle.async_write(write_info))) {
  }
  return ret;
}

int ObObjectManager::read_object(
    const ObStorageObjectReadInfo &read_info,
    ObStorageObjectHandle &object_handle)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(async_read_object(read_info, object_handle))) {
  } else if (OB_FAIL(object_handle.wait())) {
  }
  return ret;
}
int ObObjectManager::write_object(
    const ObStorageObjectOpt &opt,
    const ObStorageObjectWriteInfo &write_info,
    ObStorageObjectHandle &object_handle)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(async_write_object(opt, write_info, object_handle))) {
  } else if (OB_FAIL(object_handle.wait())) {
  }
  return ret;
}

int ObObjectManager::inc_ref(const MacroBlockId &object_id) const
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
  } else {
    ret = OB_SERVER_BLOCK_MGR.inc_ref(object_id);
  }
  return ret;
}

int ObObjectManager::dec_ref(const MacroBlockId &object_id) const
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
  } else {
    ret = OB_SERVER_BLOCK_MGR.dec_ref(object_id);
  }
  return ret;
}

int ObObjectManager::resize_local_device(
    const int64_t expected_current_size,
    const int64_t new_device_size,
    const int64_t new_device_disk_percentage,
    const int64_t reserved_size)
{
  int ret = OB_SUCCESS;
  lib::ObMutexGuard guard(resize_file_lock_); // lock resize file opt

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
  } else {
    const int64_t current_size = OB_SERVER_BLOCK_MGR.get_total_block_size();
    if (expected_current_size != current_size) {
      ret = OB_EAGAIN;
    } else {
      ret = OB_SERVER_BLOCK_MGR.resize_file(new_device_size, new_device_disk_percentage, reserved_size);
    }
  }
  return ret;
}

int ObObjectManager::check_disk_space_available()
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
  }
  return ret;
}

int64_t ObObjectManager::get_total_macro_block_count() const
{
  return macro_object_size_ > 0 ? OB_SERVER_BLOCK_MGR.get_total_block_size() / macro_object_size_ : 0;
}

int64_t ObObjectManager::get_max_macro_block_count(int64_t reserved_size) const
{
  return OB_SERVER_BLOCK_MGR.get_max_macro_block_count(reserved_size);
}

int64_t ObObjectManager::get_used_macro_block_count() const
{
  return OB_SERVER_BLOCK_MGR.get_used_macro_block_count();
}

int64_t ObObjectManager::get_free_macro_block_count() const
{
  return OB_SERVER_BLOCK_MGR.get_free_macro_block_count();
}

} // namespace blocksstable
} // oceanbase
