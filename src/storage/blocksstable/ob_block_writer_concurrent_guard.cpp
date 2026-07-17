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

#include "storage/blocksstable/ob_block_writer_concurrent_guard.h"
namespace oceanbase
{

namespace blocksstable
{


ObBlockWriterConcurrentGuard::ObBlockWriterConcurrentGuard(volatile bool &lock)
    : lock_(lock),
      ret_(OB_SUCCESS)
{
#ifndef OB_BUILD_PACKAGE
  if (OB_UNLIKELY(!ATOMIC_BCAS(&lock_, false, true))) {
    ret_ = OB_ERR_UNEXPECTED;
    COMMON_LOG_RET(ERROR, ret_, "Another thread is concurrently accessing the interfaces of the same object. "
               "The current interface is not thread-safe. Please do not perform concurrent operations "
               "on the same object.", K(lock_), K(&lock_), K_(ret), K(lbt()));
    on_error();
  }
#endif
}

ObBlockWriterConcurrentGuard::~ObBlockWriterConcurrentGuard()
{
#ifndef OB_BUILD_PACKAGE
  if (OB_LIKELY(ret_ == OB_SUCCESS)) {
    if (OB_UNLIKELY(!ATOMIC_BCAS(&lock_, true, false))) {
      ret_ = OB_ERR_UNEXPECTED;
      COMMON_LOG_RET(ERROR, ret_, "This scenario should never happen.",
                 K(lock_), K(&lock_), K(ret), K(lbt()));
      on_error();
    }
  }
#endif
}

void ObBlockWriterConcurrentGuard::on_error()
{
  ob_abort();
}

}//end namespace blocksstable
}//end namespace oceanbase
