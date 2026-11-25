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

#include "lib/alloc/ob_iallocator.h"
#include "lib/string/ob_string.h"

namespace oceanbase
{
namespace share
{
// Escape embedded backticks in a DDL identifier by doubling them, so that the
// identifier can be safely wrapped in backticks when generating DDL text.
//
// This mirrors sql::ObSQLUtils::generate_new_name_with_escape_character but is
// kept local to the share module: share is a terminal module and must not
// depend on the upper sql module (see GitHub issue #1469).
//
// On success, dst either aliases src (when no backtick needs escaping) or
// references a NUL-terminated copy allocated from allocator.  A NULL or empty
// src yields an empty dst.
int escape_ddl_identifier(
    common::ObIAllocator &allocator,
    const common::ObString &src,
    common::ObString &dst);
} // namespace share
} // namespace oceanbase
