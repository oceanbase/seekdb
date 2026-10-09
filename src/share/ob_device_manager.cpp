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

#include "ob_device_manager.h"
#include "share/io/ob_io_manager.h"
#include "share/ob_local_device.h"

namespace oceanbase
{
namespace common
{
const int ObDeviceManager::MAX_DEVICE_INSTANCE;
ObDeviceManager::ObDeviceManager() : allocator_(), device_count_(0), is_init_(false)
{
}

int ObDeviceManager::init_devices_env()
{
  int ret = OB_SUCCESS;
  const ObMemAttr mem_attr("DEVICE_MANAGER");
  if (is_init_) {
    //do nothing, does not return error code
  } else {
    //init device manager
    for (int i = 0; i < MAX_DEVICE_INSTANCE; i++ ) {
      device_ins_[i].device_ = NULL;
      device_ins_[i].device_key_ = NULL;
    }
    if (OB_FAIL(device_map_.create(MAX_DEVICE_INSTANCE*2, "DeviceMng", "DeviceMng"))) {
    } else if (OB_FAIL(handle_map_.create(MAX_DEVICE_INSTANCE*2, "DeviceMng", "DeviceMng"))) {
    } else if (OB_FAIL(allocator_.init(lib::ObMallocAllocator::get_instance(),
                                      OB_MALLOC_MIDDLE_BLOCK_SIZE, mem_attr))) {
    } else if (OB_FAIL(lock_.init(mem_attr))) {
    }
  }

  if (OB_SUCCESS == ret) {
    is_init_ = true;
  } else {
    /*release the resource*/
    destroy();
  }
  return ret;
}

void ObDeviceManager::destroy()
{
  int ret_dev = OB_SUCCESS;
  int ret_io_mgr = OB_SUCCESS;
  int ret_handle = OB_SUCCESS;
  /*destroy fun wil release all the node*/
  if (is_init_) {
    ret_dev = device_map_.destroy();
    ret_handle = handle_map_.destroy();
    if (OB_SUCCESS != ret_dev || OB_SUCCESS != ret_handle) {
      OB_LOG_RET(WARN, ret_dev, "fail to destroy device map", K(ret_dev), K(ret_handle));
    }
    for (int i = 0; i < MAX_DEVICE_INSTANCE; i++ ) {
      ObIODevice* del_device = device_ins_[i].device_;
      char *del_device_key = device_ins_[i].device_key_;
      if (OB_NOT_NULL(del_device)) {
        ret_io_mgr = ObIOManager::get_instance().remove_device_channel(del_device);
        if (OB_SUCCESS != ret_io_mgr) {
          OB_LOG_RET(WARN, ret_io_mgr, "fail to remove device channel", K(ret_io_mgr), KP(del_device));
        }
        del_device->destroy();
        allocator_.free(del_device);
      }
      if (OB_NOT_NULL(del_device_key)) {
        allocator_.free(del_device_key);
      }
      device_ins_[i].device_ = NULL;
      device_ins_[i].device_key_ = NULL;
      del_device_key = NULL;
    }
    allocator_.reset();
    lock_.destroy();
    is_init_ = false;
    device_count_ = 0;
    OB_LOG_RET(WARN, ret_dev, "release the init resource", K(ret_dev), K(ret_handle));
  }
  OB_LOG(INFO, "destroy device manager!");
}

ObDeviceManager& ObDeviceManager::get_instance()
{
  static ObDeviceManager static_instance;
  return static_instance;
}

int alloc_local_device(
    const common::ObString &storage_type_prefix,
    ObIODevice *&device_handle,
    common::ObFIFOAllocator &allocator)
{
  int ret = OB_SUCCESS;
  ObStorageType device_type = OB_STORAGE_MAX_TYPE;
  void* mem = NULL;

  if (0 == storage_type_prefix.compare(OB_LOCAL_PREFIX)) {
    device_type = OB_STORAGE_LOCAL;
    mem = allocator.alloc(sizeof(share::ObLocalDevice));
    if (NULL != mem) {new(mem)share::ObLocalDevice();}
  } else {
    ret = OB_INVALID_ARGUMENT;
    OB_LOG(WARN, "invalid local device prefix", K(ret), K(storage_type_prefix));
  }

  if (OB_SUCCESS != ret) {
  } else if (OB_ISNULL(mem)) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    OB_LOG(WARN, "fail to alloc mem for device ins!", K(storage_type_prefix));
  } else {
    device_handle = static_cast<ObIODevice*>(mem);
    device_handle->device_type_ = device_type;
  }

