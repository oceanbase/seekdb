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
#include "lib/string/ob_string.h"

namespace oceanbase
{
namespace common
{
class ObFileReader
{
public:
  ObFileReader();
  ~ObFileReader();
  int open(const ObString &fname, bool dio, int64_t align_size = 4096);
  void close();
  bool is_opened() const;
  int pread(void *buf, int64_t count, int64_t offset, int64_t &read_size);
  int get_file_size(int64_t &size) const;
private:
  DISALLOW_COPY_AND_ASSIGN(ObFileReader);
  struct Impl;
  Impl *file_;
};

class ObFileAppender
{
public:
  ObFileAppender();
  ~ObFileAppender();
  int open(const ObString &fname, bool dio, bool is_create,
           bool is_trunc = false, int64_t align_size = 4096);
  int create(const ObString &fname, bool dio, int64_t align_size = 4096);
  int finish();
  int64_t position() const;
  bool is_opened() const;
  int append(const void *buf, int64_t count);
  int fsync();
private:
  int open_(const ObString &fname, bool dio, uint8_t create, int64_t align_size);
  DISALLOW_COPY_AND_ASSIGN(ObFileAppender);
  struct Impl;
  Impl *file_;
};
}
}
