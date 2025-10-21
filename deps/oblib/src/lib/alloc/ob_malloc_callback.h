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

#ifndef _OB_MALLOC_CALLBACK_H_
#define _OB_MALLOC_CALLBACK_H_

#include "lib/alloc/alloc_struct.h"
#include "lib/coro/co_var.h"
#include "lib/utility/ob_macro_utils.h"

namespace oceanbase
{
namespace lib
{
class ObMallocCallback
{
public:
  ObMallocCallback() : prev_(this), next_(this) {}
  virtual void operator()(const ObMemAttr& attr, int64_t used) = 0;
  ObMallocCallback *prev() const { return prev_; }
  ObMallocCallback *next() const { return next_; }
  void unlink();
  void insert_before(ObMallocCallback *callback);
private:
  ObMallocCallback *prev_;
  ObMallocCallback *next_;
};

class ObDefaultMallocCallback final : public ObMallocCallback
{
public:
  virtual void operator()(const ObMemAttr& attr, int64_t used) override;
};

RLOCAL_EXTERN(ObMallocCallback *, malloc_callback);

class ObMallocCallbackGuard
{
public:
  ObMallocCallbackGuard(ObMallocCallback& cb);
  ~ObMallocCallbackGuard();
  DISABLE_COPY_ASSIGN(ObMallocCallbackGuard);
private:
  ObMallocCallback& callback_;
};

} // end of namespace lib
} // end of namespace oceanbase

#endif /* _OB_MALLOC_CALLBACK_H_ */