  return ret;
}

int ObDeviceManager::alloc_device_(
    const ObString &storage_type_prefix,
    const ObString &device_key,
    ObDeviceInsInfo *&device_info)
{
  int ret = OB_SUCCESS;
  int64_t last_no_ref_idx = -1;
  int64_t avai_idx = -1;
  ObIODevice *device_handle = nullptr;
  if (OB_FAIL(alloc_local_device(storage_type_prefix, device_handle, allocator_))) {
  } else {
    //find a device slot
    for (int i = 0; i < MAX_DEVICE_INSTANCE; i++) {
      if (NULL == device_ins_[i].device_) {
        avai_idx = i;
        break;
      } else if ((NULL != device_ins_[i].device_) && (0 == device_ins_[i].device_->get_ref_cnt())) {
        last_no_ref_idx = i;
      }
    }

    if (-1 == avai_idx && -1 == last_no_ref_idx) {
      //cannot insert into device manager
      ret = OB_OUT_OF_ELEMENT;
      OB_LOG(WARN, "devices too many!", KR(ret),
          K(MAX_DEVICE_INSTANCE), K(storage_type_prefix), KP(device_key.ptr()));
    } else {
      //try to release one
      if (-1 == avai_idx && -1 != last_no_ref_idx) {
        //erase from map
        ObIODevice* del_device = device_ins_[last_no_ref_idx].device_;
        ObString old_key(device_ins_[last_no_ref_idx].device_key_);
        if (OB_FAIL(ObIOManager::get_instance().remove_device_channel(del_device))) {
        } else if (OB_FAIL(device_map_.erase_refactored(old_key))) {
        } else if (OB_FAIL(handle_map_.erase_refactored((int64_t)(device_ins_[last_no_ref_idx].device_)))) {
        } else {
          /*free the resource*/
          del_device->destroy();
          allocator_.free(del_device);
          device_ins_[last_no_ref_idx].device_ = NULL;
          char *del_device_key = device_ins_[last_no_ref_idx].device_key_;
          if (OB_NOT_NULL(del_device_key)) {
            allocator_.free(del_device_key);
            device_ins_[last_no_ref_idx].device_key_ = NULL;
            del_device_key = NULL;
          }
          abort_unless(device_count_ == MAX_DEVICE_INSTANCE);
          device_count_--;
          avai_idx = last_no_ref_idx;
          OB_LOG(INFO, "release one device for realloc another!", KP(old_key.ptr()),
              K(storage_type_prefix), KP(device_key.ptr()));
        }
      }

      if (OB_SUCCESS == ret) {
        //insert into map
        ObString cur_key;
        if (OB_FAIL(ob_write_string(allocator_, device_key, cur_key, true/*c_style*/))) {
        } else if (FALSE_IT(device_ins_[avai_idx].device_key_ = cur_key.ptr())) {
        } else if (OB_FAIL(device_map_.set_refactored(cur_key, &(device_ins_[avai_idx])))) {
        } else if (OB_FAIL(handle_map_.set_refactored((int64_t)(device_handle), &(device_ins_[avai_idx])))) {
        } else {
          OB_LOG(INFO, "success insert into map!", K(storage_type_prefix), KP(device_key.ptr()));
        }
      }
    }
  }

  if (OB_FAIL(ret)) {
    if (NULL != device_handle) {
      allocator_.free(device_handle);
    }
    device_info = NULL;
  } else {
    device_ins_[avai_idx].device_ = device_handle;
    device_count_++;
    OB_LOG(INFO, "alloc a new device!",
           K(storage_type_prefix), K(avai_idx), K(device_count_), K(device_handle));
    device_info = &(device_ins_[avai_idx]);
  }

  return ret;
}

int ObDeviceManager::inc_device_ref_nolock_(ObDeviceInsInfo *dev_info)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(dev_info)) {
    ret = OB_ERR_UNEXPECTED;
    OB_LOG(WARN, "dev_info should not be null", KR(ret));
  } else if (OB_ISNULL(dev_info->device_)) {
    ret = OB_ERR_UNEXPECTED;
    OB_LOG(WARN, "device should not be null", KR(ret));
  } else {
    dev_info->device_->inc_ref();
  }
  return ret;
}

