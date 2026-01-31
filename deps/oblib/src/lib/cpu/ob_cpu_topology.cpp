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

#define USING_LOG_PREFIX COMMON

#include "lib/cpu/ob_cpu_topology.h"
#include "lib/oblog/ob_log_module.h"
#include "lib/container/ob_bit_set.h"

namespace oceanbase {
namespace common {

int64_t __attribute__((weak)) get_cpu_count()
{
  return get_cpu_num();
}

static bool cpu_haveOSXSAVE();
static bool cpu_have_sse42();
static bool cpu_have_avx();
static bool cpu_have_avx2();
static bool cpu_have_avxf();
static bool cpu_have_avx512bw();
static bool cpu_have_neon();

bool CpuFlagSet::have_flag(const CpuFlag flag) const
{
  return flags_ & (1 << (int)flag);
}

CpuFlagSet::CpuFlagSet() : flags_(0)
{
  int ret = OB_SUCCESS;
  uint64_t flags_from_cpu, flags_from_os;
  init_from_cpu(flags_from_cpu);
  if (OB_FAIL(init_from_os(flags_from_os))) {
    COMMON_LOG(WARN, "failed to init cpu flags from os", K(ret));
    // fork failed or grep failed
    // use flags from cpu
    flags_ = flags_from_cpu;
  } else if (flags_from_cpu != flags_from_os) {
    COMMON_LOG_RET(ERROR,
        OB_ERR_SYS,
        "There is a mismatch between the cpu flags from cpu and those from os, "
        "ISA extension like avx512bw may be not supported by the virtualization setup", K(flags_from_cpu), K(flags_from_os));
    flags_ = flags_from_cpu & flags_from_os;
  } else {
    flags_ = flags_from_cpu;
  }
#define LOG_CPUFLAG(flag) \
  if (have_flag(CpuFlag::flag)) { \
    _LOG_INFO("#flag is supported"); \
  } else { \
    _LOG_WARN("#flag is not supported"); \
  }
  LOG_CPUFLAG(SSE4_2)
  LOG_CPUFLAG(AVX)
  LOG_CPUFLAG(AVX2)
  LOG_CPUFLAG(AVX512BW)
  LOG_CPUFLAG(NEON)
#undef LOG_CPUFLAG
}

void CpuFlagSet::init_from_cpu(uint64_t& flags)
{
  flags = 0;
#define CPU_HAVE(flag, FLAG)            \
  if (cpu_have_##flag()) {              \
    flags |= (1 << (int)CpuFlag::FLAG); \
  }
  CPU_HAVE(sse42, SSE4_2);
  CPU_HAVE(avx, AVX);
  CPU_HAVE(avx2, AVX2);
  CPU_HAVE(avx512bw, AVX512BW);
  CPU_HAVE(neon, NEON);
#undef CPU_HAVE
}

int CpuFlagSet::init_from_os(uint64_t& flags)
{
  int ret = OB_SUCCESS;
  flags = 0;
#if defined(__linux__)
  const char* const CPU_FLAG_CMDS[(int)CpuFlag::MAX] = {"grep -E ' sse4_2( |$)' /proc/cpuinfo > /dev/null 2>&1",
      "grep -E ' avx( |$)' /proc/cpuinfo > /dev/null 2>&1",
      "grep -E ' avx2( |$)' /proc/cpuinfo > /dev/null 2>&1",
      "grep -E ' avx512bw( |$)' /proc/cpuinfo > /dev/null 2>&1",
      "grep -E ' asimd( |$)' /proc/cpuinfo > /dev/null 2>&1"};
  for (int i = 0; i < (int)CpuFlag::MAX; ++i) {
    int system_ret = system(CPU_FLAG_CMDS[i]);
    if (system_ret != 0) {
      if (-1 != system_ret && 1 == WEXITSTATUS(system_ret)) {
        // not found
        COMMON_LOG(WARN, "cpu flag is not found", K(CPU_FLAG_CMDS[i]));
      } else {
        ret = OB_ERR_SYS;
        _LOG_WARN("system(\"%s\") returns %d", CPU_FLAG_CMDS[i], system_ret);
      }
    } else {
      flags |= (1 << i);
    }
  }
#elif defined(__APPLE__)
  // On macOS, /proc/cpuinfo doesn't exist.
  // We can use sysctl to check for features, but for now we rely on init_from_cpu
  // and just return success here with flags set to a reasonable default or
  // matched with cpu flags to avoid mismatch error in constructor.
  init_from_cpu(flags);
#else
  // For other platforms, also rely on init_from_cpu for now
  init_from_cpu(flags);
#endif
  return ret;
}

#if defined(__x86_64__)
void get_cpuid(int reg[4], int func_id)
{
  __asm__("cpuid\n\t"
           : "=a"(reg[0]), "=b"(reg[1]), "=c"(reg[2]),
             "=d"(reg[3])
           : "a"(func_id), "c"(0));
}
uint64_t our_xgetbv(uint32_t xcr) noexcept
{
  uint32_t eax;
  uint32_t edx;
  __asm__ volatile("xgetbv"
                    : "=a"(eax), "=d"(edx)
                    : "c"(xcr));
  return (static_cast<uint64_t>(edx) << 32) | eax;
}
#endif
bool cpu_haveOSXSAVE()
{
#if defined(__x86_64__)
  int regs[4];
  get_cpuid(regs, 0x1);
  return (regs[2] >> 27) & 1u;
#else
  return false;
#endif
}
bool cpu_have_sse42()
{
#if defined(__x86_64__)
  int regs[4];
  get_cpuid(regs, 0x1);
  return regs[2] >> 20 & 1;
#else
  return false;
#endif
}
bool cpu_have_avx()
{
#if defined(__x86_64__)
  int regs[4];
  get_cpuid(regs, 0x1);
  return cpu_haveOSXSAVE() && ((our_xgetbv(0) & 6u) == 6u) && (regs[2] >> 28 & 1);
#else
  return false;
#endif
}
bool cpu_have_avx2()
{
#if defined(__x86_64__)
  int regs[4];
  get_cpuid(regs, 0x7); 
  return cpu_have_avx() && (regs[1] >> 5 & 1);
#else
  return false;
#endif
}
bool cpu_have_avx512f()
{
#if defined(__x86_64__)
  int regs[4];
  get_cpuid(regs, 0x7); 
  return regs[1] >> 16 & 1;
#else
  return false;
#endif
}
bool cpu_have_avx512bw()
{
#if defined(__x86_64__)
  int regs[4];
  get_cpuid(regs, 0x7); 
  return cpu_have_avx512f() && (regs[1] >> 30 & 1);
#else
  return false;
#endif
}

bool cpu_have_neon()
{
#if defined(__aarch64__) || defined(__ARM_NEON)
  return true;
#else
  return false;
#endif
}


} // common
} // oceanbase

