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

#include "storage/direct_load/ob_direct_load_external_table.h"

namespace oceanbase
{
namespace storage
{
using namespace common;

/**
 * ObDirectLoadExternalTableCreateParam
 */

ObDirectLoadExternalTableCreateParam::ObDirectLoadExternalTableCreateParam()
  : data_block_size_(0), row_count_(0), max_data_block_size_(0)
{
}

ObDirectLoadExternalTableCreateParam::~ObDirectLoadExternalTableCreateParam()
{
}

bool ObDirectLoadExternalTableCreateParam::is_valid() const
{
  return data_block_size_ > 0 && data_block_size_ % DIO_ALIGN_SIZE == 0 && row_count_ > 0 &&
         max_data_block_size_ > 0 && max_data_block_size_ % DIO_ALIGN_SIZE == 0 &&
         !fragments_.empty();
}

/**
 * ObDirectLoadExternalTableMeta
 */

ObDirectLoadExternalTableMeta::ObDirectLoadExternalTableMeta()
  : data_block_size_(0), row_count_(0), max_data_block_size_(0)
{
}

ObDirectLoadExternalTableMeta::~ObDirectLoadExternalTableMeta()
{
}

void ObDirectLoadExternalTableMeta::reset()
{
  tablet_id_.reset();
  data_block_size_ = 0;
  row_count_ = 0;
  max_data_block_size_ = 0;
}

/**
 * ObDirectLoadExternalTable
 */

ObDirectLoadExternalTable::ObDirectLoadExternalTable()
  : is_inited_(false)
{
  table_type_ = ObDirectLoadTableType::EXTERNAL_TABLE;
}

ObDirectLoadExternalTable::~ObDirectLoadExternalTable()
{
}

void ObDirectLoadExternalTable::reset()
{
  meta_.reset();
  fragments_.reset();
  is_inited_ = false;
}

int ObDirectLoadExternalTable::init(const ObDirectLoadExternalTableCreateParam &param)
{
  int ret = OB_SUCCESS;
  if (IS_INIT) {
    ret = OB_INIT_TWICE;
    LOG_WARN("ObDirectLoadExternalTable init twice", KR(ret), KP(this));
  } else if (OB_UNLIKELY(!param.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid args", KR(ret), K(param));
  } else {
    meta_.tablet_id_ = param.tablet_id_;
    meta_.data_block_size_ = param.data_block_size_;
    meta_.row_count_ = param.row_count_;
    meta_.max_data_block_size_ = param.max_data_block_size_;
    if (OB_FAIL(fragments_.assign(param.fragments_))) {
      LOG_WARN("fail to assign fragments", KR(ret));
    } else {
      is_inited_ = true;
    }
  }
  return ret;
}


} // namespace storage
} // namespace oceanbase
