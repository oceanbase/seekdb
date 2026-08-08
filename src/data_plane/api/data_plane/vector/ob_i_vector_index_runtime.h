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

#ifndef OCEANBASE_DATA_PLANE_VECTOR_OB_I_VECTOR_INDEX_RUNTIME_H_
#define OCEANBASE_DATA_PLANE_VECTOR_OB_I_VECTOR_INDEX_RUNTIME_H_

#include <stdint.h>

namespace oceanbase
{
namespace logservice
{
class ObIReplaySubHandler;
class ObICheckpointSubHandler;
class ObILocalLogHandler;
}
namespace data_plane
{

constexpr int64_t OB_VECTOR_INDEX_SNAPSHOT_KEY_LENGTH = 256;

// Storage owns LS lifecycle and log-handler registration, while Observer owns
// the vector-index implementation.  This interface is the seam between them.
class ObIVectorIndexLogHandler
{
public:
  virtual ~ObIVectorIndexLogHandler() = default;
  virtual logservice::ObIReplaySubHandler &replay_handler() = 0;
  virtual logservice::ObICheckpointSubHandler &checkpoint_handler() = 0;
  virtual logservice::ObILocalLogHandler &local_handler() = 0;
};

class ObIVectorIndexScheduler : public ObIVectorIndexLogHandler
{
public:
  virtual ~ObIVectorIndexScheduler() = default;
  virtual void stop() = 0;
};

// Run and wait for the Observer-owned manual maintenance tasks associated
// with a vector index. Storage provides only the index id and intent.
int process_vector_index_embedding_task(int64_t index_table_id);
int process_vector_index_optimization_task(int64_t index_table_id);

} // namespace data_plane
} // namespace oceanbase

#endif // OCEANBASE_DATA_PLANE_VECTOR_OB_I_VECTOR_INDEX_RUNTIME_H_
