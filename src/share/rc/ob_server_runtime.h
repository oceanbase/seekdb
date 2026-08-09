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

#ifndef OCEANBASE_SHARE_RC_OB_SERVER_RUNTIME_H_
#define OCEANBASE_SHARE_RC_OB_SERVER_RUNTIME_H_

#include "lib/ob_define.h"
#include "lib/ob_errno.h"
#include "lib/utility/ob_macro_utils.h"
#include "lib/worker.h"
#include "lib/ob_running_mode.h"
#include "lib/allocator/ob_malloc.h"
#include "share/ob_server_role.h"

namespace oceanbase
{
namespace common
{
template <typename T> class ObServerObjectPool;
}
namespace storage
{
class ObTableScanIterator;
}
namespace share
{
class ObServerModuleInitCtx;

using ObTableScanIteratorObjPool = common::ObServerObjectPool<storage::ObTableScanIterator>;

// Narrow process-memory seam used by LogService.  The implementation remains
// owned by Observer; the lower module depends only on this Share interface.
class ObIMemstoreRuntime
{
public:
  virtual ~ObIMemstoreRuntime() = default;
  virtual int get_memstore_limit_percentage(int64_t &limit_percent) = 0;
  virtual int set_memstore_threshold() = 0;
};

// Process-wide runtime state for the one local database.  It deliberately has
// no registry, routing key, or per-request switch operation.
class ObServerRuntimeState : public lib::IRunWrapper
{
public:
  ObServerRuntimeState();
  virtual ~ObServerRuntimeState() = default;

  int init();
  void destroy();
  uint64_t id() const override { return common::OB_SERVER_RUNTIME_ID; }

  double max_cpu() const { return max_cpu_; }
  double min_cpu() const { return min_cpu_; }
  void set_max_cpu(const double cpu) { max_cpu_ = cpu; }
  void set_min_cpu(const double cpu) { min_cpu_ = cpu; }
  int64_t set_memory_size(const int64_t memory_size)
  {
    const int64_t old_size = memory_size_;
    memory_size_ = memory_size;
    return old_size;
  }
  int64_t memory_size() const { return memory_size_; }
  bool is_mini_mode() const { return lib::is_mini_mode(); }

  const ObServerModuleInitCtx *get_module_init_ctx() const { return module_init_ctx_; }

  void set_role(const ObServerRole::Role role);
  ObServerRole::Role role() const { return ATOMIC_LOAD(&role_); }
  bool is_primary() const { return share::is_primary_role(role()); }
  bool role_is_invalid() const { return share::is_invalid_role(role()); }

  void set_switchover_epoch(const int64_t switchover_epoch);
  int64_t switchover_epoch() const { return ATOMIC_LOAD(&switchover_epoch_); }

  virtual void on_schema_publish() {}

protected:
  bool inited_;
  ObServerModuleInitCtx *module_init_ctx_;
  ObServerRole::Role role_;
  double max_cpu_;
  double min_cpu_;
  int64_t memory_size_;
  int64_t switchover_epoch_;
};

extern ObServerRuntimeState g_bootstrap_server_runtime;
inline ObServerRuntimeState *g_server_runtime = &g_bootstrap_server_runtime;
inline bool g_server_modules_ready = false;

inline ObServerRuntimeState *server_runtime()
{
  return g_server_runtime;
}

// Process-wide service slots for the single SeekDB runtime.  The Observer
// composition root binds each concrete service after constructing the module
// graph and clears the slots after the graph is destroyed.  Share does not
// know any upper-layer service type: the slot is instantiated only at the
// binding and consuming sites that already own those type dependencies.
template <typename Service>
struct ObServerServiceSlot
{
  inline static Service *service_ = nullptr;
};

template <typename Service>
inline void bind_server_service(Service *service)
{
  ObServerServiceSlot<Service>::service_ = service;
}

template <typename Service>
inline Service *server_service()
{
  return ObServerServiceSlot<Service>::service_;
}

template <typename Service>
inline void unbind_server_service()
{
  ObServerServiceSlot<Service>::service_ = nullptr;
}

template <typename T>
inline common::ObServerObjectPool<T> *server_obj_pool()
{
  return server_service<common::ObServerObjectPool<T>>();
}

template <typename T>
inline T *borrow_server_object()
{
  return server_obj_pool<T>()->borrow_object();
}

template <typename T>
inline void return_server_object(T *object)
{
  server_obj_pool<T>()->return_object(object);
}

inline lib::IRunWrapper *server_run_wrapper()
{
  return g_server_runtime;
}

inline double server_cpu_count()
{
  return g_server_runtime->max_cpu();
}

inline bool server_is_mini_mode()
{
  return g_server_runtime->is_mini_mode();
}

inline ObServerRole::Role server_role()
{
  return g_server_runtime->role();
}

inline void set_server_role(const ObServerRole::Role role)
{
  g_server_runtime->set_role(role);
}

inline bool server_is_primary()
{
  return g_server_runtime->is_primary();
}

inline bool server_role_is_invalid()
{
  return g_server_runtime->role_is_invalid();
}

inline int64_t server_switchover_epoch()
{
  return g_server_runtime->switchover_epoch();
}

inline void set_server_switchover_epoch(const int64_t epoch)
{
  g_server_runtime->set_switchover_epoch(epoch);
}

inline int check_server_runtime_ready()
{
  return (g_server_runtime != &g_bootstrap_server_runtime)
      ? common::OB_SUCCESS
      : common::OB_SERVER_RUNTIME_NOT_READY;
}

#define SERVER_MODULE_SCOPE \
  for (int64_t _server_module_loop = 0; _server_module_loop == 0; ++_server_module_loop) \
    if (OB_LIKELY(::oceanbase::share::g_server_modules_ready))

inline void *server_malloc(const int64_t nbyte, const common::ObMemAttr &attr)
{
  return ob_malloc(nbyte, attr);
}

inline void *server_malloc(const int64_t nbyte, const lib::ObLabel &label)
{
  common::ObMemAttr attr;
  attr.label_ = label;
  return server_malloc(nbyte, attr);
}

inline void server_free(void *ptr)
{
  ob_free(ptr);
}

inline void *server_malloc_align(const int64_t alignment,
                                 const int64_t nbyte,
                                 const common::ObMemAttr &attr)
{
  return ob_malloc_align(alignment, nbyte, attr);
}

inline void *server_malloc_align(const int64_t alignment,
                                 const int64_t nbyte,
                                 const lib::ObLabel &label)
{
  common::ObMemAttr attr;
  attr.label_ = label;
  return server_malloc_align(alignment, nbyte, attr);
}

inline void server_free_align(void *ptr)
{
  ob_free_align(ptr);
}

#define SERVER_NEW(T, label, ...)                                      \
  ({                                                                  \
    T *_server_new_result = nullptr;                                  \
    void *_server_new_buf = ::oceanbase::share::server_malloc(sizeof(T), label); \
    if (OB_NOT_NULL(_server_new_buf)) {                                \
      _server_new_result = new (_server_new_buf) T(__VA_ARGS__);       \
    }                                                                 \
    _server_new_result;                                                \
  })

#define SERVER_DELETE(T, label, ptr)                   \
  do {                                                 \
    UNUSED(label);                                     \
    if (OB_NOT_NULL(ptr)) {                            \
      (ptr)->~T();                                     \
      ::oceanbase::share::server_free(ptr);            \
      (ptr) = nullptr;                                 \
    }                                                  \
  } while (false)

} // namespace share
} // namespace oceanbase

#endif // OCEANBASE_SHARE_RC_OB_SERVER_RUNTIME_H_
