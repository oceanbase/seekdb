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

#ifndef OCEANBASE_LOGSERVICE_I_LOG_STORAGE_
#define OCEANBASE_LOGSERVICE_I_LOG_STORAGE_
#include <cstdint>
#include "lib/utility/ob_print_utils.h"                 // VIRTUAL_TO_STRING_KV
namespace oceanbase
{
namespace palf
{
class ReadBuf;
class LSN;
class LogIOContext;
enum class ILogStorageType {
  MEMORY_STORAGE = 0,
  DISK_STORAGE = 1,
  HYBRID_STORAGE = 2
};

class ILogStorage
{
public:
  ILogStorage(const ILogStorageType type) : type_(type) {}
  virtual ~ILogStorage() {}
  // @retval
  //   OB_SUCCESS
  //   OB_INVALID_ARGUMENT
  //   OB_ERR_OUT_OF_UPPER_BOUND
  //   OB_ERR_OUT_OF_LOWER_BOUND
  //   OB_ERR_UNEXPECTED, file maybe deleted by human.
  virtual int pread(const LSN &lsn,
                    const int64_t in_read_size,
                    ReadBuf &read_buf,
                    int64_t &out_read_size,
                    LogIOContext &io_ctx) = 0;
  ILogStorageType get_log_storage_type()
  { return type_; }
  const char *get_log_storage_type_str()
  {
    if (ILogStorageType::MEMORY_STORAGE == type_) {
      return "MEMORY_STORAGE";
    } else if (ILogStorageType::DISK_STORAGE == type_) {
      return "DISK_STORAGE";
    } else {
      return "HYBRID_STORAGE";
    }
  }
  VIRTUAL_TO_STRING_KV(K_(type));
private:
  ILogStorageType type_;
};
}
}
#endif
