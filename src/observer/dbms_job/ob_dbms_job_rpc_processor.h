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

#ifndef SRC_OBSERVER_DBMS_JOB_RPC_PROCESSOR_H_
#define SRC_OBSERVER_DBMS_JOB_RPC_PROCESSOR_H_

#include "ob_dbms_job_rpc_proxy.h"
#include "observer/ob_server_struct.h"


namespace oceanbase
{
namespace obrpc
{

class ObRpcAPDBMSJobCB
  : public obrpc::ObDBMSJobRpcProxy::AsyncCB<obrpc::OB_RUN_DBMS_JOB>
{
public:
  ObRpcAPDBMSJobCB() {}
  virtual ~ObRpcAPDBMSJobCB() {}

public:
  virtual int process();
  virtual void on_invalid() {}
  virtual void on_timeout()
  {
    SQL_EXE_LOG_RET(WARN, common::OB_TIMEOUT, "run dbms job timeout!");
  }

  rpc::frame::ObReqTransport::AsyncCB *clone(const rpc::frame::SPAlloc &alloc) const
  {
    void *buf = alloc(sizeof(*this));
    rpc::frame::ObReqTransport::AsyncCB *newcb = NULL;
    if (NULL != buf) {
      newcb = new (buf) ObRpcAPDBMSJobCB();
    }
    return newcb;
  }

  virtual void set_args(const Request &arg) { UNUSED(arg); }

private:
  DISALLOW_COPY_AND_ASSIGN(ObRpcAPDBMSJobCB);
};

class ObRpcRunDBMSJobP
  : public obrpc::ObRpcProcessor<obrpc::ObDBMSJobRpcProxy::ObRpc<obrpc::OB_RUN_DBMS_JOB> >
{
public:
  ObRpcRunDBMSJobP(const observer::ObGlobalContext &gctx) : gctx_(gctx) {}

protected:
  int process();

private:
  const observer::ObGlobalContext &gctx_;
};

}
}
#endif /* SRC_OBSERVER_DBMS_JOB_RPC_PROCESSOR_H_ */


