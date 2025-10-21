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

#ifndef OCEANBASE_MEMTABLE_OB_MEMTABLE_DATA_
#define OCEANBASE_MEMTABLE_OB_MEMTABLE_DATA_

#include "share/ob_define.h"
#include "lib/utility/ob_print_utils.h"

#include "storage/ob_i_store.h"

namespace oceanbase
{
namespace memtable
{
// The structure actually written to MvccTransNode::buf_
class ObMemtableDataHeader
{
public:
  ObMemtableDataHeader(blocksstable::ObDmlFlag dml_flag, int64_t buf_len)
      : dml_flag_(dml_flag), buf_len_(buf_len)
  {}
  ~ObMemtableDataHeader() {}
  TO_STRING_KV(K_(dml_flag), K_(buf_len));
  inline int64_t dup_size() const { return (sizeof(ObMemtableDataHeader) + buf_len_); }
  inline int checksum(common::ObBatchChecksum &bc) const
  {
    int ret = common::OB_SUCCESS;
    if (buf_len_ <= 0) {
      ret = common::OB_NOT_INIT;
    } else {
      bc.fill(&dml_flag_, sizeof(dml_flag_));
      bc.fill(&buf_len_, sizeof(buf_len_));
      bc.fill(buf_, buf_len_);
    }
    return ret;
  }

  // the template parameter T supports ObMemtableData and ObMemtableDataHeader,
  // but in practice only ObMemtableDataHeader is involved in building.
  template<class T>
  static int build(ObMemtableDataHeader *new_data, const T *data)
  {
    int ret = OB_SUCCESS;
    if (OB_UNLIKELY(data->buf_len_ < 0)) {
      ret = OB_NOT_INIT;
    } else if (OB_ISNULL(data->buf_) || 0 == data->buf_len_) {
      // do nothing
    } else if (OB_ISNULL(new(new_data) ObMemtableDataHeader(data->dml_flag_, data->buf_len_))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
    } else {
      MEMCPY(new_data->buf_, data->buf_, data->buf_len_);
    }
    return ret;
  }
  blocksstable::ObDmlFlag dml_flag_;
  int64_t buf_len_;
  char buf_[0];
};

// only used for more conveniently passing parameters
class ObMemtableData
{
public:
  ObMemtableData()
    : dml_flag_(blocksstable::ObDmlFlag::DF_MAX), buf_len_(0), buf_(nullptr)
    {}
  ObMemtableData(blocksstable::ObDmlFlag dml_flag, int64_t buf_len, const char *buf)
      : dml_flag_(dml_flag), buf_len_(buf_len), buf_(buf)
  {}
  ~ObMemtableData() {}
  TO_STRING_KV(K_(dml_flag), K_(buf_len));
  void reset()
  {
    dml_flag_ = blocksstable::ObDmlFlag::DF_MAX;
    buf_len_ = 0;
    buf_ = nullptr;
  }
  void set(blocksstable::ObDmlFlag dml_flag, const int64_t data_len, char *buf)
  {
    dml_flag_ = dml_flag;
    buf_len_ = data_len;
    buf_ = buf;
  }
  // must use the size of ObMemtableDataHeader, since the actual structure
  // involved in dup is always ObMemtableDataHeader
  inline int64_t dup_size() const { return (sizeof(ObMemtableDataHeader) + buf_len_); }

  blocksstable::ObDmlFlag dml_flag_;
  int64_t buf_len_;
  const char *buf_;
};

}
}

#endif // OCEANBASE_MEMTABLE_OB_MEMTABLE_DATA_
