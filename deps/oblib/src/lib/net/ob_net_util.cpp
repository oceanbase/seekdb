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

#define USING_LOG_PREFIX LIB

#include "lib/charset/ob_charset.h"
#include "lib/net/ob_net_util.h"

using namespace oceanbase::common;

namespace oceanbase {
namespace obsys {

#ifdef _WIN32

static PIP_ADAPTER_ADDRESSES ob_win32_get_adapters(ULONG family)
{
  ULONG buf_len = 15000;
  PIP_ADAPTER_ADDRESSES addrs = nullptr;
  ULONG rc = ERROR_BUFFER_OVERFLOW;
  for (int tries = 0; tries < 3 && rc == ERROR_BUFFER_OVERFLOW; ++tries) {
    addrs = (PIP_ADAPTER_ADDRESSES)malloc(buf_len);
    if (nullptr == addrs) {
      return nullptr;
    }
    rc = GetAdaptersAddresses(family, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER,
                              nullptr, addrs, &buf_len);
    if (rc == ERROR_BUFFER_OVERFLOW) {
      free(addrs);
      addrs = nullptr;
    }
  }
  if (rc != NO_ERROR) {
    if (addrs) { free(addrs); }
    return nullptr;
  }
  return addrs;
}

static bool ob_win32_match_adapter_name(PIP_ADAPTER_ADDRESSES adapter, const char *dev_name)
{
  if (0 == strcmp(adapter->AdapterName, dev_name)) {
    return true;
  }
  char friendly[256] = {0};
  WideCharToMultiByte(CP_UTF8, 0, adapter->FriendlyName, -1, friendly, sizeof(friendly), nullptr, nullptr);
  return (0 == strcmp(friendly, dev_name));
}

int ObNetUtil::get_local_addr_ipv6(const char *dev_name, char *ipv6, int len,
                                   bool *is_linklocal)
{
  int ret = OB_ERROR;
  if (nullptr == dev_name) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("devname can not be NULL", K(ret));
  } else if (len < INET6_ADDRSTRLEN) {
    ret = OB_BUF_NOT_ENOUGH;
    LOG_WARN("the buffer size cannot be less than INET6_ADDRSTRLEN",
             "INET6_ADDRSTRLEN", INET6_ADDRSTRLEN, K(len), K(ret));
  } else {
    PIP_ADAPTER_ADDRESSES addrs = ob_win32_get_adapters(AF_INET6);
    if (nullptr == addrs) {
      ret = OB_ERR_SYS;
      LOG_WARN("GetAdaptersAddresses failed", K(ret));
    } else {
      int level = -1;
      for (PIP_ADAPTER_ADDRESSES cur = addrs; cur != nullptr; cur = cur->Next) {
        if (!ob_win32_match_adapter_name(cur, dev_name)) {
          continue;
        }
        for (PIP_ADAPTER_UNICAST_ADDRESS ua = cur->FirstUnicastAddress; ua != nullptr; ua = ua->Next) {
          if (ua->Address.lpSockaddr->sa_family != AF_INET6) {
            continue;
          }
          struct sockaddr_in6 *in6 = (struct sockaddr_in6 *)ua->Address.lpSockaddr;
          int cur_level = -1;
          bool linklocal = false;
          if (IN6_IS_ADDR_LOOPBACK(&in6->sin6_addr)) {
            cur_level = 0;
          } else if (IN6_IS_ADDR_LINKLOCAL(&in6->sin6_addr)) {
            cur_level = 1;
            linklocal = true;
          } else if (IN6_IS_ADDR_SITELOCAL(&in6->sin6_addr)) {
            cur_level = 2;
          } else if (IN6_IS_ADDR_V4MAPPED(&in6->sin6_addr)) {
            cur_level = 3;
          } else {
            cur_level = 4;
          }
          if (cur_level > level) {
            if (nullptr == inet_ntop(AF_INET6, &in6->sin6_addr, ipv6, len)) {
              ret = OB_ERR_SYS;
              LOG_WARN("call inet_ntop fail", K(errno), K(ret));
            } else {
              level = cur_level;
              ret = OB_SUCCESS;
              if (nullptr != is_linklocal) {
                *is_linklocal = linklocal;
              }
            }
          }
        }
      }
      if (level == -1 && OB_SUCCESS != ret) {
        ret = OB_INVALID_ARGUMENT;
        LOG_WARN("invalid devname specified by -i", "devname", dev_name, "info",
                 "you may list devnames by shell command: ipconfig");
      }
      free(addrs);
    }
  }
  return ret;
}

int ObNetUtil::get_local_addr_ipv4(const char *dev_name, uint32_t &addr)
{
  int ret = OB_SUCCESS;
  if (nullptr == dev_name) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("devname can not be NULL", K(ret));
  } else {
    PIP_ADAPTER_ADDRESSES addrs = ob_win32_get_adapters(AF_INET);
    if (nullptr == addrs) {
      ret = OB_ERR_SYS;
      LOG_WARN("GetAdaptersAddresses failed", K(ret));
    } else {
      bool has_found = false;
      for (PIP_ADAPTER_ADDRESSES cur = addrs; cur != nullptr && !has_found; cur = cur->Next) {
        if (!ob_win32_match_adapter_name(cur, dev_name)) {
          continue;
        }
        for (PIP_ADAPTER_UNICAST_ADDRESS ua = cur->FirstUnicastAddress; ua != nullptr && !has_found; ua = ua->Next) {
          if (ua->Address.lpSockaddr->sa_family != AF_INET) {
            continue;
          }
          has_found = true;
          struct sockaddr_in *in = (struct sockaddr_in *)ua->Address.lpSockaddr;
          addr = in->sin_addr.s_addr;
        }
      }
      if (!has_found) {
        ret = OB_INVALID_ARGUMENT;
        LOG_WARN("invalid devname specified by -i", "devname", dev_name, "info",
                 "you may list devnames by shell command: ipconfig");
      }
      free(addrs);
    }
  }
  return ret;
}
#else
int ObNetUtil::get_local_addr_ipv6(const char *dev_name, char *ipv6, int len,
                                   bool *is_linklocal)
{
  int ret = OB_ERROR;
  int level = -1; // 0: loopback; 1: linklocal; 2: sitelocal; 3: v4mapped; 4: global
  struct ifaddrs *ifa = nullptr, *ifa_tmp = nullptr;

  if (nullptr == dev_name) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("devname can not be NULL", K(ret));
  } else if (len < INET6_ADDRSTRLEN) {
    ret = OB_BUF_NOT_ENOUGH;
    LOG_WARN("the buffer size cannot be less than INET6_ADDRSTRLEN",
             "INET6_ADDRSTRLEN", INET6_ADDRSTRLEN, K(len), K(ret));
  } else if (-1 == getifaddrs(&ifa)) {
    ret = OB_ERR_SYS;
    LOG_WARN("call getifaddrs fail", K(errno), K(ret));
  } else {
    ifa_tmp = ifa;
    while (ifa_tmp) {
      if (ifa_tmp->ifa_addr &&
          ifa_tmp->ifa_addr->sa_family == AF_INET6 &&
          0 == strcmp(ifa_tmp->ifa_name, dev_name)) {
        struct sockaddr_in6 *in6 = (struct sockaddr_in6 *) ifa_tmp->ifa_addr;
        int cur_level = -1;
        bool linklocal = false;
        if (IN6_IS_ADDR_LOOPBACK(&in6->sin6_addr)) {
          cur_level = 0;
        } else if (IN6_IS_ADDR_LINKLOCAL(&in6->sin6_addr)) {
          cur_level = 1;
          linklocal = true;
        } else if (IN6_IS_ADDR_SITELOCAL(&in6->sin6_addr)) {
          cur_level = 2;
        } else if (IN6_IS_ADDR_V4MAPPED(&in6->sin6_addr)) {
          cur_level = 3;
        } else {
          cur_level = 4;
        }
        if (cur_level > level) {
          if (nullptr == inet_ntop(AF_INET6, &in6->sin6_addr, ipv6, len)) {
            ret = OB_ERR_SYS;
            LOG_WARN("call inet_ntop fail", K(errno), K(ret));
          } else {
            level = cur_level;
            ret = OB_SUCCESS;
            if (nullptr != is_linklocal) {
              *is_linklocal = linklocal;
            }
          }
        }
      } // if end
      ifa_tmp = ifa_tmp->ifa_next;
    } // while end

    // if dev_name is invalid, then level will keep its initial value(-1)
    if (level == -1) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("invalid devname specified by -i", "devname", dev_name, "info",
               "you may list devnames by shell command: ifconfig");
    }
  }

  if (nullptr != ifa) {
    freeifaddrs(ifa);
  }

  return ret;
}

