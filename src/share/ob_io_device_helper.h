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

#ifndef OCEANBASE_STORAGE_OB_IO_DEVICE_HELPER_H_
#define OCEANBASE_STORAGE_OB_IO_DEVICE_HELPER_H_

#include <stdint.h>
#include "lib/restore/ob_io_device.h"

namespace oceanbase
{
namespace share
{
class ObLocalDevice;
struct LocalIOConfig;
class ObILocalDeviceSpaceProvider;

/*this class seems like the adapter for local/ofs device*/
class ObSNIODeviceWrapper final
{
public:
  static ObSNIODeviceWrapper &get_instance();

  int init(
      const char *data_dir,
      const char *sstable_dir,
      const int64_t block_size,
      const int64_t data_disk_percentage,
      const int64_t data_disk_size,
      const ObILocalDeviceSpaceProvider &space_provider,
      const LocalIOConfig &io_config);
  void destroy();

  ObIODevice &get_local_device();

  // Used only by the runtime module environment test fixture.
  void set_local_device(ObLocalDevice *local_device)
  {
    local_device_ = local_device;
  }

private:
  ObSNIODeviceWrapper();
  ~ObSNIODeviceWrapper();
  int get_local_device_from_mgr(share::ObLocalDevice *&local_device);

private:
  ObLocalDevice *local_device_;
  bool is_inited_;
};

class ObIODeviceLocalFileOp
{
public:
  static int convert_sys_errno();
  static int convert_sys_errno(const int error_no);

  static int get_block_file_size(const char *sstable_dir,
                                 const int64_t reserved_size,
                                 const int64_t block_size,
                                 const int64_t suggest_file_size,
                                 const int64_t disk_percentage,
                                 int64_t &block_file_size);

  static int compute_block_file_size(const char *sstable_dir,
                                     const int64_t reserved_size,
                                     const int64_t block_size,
                                     const int64_t suggest_file_size,
                                     const int64_t disk_percentage,
                                     int64_t &block_file_size);

  static int check_disk_space_available(const char *sstable_dir,
                                        const int64_t data_disk_size,
                                        const int64_t reserved_size,
                                        const int64_t used_disk_size,
                                        const bool need_report_user_error);

};

#define ObIODeviceWrapper ObSNIODeviceWrapper

#define LOCAL_DEVICE_INSTANCE ::oceanbase::share::ObIODeviceWrapper::get_instance().get_local_device()
} // namespace share
} // namespace oceanbase

#endif // OCEANBASE_STORAGE_OB_IO_DEVICE_HELPER_H_
