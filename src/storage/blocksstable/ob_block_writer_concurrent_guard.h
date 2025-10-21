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

#ifndef OCEANBASE_STORAGE_BLOCKSSTABLE_BLOCK_WRITER_CONCURRENT_GUARD_H_
#define OCEANBASE_STORAGE_BLOCKSSTABLE_BLOCK_WRITER_CONCURRENT_GUARD_H_

#include <new>
#include "lib/oblog/ob_log.h"
#include "lib/ob_abort.h"

namespace oceanbase
{
namespace blocksstable
{

class ObBlockWriterConcurrentGuard final
{
public:
  [[nodiscard]] explicit ObBlockWriterConcurrentGuard (volatile bool &lock);
  ~ObBlockWriterConcurrentGuard();
private:
  // disallow copy
  ObBlockWriterConcurrentGuard(const ObBlockWriterConcurrentGuard &other);
  ObBlockWriterConcurrentGuard &operator=(const ObBlockWriterConcurrentGuard &other);
  // disallow new
  void *operator new(std::size_t size);
  void *operator new(std::size_t size, const std::nothrow_t &nothrow_constant) throw();
  void *operator new(std::size_t size, void *ptr) throw();
  OB_NOINLINE void on_error();
private:
  // data members
  volatile bool &lock_;
  int ret_;
};

}//end namespace blocksstable
}//end namespace oceanbase
#endif