int ObNetUtil::get_local_addr_ipv4(const char *dev_name, uint32_t &addr)
{
  int ret = OB_SUCCESS;
  struct ifaddrs *ifa = nullptr, *ifa_tmp = nullptr;

  if (nullptr == dev_name) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("devname can not be NULL", K(ret));
  } else if (-1 == getifaddrs(&ifa)) {
    ret = OB_ERR_SYS;
    LOG_WARN("call getifaddrs fail", K(errno), K(ret));
  } else {
    ifa_tmp = ifa;
    bool has_found = false;
    while (nullptr != ifa_tmp && !has_found) {
      if (ifa_tmp->ifa_addr &&
          ifa_tmp->ifa_addr->sa_family == AF_INET &&
          0 == strcmp(ifa_tmp->ifa_name, dev_name)) {
        has_found = true;
        struct sockaddr_in *in = (struct sockaddr_in *) ifa_tmp->ifa_addr;
        addr = in->sin_addr.s_addr;
      } // if end
      ifa_tmp = ifa_tmp->ifa_next;
    } // while end

    if (!has_found) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("invalid devname specified by -i", "devname", dev_name, "info",
               "you may list devnames by shell command: ifconfig");
    }
  }

  if (nullptr != ifa) {
    freeifaddrs(ifa);
  }

  return ret;
}
#endif

