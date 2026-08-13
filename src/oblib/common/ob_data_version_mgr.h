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

#ifndef OCEANBASE_COMMON_OB_DATA_VERSION_MGR_H_
#define OCEANBASE_COMMON_OB_DATA_VERSION_MGR_H_

#include "lib/ob_define.h"
#include "common/ob_version_def.h"

namespace oceanbase
{
namespace common
{
/**
 * ObDataVersionMgr persists the only data format version accepted by
 * this binary. Startup rejects an existing file with any other format or
 * version.
 *
 * The path of the disk file is $observer_home/etc/seekdb.data_version.bin.
 * This file is composed of a header part and a data part.
 * | ---------------------HEADER (Not Readable)---------------------- |
 * | ObRecordHeader(magic_number, length, checksum...)                |
 * | ------------------------DATA (Readable)------------------------- |
 * | version_str version_val                                      \n |
 * | current-version encoded-current-version                       \n |
 * | ------------------------------------------------------------------
 *
 */
class ObDataVersionMgr
{
public:
  ObDataVersionMgr()
      : is_inited_(false), version_(nullptr), allocator_(lib::ObLabel("DataVersionMgr")),
        file_exists_when_loading_(false)
  {
  }
  ~ObDataVersionMgr() {}
  static ObDataVersionMgr& get_instance();
  int init();
  int load_from_file();
  int validate_or_init_current_version();
  bool get_file_exists_when_loading()
  {
    return ATOMIC_LOAD(&file_exists_when_loading_);
  }
private:
  struct ObDataVersion
  {
    explicit ObDataVersion(uint64_t version) : version_(version) {}
    ~ObDataVersion() {}
#ifdef _WIN32
    static constexpr const char *DUMP_BUF_FORMAT = "%s %llu";
    static constexpr const char *LOAD_BUF_FORMAT = "%63s %llu %c";
#else
    static constexpr const char *DUMP_BUF_FORMAT = "%s %lu";
    static constexpr const char *LOAD_BUF_FORMAT = "%63s %lu %c";
#endif
    static constexpr int64_t MAX_DUMP_BUF_SIZE = OB_SERVER_VERSION_LENGTH + 20 + 4;
    uint64_t get_version() const
    {
      return ATOMIC_LOAD(&version_);
    }
    TO_STRING_KV(K_(version));
  private:
    uint64_t version_;
  };
  int init_current_version_();
  int dump_current_version_to_file_();
  int dump_data_version_(char *buf, int64_t buf_length, int64_t &pos,
                         const uint64_t data_version);
  int load_data_version_(char *buf, int64_t &pos);
  int write_to_file_(char *buf, int64_t buf_length, int64_t data_length);
  void set_file_exists_when_loading_()
  {
    ATOMIC_STORE(&file_exists_when_loading_, true);
  }
private:
  static constexpr const char *DATA_VERSION_FILE_PATH = "etc/seekdb.data_version.bin";
  static constexpr int64_t DATA_VERSION_FILE_MAX_SIZE = 1 << 26; // 64MB
  static constexpr int16_t OB_CONFIG_MAGIC = static_cast<int16_t>(0XBEDE);
  static constexpr int16_t OB_CONFIG_VERSION = 2;
  bool is_inited_;
  ObDataVersion *version_;
  common::SpinRWLock lock_;
  common::ObArenaAllocator allocator_;
  bool file_exists_when_loading_;
};

} // namespace common
} // namespace oceanbase
#define DATA_VERSION_MGR (::oceanbase::common::ObDataVersionMgr::get_instance())
#endif // OCEANBASE_COMMON_OB_DATA_VERSION_MGR_H_
