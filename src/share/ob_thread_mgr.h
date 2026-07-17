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

#ifndef OB_THREAD_MGR_H_
#define OB_THREAD_MGR_H_

#include "lib/thread/thread_mgr.h"
#include "share/ob_thread_pool.h"

namespace oceanbase
{
namespace lib {
namespace TGDefIDs {
enum OBTGDefIDEnum
{
  OB_START = TGDefIDs::LIB_END - 1,
#define TG_DEF(id, ...) id,
#include "share/ob_thread_define.h"
#undef TG_DEF
  OB_END,
};
}
} // end of namespace lib
} // end of namespace oceanbase

#endif // OB_THREAD_MGR_H_
