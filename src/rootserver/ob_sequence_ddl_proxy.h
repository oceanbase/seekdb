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

#ifndef __OB_SHARE_SEQUENCE_SEQUENCE_DDL_PROXY_H__
#define __OB_SHARE_SEQUENCE_SEQUENCE_DDL_PROXY_H__

#include "lib/utility/ob_macro_utils.h"
#include "lib/container/ob_bit_set.h"

namespace oceanbase
{
namespace common
{
class ObMySQLProxy;
class ObMySQLTransaction;
}
namespace share
{
namespace schema
{
class ObSchemaGetterGuard;
class ObSequenceSchema;
class ObMultiVersionSchemaService;
}

enum ObSeqActionType {
  FROM_TABLE_DDL,
  FROM_SEQUENCE_DDL
};

class ObSequenceDDLProxy
{
public:
  ObSequenceDDLProxy(share::schema::ObMultiVersionSchemaService &schema_service);
  virtual ~ObSequenceDDLProxy();
  int create_sequence_without_bitset(share::schema::ObSequenceSchema &seq_schema,
                                     common::ObMySQLTransaction &trans,
                                     share::schema::ObSchemaGetterGuard &schema_guard,
                                     const common::ObString *ddl_stmt_str);
  int create_sequence(share::schema::ObSequenceSchema &seq_schema,
                      const common::ObBitSet<> &opt_bitset,
                      common::ObMySQLTransaction &trans,
                      share::schema::ObSchemaGetterGuard &schema_guard,
                      const common::ObString *ddl_stmt_str);
  // Notice that, offline ddl and truncate operation, sequence object is inherited from origin one.
  // And need_sync_seq_val is used to judge whether to synchronize origin next value of sequence to the inherited one.
  int inner_create_sequence(share::schema::ObSequenceSchema &seq_schema,
                            common::ObMySQLTransaction &trans,
                            share::schema::ObSchemaGetterGuard &schema_guard,
                            const common::ObString *ddl_stmt_str,
                            const bool need_sync_seq_val);
  int alter_sequence(share::schema::ObSequenceSchema &seq_schema,
                     const common::ObBitSet<> &opt_bitset,
                     common::ObMySQLTransaction &trans,
                     share::schema::ObSchemaGetterGuard &schema_guard,
                     const common::ObString *ddl_stmt_str,
                     ObSeqActionType seq_action_type);
  int drop_sequence(share::schema::ObSequenceSchema &seq_schema,
                    common::ObMySQLTransaction &trans,
                    share::schema::ObSchemaGetterGuard &schema_guard,
                    const common::ObString *ddl_stmt_str,
                    ObSeqActionType seq_action_type);
  int rename_sequence(share::schema::ObSequenceSchema &seq_schema,
                      common::ObMySQLTransaction &trans,
                      const common::ObString *ddl_stmt_str);
private:
  /* functions */
  /* variables */
  DISALLOW_COPY_AND_ASSIGN(ObSequenceDDLProxy);
  share::schema::ObMultiVersionSchemaService &schema_service_;
};
}
}
#endif /* __OB_SHARE_SEQUENCE_SEQUENCE_DDL_PROXY_H__ */
//// end of header file

