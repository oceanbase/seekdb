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
// sync_call / ddl_call / async_call
// Direct dispatch replacing proxy/pcode/handler/packet/frame.
#ifndef OCEANBASE_OBSERVER_OB_SYNC_CALL_H_
#define OCEANBASE_OBSERVER_OB_SYNC_CALL_H_

#include <functional>
#include <mutex>
#include <memory>
#include <atomic>
#include <type_traits>
#include "lib/lock/ob_futex.h"
#include "lib/utility/serialization.h"
#include "lib/utility/ob_macro_utils.h"
#include "lib/worker.h"  // THIS_WORKER (TimeoutGuard)

namespace oceanbase {
namespace ex_rpc {

// MAX_RPC_TIMEOUT (the proxy default applied when a former RPC set no .timeout()).
static const int64_t EX_RPC_DEFAULT_TIMEOUT_US = 9L * 1000 * 1000;

// Defined in ob_ex_rpc.cpp: dispatch fn to the caller's ReqWorker (real
// ObThWorker) carrying timeout_us as the deadline, and WAIT (reproduces the former
// loopback sync RPC). fn captures by reference -- safe because the caller waits, so
// no arg serialization is needed.
int sync_call_internal(int64_t timeout_us, std::function<int()> fn);

template<typename F> inline auto sync_call(int64_t timeout_us, F&& fn) -> decltype(fn()) {
    using R = decltype(fn());
    if constexpr (std::is_void_v<R>) {
        (void)sync_call_internal(timeout_us, [&]() -> int { fn(); return oceanbase::common::OB_SUCCESS; });
    } else {
        return sync_call_internal(timeout_us, [&]() -> int { return fn(); });
    }
}
template<typename F> inline auto sync_call(F&& fn) -> decltype(fn()) {
    return sync_call(EX_RPC_DEFAULT_TIMEOUT_US, std::forward<F>(fn));
}



// === async_call: 3 overloads, 1 internal impl, 1 handle type ===

namespace detail {
template<typename Res> struct HandleStorage { Res result_{}; };
template<> struct HandleStorage<void> {};
}

template<typename Res = void>
class AsyncHandle : detail::HandleStorage<Res> {
public:
    void mark_done(int ret) { ret_ = ret; done_.store(1, std::memory_order_release); futex_wake(reinterpret_cast<int*>(&done_), 1); }
    int wait() { while (done_.load(std::memory_order_acquire) == 0) futex_wait(reinterpret_cast<int*>(&done_), 0, nullptr); return ret_; }

    template<typename T = Res>
    std::enable_if_t<!std::is_void_v<T>, T>& result() { return this->result_; }
    template<typename T = Res>
    std::enable_if_t<!std::is_void_v<T>, const T&> result() const { return this->result_; }

private:
    template<typename R, typename W> friend auto async_call_impl(W&&);
    template<typename F> friend auto async_call(F&&);
    template<typename R, typename A, typename F> friend auto async_call(const A&, F&&);
    std::atomic<int> done_{0};
    int ret_{0};
};

// Deriving from shared_ptr (public non-virtual dtor) is safe HERE because HandleRef
// adds NO extra state and is never deleted polymorphically through a shared_ptr*
// (only value / container semantics) -- so the missing virtual dtor can never bite.
template<typename Res = void>
class HandleRef : public std::shared_ptr<AsyncHandle<Res>> {
public:
    HandleRef() = default;
    HandleRef(std::shared_ptr<AsyncHandle<Res>> p)
        : std::shared_ptr<AsyncHandle<Res>>(std::move(p)) {}
    int64_t to_string(char *, const int64_t) const { return 0; }
};

// Internal: create handle, dispatch, handle lifecycle.
template<typename Res, typename Work>
auto async_call_impl(Work &&work) -> HandleRef<Res> {
    auto h = std::make_shared<AsyncHandle<Res>>();
    int dispatch_ret = async_call_internal([h, work = std::forward<Work>(work)]() mutable {
        if constexpr (std::is_void_v<Res>) {
            int ret = work();
            h->mark_done(ret);
        } else {
            int ret = work(h->result());
            h->mark_done(ret);
        }
    });
    // A rejected dispatch never reaches the worker callback. Complete the
    // handle here so callers waiting for an in-process RPC cannot hang.
    if (oceanbase::common::OB_SUCCESS != dispatch_ret) {
        h->mark_done(dispatch_ret);
    }
    return HandleRef<Res>(std::move(h));
}

// 1. async_call(fn) -- no arg, wait only
template<typename Fn>
auto async_call(Fn &&fn) -> HandleRef<void> {
    return async_call_impl<void>([fn = std::forward<Fn>(fn)]() mutable -> int {
        if constexpr (std::is_void_v<decltype(fn())>) { fn(); return 0; }
        else { return fn(); }
    });
}

// 2. async_call<Res>(fn) -- no arg, wait + result
template<typename Res, typename Fn>
auto async_call(Fn &&fn) -> HandleRef<Res> {
    return async_call_impl<Res>([fn = std::forward<Fn>(fn)](Res& res) mutable -> int {
        if constexpr (std::is_void_v<decltype(fn(res))>) { fn(res); return 0; }
        else { return fn(res); }
    });
}

// 3. async_call<Res>(arg, fn) -- serialized arg, wait [+ result if Res != void]
//    - Res == void: fn(req)        (no result)
//    - Res != void: fn(req, res)   (fills result)
// arg is serialized into an owned buffer and decoded on the worker, so it is
// lifecycle-safe even when Arg holds shallow references (e.g. ObString/ObIArray).
template<typename Res, typename Arg, typename Fn>
auto async_call(const Arg &arg, Fn &&fn) -> HandleRef<Res> {
    int64_t len = common::serialization::encoded_length(arg);
    auto buf = std::make_shared<char[]>(len + common::OB_MALLOC_BIG_BLOCK_SIZE);
    int64_t pos = 0;
    common::serialization::encode(buf.get(), len + common::OB_MALLOC_BIG_BLOCK_SIZE, pos, arg);
    if constexpr (std::is_void_v<Res>) {
        return async_call_impl<void>([buf = std::move(buf), len, fn = std::forward<Fn>(fn)]() mutable -> int {
            Arg req;
            int64_t p = 0;
            int ret = common::serialization::decode(buf.get(), len, p, req);
            if (0 != ret) return ret;
            if constexpr (std::is_void_v<decltype(fn(req))>) { fn(req); return 0; }
            else { return fn(req); }
        });
    } else {
        return async_call_impl<Res>([buf = std::move(buf), len, fn = std::forward<Fn>(fn)](Res& res) mutable -> int {
            Arg req;
            int64_t p = 0;
            int ret = common::serialization::decode(buf.get(), len, p, req);
            if (0 != ret) return ret;
            if constexpr (std::is_void_v<decltype(fn(req, res))>) { fn(req, res); return 0; }
            else { return fn(req, res); }
        });
    }
}

// Dispatch to worker thread.
int async_call_internal(std::function<void()> fn);

// Backward compat.
inline int async_call(std::function<void()> fn) { return async_call_internal(std::move(fn)); }

} // namespace ex_rpc
} // namespace oceanbase
#endif
