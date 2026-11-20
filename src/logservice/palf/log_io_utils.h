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

#pragma once

namespace oceanbase
{
namespace palf
{

int rename_directory(const char *src_name, const char *dest_name);

class ObBaseDirFunctor
{
public:
  virtual int func(const char *entry_name) = 0;
};

int scan_dir(const char *dir_name, ObBaseDirFunctor &functor);

} // namespace palf
} // namespace oceanbase
