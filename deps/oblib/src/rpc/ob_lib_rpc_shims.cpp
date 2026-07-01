// Minimal lib-level shims: ASH name helpers (avoid oblib/ash depending on rpc/sql)
#include "rpc/obmysql/ob_mysql_packet.h"
namespace oceanbase
{
const char *ob_ash_mysql_cmd_name(int32_t mysql_cmd)
{
  return obmysql::ObMySQLPacket::get_mysql_cmd_name(static_cast<obmysql::ObMySQLCmd>(mysql_cmd));
}
const char *ob_ash_rpc_pcode_name(int64_t pcode)
{
  UNUSED(pcode);
  return "RPC";  // RPC pcode registry removed along with RPC offlining
}
}
