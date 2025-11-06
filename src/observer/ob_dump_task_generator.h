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

#ifndef OCEANBASE_OBSERVER_OB_DUMP_TASK_GEN_
#define OCEANBASE_OBSERVER_OB_DUMP_TASK_GEN_

#include <stdint.h>

namespace oceanbase
{
namespace observer
{
class ObDumpTaskGenerator
{
  /*
    1. Write the corresponding instructions to the etc/dump.config file
       dump entity all
       dump entity p_entity='0xffffffffff',slot_idx=1000
       dump chunk all
       dump chunk tenant_id=1,ctx_id=1
       dump chunk p_chunk='0xfffffffff'
       set option leak_mod = 'xxx'
       set option leak_rate = xxx
       dump memory leak
    2. kill -62 pid
    3. See the results in log/memory_meta file
  */
  enum TaskType
  {
    CONTEXT_ALL          = 0,
    CONTEXT              = 1,
    CHUNK_ALL           = 2,
    CHUNK_OF_TENANT_CTX = 3,
    CHUNK               = 4,
    SET_LEAK_MOD        = 5,
    SET_LEAK_RATE       = 6,
    MEMORY_LEAK         = 7,
  };
public:
  static int generate_task_from_file();
private:
  static int read_cmd(char *buf, int64_t len, int64_t &real_size);
  static void  dump_memory_leak();
};

}
}

#endif
