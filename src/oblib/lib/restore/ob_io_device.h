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

#ifndef DEPS_OBLIB_SRC_COMMON_STORAGE_OB_IO_DEVICE_H_
#define DEPS_OBLIB_SRC_COMMON_STORAGE_OB_IO_DEVICE_H_

#include "lib/restore/ob_device_common.h"
#include "lib/ob_define.h"
#include "lib/container/ob_iarray.h"
#include "lib/container/ob_fixed_array.h"
#include "lib/utility/ob_print_utils.h"
#include "lib/utility/ob_platform_utils.h"

namespace oceanbase
{
namespace common
{
class ObIODevice;
// A descriptor represents a normal file or a data block.
struct ObIOFd
{
  ObIOFd(ObIODevice *device_handle = nullptr, const int64_t first_id = -1,
      const int64_t second_id = -1, const int64_t third_id = -1);
  bool is_normal_file() const { return NORMAL_FILE_ID == first_id_ && second_id_ > 0; }
  bool is_block_file() const { return first_id_ != NORMAL_FILE_ID; }
  void reset();
  bool operator == (const ObIOFd& other) const
  {
    return other.first_id_ == this->first_id_ && other.second_id_ == this->second_id_
        && other.third_id_ == this->third_id_ && other.device_handle_ == this->device_handle_;
  }
  bool operator != (const ObIOFd& other) const
  {
    return other.first_id_ != this->first_id_ || other.second_id_ != this->second_id_
        || other.third_id_ != this->third_id_ || other.device_handle_ != this->device_handle_;
  }
  bool is_valid() const;
  NEED_SERIALIZE_AND_DESERIALIZE;
  TO_STRING_KV(K_(first_id), K_(second_id), K_(third_id), KP_(device_handle));
  static const int64_t NORMAL_FILE_ID = 0xFFFFFFFFFFFFFFFF;// all of bit is one.
  int64_t first_id_;
  int64_t second_id_;
  int64_t third_id_;
  ObIODevice *device_handle_; // no need to serialize
};

union ObOptValue {
  ObOptValue() : value_str (NULL) {}
  void reset() {value_str = NULL;}
  const char * value_str;
  int64_t      value_int64;
  bool         value_bool;
};

struct ObIODOpt
{
  ObIODOpt() : key_(nullptr)
  {
    value_.value_str = NULL;
  }

  void set(const char *key, const char *value)
  {
    key_ = key;
    value_.value_str = value;
  }

  void set(const char* key, int64_t value)
  {
    key_ = key;
    value_.value_int64 = value;
  }

  void set(const char* key, bool value)
  {
    key_ = key;
    value_.value_bool = value;
  }
  const char *key_;
  ObOptValue value_;
};

struct ObIODOpts
{
  ObIODOpts() : opts_(nullptr), opt_cnt_(0) {}
  ObIODOpt *opts_;
  int64_t opt_cnt_;
};

struct ObIODFileStat
{
  ObIODFileStat()
  {
    memset(this, 0x00, sizeof(ObIODFileStat));
  }
  uint64_t rdev_;        // device id (if special file)
  uint64_t dev_;         // device id
  uint64_t inode_;       // inode number
  uint32_t mask_;        // stat got mask
  uint32_t mode_;        // file type and mode
  uint32_t nlink_;       // number of hard links
  uint64_t uid_;         // user id of owner
  uint64_t gid_;         // group id of owner
  uint64_t size_;        // total size, in bytes
  uint64_t block_cnt_;   // number of 512B blocks allocated
  uint64_t block_size_;  // block size for filesystem I/O
  int64_t atime_s_;      // time of last access
  int64_t mtime_s_;      // time of last modification
  int64_t ctime_s_;      // time of last status change
  int64_t btime_s_;      // time of birth

  TO_STRING_KV(K_(rdev), K_(dev), K_(inode), K_(mask), K_(mode), K_(nlink), K_(uid), K_(gid),
    K_(size), K_(block_cnt), K_(block_size), K_(atime_s), K_(mtime_s), K_(ctime_s), K_(btime_s));
};

struct ObIODirentEntry {
  ObIODirentEntry() : type_(0) {}
  ObIODirentEntry(const char *name, const int type) : type_(type) {
    STRNCPY(name_, name, sizeof(name_) - 1);
  }
  TO_STRING_KV(KCSTRING_(name), K_(type));
  bool is_valid() const { return 0 != name_[0] && (DT_DIR == type_ || DT_REG == type_); }
  bool is_dir() const { return DT_DIR == type_; }
  bool is_file() const { return DT_REG == type_; }

