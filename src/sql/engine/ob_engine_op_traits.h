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

#ifndef OCEANBASE_ENGINE_OB_ENGINE_OP_TRAITS_H_
#define OCEANBASE_ENGINE_OB_ENGINE_OP_TRAITS_H_

// helper template to get some operator of old && new engine.

namespace oceanbase
{
namespace sql
{
class ObOpSpec;
class ObTableScanSpec;
class ObMVTableScanSpec;
class ObTableInsertSpec;
class ObTableModifySpec;
class ObGranuleIteratorSpec;

template <bool NEW_ENG>
struct ObEngineOpTraits {};

template <>
struct ObEngineOpTraits<true>
{
  typedef ObOpSpec Root;
  typedef ObTableScanSpec TSC;
  typedef ObMVTableScanSpec MV_TSC;
  typedef ObTableModifySpec TableModify;
  typedef ObTableInsertSpec TableInsert;
  typedef ObGranuleIteratorSpec GI;
};

} // end namespace sql
} // end namespace oceanbase

#endif // OCEANBASE_ENGINE_OB_ENGINE_OP_TRAITS_H_
