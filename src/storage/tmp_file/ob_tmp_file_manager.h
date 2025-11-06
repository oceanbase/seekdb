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
#ifdef OB_BUILD_SHARED_STORAGE
#include "storage/tmp_file/ob_ss_tmp_file_manager.h"
#endif
#include "storage/tmp_file/ob_sn_tmp_file_manager.h"

namespace oceanbase
{
namespace tmp_file
{

class ObTenantTmpFileManager
{
public:
  ObTenantTmpFileManager(): is_inited_(false) {}
  virtual ~ObTenantTmpFileManager() { destroy(); }
  static int mtl_init(ObTenantTmpFileManager *&manager);
  virtual ObSNTenantTmpFileManager &get_sn_file_manager() { return sn_file_manager_; }
#ifdef OB_BUILD_SHARED_STORAGE
  ObSSTenantTmpFileManager &get_ss_file_manager() { return ss_file_manager_; }
#endif
  virtual int init();
  int start();
  void stop();
  void wait();
  void destroy();

  int alloc_dir(int64_t &dir_id);
  virtual int open(int64_t &fd, const int64_t &dir_id, const char* const label);
  int remove(const int64_t fd);

public:
  int aio_read(const uint64_t tenant_id, const ObTmpFileIOInfo &io_info, ObTmpFileIOHandle &io_handle);
  int aio_pread(const uint64_t tenant_id, const ObTmpFileIOInfo &io_info,
                const int64_t offset, ObTmpFileIOHandle &io_handle);
  int read(const uint64_t tenant_id, const ObTmpFileIOInfo &io_info, ObTmpFileIOHandle &io_handle);
  int pread(const uint64_t tenant_id, const ObTmpFileIOInfo &io_info,
            const int64_t offset, ObTmpFileIOHandle &io_handle);
  // NOTE:
  //   only support append write.
  int aio_write(const uint64_t tenant_id, const ObTmpFileIOInfo &io_info, ObTmpFileIOHandle &io_handle);
  // NOTE:
  //   only support append write.
  int write(const uint64_t tenant_id, const ObTmpFileIOInfo &io_info);
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
  ObSNTenantTmpFileManager sn_file_manager_;

#ifdef OB_BUILD_SHARED_STORAGE
  ObSSTenantTmpFileManager ss_file_manager_;
#endif
};

class ObTenantTmpFileManagerWithMTLSwitch final
{
public:
  static ObTenantTmpFileManagerWithMTLSwitch &get_instance();
  int alloc_dir(const uint64_t tenant_id, int64_t &dir_id);
  int open(const uint64_t tenant_id,
           int64_t &fd,
           const int64_t &dir_id,
           const char* const label = nullptr);
  int remove(const uint64_t tenant_id, const int64_t fd);

public:
  int aio_read(const uint64_t tenant_id, const ObTmpFileIOInfo &io_info, ObTmpFileIOHandle &io_handle);
  int aio_pread(const uint64_t tenant_id, const ObTmpFileIOInfo &io_info, const int64_t offset, ObTmpFileIOHandle &io_handle);
  int pread(const uint64_t tenant_id, const ObTmpFileIOInfo &io_info, const int64_t offset, ObTmpFileIOHandle &io_handle);
  // NOTE:
  //   only support append write.
  int aio_write(const uint64_t tenant_id, const ObTmpFileIOInfo &io_info, ObTmpFileIOHandle &io_handle);
  // NOTE:
  //   only support append write.
  int write(const uint64_t tenant_id, const ObTmpFileIOInfo &io_info);
  int truncate(const uint64_t tenant_id, const int64_t fd, const int64_t offset);
  int seal(const uint64_t tenant_id, const int64_t fd);
  int get_tmp_file_size(const uint64_t tenant_id, const int64_t fd, int64_t &file_size);
  int get_tmp_file_fds(const uint64_t tenant_id, ObIArray<int64_t> &fd_arr);
  int get_tmp_file_info(const uint64_t tenant_id, const int64_t fd, ObTmpFileInfo *tmp_file_info);
};

#define FILE_MANAGER_INSTANCE_WITH_MTL_SWITCH (::oceanbase::tmp_file::ObTenantTmpFileManagerWithMTLSwitch::get_instance())
}  // end namespace tmp_file
}  // end namespace oceanbase

#endif // OCEANBASE_STORAGE_TMP_FILE_OB_TMP_FILE_MANAGER_H_