int ObDeviceManager::get_deivce_(const ObString &device_key, ObIODevice *&device_handle)
{
  int ret = OB_SUCCESS;
  ObDeviceInsInfo *dev_info = nullptr;
  device_handle = nullptr;
  ObQSyncLockReadGuard guard(lock_);

  if (OB_FAIL(device_map_.get_refactored(device_key, dev_info))) {
    if (OB_HASH_NOT_EXIST == ret) {
      // device not found; defer creation to subsequent steps
    } else {
      OB_LOG(WARN, "fail to get device from device manager ", KR(ret), KP(device_key.ptr()));
    }
    // device_->inc_ref/dec_ref/get_ref_cnt are atomic operations, so acquiring a read lock suffices
  } else if (OB_FAIL(inc_device_ref_nolock_(dev_info))) {
  } else {
    device_handle = dev_info->device_;
  }
  return ret;
}

int ObDeviceManager::alloc_device_and_init_(
    const ObString &storage_type_prefix,
    const ObString &device_key,
    ObIODevice *&device_handle)
{
  int ret = OB_SUCCESS;
  ObDeviceInsInfo *dev_info = nullptr;
  device_handle = nullptr;
  ObQSyncLockWriteGuard guard(lock_);

  // Re-check to see if the device was created while acquiring the lock
  if (OB_FAIL(device_map_.get_refactored(device_key, dev_info))) {
    if (OB_HASH_NOT_EXIST == ret) {
      ret = OB_SUCCESS;
      // alloc a device, and set into the map
      if (OB_FAIL(alloc_device_(storage_type_prefix, device_key, dev_info))) {
      }
    } else {
      OB_LOG(WARN, "fail to re-check device existence", KR(ret), K(storage_type_prefix), KP(device_key.ptr()));
    }
  }

  if (FAILEDx(inc_device_ref_nolock_(dev_info))) {
    OB_LOG(WARN, "fail to inc device ref", KR(ret), KP(device_key.ptr()));
  } else {
    device_handle = dev_info->device_;
  }

  return ret;
}

int ObDeviceManager::get_device_(
    const ObString &storage_type_prefix,
    const ObStorageIdMod &storage_id_mod,
    ObIODevice *&device_handle)
{
  int ret = OB_SUCCESS;
  ObArenaAllocator allocator;
  device_handle = nullptr;
  char *tmp_device_key = nullptr;

  if (OB_UNLIKELY(!is_init_)) {
    ret = OB_NOT_INIT;
    OB_LOG(WARN, "device manager is not inited", KR(ret));
  } else if (OB_FAIL(get_device_key_(
      allocator, storage_type_prefix, storage_id_mod, tmp_device_key))) {
  } else {
    if (OB_FAIL(get_deivce_(tmp_device_key, device_handle))) {
      if (OB_HASH_NOT_EXIST == ret) {
        ret = OB_SUCCESS;
        if (OB_FAIL(alloc_device_and_init_(
              storage_type_prefix, tmp_device_key, device_handle))) {
        }
      } else {
        OB_LOG(WARN, "fail to get device from device manager ", KR(ret),
            K(storage_type_prefix), K(storage_id_mod), KP(tmp_device_key));
      }
    }
  }

  return ret;
}

