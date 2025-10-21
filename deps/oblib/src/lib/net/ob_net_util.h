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

#ifndef OCEANBASE_NET_UTIL_H_
#define OCEANBASE_NET_UTIL_H_

#include <stdint.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netinet/tcp.h>
#include <ifaddrs.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <netdb.h>
#include <sys/time.h>
#include <net/if.h>
#include <inttypes.h>
#include <sys/types.h>
#include <linux/unistd.h>
#include <string>

#include "lib/string/ob_string.h"
#include "lib/net/ob_addr.h"

namespace oceanbase
{
namespace obsys
{

class ObNetUtil
{
private:
  static const uint32_t FAKE_PORT = 0;
  static int get_int_value(const common::ObString &str, int64_t &value);
  static bool calc_ip(const common::ObString &host_ip, common::ObAddr &addr);
  static bool calc_ip_mask(const common::ObString &host_name, common::ObAddr &host, common::ObAddr &mask);
  static bool is_ip_match(const common::ObString &client_ip, common::ObString host_name);
  static bool is_wild_match(const common::ObString &client_ip, const common::ObString &host_name);
public:
  static int get_local_addr_ipv6(const char *dev_name, char *ipv6, int len, bool *is_linklocal = nullptr);
  static int get_local_addr_ipv4(const char *dev_name, uint32_t &addr);
  static std::string addr_to_string(uint64_t ipport);
  static uint64_t ip_to_addr(uint32_t ip, int port);
  // get ipv4 by hostname, no need free the returned value
  static int get_ifname_by_addr(const char *local_ip, char *if_name, uint64_t if_name_len, bool& has_found);
  static struct sockaddr_storage* make_unix_sockaddr_any(bool is_ipv6, int port, struct sockaddr_storage *sock_addr);
  static struct sockaddr_storage* make_unix_sockaddr(bool is_ipv6, const void *ip, int port, struct sockaddr_storage *sock_addr);
  static void sockaddr_to_addr(struct sockaddr_storage *sock_addr, bool &is_ipv6, void *ip, int &port);
  static bool straddr_to_addr(const char *ip_str, bool &is_ipv6, void *ip);
  static char *sockaddr_to_str(struct sockaddr_storage *sock_addr, char *buf, int len);

  static bool is_match(const common::ObString &client_ip, const common::ObString &host_name);
  static bool is_in_white_list(const common::ObString &client_ip, common::ObString &orig_ip_white_list);
};
}  // namespace obsys
}  // namespace oceanbase

extern "C" {
} /* extern "C" */
#endif
