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

#ifdef STAT_CLASS_DEF
STAT_CLASS_DEF(NETWORK, 1)
STAT_CLASS_DEF(QUEUE, 2)
STAT_CLASS_DEF(TRANS, 4)
STAT_CLASS_DEF(SQL, 8)
STAT_CLASS_DEF(CACHE, 16)
STAT_CLASS_DEF(STORAGE, 32)
STAT_CLASS_DEF(RESOURCE, 64)
STAT_CLASS_DEF(DEBUG, 128)
STAT_CLASS_DEF(CLOG, 256)
STAT_CLASS_DEF(ELECT, 512)
STAT_CLASS_DEF(OBSERVER, 1024)
STAT_CLASS_DEF(RS, 2048)
STAT_CLASS_DEF(SYS, 3072)
STAT_CLASS_DEF(TABLEAPI, 4096)
STAT_CLASS_DEF(WR, 8192)
STAT_CLASS_DEF(REDISAPI, 16384)
#endif

#ifndef OB_STAT_CLASS_H_
#define OB_STAT_CLASS_H_
#include "lib/ob_define.h"

namespace oceanbase
{
namespace common
{

static const int64_t MAX_STAT_CLASS_NAME_LENGTH = 64;

struct ObStatClassIds
{
  enum ObStatClassIdEnum
  {
#define STAT_CLASS_DEF(name, num) name = num,
#include "lib/statistic_event/ob_stat_class.h"
#undef STAT_CLASS_DEF
  };
};

struct ObStatClass
{
  char stat_class_[MAX_STAT_CLASS_NAME_LENGTH];
};


static const ObStatClass OB_STAT_CLASSES[] = {
#define STAT_CLASS_DEF(name, num) \
  {#name},
#include "lib/statistic_event/ob_stat_class.h"
#undef STAT_CLASS_DEF
};

}
}


#endif /* OB_WAIT_CLASS_H_ */
