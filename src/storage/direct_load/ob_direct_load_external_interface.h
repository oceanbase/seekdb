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

#include "lib/utility/ob_print_utils.h"

namespace oceanbase
{
namespace storage
{
class ObDirectLoadTmpFileHandle;

template <typename T>
class ObDirectLoadExternalIterator
{
public:
  virtual ~ObDirectLoadExternalIterator() = default;
  virtual int get_next_item(const T *&item) = 0;
  TO_STRING_EMPTY();
};

template <typename T>
class ObDirectLoadExternalWriter
{
public:
  virtual ~ObDirectLoadExternalWriter() = default;
  virtual int open(const ObDirectLoadTmpFileHandle &file_handle) = 0;
  virtual int write_item(const T &item) = 0;
  virtual int close() = 0;
  TO_STRING_EMPTY();
};

} // namespace storage
} // namespace oceanbase
