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
#include "lib/charset/ob_charset.h"
namespace oceanbase
{
namespace common
{
extern void __init_str_func0();
extern void __init_str_func1();
extern void __init_str_func2();
void __init_all_str_funcs() {
  __init_str_func0();
  __init_str_func1();
  __init_str_func2();
}
} // end common
} // end oceanbase