struct sockaddr_storage* ObNetUtil::make_unix_sockaddr_any(bool is_ipv6,
                                                           int port,
                                                           struct sockaddr_storage *sock_addr)
{
  uint32_t addr_v4 = INADDR_ANY;
  in6_addr addr_v6 = in6addr_any;
  return make_unix_sockaddr(is_ipv6, !is_ipv6 ? (void*)&addr_v4 : (void*)&addr_v6, port, sock_addr);
}

struct sockaddr_storage* ObNetUtil::make_unix_sockaddr(bool is_ipv6, const void *ip, int port,
                                                       struct sockaddr_storage *sock_addr)
{
  if (!is_ipv6) {
    struct sockaddr_in      *sin = (struct sockaddr_in *)sock_addr;
#ifdef __APPLE__
    sin->sin_len = sizeof(struct sockaddr_in);
#endif
    sin->sin_port = (uint16_t)htons((uint16_t)port);
    sin->sin_addr.s_addr = htonl(*(uint32_t*)ip);
    sin->sin_family = AF_INET;
  } else {
    struct sockaddr_in6     *sin = (struct sockaddr_in6 *)sock_addr;
#ifdef __APPLE__
    sin->sin6_len = sizeof(struct sockaddr_in6);
#endif
    sin->sin6_port = (uint16_t)htons((uint16_t)port);
    memcpy(&sin->sin6_addr.s6_addr, ip, sizeof(in6_addr::s6_addr));
    sin->sin6_family = AF_INET6;
  }
  return sock_addr;
}

