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

#ifndef OCEANBASE_COMMON_OB_TRANS_CHARACTER_H
#define OCEANBASE_COMMON_OB_TRANS_CHARACTER_H

#ifdef __cplusplus
extern "C"
{
#endif

//never change the value;
typedef enum ObTransCharacter
{
  OB_WITH_CONSTISTENT_SNAPSHOT = 1,
  OB_TRANS_READ_ONLY = 2,
  OB_TRANS_READ_WRITE = 4,
} ObTransCharacter;

#define IS_READ_ONLY(mode) (mode & OB_TRANS_READ_ONLY)
#define IS_READ_WRITE(mode) (mode & OB_TRANS_READ_WRITE)
#define IS_WITH_SNAPSHOT(mode) (mode & OB_WITH_CONSTISTENT_SNAPSHOT)

#ifdef __cplusplus
}
#endif
#endif //OCEANBASE_COMMON_OB_TRANS_CHARACTER_H
