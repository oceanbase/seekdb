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

#ifndef OB_CONSTRUCT_QUEUE_
#define OB_CONSTRUCT_QUEUE_

#include "lib/task/ob_timer.h"

namespace oceanbase
{
namespace obmysql
{

class ObMySQLRequestManager;

class ObConstructQueueTask : public common::ObTimerTask
{
public:
  ObConstructQueueTask();
  virtual ~ObConstructQueueTask();

  void runTimerTask();
  int init(const ObMySQLRequestManager *request_manager);

private:
  ObMySQLRequestManager *request_manager_;
  bool is_tp_trigger_;
};

} // end of namespace obmysql
} // end of namespace oceanbase
#endif /* OB_CONSTRUCT_QUEUE_ */
