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

#include "ob_malloc_callback.h"

namespace oceanbase
{
namespace lib
{
void ObDefaultMallocCallback::operator()(const ObMemAttr& attr, int64_t used)
{
  UNUSED(attr);
  UNUSED(used);
}

_RLOCAL(ObMallocCallback *, malloc_callback);

void ObMallocCallback::unlink()
{
  prev_->next_ = next_;
  next_->prev_ = prev_;
  prev_ = this;
  next_ = this;
}

void ObMallocCallback::insert_before(ObMallocCallback *callback)
{
  prev_ = callback->prev_;
  next_ = callback;
  prev_->next_ = this;
  next_->prev_ = this;
}

ObMallocCallbackGuard::ObMallocCallbackGuard(ObMallocCallback& cb)
    : callback_(cb)
{
  if (OB_ISNULL(malloc_callback)) {
    malloc_callback = &callback_;
  } else {
    callback_.insert_before(malloc_callback);
  }
}

ObMallocCallbackGuard::~ObMallocCallbackGuard()
{
  if (callback_.next() == &callback_) {
    malloc_callback = nullptr;
  } else {
    callback_.unlink();
  }
}

}
}
