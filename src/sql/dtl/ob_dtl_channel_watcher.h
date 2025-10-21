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

#ifndef OB_DTL_CHANNEL_WATCHER_H
#define OB_DTL_CHANNEL_WATCHER_H

#include <cstdint>

namespace oceanbase {
namespace sql {
namespace dtl {

class ObDtlChannel;

// Watcher
class ObDtlChannelWatcher
{
public:
  virtual void notify(ObDtlChannel &chan) = 0;
  virtual void remove_data_list(ObDtlChannel *chan, bool force = false) = 0;
  virtual void add_last_data_list(ObDtlChannel *ch) = 0;
  virtual void set_first_no_data(ObDtlChannel *ch) = 0;
};

}  // dtl
}  // sql
}  // oceanbase

#endif /* OB_DTL_CHANNEL_WATCHER_H */
