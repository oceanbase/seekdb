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

#ifndef OCEANBASE_SRC_SHARE_SCHEMA_OB_PL_INTEGER_TYPE_H_
#define OCEANBASE_SRC_SHARE_SCHEMA_OB_PL_INTEGER_TYPE_H_

namespace oceanbase
{
namespace pl
{

enum ObPLIntegerType
{
  PL_INTEGER_INVALID = 0,
  PL_PLS_INTEGER,
  PL_BINARY_INTEGER,
  PL_NATURAL,
  PL_NATURALN,
  PL_POSITIVE,
  PL_POSITIVEN,
  PL_SIGNTYPE,
  PL_SIMPLE_INTEGER,
  PL_INTEGER_MAX,
};

}  // namespace pl
}  // namespace oceanbase

#endif /* OCEANBASE_SRC_SHARE_SCHEMA_OB_PL_INTEGER_TYPE_H_ */
