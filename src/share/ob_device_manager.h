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

#ifndef SRC_LIBRARY_SRC_COMMON_STORAGE_OB_DEVICE_MANAGER_H_
#define SRC_LIBRARY_SRC_COMMON_STORAGE_OB_DEVICE_MANAGER_H_

#include "lib/restore/ob_io_device.h"
#include "lib/allocator/ob_fifo_allocator.h"
#include "lib/hash/ob_hashmap.h"
#include "lib/lock/ob_qsync_lock.h"

namespace oceanbase
{
namespace common
{

class ObDeviceManager
{
public:
  const static int MAX_DEVICE_INSTANCE = 50;
  int init_devices_env();
  void destroy();
  static ObDeviceManager &get_instance();

  // @storage_type_prefix only allows OB_LOCAL_PREFIX and OB_LOCAL_CACHE_PREFIX.
  static int get_local_device(const ObString &storage_type_prefix,
                              const ObStorageIdMod &storage_id_mod,
                              ObIODevice *&device_handle);
  int release_device(common::ObIODevice*& device_handle);
  //for test
  int64_t get_device_cnt() {return device_count_;}

private:
  ObDeviceManager();
  ~ObDeviceManager() { destroy(); }

  struct ObDeviceInsInfo {
    ObIODevice* device_;
    char *device_key_; // dynamically alloc memory
  };

  // Hash map keys reference externally allocated strings, so the manager owns their memory.
  typedef common::hash::ObHashMap<ObString, ObDeviceInsInfo*> DeviceKeyInfoMap;
  typedef common::hash::ObHashMap<int64_t, ObDeviceInsInfo*> DeviceHandleDeviceInfoMap;

  int alloc_device_(const ObString &storage_type_prefix,
                    const ObString &device_key,
                    ObDeviceInsInfo *&device_info);
  int get_device_key_(ObIAllocator &allcator,
                      const ObString &storage_type_prefix,
                      const ObStorageIdMod &storage_id_mod,
                      char *&device_key);
  int get_device_(const ObString &storage_type_prefix,
                  const ObStorageIdMod &storage_id_mod,
                  ObIODevice *&device_handle);
  int inc_device_ref_nolock_(ObDeviceInsInfo *dev_info);
  int get_deivce_(const ObString &device_key, ObIODevice *&device_handle);
  int alloc_device_and_init_(const ObString &storage_type_prefix,
                             const ObString &device_key,
                             ObIODevice *&device_handle);

  common::ObFIFOAllocator allocator_; /*alloc/free dynamic device mem*/
  int32_t device_count_;
  common::ObQSyncLock lock_;  /*the manager is global used, so need lock to guarante thread safe*/
  bool is_init_;
  ObDeviceInsInfo device_ins_[MAX_DEVICE_INSTANCE];
  DeviceKeyInfoMap device_map_;
  DeviceHandleDeviceInfoMap handle_map_; /*the key is a ObIODevice pointer, need cast when used*/
};


}
}

#endif