  char name_[common::MAX_PATH_SIZE];
  int  type_;
};

class ObDirRegularEntryNameFilter : public ObBaseDirEntryOperator
{
public:
  enum FilterType
  {
    PREFIX = 1,
    KEY_WORD = 2,
    SUFFIX = 3,
    MAX,
  };
  ObDirRegularEntryNameFilter(const char *filter_str, FilterType type, common::ObIArray<ObIODirentEntry> &d_entrys)
    : type_(type), d_entrys_(d_entrys)
  {
    STRNCPY(filter_str_, filter_str, sizeof(filter_str_) - 1);
  }
  virtual ~ObDirRegularEntryNameFilter() = default;
  virtual int func(const dirent *entry) override;

private:
  char filter_str_[common::MAX_PATH_SIZE];
  FilterType type_;
  common::ObIArray<ObIODirentEntry> &d_entrys_;
};

class ObIBlockIterator
{
public:
  ObIBlockIterator() = default;
  virtual ~ObIBlockIterator() = default;
  virtual int get_next_block(ObIOFd &block_id) = 0;
};

class ObIODevice
{
public:
  ObIODevice() : device_type_(OB_STORAGE_MAX_TYPE), media_id_(0), ref_cnt_(0) {}
  virtual ~ObIODevice() {}
  virtual int reconfig(const ObIODOpts &opts) = 0;
  virtual int get_config(ObIODOpts &opts) = 0;
  int get_device_name(char *buf, int32_t len);
  virtual void destroy() = 0;

  virtual int start(const ObIODOpts &opts) = 0;

  //file/dir interfaces
  virtual int open(const char *pathname, const int flags, const mode_t mode,
                    ObIOFd &fd, ObIODOpts *opts= NULL) = 0;
  virtual int close(const ObIOFd &fd) = 0;
  virtual int mkdir(const char *pathname, mode_t mode) = 0;
  virtual int rmdir(const char *pathname) = 0;
  virtual int unlink(const char *pathname) = 0;
  virtual int rename(const char *oldpath, const char *newpath) = 0;
  virtual int scan_dir(const char *dir_name, int (*func)(const dirent *entry)) = 0;
  virtual int scan_dir(const char *dir_name, ObBaseDirEntryOperator &op) = 0;
  virtual int fsync(const ObIOFd &fd) = 0;
  virtual int fdatasync(const ObIOFd &fd) = 0;
  virtual int fallocate(const ObIOFd &fd, mode_t mode, const int64_t offset, const int64_t len) = 0;
  virtual int lseek(const ObIOFd &fd, const int64_t offset, const int whence, int64_t &result_offset) = 0;
  virtual int truncate(const char *pathname, const int64_t len) = 0;
  virtual int ftruncate(const ObIOFd &fd, const int64_t len)
  { UNUSED(fd); UNUSED(len); return OB_NOT_SUPPORTED; }
  virtual int exist(const char *pathname, bool &is_exist) = 0;
  virtual int stat(const char *pathname, ObIODFileStat &statbuf) = 0;
  virtual int fstat(const ObIOFd &fd, ObIODFileStat &statbuf) = 0;

  //block interfaces
  virtual int mark_blocks(ObIBlockIterator &block_iter) = 0;
  virtual int alloc_block(const ObIODOpts *opts, ObIOFd &block_id) = 0;
  virtual int alloc_blocks(
    const ObIODOpts *opts,
    const int64_t count,
    ObIArray<ObIOFd> &blocks) = 0;
  virtual void free_block(const ObIOFd &block_id) = 0;
  virtual int fsync_block() = 0;
  virtual int mark_blocks(const ObIArray<ObIOFd> &blocks) = 0;
  virtual int get_restart_sequence(uint32_t &restart_id) const = 0;

  //sync io interfaces
  virtual int pread(
    const ObIOFd &fd,
    const int64_t offset,
    const int64_t size,
    void *buf,
    int64_t &read_size) = 0;
  virtual int pwrite(
    const ObIOFd &fd,
    const int64_t offset,
    const int64_t size,
    const void *buf,
    int64_t &write_size) = 0;
  virtual int read(
    const ObIOFd &fd,
    void *buf,
    const int64_t size,
    int64_t &read_size) = 0;
  virtual int write(
    const ObIOFd &fd,
    const void *buf,
    const int64_t size,
    int64_t &write_size) = 0;

  virtual bool supports_native_aio() const { return false; }

  // space management interface
  virtual int64_t get_total_block_size() const = 0;
  virtual int64_t get_free_block_count() const = 0;
  virtual int64_t get_max_block_size(int64_t reserved_size) const = 0;
  virtual int64_t get_max_block_count(int64_t reserved_size) const = 0;
  virtual int64_t get_reserved_block_count() const = 0;
  virtual int check_space_full(
    const int64_t required_size,
    const bool alarm_if_space_full = true) const = 0;
  virtual int check_write_limited() const = 0;

  // ref cnt
  virtual void inc_ref();
  virtual void dec_ref();
  virtual int64_t get_ref_cnt();

  OB_INLINE bool is_local_device() const
  {
    return ObStorageType::OB_STORAGE_LOCAL == device_type_
        || ObStorageType::OB_STORAGE_LOCAL_CACHE == device_type_;
  }

  int get_io_aligned_size(int64_t &aligned_size) const;
  TO_STRING_KV(K_(device_type), K_(media_id), K_(ref_cnt));

public:
  ObStorageType device_type_;
  int64_t media_id_;

protected:
  int64_t ref_cnt_;
};

}
}

#endif /* DEPS_OBLIB_SRC_COMMON_STORAGE_OB_IO_DEVICE_H_ */
