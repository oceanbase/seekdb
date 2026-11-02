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

#ifdef WAIT_CLASS_DEF
WAIT_CLASS_DEF(OTHER, "OTHER", 100)
WAIT_CLASS_DEF(APPLICATION, "APPLICATION", 101)
WAIT_CLASS_DEF(CONFIGURATION, "CONFIGURATION", 102)
WAIT_CLASS_DEF(ADMINISTRATIVE, "ADMINISTRATIVE", 103)
WAIT_CLASS_DEF(CONCURRENCY, "CONCURRENCY", 104)
WAIT_CLASS_DEF(COMMIT, "COMMIT", 105)
WAIT_CLASS_DEF(IDLE, "IDLE", 106)
WAIT_CLASS_DEF(NETWORK, "NETWORK", 107)
WAIT_CLASS_DEF(USER_IO, "USER_IO", 108)
WAIT_CLASS_DEF(SYSTEM_IO, "SYSTEM_IO", 109)
WAIT_CLASS_DEF(SCHEDULER, "SCHEDULER", 110)
WAIT_CLASS_DEF(CLUSTER, "CLUSTER", 111)
WAIT_CLASS_DEF(QUEUEING, "QUEUEING", 112)

#endif

#ifndef OB_WAIT_CLASS_H_
#define OB_WAIT_CLASS_H_
#include "lib/ob_define.h"

namespace oceanbase
{
namespace common
{

static const int64_t MAX_WAIT_CLASS_NAME_LENGTH = 64;

struct ObWaitClassIds
{
  enum ObWaitClassIdEnum
  {
#define WAIT_CLASS_DEF(name, wait_class, wait_class_id) name,
#include "lib/wait_event/ob_wait_class.h"
#undef WAIT_CLASS_DEF
  };
};

struct ObWaitClass
{
  char wait_class_[MAX_WAIT_CLASS_NAME_LENGTH];
  int64_t wait_class_id_;
};

extern const ObWaitClass OB_WAIT_CLASSES[];

}
}


#endif /* OB_WAIT_CLASS_H_ */