void ObNetUtil::sockaddr_to_addr(struct sockaddr_storage *sock_addr_s, bool &is_ipv6, void *ip, int &port)
{
#ifdef _WIN32
  struct sockaddr *sock_addr = (struct sockaddr *)sock_addr_s;
#else
  struct sockaddr *sock_addr = (typeof(sock_addr))sock_addr_s;
#endif
  is_ipv6 = AF_INET6 == sock_addr->sa_family;
  if (!is_ipv6) {
    sockaddr_in *addr_in = (sockaddr_in*)sock_addr;
    *(uint32_t*)ip = ntohl(addr_in->sin_addr.s_addr);
    port = ntohs(addr_in->sin_port);
  } else {
    sockaddr_in6 *addr_in6 = (sockaddr_in6 *)sock_addr;
    MEMCPY(ip, &addr_in6->sin6_addr, sizeof(in6_addr::s6_addr));
    port = ntohs(addr_in6->sin6_port);
  }
}

bool ObNetUtil::straddr_to_addr(const char *ip_str, bool &is_ipv6, void *ip)
{
  bool bret = true;
  if (!ip_str) {
    bret = false;
  } else if ('\0' == *ip_str) {
    // empty ip
    *(uint32_t*)ip = 0;
  } else {
    const char *colonp = strchr(ip_str, ':');
    is_ipv6 = colonp != NULL;
    if (!is_ipv6) {
      in_addr in;
      int rc = inet_pton(AF_INET, ip_str, &in);
      if (rc != 1) {
        // wrong ip or error
        bret = false;
      } else {
        *(uint32_t*)ip = ntohl(in.s_addr);
      }
    } else {
      in6_addr in6;
      int rc = inet_pton(AF_INET6, ip_str, &in6);
      if (rc != 1) {
        // wrong ip or error
        bret = false;
      } else {
        memcpy(ip, in6.s6_addr, sizeof(in6_addr::s6_addr));
      }
    }
  }
  return bret;
}


char* ObNetUtil::sockaddr_to_str(struct sockaddr_storage *addr, char *buf, int len)
{
  ObAddr ob_addr;
  ob_addr.from_sockaddr(addr);
  ob_addr.to_string(buf, len);
  return buf;
}





