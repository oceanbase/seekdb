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
