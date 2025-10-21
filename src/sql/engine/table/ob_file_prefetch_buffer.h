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

#ifndef OB_FILE_PREFETCH_BUFFER_H
#define OB_FILE_PREFETCH_BUFFER_H

#include "share/ob_i_tablet_scan.h"
#include "lib/file/ob_file.h"
#include "sql/engine/table/ob_external_table_access_service.h"

namespace oceanbase {
namespace sql {
class ObFilePrefetchBuffer
{
public:
  ObFilePrefetchBuffer(ObExternalDataAccessDriver &file_reader) :
    offset_(0), length_(0), buffer_size_(0), buffer_(nullptr), file_reader_(file_reader),
    alloc_(common::ObMemAttr(MTL_ID(), "PrefetchBuffer"))
  {}
  ~ObFilePrefetchBuffer()
  {
    destroy();
  }
  void clear();
  void destroy();
  int prefetch(const int64_t file_offset, const int64_t size);
  bool in_prebuffer_range(const int64_t position, const int64_t nbytes);
  // NOTE: before calling, make sure it is within the buffer range.
  void fetch(const int64_t position, const int64_t nbytes, void *out);

private:
  int64_t offset_;
  int64_t length_;
  int64_t buffer_size_;
  void *buffer_;
  ObExternalDataAccessDriver &file_reader_;
  common::ObMalloc alloc_;
};
}
}

#endif // OB_FILE_PREFETCH_BUFFER_H
