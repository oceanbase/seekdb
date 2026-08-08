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

#ifndef OCEANBASE_SHARE_SCHEMA_OB_CONTEXT_TYPE_H_
#define OCEANBASE_SHARE_SCHEMA_OB_CONTEXT_TYPE_H_

// Schema context vocabulary belongs to Share. It must not be inherited from
// the SQL parser's unrelated ObItemType enumeration.
typedef enum ObContextType
{
  ACCESSED_LOCALLY = 0,
  INITIALIZED_EXTERNALLY = 1,
  ACCESSED_GLOBALLY = 2,
  INITIALIZED_GLOBALLY = 3,
} ObContextType;

#endif // OCEANBASE_SHARE_SCHEMA_OB_CONTEXT_TYPE_H_
