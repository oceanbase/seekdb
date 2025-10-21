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

#ifndef SHARE_STORAGE_MULTI_DATA_SOURCE_MDS_TLOCAL_INFO_H
#define SHARE_STORAGE_MULTI_DATA_SOURCE_MDS_TLOCAL_INFO_H

#include "storage/tx/ob_multi_data_source.h"

namespace oceanbase
{
namespace storage
{
namespace mds
{

struct MdsTLocalInfo 
{
  MdsTLocalInfo() : notify_type_(transaction::NotifyType::UNKNOWN) {}
  void reset() { new (this) MdsTLocalInfo(); }
  TO_STRING_KV(K_(notify_type))
  transaction::NotifyType notify_type_;
};

}
}
}
#endif
