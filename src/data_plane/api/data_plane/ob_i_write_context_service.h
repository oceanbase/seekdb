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

#ifndef OCEANBASE_DATA_PLANE_API_OB_I_WRITE_CONTEXT_SERVICE_H_
#define OCEANBASE_DATA_PLANE_API_OB_I_WRITE_CONTEXT_SERVICE_H_

#include <stdint.h>
#include "data_plane/memtable/ob_write_flag.h"

namespace oceanbase
{
namespace storage
{
}
namespace transaction
{
class ObTxDesc;
class ObTxReadSnapshot;
}
namespace data_plane
{

// Move-only lifetime handle for a data-plane write context.  The concrete
// context stays owned by the adapter and is destroyed through its registered
// release function.  native_handle() is a temporary compatibility escape hatch
// for legacy data-plane parameter structs; query code must not interpret it.
class ObWriteContext
{
public:
  typedef void (*ReleaseFn)(void *);

  ObWriteContext() : native_handle_(nullptr), release_(nullptr) {}
  ~ObWriteContext() { reset(); }

  void bind(void *native_handle, ReleaseFn release)
  {
    reset();
    native_handle_ = native_handle;
    release_ = release;
  }

  void reset()
  {
    if (nullptr != native_handle_ && nullptr != release_) {
      release_(native_handle_);
    }
    native_handle_ = nullptr;
    release_ = nullptr;
  }

  void *native_handle() const { return native_handle_; }
  bool is_valid() const { return nullptr != native_handle_; }

private:
  ObWriteContext(const ObWriteContext &) = delete;
  ObWriteContext &operator=(const ObWriteContext &) = delete;

private:
  void *native_handle_;
  ReleaseFn release_;
};

// Isolation seam for write-context acquisition.  The storage-owned argument
// types no longer leak through the interface.  ObWriteFlag remains a legacy
// forward declaration to be replaced by a neutral options value when deepened.
class ObIWriteContextService
{
public:
  virtual ~ObIWriteContextService() {}

  virtual int acquire_write_context(
      const int64_t timeout,
      transaction::ObTxDesc &tx_desc,
      const transaction::ObTxReadSnapshot &snapshot,
      const int16_t branch_id,
      concurrent_control::ObWriteFlag &write_flag,
      ObWriteContext &write_context) = 0;
};

} // namespace data_plane
} // namespace oceanbase

#endif // OCEANBASE_DATA_PLANE_API_OB_I_WRITE_CONTEXT_SERVICE_H_
