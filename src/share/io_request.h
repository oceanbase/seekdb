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

#pragma once

#include "lib/ob_define.h"
#include "lib/allocator/ob_allocator.h"
#include "lib/utility/ob_print_utils.h"
#include "rust/storage_io.h"
#include "rust/storage-io/include/callback_adapter.h"
#include <optional>
#include <memory>

namespace oceanbase
{
namespace common
{

static constexpr int64_t DEFAULT_IO_WAIT_TIME_MS = 5000L;
static constexpr int64_t MAX_IO_WAIT_TIME_MS = 300L * 1000L;
static constexpr int64_t DEFAULT_IO_WAIT_TIME_US = 5000L * 1000L;

enum class ObIOMode : uint8_t { READ = 0, WRITE = 1, MAX_MODE };
const char *get_io_mode_string(ObIOMode mode);

enum ObIOModule {
  SYS_MODULE_START_ID = 1,
  SLOG_IO = SYS_MODULE_START_ID,
  CALIBRATION_IO,
  DETECT_IO,
  SSTABLE_WHOLE_SCANNER_IO,
  INSPECT_BAD_BLOCK_IO,
  SSTABLE_INDEX_BUILDER_IO,
  BACKUP_READER_IO,
  BLOOM_FILTER_IO,
  INDEX_BLOCK_TREE_CURSOR_IO,
  MICRO_BLOCK_CACHE_IO,
  ROOT_BLOCK_IO,
  TMP_PAGE_CACHE_IO,
  INDEX_BLOCK_MICRO_ITER_IO,
  LINKED_MACRO_BLOCK_IO,
  TMP_MEM_BLOCK_IO,
  SSTABLE_MACRO_BLOCK_WRITE_IO,
  CLOG_WRITE_IO,
  CLOG_READ_IO,
  // end
  SYS_MODULE_END_ID
};

const int64_t SYS_MODULE_CNT = SYS_MODULE_END_ID - SYS_MODULE_START_ID;

struct ObIOFlag final
{
  void reset() { *this = ObIOFlag{}; }
  bool is_valid() const { return mode_ == ObIOMode::READ || mode_ == ObIOMode::WRITE; }
  void set_mode(ObIOMode mode) { mode_ = mode; }
  ObIOMode get_mode() const { return mode_; }
  void set_sys_module_id(uint64_t module) { sys_module_id_ = module; }
  uint64_t get_sys_module_id() const { return sys_module_id_; }
  bool is_sys_module() const
  {
    return sys_module_id_ >= SYS_MODULE_START_ID && sys_module_id_ < SYS_MODULE_END_ID;
  }
  void set_read() { mode_ = ObIOMode::READ; }
  bool is_read() const { return mode_ == ObIOMode::READ; }
  void set_write() { mode_ = ObIOMode::WRITE; }
  bool is_write() const { return mode_ == ObIOMode::WRITE; }
  void set_sync() { worker_required_ = true; }
  bool is_sync() const { return worker_required_; }
  TO_STRING_KV("mode", get_io_mode_string(mode_), K_(sys_module_id), K_(worker_required));

private:
  ObIOMode mode_ = ObIOMode::READ;
  uint64_t sys_module_id_ = OB_INVALID_ID;
  bool worker_required_ = false;
};

storage_io::RequestOptions make_request_options(const ObIOFlag &descriptor,
    storage_io::RequestMode mode, int64_t offset, int64_t length,
    int64_t timeout_us = DEFAULT_IO_WAIT_TIME_US);

enum class ObIOCallbackType : uint8_t {
  ASYNC_SINGLE_MICRO_BLOCK_CALLBACK = 0,
  MULTI_DATA_BLOCK_CALLBACK = 1,
  SYNC_SINGLE_MICRO_BLOCK_CALLBACK = 2,
  STORAGE_META_CALLBACK = 3,
  TMP_PAGE_CALLBACK = 4,
  TMP_MULTI_PAGE_CALLBACK = 5,
  TMP_DIRECT_READ_PAGE_CALLBACK = 6,
  TEST_CALLBACK = 7, // just for unittest
  TMP_CACHED_READ_CALLBACK = 8,
  MAX_CALLBACK_TYPE = 9
};

class ObIOCallback
{
public:
  ObIOCallback(const ObIOCallbackType type);
  virtual ~ObIOCallback();

  virtual ObIAllocator *get_allocator() = 0;
  virtual const char *get_data() const = 0;
  virtual int process(const char *data_buffer, const int64_t size) = 0;
  ObIOCallbackType get_type() const
  {
    return type_;
  }
  DECLARE_PURE_VIRTUAL_TO_STRING;

protected:
  static int alloc_and_copy_data(
      const char *io_data_buffer, const int64_t data_size, common::ObIAllocator *allocator, char *&data_buffer);

  ObIOCallbackType type_;
};

int64_t get_io_interval(const int64_t &end_time, const int64_t &begin_time);

class ObIOHandle final
{
public:
  ObIOHandle() = default;
  ~ObIOHandle() = default;
  ObIOHandle(const ObIOHandle &other);
  ObIOHandle &operator=(const ObIOHandle &other);
  bool is_empty() const { return !request_.has_value(); }
  bool is_valid() const { return request_.has_value(); }
  bool is_finished() const;
  bool cancel() const;
  int wait(uint64_t wait_timeout_ms = UINT64_MAX);
  const char *get_buffer() const;
  const storage_io::CallbackAdapter *get_callback_output() const;
  int64_t get_data_size() const;
  int get_fs_errno(int &io_errno) const;
  int get_io_time_us(int64_t &io_time_us) const;
  void reset() { request_.reset(); }
  void set_request(rust::Box<storage_io::Request> request);
  TO_STRING_KV("valid", is_valid(), "finished", is_finished());
private:
  std::optional<rust::Box<storage_io::Request>> request_;
};

int convert_file_error(int error);
int convert_storage_error(int error);
int convert_block_status(const storage_io::BlockStatus &status);
int accept_io_request(rust::Box<storage_io::Request> request, ObIOHandle &handle);

} // namespace common
} // namespace oceanbase
