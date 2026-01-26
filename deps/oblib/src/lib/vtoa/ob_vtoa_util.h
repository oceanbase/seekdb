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
 
#ifndef OB_VTOA_UTIL_H
#define OB_VTOA_UTIL_H

#ifdef __linux__
#include <linux/types.h>
#elif defined(__APPLE__)
// macOS doesn't have linux/types.h, provide type definitions
#include <stdint.h>
#include <sys/types.h>
typedef uint32_t __u32;
typedef uint16_t __u16;
// __be32 and __be16 are big-endian types, on macOS (which is little-endian on ARM/x86_64),
// we use regular types and rely on network byte order conversion functions
typedef uint32_t __be32;
typedef uint16_t __be16;
#endif
#include <netinet/in.h>
#include "lib/hash/ob_hashmap.h"
#include <lib/net/ob_addr.h>

namespace oceanbase
{
namespace lib
{
struct vtoa_get_vs4rds;
class ObVTOAUtility
{
public:
  static int get_virtual_addr(const int connfd, bool &is_vip, int64_t &vid, ObAddr &vaddr);

private:
  static const int VTOA_BASE_CTL_VS = 64 + 1024 + 64 + 64 + 64 + 64;
  static const int VTOA_SO_GET_VS = VTOA_BASE_CTL_VS + 1;
  static const int VTOA_SO_GET_VS4RDS = VTOA_BASE_CTL_VS + 2;
  // only support get ipv4 addr for RDS product, support on vtoa 1.x.x and 2.x.x version
  static int get_vip4rds(int sockfd, struct vtoa_get_vs4rds *vs, socklen_t *len);
  ObVTOAUtility() = delete;
};

}  // namespace lib
}  // namespace oceanbase

#endif // OB_VTOA_UTIL_H
