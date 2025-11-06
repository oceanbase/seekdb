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

#ifndef OCEANBASE_FRAME_OB_SQL_PROCESSOR_H_
#define OCEANBASE_FRAME_OB_SQL_PROCESSOR_H_

#include "rpc/frame/ob_req_processor.h"

namespace oceanbase
{

namespace rpc
{

namespace frame
{
class ObSqlProcessor: public ObReqProcessor
{
public:
  ObSqlProcessor() {}
  virtual ~ObSqlProcessor() {}

  int run();
  virtual common::ObAddr get_peer() const;
  virtual int process() = 0;
protected:
  virtual int setup_packet_sender() = 0;
  virtual int deserialize() = 0;

  virtual int before_process() = 0;
  virtual int after_process(int error_code) = 0;

  virtual void cleanup() = 0;

  virtual int response(const int retcode) = 0;

private:
  DISALLOW_COPY_AND_ASSIGN(ObSqlProcessor);
};
} // end of namespace frame
} // end of namespace rpc
} // end of namespace oceanbase

#endif /* OCEANBASE_FRAME_OB_SQL_PROCESSOR_H_ */
