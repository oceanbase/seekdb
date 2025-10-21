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

#ifndef OCEANBASE_STORAGE_OB_LOB_CONSTANTS_H_
#define OCEANBASE_STORAGE_OB_LOB_CONSTANTS_H_

namespace oceanbase
{
namespace storage
{

class ObLobConstants
{
public:
  static const int64_t LOB_AUX_TABLE_COUNT = 2; // lob aux table count for each table
  static const int64_t LOB_WITH_OUTROW_CTX_SIZE = sizeof(ObLobCommon) + sizeof(ObLobData) + sizeof(ObLobDataOutRowCtx);
  static const int64_t LOB_OUTROW_FULL_SIZE = ObLobLocatorV2::DISK_LOB_OUTROW_FULL_SIZE;
  static const uint64_t LOB_QUERY_RETRY_MAX = 100L; // 100 times
};


}  // end namespace storage
}  // end namespace oceanbase

#endif  // OCEANBASE_STORAGE_OB_LOB_CONSTANTS_H_