int ObDeviceManager::get_local_device(
    const ObString &storage_type_prefix,
    const ObStorageIdMod &storage_id_mod,
    ObIODevice *&device_handle)
{
  int ret = OB_SUCCESS;
  ObString local_prefix(OB_LOCAL_PREFIX);
  if (OB_UNLIKELY(0 != storage_type_prefix.compare(local_prefix))) {
    ret = OB_INVALID_ARGUMENT;
    OB_LOG(WARN, "invalid storage type prefix", K(ret), K(storage_type_prefix));
  } else {
    if (OB_FAIL(ObDeviceManager::get_instance().get_device_(
            storage_type_prefix, storage_id_mod, device_handle))) {
    }
  }
  return ret;
}

/*
* 1、release just modify the ref cnt, no need query map
* 2、when the device cnt exceed max cnt, will destroy a device which ref cnt is 0
*/
int ObDeviceManager::release_device(ObIODevice *&device_handle)
{
  int ret = OB_SUCCESS;
  ObDeviceInsInfo *device_info = nullptr;
  // device_->inc_ref/dec_ref/get_ref_cnt are atomic operations, so acquiring a read lock suffices
  ObQSyncLockReadGuard guard(lock_);
  if (!is_init_) {
    OB_LOG(WARN, "device manager not init!");
    ret = OB_NOT_INIT;
  } else if (OB_ISNULL(device_handle)) {
    ret = OB_INVALID_ARGUMENT;
    OB_LOG(WARN, "device_handle is null!");
  } else if (OB_FAIL(handle_map_.get_refactored((int64_t)(device_handle), device_info))) {
  }

  if (OB_SUCCESS == ret) {
    if (OB_ISNULL(device_info) || OB_ISNULL(device_info->device_)) {
      ret = OB_ERR_UNEXPECTED;
      OB_LOG(WARN, "Exception: get a null device handle!", K(device_handle));
    } else {
      if (0 >= device_info->device_->get_ref_cnt()) {
        OB_LOG(WARN, "the device ref is 0/small 0, maybe a invalid release!", K(device_info->device_));
        ret = OB_INVALID_ARGUMENT;
      } else {
        abort_unless(device_count_ > 0);
        abort_unless(device_info->device_->get_ref_cnt() > 0);
        device_info->device_->dec_ref();
        if (0 == device_info->device_->get_ref_cnt()) {
        } else {
          OB_LOG(DEBUG, "released dev info", K(device_info->device_), K(device_info->device_->get_ref_cnt()));
        }
        device_handle = NULL;
      }
    }
  }
  return ret;
}

int ObDeviceManager::get_device_key_(
    ObIAllocator &allcator,
    const ObString &storage_type_prefix,
    const ObStorageIdMod &storage_id_mod,
    char *&device_key)
{
  int ret = OB_SUCCESS;
  if (OB_NOT_NULL(device_key)) {
    ret = OB_INVALID_ARGUMENT;
    OB_LOG(WARN, "device key is already not null", K(ret));
  } else if (0 == storage_type_prefix.compare(OB_LOCAL_PREFIX)) {
    // uint64_t occupies up to 20 characters.
    // 20(storage_used_mod_) + 20(storage_id_) + 2(two '&') + 1(one '\0') = 43.
    // reserve some free space, increase 43 to 50.
    const int64_t alloc_size = STRLEN(OB_LOCAL_PREFIX) + 50;
    if (OB_ISNULL(device_key = static_cast<char *>(allcator.alloc(alloc_size)))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      OB_LOG(WARN, "fail to alloc mem for device key", K(ret), K(alloc_size));
    } else if (OB_FAIL(databuff_printf(device_key, alloc_size, "%s&%lu&%lu",
                                       OB_LOCAL_PREFIX,
                                       (uint64_t)storage_id_mod.storage_used_mod_,
                                       storage_id_mod.storage_id_))) {
    }
  } else {
    ret = OB_INVALID_ARGUMENT;
    OB_LOG(WARN, "invalid storage type prefix", K(ret), K(storage_type_prefix), K(storage_id_mod));
  }
  return ret;
}


}
}
