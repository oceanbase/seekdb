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

#include "storage/direct_load/ob_direct_load_external_fragment.h"

namespace oceanbase
{
namespace storage
{
using namespace common;

/**
 * ObDirectLoadExternalFragment
 */

ObDirectLoadExternalFragment::ObDirectLoadExternalFragment()
  : file_size_(0), row_count_(0), max_data_block_size_(0)
{
}

ObDirectLoadExternalFragment::~ObDirectLoadExternalFragment()
{
  reset();
}

void ObDirectLoadExternalFragment::reset()
{
  file_handle_.reset();
  file_size_ = 0;
  row_count_ = 0;
  max_data_block_size_ = 0;
}

bool ObDirectLoadExternalFragment::is_valid() const
{
  return file_size_ > 0 && row_count_ > 0 && max_data_block_size_ > 0 && file_handle_.is_valid();
}

int ObDirectLoadExternalFragment::assign(const ObDirectLoadExternalFragment &other)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(file_handle_.assign(other.file_handle_))) {
    LOG_WARN("fail to assign file handle", KR(ret));
  } else {
    file_size_ = other.file_size_;
    row_count_ = other.row_count_;
    max_data_block_size_ = other.max_data_block_size_;
  }
  return ret;
}

/**
 * ObDirectLoadExternalFragmentArray
 */

ObDirectLoadExternalFragmentArray::ObDirectLoadExternalFragmentArray()
{
  fragments_.set_tenant_id(MTL_ID());
}

ObDirectLoadExternalFragmentArray::~ObDirectLoadExternalFragmentArray()
{
  reset();
}

void ObDirectLoadExternalFragmentArray::reset()
{
  fragments_.reset();
}

int ObDirectLoadExternalFragmentArray::assign(const ObDirectLoadExternalFragmentArray &other)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(fragments_.assign(other.fragments_))) {
    LOG_WARN("fail to assign vector", KR(ret));
  }
  return ret;
}

int ObDirectLoadExternalFragmentArray::push_back(const ObDirectLoadExternalFragment &fragment)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(fragments_.push_back(fragment))) {
    LOG_WARN("fail to push back fragment", KR(ret));
  }
  return ret;
}

int ObDirectLoadExternalFragmentArray::push_back(const ObDirectLoadExternalFragmentArray &other)
{
  int ret = OB_SUCCESS;
  for (int64_t i = 0; OB_SUCC(ret) && i < other.count(); ++i) {
    if (OB_FAIL(fragments_.push_back(other.at(i)))) {
      LOG_WARN("fail to push back fragment", KR(ret));
    }
  }
  return ret;
}

/**
 * ObDirectLoadExternalFragmentCompare
 */

ObDirectLoadExternalFragmentCompare::ObDirectLoadExternalFragmentCompare()
  : result_code_(OB_SUCCESS)
{
}

ObDirectLoadExternalFragmentCompare::~ObDirectLoadExternalFragmentCompare()
{
}


} // namespace storage
} // namespace oceanbase
