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

#include "common/datum/datum_core.h"

namespace oceanbase
{
namespace sql
{
class ObExpr;
class ObEvalCtx;

// Parameters carry SQL-array addresses with separate metadata, while an
// untyped expression may require a full native Extend payload. No Obj value
// is used to map between those two native datum forms.
int assign_parameter_to_expr(ObEvalCtx &ctx, const ObExpr &expr,
                             const common::ObDatum &value, const common::SqlTypeMeta &meta);
} // namespace sql
} // namespace oceanbase