#ifdef _WIN32
int ObNetUtil::get_ifname_by_addr(const char *local_ip, char *if_name, uint64_t if_name_len, bool& has_found)
{
  int ret = OB_SUCCESS;
  struct in_addr ip;
  struct in6_addr ip6;
  int af_type = AF_INET;
  if (1 == inet_pton(AF_INET, local_ip, &ip)) {
  } else if (1 == inet_pton(AF_INET6, local_ip, &ip6)) {
    af_type = AF_INET6;
  } else {
    ret = OB_ERR_SYS;
    LOG_ERROR("call inet_pton failed, maybe the local_ip is invalid",
               KCSTRING(local_ip), K(errno), K(ret));
  }

  if (OB_SUCCESS == ret) {
    PIP_ADAPTER_ADDRESSES addrs = ob_win32_get_adapters(af_type);
    if (nullptr == addrs) {
      ret = OB_ERR_SYS;
      LOG_ERROR("GetAdaptersAddresses failed", K(ret));
    } else {
      for (PIP_ADAPTER_ADDRESSES cur = addrs; cur != nullptr && !has_found; cur = cur->Next) {
        for (PIP_ADAPTER_UNICAST_ADDRESS ua = cur->FirstUnicastAddress; ua != nullptr && !has_found; ua = ua->Next) {
          bool matched = false;
          if (AF_INET == af_type && AF_INET == ua->Address.lpSockaddr->sa_family) {
            matched = (0 == memcmp(&ip, &(((struct sockaddr_in *)ua->Address.lpSockaddr)->sin_addr), sizeof(ip)));
          } else if (AF_INET6 == af_type && AF_INET6 == ua->Address.lpSockaddr->sa_family) {
            matched = (0 == memcmp(&ip6, &(((struct sockaddr_in6 *)ua->Address.lpSockaddr)->sin6_addr), sizeof(ip6)));
          }
          if (matched) {
            has_found = true;
            char friendly[256] = {0};
            WideCharToMultiByte(CP_UTF8, 0, cur->FriendlyName, -1, friendly, sizeof(friendly), nullptr, nullptr);
            if (if_name_len < strlen(friendly) + 1) {
              ret = OB_BUF_NOT_ENOUGH;
              _LOG_ERROR("the buffer is not enough, need:%lu, have:%lu, ret:%d",
                         (unsigned long)(strlen(friendly) + 1), (unsigned long)if_name_len, ret);
            } else {
              snprintf(if_name, (size_t)if_name_len, "%s", friendly);
            }
          }
        }
      }
      if (!has_found) {
        LOG_WARN("can not find ifname by local ip", KCSTRING(local_ip));
      }
      free(addrs);
    }
  }
  return ret;
}
#else
int ObNetUtil::get_ifname_by_addr(const char *local_ip, char *if_name, uint64_t if_name_len, bool& has_found)
{
  int ret = OB_SUCCESS;
  struct in_addr ip;
  struct in6_addr ip6;
  int af_type = AF_INET;
  if (1 == inet_pton(AF_INET, local_ip, &ip)) {
    // do nothing
  } else if (1 == inet_pton(AF_INET6, local_ip, &ip6)) {
    af_type = AF_INET6;
  } else {
    ret = OB_ERR_SYS;
    LOG_ERROR("call inet_pton failed, maybe the local_ip is invalid",
               KCSTRING(local_ip), K(errno), K(ret));
  }

  if (OB_SUCCESS == ret) {
    struct ifaddrs *ifa_list = nullptr, *ifa = nullptr;
    if (-1 == getifaddrs(&ifa_list)) {
      ret = OB_ERR_SYS;
      LOG_ERROR("call getifaddrs failed", K(errno), K(ret));
    } else {
      for (ifa = ifa_list; nullptr != ifa && !has_found; ifa = ifa->ifa_next) {
        if (nullptr != ifa->ifa_addr &&
            ((AF_INET == af_type && AF_INET == ifa->ifa_addr->sa_family &&
                 0 == memcmp(&ip, &(((struct sockaddr_in *)ifa->ifa_addr)->sin_addr), sizeof(ip))) ||
             (AF_INET6 == af_type && AF_INET6 == ifa->ifa_addr->sa_family &&
                 0 == memcmp(&ip6, &(((struct sockaddr_in6 *)ifa->ifa_addr)->sin6_addr), sizeof(ip6))))) {
          has_found = true;
          if (if_name_len < strlen(ifa->ifa_name) + 1) {
            ret = OB_BUF_NOT_ENOUGH;
            _LOG_ERROR("the buffer is not enough, need:%lu, have:%lu, ret:%d",
                       strlen(ifa->ifa_name) + 1, if_name_len, ret);
          } else {
            snprintf(if_name, if_name_len, "%s", ifa->ifa_name);
          }
        }
      } // end for
      if (!has_found) {
        LOG_WARN("can not find ifname by local ip", KCSTRING(local_ip));
      }
    }
    if (nullptr != ifa_list) {
      freeifaddrs(ifa_list);
    }
  }
  return ret;
}
#endif

int ObNetUtil::get_int_value(const ObString &str, int64_t &value)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(str.empty())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("str is empty", K(str), K(ret));
  } else {
    static const int32_t MAX_INT64_STORE_LEN = 31;
    char int_buf[MAX_INT64_STORE_LEN + 1];
    int64_t len = std::min(str.length(), MAX_INT64_STORE_LEN);
    MEMCPY(int_buf, str.ptr(), len);
    int_buf[len] = '\0';
    char *end_ptr = NULL;
    value = strtoll(int_buf, &end_ptr, 10);
    if (('\0' != *int_buf) && ('\0' == *end_ptr)) {
      // succ, do nothing
    } else {
      ret = OB_INVALID_DATA;
      LOG_WARN("invalid int value", K(value), K(str), K(ret));
    }
  }
  return ret;
}

