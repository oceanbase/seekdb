/**
 * Copyright (c) 2021 OceanBase
 * OceanBase CE is licensed under Mulan PubL v2.
 * You can use this software according to the terms and conditions of the Mulan PubL v2.
 * You may obtain a copy of Mulan PubL v2 at:
 *          http://license.coscl.org.cn/MulanPubL-2.0
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
 * EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
 * MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PubL v2 for more details.
 */

#ifndef OCEANBASE_LIB_GUARD_OB_SCOPE_GUARD_H
#define OCEANBASE_LIB_GUARD_OB_SCOPE_GUARD_H
#include <type_traits>
#include <utility>

namespace oceanbase
{
namespace common
{
// ScopeGuard is a template class that includes the type of the protected resource, as well as the type of the callable function to destruct the resource
template <typename Resource, typename Deleter>
class ScopeGuard
{
public:
  template <typename T>
  ScopeGuard(Resource &p, T &&deleter) : p_(&p), deleter_(std::forward<Deleter>(deleter)) {}// ScopeGuard will allocate a pointer on the stack, pointing to the protected resource
  ScopeGuard(const ScopeGuard &g) = delete;// ScopeGuard is prohibited from being copied
  ScopeGuard(ScopeGuard &&g) : p_(g.p_), deleter_(std::move(g.deleter_)) { g.p_ = nullptr; }// but allows move
  ~ScopeGuard() { if (p_) { deleter_(*p_); p_ = nullptr; } }// When ScopeGuard is destructed, if the resource it points to is still valid, the destructor operation will be called
  Resource &resource() { return *p_; }// Access the protected resource through the resource() method
  Resource &fetch_resource() { // Retrieve the protected resource through fetch_resource()
    Resource *p = p_;
    p_ = nullptr;
    return *p;
  }
  // The following two functions use the SFINAE technique, and the compiler will generate the following two functions only when the protected resource is a pointer type
  template <typename T = Resource, typename std::enable_if<std::is_pointer<T>::value, bool>::type = true>
  typename std::add_lvalue_reference<typename std::remove_pointer<T>::type>::type operator*() { return **p_; }
  template <typename T = Resource, typename std::enable_if<std::is_pointer<T>::value, bool>::type = true>
  Resource operator->() { return *p_; }
private:
  Resource *p_;
  Deleter deleter_;
};
// MAKE_SCOPE macro is used to remove syntax noise when using ScopeGuard
#define MAKE_SCOPE(resource, lambda) \
({\
auto deleter = lambda;\
auto my_guard = ScopeGuard<typename std::remove_reference<decltype(resource)>::type, typename std::remove_reference<decltype(deleter)>::type>(resource, deleter); \
std::move(my_guard);\
})

}// namespace common
}// namespace oceanbase

#endif