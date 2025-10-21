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

#define USING_LOG_PREFIX STORAGE

#include "storage/tmp_file/ob_tmp_file_io_info.h"

namespace oceanbase
{
using namespace share;

namespace tmp_file
{

ObTmpFileIOInfo::ObTmpFileIOInfo()
    : fd_(ObTmpFileGlobal::INVALID_TMP_FILE_FD),
      dir_id_(0), buf_(nullptr), size_(0),
      disable_page_cache_(false), disable_block_cache_(false),
      io_desc_(), io_timeout_ms_(DEFAULT_IO_WAIT_TIME_MS)
{}

ObTmpFileIOInfo::~ObTmpFileIOInfo()
{
  reset();
}

void ObTmpFileIOInfo::reset()
{
  fd_ = ObTmpFileGlobal::INVALID_TMP_FILE_FD;
  dir_id_ = 0;
  size_ = 0;
  io_timeout_ms_ = DEFAULT_IO_WAIT_TIME_MS;
  buf_ = nullptr;
  io_desc_.reset();
  disable_page_cache_ = false;
  disable_block_cache_ = false;
}

bool ObTmpFileIOInfo::is_valid() const
{
  return fd_ != ObTmpFileGlobal::INVALID_TMP_FILE_FD &&
         dir_id_ != ObTmpFileGlobal::INVALID_TMP_FILE_DIR_ID &&
         size_ > 0 &&
         nullptr != buf_ && io_desc_.is_valid() && io_timeout_ms_ >= 0;
}

} // end namespace tmp_file
} // end namespace oceanbase
