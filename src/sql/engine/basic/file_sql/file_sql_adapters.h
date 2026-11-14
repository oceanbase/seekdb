/*
 * Copyright (c) 2026 OceanBase.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *     http://www.apache.org/licenses/LICENSE-2.0
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#pragma once

// The adapters between the framework interface of member A and the format
// modules of member C.
//
// The framework owns the SQL entry, the switch, the path check and the
// execution operator. A format module only turns an opened file into a
// schema and then into batches of rows. The adapters below are the glue: they
// map the options of the framework onto the options of a format module and
// they translate the result of the module into the interface of the
// framework.
//
// The adapters do not open a path and they do not decide whether a path is
// allowed. The framework does that before it hands over the borrowed handle.

#include "file_sql_reader.h"

namespace oceanbase
{
namespace sql
{
namespace file_sql
{

// Registers the scanners of the format modules that are not part of the
// framework itself. The call is idempotent and it must happen before the
// first scan of a file.
FileStatus register_builtin_file_scanners();

} // namespace file_sql
} // namespace sql
} // namespace oceanbase