bool ObNetUtil::calc_ip(const ObString &host_ip, ObAddr &addr)
{
  return addr.set_ip_addr(host_ip, FAKE_PORT);
}

bool ObNetUtil::calc_ip_mask(const ObString &host_name, ObAddr &host, ObAddr &mask)
{
  bool ret_bool = false;
  int64_t ip_mask_int64 = 0;
  ObString host_ip_mask = host_name;
  ObString ip = host_ip_mask.split_on('/');
  if (OB_UNLIKELY(host_ip_mask.empty())) {
  } else if (calc_ip(ip, host)) {
    if (host_ip_mask.find('.') || host_ip_mask.find(':')) {
      ret_bool = mask.set_ip_addr(host_ip_mask, FAKE_PORT);
    } else {
      if (OB_SUCCESS != (get_int_value(host_ip_mask, ip_mask_int64))) {
        // break
      } else if (mask.as_mask(ip_mask_int64, host.get_version())) {
        ret_bool = true;
      }
    }
  }
  return ret_bool;
}

bool ObNetUtil::is_ip_match(const ObString &client_ip, ObString host_name)
{
  bool ret_bool = false;
  ObAddr client;
  ObAddr host;
  if (OB_UNLIKELY(host_name.empty()) || OB_UNLIKELY(client_ip.empty())) {
    // not match
  } else if (host_name.find('/')) {
    ObAddr mask;
    if (calc_ip(client_ip, client) && calc_ip_mask(host_name, host, mask) &&
        client.get_version() == host.get_version()) {
      ret_bool = (client.as_subnet(mask) == host.as_subnet(mask));
    }
  } else {
    if (calc_ip(host_name, host) && calc_ip(client_ip, client)) {
      ret_bool = (client == host);
    }
  }
  return ret_bool;
}

bool ObNetUtil::is_wild_match(const ObString &client_ip, const ObString &host_name)
{
  return ObCharset::wildcmp(CS_TYPE_UTF8MB4_BIN, client_ip, host_name, 0, '_', '%');
}

bool ObNetUtil::is_match(const ObString &client_ip, const ObString &host_name)
{
  return ObNetUtil::is_wild_match(client_ip, host_name) || ObNetUtil::is_ip_match(client_ip, host_name);
}

bool ObNetUtil::is_in_white_list(const ObString &client_ip, ObString &orig_ip_white_list)
{
  bool ret_bool = false;
  ObAddr client;
  if (orig_ip_white_list.empty() || client_ip.empty()) {
    LOG_WARN_RET(OB_SUCCESS, "ip_white_list or client_ip is emtpy, denied any client", K(client_ip), K(orig_ip_white_list));
  } else if (calc_ip(client_ip, client)) {
    const char COMMA = ',';
    ObString ip_white_list = orig_ip_white_list;
    while (NULL != ip_white_list.find(COMMA) && !ret_bool) {
      ObString invited_ip = ip_white_list.split_on(COMMA).trim();
      if (!invited_ip.empty()) {
        if (ObNetUtil::is_match(client_ip, invited_ip)) {
          ret_bool = true;
        }
        LOG_TRACE("match result", K(ret_bool), K(client_ip), K(invited_ip));
      }
    }
    if (!ret_bool) {
      ip_white_list = ip_white_list.trim();
      if (ip_white_list.empty()) {
        LOG_WARN_RET(OB_SUCCESS, "ip_white_list is emtpy, denied any client", K(client_ip), K(orig_ip_white_list));
      } else if (!ObNetUtil::is_match(client_ip, ip_white_list)) {
        LOG_WARN_RET(OB_SUCCESS, "client ip is not in ip_white_list", K(client_ip), K(orig_ip_white_list));
      } else {
        ret_bool = true;
        LOG_TRACE("match result", K(ret_bool), K(client_ip), K(ip_white_list));
      }
    }
  }
  return ret_bool;
}

}  // namespace obsys
}  // namespace oceanbase

extern "C" {
} /* extern "C" */
