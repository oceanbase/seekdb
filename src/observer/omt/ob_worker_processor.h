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

#ifndef _OCEABASE_OBSERVER_OMT_OB_WORKER_PROCESSOR_H_
#define _OCEABASE_OBSERVER_OMT_OB_WORKER_PROCESSOR_H_

#include "share/ob_define.h"
#include "lib/net/ob_addr.h"

namespace oceanbase
{

namespace rpc { class ObRequest; } // end of namespace rpc
namespace rpc { namespace frame { class ObReqTranslator; } }

namespace omt
{
class ObWorkerProcessor
{
public:
  ObWorkerProcessor(rpc::frame::ObReqTranslator &xlator,
                    const common::ObAddr &myaddr);

  virtual void th_created();
  virtual void th_destroy();

  virtual int process(rpc::ObRequest &req);

public:
  int process_err_test(); 
private:
  int process_one(rpc::ObRequest &req);

private:
  rpc::frame::ObReqTranslator &translator_;
  const common::ObAddr &myaddr_;
}; // end of class ObWorkerProcessor

} // end of namespace omt
} // end of namespace oceanbase


#endif /* _OCEABASE_OBSERVER_OMT_OB_WORKER_PROCESSOR_H_ */
