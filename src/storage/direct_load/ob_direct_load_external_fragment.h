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

#include "storage/direct_load/ob_direct_load_tmp_file.h"

namespace oceanbase
{
namespace storage
{

struct ObDirectLoadExternalFragment
{
public:
  ObDirectLoadExternalFragment();
  ~ObDirectLoadExternalFragment();
  void reset();
  bool is_valid() const;
  int assign(const ObDirectLoadExternalFragment &fragment);
  TO_STRING_KV(K_(file_size), K_(row_count), K_(max_data_block_size), K_(file_handle));
public:
  int64_t file_size_;
  int64_t row_count_;
  int64_t max_data_block_size_;
  ObDirectLoadTmpFileHandle file_handle_;
};

class ObDirectLoadExternalFragmentArray
{
public:
  ObDirectLoadExternalFragmentArray();
  ~ObDirectLoadExternalFragmentArray();
  void reset();
  int assign(const ObDirectLoadExternalFragmentArray &other);
  int push_back(const ObDirectLoadExternalFragment &fragment);
  int push_back(const ObDirectLoadExternalFragmentArray &other);
  int64_t count() const { return fragments_.count(); }
  bool empty() const { return fragments_.empty(); }
  ObDirectLoadExternalFragment &at(int64_t idx)
  {
    return fragments_.at(idx);
  }
  const ObDirectLoadExternalFragment &at(int64_t idx) const
  {
    return fragments_.at(idx);
  }
  TO_STRING_KV(K_(fragments));
private:
  common::ObArray<ObDirectLoadExternalFragment> fragments_;
};

class ObDirectLoadExternalFragmentCompare
{
public:
  ObDirectLoadExternalFragmentCompare();
  ~ObDirectLoadExternalFragmentCompare();
  int get_error_code() const { return result_code_; }
  int result_code_;
};

} // namespace storage
} // namespace oceanbase
