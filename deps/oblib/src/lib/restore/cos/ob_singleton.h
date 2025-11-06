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

#ifndef SRC_LIBRARY_SRC_LIB_OB_SINGLETON_H_
#define SRC_LIBRARY_SRC_LIB_OB_SINGLETON_H_

#include "lib/ob_define.h"
#include "lib/string/ob_string.h"
#include "lib/utility/utility.h"
#include <type_traits>

namespace oceanbase
{
namespace common
{
namespace qcloud_cos
{

// A singleton base class offering an easy way to create singleton.
template <typename T>
class __attribute__ ((visibility ("default"))) ObSingleton
{
public:
  // not thread safe
  static T& get_instance()
  {
    static T instance;
    return instance;
  }

  virtual ~ObSingleton() {}

protected:
  ObSingleton() {}

private:
  ObSingleton(const ObSingleton &);
  ObSingleton& operator=(const ObSingleton&);
};

} // qcloud_cos

} // common
} // oceanbase

#endif
