// Throwaway V18: native internal connections keep storage transaction ownership.
#ifndef SEEKDB_NAMESPACE_WORKER_INNER_SQL_PROTOTYPE_H_
#define SEEKDB_NAMESPACE_WORKER_INNER_SQL_PROTOTYPE_H_
#include "observer/namespace_worker_protocol_prototype.h"
#include "common/mysqlclient/ob_isql_client.h"
namespace oceanbase { namespace observer {
class ObInnerSQLConnection;
namespace namespace_worker_prototype {
int inner_call(uint64_t namespace_id, SessionBinding *&binding,
               ObInnerSQLConnection &connection, Frame payload, int64_t &affected);
int inner_read(uint64_t namespace_id, SessionBinding *&binding,
               ObInnerSQLConnection &connection, const common::ObString &sql,
               common::ObISQLClient::ReadResult &result, bool is_user_sql);
} } }
#endif
