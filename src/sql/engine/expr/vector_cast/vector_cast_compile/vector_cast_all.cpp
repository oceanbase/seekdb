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

namespace oceanbase
{
namespace sql
{
extern void __init_vec_cast_func0();
extern void __init_vec_cast_func1();
extern void __init_vec_cast_func2();
extern void __init_vec_cast_func3();
extern void __init_vec_cast_func4();
extern void __init_vec_cast_func5();
extern void __init_vec_cast_func6();

int __init_all_vec_cast_funcs()
{
  __init_vec_cast_func0();
  __init_vec_cast_func1();
  __init_vec_cast_func2();
  __init_vec_cast_func3();
  __init_vec_cast_func4();
  __init_vec_cast_func5();
  __init_vec_cast_func6();
  return 0;
}
} // end sq
} // end oceanbase
