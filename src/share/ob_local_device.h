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

#ifndef SRC_SHARE_OB_LOCAL_DEVICE_H_
#define SRC_SHARE_OB_LOCAL_DEVICE_H_

#include <shared_mutex>
#include "lib/restore/ob_io_device.h"
#include "share/ob_i_local_device_space_provider.h"
#include "share/local_io_config.h"
#include "rust/file_io.h"
#include "rust/storage_io.h"

namespace oceanbase {
namespace share {

class ObLocalDevice;

class ObLocalDevice : public common::ObIODevice {
public:
  ObLocalDevice();
  virtual ~ObLocalDevice();
  int init(
      const common::ObIODOpts &opts,
      const LocalIOConfig &io_config,
      const ObILocalDeviceSpaceProvider *space_provider = nullptr);
  virtual int reconfig(const common::ObIODOpts &opts) override;
  virtual int get_config(common::ObIODOpts &opts) override;
  virtual void destroy() override;

  virtual int start(const common::ObIODOpts &opts) override;

  //file/dir interfaces
  virtual int open(const char *pathname, const int flags, const mode_t mode, common::ObIOFd &fd, common::ObIODOpts *opts = NULL) override;
  virtual int close(const common::ObIOFd &fd) override;
  virtual int mkdir(const char *pathname, mode_t mode) override;
  virtual int rmdir(const char *pathname) override;
  virtual int unlink(const char *pathname) override;
  virtual int rename(const char *oldpath, const char *newpath) override;
  virtual int scan_dir(const char *dir_name, int (*func)(const dirent *entry)) override;
  virtual int scan_dir(const char *dir_name, common::ObBaseDirEntryOperator &op) override;
  virtual int fsync(const common::ObIOFd &fd) override;
  virtual int fdatasync(const common::ObIOFd &fd) override;
  virtual int fallocate(const common::ObIOFd &fd, mode_t mode, const int64_t offset, const int64_t len) override;
  virtual int lseek(const common::ObIOFd &fd, const int64_t offset, const int whence, int64_t &result_offset) override;
  virtual int truncate(const char *pathname, const int64_t len) override;
  virtual int ftruncate(const common::ObIOFd &fd, const int64_t len) override;
  virtual int exist(const char *pathname, bool &is_exist) override;
  virtual int stat(const char *pathname, common::ObIODFileStat &statbuf) override;
  virtual int fstat(const common::ObIOFd &fd, common::ObIODFileStat &statbuf) override;

  //block interfaces
  virtual int mark_blocks(common::ObIBlockIterator &block_iter) override;
  virtual int alloc_block(const common::ObIODOpts *opts, common::ObIOFd &block_id) override;
  virtual int alloc_blocks(
    const common::ObIODOpts *opts,
    const int64_t count,
    common::ObIArray<common::ObIOFd> &blocks) override;
  virtual void free_block(const common::ObIOFd &block_id) override;
  virtual int fsync_block() override;
  virtual int mark_blocks(const common::ObIArray<common::ObIOFd> &blocks) override;
  virtual int get_restart_sequence(uint32_t &restart_id) const override;

  //sync io interfaces
  virtual int pread(
    const common::ObIOFd &fd,
    const int64_t offset,
    const int64_t size,
    void *buf,
    int64_t &read_size) override;
  virtual int pwrite(
    const common::ObIOFd &fd,
    const int64_t offset,
    const int64_t size,
    const void *buf,
    int64_t &write_size) override;
  virtual int read(
    const common::ObIOFd &fd,
    void *buf,
    const int64_t size,
    int64_t &read_size) override;
  virtual int write(
    const common::ObIOFd &fd,
    const void *buf,
    const int64_t size,
    int64_t &write_size) override;

  bool supports_native_aio() const override
  { return io_config_.backend == LocalIOBackend::NATIVE_AIO; }

  int attach_named_owner(const storage_io::NamedOwnerHandle &owner);
  int bind_request_target(const common::ObIOFd &fd, storage_io::RequestSlot &request, storage_io::RequestLifetime &lifetime);
  int64_t get_block_size() const { return block_size_; }

  // space management interface
  virtual int64_t get_total_block_size() const override;
  virtual int64_t get_free_block_count() const override;
  virtual int64_t get_max_block_size(int64_t reserved_size) const override;
  virtual int64_t get_max_block_count(int64_t reserved_size) const override;
  virtual int64_t get_reserved_block_count() const override;
  virtual int check_space_full(
    const int64_t required_size,
    const bool alarm_if_space_full = true) const override;
  virtual int check_write_limited() const override;

public:
  static const int64_t RESERVED_BLOCK_INDEX = 1; // block zero holds the immutable file header

private:
  int get_data_disk_used_percentage_(
      const int64_t required_size,
      int64_t &percent) const;
  int resize_block_file(const int64_t new_size, const int64_t reserved_size);
  void reset_file_session_(); // Requires exclusive file_session_lock_.

private:
  // Descriptor admission takes this lock before registry or native owner locks.
  std::shared_mutex file_session_lock_;
  rust::Box<file_io::FileRegistry> files_;
  rust::Box<storage_io::NamedBindings> named_;
  rust::Box<storage_io::BlockOwner> blocks_;
  bool is_inited_;
  bool is_marked_;
  char store_dir_[common::OB_MAX_FILE_NAME_LENGTH];
  char sstable_dir_[common::OB_MAX_FILE_NAME_LENGTH];
  char store_path_[common::OB_MAX_FILE_NAME_LENGTH];
  lib::ObMutex block_lock_;
  int64_t block_size_;
  int64_t block_file_size_;
  int64_t disk_percentage_;
  LocalIOConfig io_config_;
  const ObILocalDeviceSpaceProvider *space_provider_;
};

} /* namespace share */
} /* namespace oceanbase */

#endif /* SRC_SHARE_OB_LOCAL_DEVICE_H_ */
