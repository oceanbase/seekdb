/**
 * Copyright (c) 2021 OceanBase
 * OceanBase CE is licensed under Mulan PubL v2.
 * You can use this software according to the terms and conditions of the Mulan PubL v2.
 * You may obtain a copy of Mulan PubL v2 at:
 *          http://license.coscl.org.cn/MulanPubL-2.0
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
 * EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
 * MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PubL v2 for more details.
 */

#include "storage/memtable/ob_memtable_compact_writer.h"

namespace oceanbase
{
using namespace common;
namespace memtable
{
ObMemtableCompactWriter::ObMemtableCompactWriter() : buffer_(nullptr), buf_size_(0)
{
}

ObMemtableCompactWriter::~ObMemtableCompactWriter()
{
  if (SMALL_BUFFER_SIZE != buf_size_) {
    ob_free(buffer_);
  }
  buffer_ = nullptr;
  buf_size_ = 0;
}





int ObMemtableCompactWriter::extend_buf()
{
  int ret = OB_SUCCESS;
  char *buffer = nullptr;
  int64_t buf_size = buf_size_;

  switch (buf_size)
  {
    case SMALL_BUFFER_SIZE:
      if (OB_ISNULL(buffer = (char *)ob_malloc(NORMAL_BUFFER_SIZE,
                                               ObModIds::OB_MEMTABLE_COMPACT_WRITER_BUFFER))) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        TRANS_LOG(WARN, "ob_malloc fail", KR(ret), "size", +NORMAL_BUFFER_SIZE);            
      } else {
        buf_size = NORMAL_BUFFER_SIZE;
      }
      break;
    case NORMAL_BUFFER_SIZE:
      if (OB_ISNULL(buffer = (char *)ob_malloc(BIG_ROW_BUFFER_SIZE,
                                               ObModIds::OB_MEMTABLE_COMPACT_WRITER_BUFFER))) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        TRANS_LOG(WARN, "ob_malloc fail", KR(ret), "size", +BIG_ROW_BUFFER_SIZE);            
      } else {
        buf_size = BIG_ROW_BUFFER_SIZE;
      }
      break;
    case BIG_ROW_BUFFER_SIZE:
      ret = OB_BUF_NOT_ENOUGH;
      TRANS_LOG(WARN, "row size is too big, not supportted", KR(ret), K(buffer_), K(buf_size_));
      break;
    default:
      ret = OB_ERR_UNEXPECTED;
      TRANS_LOG(ERROR, "Unknown buffer len", KR(ret), K(buffer_), K(buf_size_));
      break;
  }
  if (OB_SUCC(ret)) {
    if (OB_FAIL(ObCellWriter::extend_buf(buffer, buf_size))) {
      TRANS_LOG(ERROR, "extend buf error", KR(ret), K(buffer), K(buf_size));
      ob_free(buffer);
      buffer = nullptr;
    } else {
      if (SMALL_BUFFER_SIZE != buf_size_) {
        ob_free(buffer_);
      }
      buffer_ = buffer;
      buf_size_ = buf_size;
    }
  }

  return ret;
}

}//namespace memtable
}//namespace oceanbase
