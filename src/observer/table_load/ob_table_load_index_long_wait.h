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

#ifndef OB_TABLE_LOAD_INDEX_LONG_WAIT_H_
#define OB_TABLE_LOAD_INDEX_LONG_WAIT_H_

#include "lib/utility/utility.h"

namespace oceanbase
{
namespace observer
{

class ObTableLoadIndexLongWait
{
public:
  ObTableLoadIndexLongWait(int64_t wait_us, int64_t max_wait_us)
    : wait_us_(wait_us), max_wait_us_(max_wait_us) {}
  virtual ~ObTableLoadIndexLongWait() {}

  void wait()
  {
    if (wait_us_ < max_wait_us_) {
      ob_usleep(wait_us_);
      wait_us_ = 2 * wait_us_;
    } else {
      ob_usleep(max_wait_us_);
    }
  }

private:
  DISALLOW_COPY_AND_ASSIGN(ObTableLoadIndexLongWait);

private:
  // data members
  int64_t wait_us_;
  int64_t max_wait_us_;
};

}
}

#endif /* OB_TABLE_LOAD_INDEX_LONG_WAIT_H_ */


