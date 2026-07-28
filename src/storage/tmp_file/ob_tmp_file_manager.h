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

#ifndef OCEANBASE_STORAGE_TMP_FILE_OB_TMP_FILE_MANAGER_H_
#define OCEANBASE_STORAGE_TMP_FILE_OB_TMP_FILE_MANAGER_H_

#include "storage/tmp_file/ob_tmp_file_io_info.h"
#include "storage/tmp_file/ob_tmp_file_io_handle.h"
#include "storage/tmp_file/ob_sn_tmp_file_manager.h"

namespace oceanbase
{
namespace tmp_file
{

class ObTmpFileManager
{
public:
  ObTmpFileManager(): is_inited_(false) {}
  virtual ~ObTmpFileManager() { destroy(); }
  static int server_module_init(ObTmpFileManager *&manager);
  virtual ObSNTmpFileManager &get_sn_file_manager() { return sn_file_manager_; }
  virtual int init();
  int start();
  void stop();
  void wait();
  void destroy();

  int alloc_dir(int64_t &dir_id);
  virtual int open(int64_t &fd, const int64_t &dir_id, const char* const label);
  int remove(const int64_t fd);

public:
  int aio_read(const ObTmpFileIOInfo &io_info, ObTmpFileIOHandle &io_handle);
  int aio_pread(const ObTmpFileIOInfo &io_info,
                const int64_t offset, ObTmpFileIOHandle &io_handle);
  int read(const ObTmpFileIOInfo &io_info, ObTmpFileIOHandle &io_handle);
  int pread(const ObTmpFileIOInfo &io_info,
            const int64_t offset, ObTmpFileIOHandle &io_handle);
  // NOTE:
  //   only support append write.
  int aio_write(const ObTmpFileIOInfo &io_info, ObTmpFileIOHandle &io_handle);
  // NOTE:
  //   only support append write.
  int write(const ObTmpFileIOInfo &io_info);
  int truncate(const int64_t fd, const int64_t offset);
  int seal(const int64_t fd);
  int get_tmp_file_size(const int64_t fd, int64_t &file_size);
  int get_tmp_file(const int64_t fd, ObITmpFileHandle &handle);
  int get_tmp_file_disk_usage(int64_t &disk_data_size, int64_t &occupied_disk_size);

public:
  //for virtual table to show
  int get_tmp_file_fds(ObIArray<int64_t> &fd_arr);
  int get_tmp_file_info(const int64_t fd, ObTmpFileInfo *tmp_file_info);
private:
  bool is_inited_;
  ObSNTmpFileManager sn_file_manager_;

};

class ObServerTmpFileManagerProxy final
{
public:
  static ObServerTmpFileManagerProxy &get_instance();
  int alloc_dir(int64_t &dir_id);
  int open(int64_t &fd,
           const int64_t &dir_id,
           const char* const label = nullptr);
  int remove(const int64_t fd);

public:
  int aio_read(const ObTmpFileIOInfo &io_info, ObTmpFileIOHandle &io_handle);
  int aio_pread(const ObTmpFileIOInfo &io_info, const int64_t offset, ObTmpFileIOHandle &io_handle);
  int pread(const ObTmpFileIOInfo &io_info, const int64_t offset, ObTmpFileIOHandle &io_handle);
  // NOTE:
  //   only support append write.
  int aio_write(const ObTmpFileIOInfo &io_info, ObTmpFileIOHandle &io_handle);
  // NOTE:
  //   only support append write.
  int write(const ObTmpFileIOInfo &io_info);
  int truncate(const int64_t fd, const int64_t offset);
  int seal(const int64_t fd);
  int get_tmp_file_size(const int64_t fd, int64_t &file_size);
  int get_tmp_file_fds(ObIArray<int64_t> &fd_arr);
  int get_tmp_file_info(const int64_t fd, ObTmpFileInfo *tmp_file_info);
};

#define SERVER_TMP_FILE_MANAGER (::oceanbase::tmp_file::ObServerTmpFileManagerProxy::get_instance())
}  // end namespace tmp_file
}  // end namespace oceanbase

#endif // OCEANBASE_STORAGE_TMP_FILE_OB_TMP_FILE_MANAGER_H_
