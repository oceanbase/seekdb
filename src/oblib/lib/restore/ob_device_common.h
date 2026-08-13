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

#ifndef SRC_LIBRARY_SRC_COMMON_STORAGE_OB_DEVICE_COMMON_
#define SRC_LIBRARY_SRC_COMMON_STORAGE_OB_DEVICE_COMMON_

#include <dirent.h>
#include "lib/ob_define.h"
#include "lib/utility/ob_print_utils.h"

namespace oceanbase
{
namespace common
{

class ObBaseDirEntryOperator
{
public:
  enum ObDirOpFlag {
    DOF_REG = 0,
    DOF_DIR = 1,
    DOF_MAX_FLAG
  };
  ObBaseDirEntryOperator() : op_flag_(DOF_REG), size_(0) {}
  virtual ~ObBaseDirEntryOperator() = default;
  virtual int func(const dirent *entry) = 0;
  virtual bool need_get_file_size() const { return false; }
  void set_dir_flag() {op_flag_ = DOF_DIR;}
  bool is_dir_scan() {return (op_flag_ == DOF_DIR) ? true : false;}
  void set_size(const int64_t size) { size_ = size; }
  int64_t get_size() const { return size_; }
  TO_STRING_KV(K_(op_flag), K_(size));
private:
  int op_flag_;
  int64_t size_; // Always set 0 for directory.
};

enum ObStorageType : uint8_t
{
  OB_STORAGE_LOCAL = 0,
  OB_STORAGE_LOCAL_CACHE = 1,
  OB_STORAGE_MAX_TYPE
};

enum ObStorageInfoType : uint8_t {
  ALL_ZONE_STORAGE
};

enum class ObStorageUsedMod : uint8_t {
  STORAGE_USED_DATA = 0,
  STORAGE_USED_CLOG = 1,
  STORAGE_USED_MAX
};

struct ObStorageIdMod
{
  ObStorageIdMod()
    : storage_id_(OB_INVALID_ID), storage_used_mod_(ObStorageUsedMod::STORAGE_USED_MAX)
  {}

  ObStorageIdMod(const uint64_t storage_id, const ObStorageUsedMod storage_used_mod)
    : storage_id_(storage_id), storage_used_mod_(storage_used_mod)
  {}

  virtual ~ObStorageIdMod() {}

  bool is_valid() const { return storage_used_mod_ != ObStorageUsedMod::STORAGE_USED_MAX
                                 && storage_id_ != OB_INVALID_ID; }

  void reset()
  {
    storage_id_ = OB_INVALID_ID;
    storage_used_mod_ = ObStorageUsedMod::STORAGE_USED_MAX;
  }

  TO_STRING_KV(K_(storage_id), K_(storage_used_mod));

  uint64_t storage_id_;
  ObStorageUsedMod storage_used_mod_;
};

}
}
#endif
