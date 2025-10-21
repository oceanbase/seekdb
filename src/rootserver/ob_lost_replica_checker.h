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

#ifndef OCEANBASE_ROOTSERVER_OB_LOST_REPLICA_CHECKER_H_
#define OCEANBASE_ROOTSERVER_OB_LOST_REPLICA_CHECKER_H_

#include "share/ob_define.h"
#include "lib/thread/ob_reentrant_thread.h"//block_run
#include "rootserver/ob_rs_reentrant_thread.h"



namespace oceanbase
{
namespace share
{
class ObLSTableOperator;
class ObLSInfo;
class ObLSReplica;
namespace schema
{
class ObMultiVersionSchemaService;
}//end namespace schema
}//end namespace share

namespace rootserver
{

class ObLostReplicaChecker :  public ObRsReentrantThread 
{
public:
  ObLostReplicaChecker();
  virtual ~ObLostReplicaChecker();

  int check_lost_replicas();
  virtual void run3() override;
  virtual int blocking_run() {
     BLOCKING_RUN_IMPLEMENT();
  }
  virtual void wakeup();
  virtual void stop();

private:
  int check_lost_replica_by_ls_(const share::ObLSInfo &ls_info);
  // (server lost and not in member list) or (server lost and not in schema)
  int check_lost_replica_(const share::ObLSInfo &ls_info,
                         const share::ObLSReplica &replica,
                         bool &is_lost_replica) const;
  int check_lost_server_(const common::ObAddr &server,
                        bool &is_lost_server) const;
  int check_cancel_();

 private:
  bool inited_;
  share::ObLSTableOperator *lst_operator_;
  share::schema::ObMultiVersionSchemaService *schema_service_;
private:
  DISALLOW_COPY_AND_ASSIGN(ObLostReplicaChecker);
};
}//end namespace rootserver
}//end namespace oceanbase

#endif // OCEANBASE_ROOTSERVER_OB_LOST_REPLICA_CHECKER_H_
