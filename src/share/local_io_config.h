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

namespace oceanbase::share
{
enum class LocalIOBackend { WORKER, NATIVE_AIO };

struct LocalIOConfig
{
  bool direct_io = false;
  LocalIOBackend backend = LocalIOBackend::WORKER;

  int file_flags(int flags) const;
};

// Return zero or a positive system errno. Only unsupported capabilities fall
// back; storage, permission and resource errors must fail device initialization.
int probe_local_io(const char *directory, LocalIOConfig &config);
}
