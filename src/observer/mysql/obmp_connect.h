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

#ifndef _OBMP_CONNECT_H_
#define _OBMP_CONNECT_H_

#include "rpc/obmysql/ob_login_info.h"
#include "observer/mysql/obmp_base.h"
#include "rpc/obmysql/ob_i_cs_mem_pool.h"

namespace oceanbase
{
namespace sql
{
class ObMultiStmtItem;
class ObSQLSessionInfo;
}
namespace observer
{
struct ObSMConnection;

ObString extract_user_name(const ObString &in);

class AuthSwitchResonseMemPool : public obmysql::ObICSMemPool
{
public:
  explicit AuthSwitchResonseMemPool(ObIAllocator *allocator)
      : allocator_(allocator)
  {}

  virtual ~AuthSwitchResonseMemPool() {}

  void *alloc(int64_t size) override
  {
    return allocator_->alloc(size);
  }
private:
  ObIAllocator *allocator_;
};

class ObMPConnect
    : public ObMPBase
{
public:
  explicit ObMPConnect(const ObGlobalContext &gctx);
  virtual ~ObMPConnect();

protected:
  int process();
  int deserialize();

  int load_privilege_info(sql::ObSQLSessionInfo &session);

private:
  int64_t get_user_id();
  int64_t get_database_id();
  int get_conn_id(uint32_t &conn_id) const;


  int check_client_property(ObSMConnection &conn);
  int init_process_single_stmt(const sql::ObMultiStmtItem &multi_stmt_item,
                               sql::ObSQLSessionInfo &session,
                               bool has_more_result) const;
  int init_connect_process(common::ObString &init_sql,
                           sql::ObSQLSessionInfo &session) const;
  int verify_connection() const;
  int verify_identify(ObSMConnection &conn, sql::ObSQLSessionInfo &session);
  int verify_ip_white_list() const;

  int check_password_expired(share::schema::ObSchemaGetterGuard &schema_guard,
                             sql::ObSQLSessionInfo &session);
  int set_client_version(ObSMConnection &conn);
private:
  DISALLOW_COPY_AND_ASSIGN(ObMPConnect);
  obmysql::ObHandshakeResponse hsr_;
  common::ObString user_name_;
  common::ObString client_ip_;
  common::ObString db_name_;
  char client_ip_buf_[common::MAX_IP_ADDR_LENGTH + 1];
  char user_name_var_[OB_MAX_USER_NAME_BUF_LENGTH];
  char db_name_var_[OB_MAX_DATABASE_NAME_BUF_LENGTH];
  int deser_ret_;
  ObArenaAllocator allocator_;
  AuthSwitchResonseMemPool asr_mem_pool_;
  int32_t client_port_;
}; // end of class ObMPConnect

} // end of namespace observer
} // end of namespace oceanbase

#endif /* _OBMP_CONNECT_H_ */
