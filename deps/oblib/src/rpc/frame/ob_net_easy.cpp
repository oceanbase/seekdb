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

#define USING_LOG_PREFIX RPC_FRAME

#include "rpc/frame/ob_net_easy.h"
#include "rpc/obrpc/ob_poc_rpc_server.h"

#include "lib/utility/utility.h"
#include "lib/thread/ob_thread_name.h"

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <unistd.h>

#ifndef EASY_NUM_LEN
#define EASY_NUM_LEN 32
#endif

#ifndef easy_align_ptr
#define easy_align_ptr(ptr, align) ((ptr + align - 1) & ~(align - 1))
#endif

#ifndef easy_min
#define easy_min(a, b) ((a) < (b) ? (a) : (b))
#endif

#ifndef easy_memcpy
#define easy_memcpy(dst, src, len) memcpy(dst, src, len)
#endif

#ifndef EASY_POOL_LOCK
#define EASY_POOL_LOCK(pool) 
#endif

#ifndef EASY_POOL_UNLOCK
#define EASY_POOL_UNLOCK(pool)
#endif

#ifndef easy_pool_alloc_block
#define easy_pool_alloc_block(pool, size) (malloc(size))
#endif

#ifndef easy_pool_alloc_large
#define easy_pool_alloc_large(pool, large, size) (malloc(size))
#endif

extern "C" {

uint64_t easy_fnv_hash(const char *str) {
    uint32_t offset_basis = 0x811C9DC5;
    uint32_t prime = 0x01000193;
    uint32_t fnv1 = offset_basis;
    uint32_t fnv1a = offset_basis;
    int i = strlen(str);
    while (i-- && str[i] != '/') {
        fnv1 *= prime;
        fnv1 ^= str[i];
        fnv1a ^= str[i];
        fnv1a *= prime;
    }
    return (uint64_t)fnv1 << 32 | fnv1a;
}
easy_log_level_t easy_log_level = EASY_LOG_INFO;

easy_log_format_pt easy_log_format = easy_log_format_default;

void easy_log_format_default(int level, const char *file, int line, const char *function,
                             const char *fmt, ...) {
}

static char *easy_sprintf_num(char *buf, char *last, uint64_t ui64, char zero, int hexadecimal, int width, int sign);

__thread easy_baseth_t *easy_baseth_self;

__thread mod_stat_t* easy_cur_mod_stat;

char *easy_inet_addr_to_str(easy_addr_t *addr, char *buffer, int len)
{
    unsigned char *b;

    if (addr->family == AF_INET6) {
        char tmp[INET6_ADDRSTRLEN];

        if (inet_ntop(AF_INET6, addr->u.addr6, tmp, INET6_ADDRSTRLEN) != NULL) {
            if (addr->port) {
                lnprintf(buffer, len, "[%s]:%d", tmp, ntohs(addr->port));
            } else {
                lnprintf(buffer, len, "%s", tmp);
            }
        }
    } else {
        b = (unsigned char *) &addr->u.addr;

        if (addr->port)
            lnprintf(buffer, len, "%d.%d.%d.%d:%d", b[0], b[1], b[2], b[3], ntohs(addr->port));
        else
            lnprintf(buffer, len, "%d.%d.%d.%d", b[0], b[1], b[2], b[3]);
    }

    return buffer;
}

void easy_inet_atoe(void *a, easy_addr_t *e)
{
    struct sockaddr_storage *addr = (struct sockaddr_storage *) a;

    memset(e, 0, sizeof(easy_addr_t));

    if (addr->ss_family == AF_INET6) {
        struct sockaddr_in6 *s = (struct sockaddr_in6 *)a;
        e->family = AF_INET6;
        e->port = s->sin6_port;
        memcpy(e->u.addr6, &s->sin6_addr, sizeof(e->u.addr6));
    } else {
        struct sockaddr_in *s = (struct sockaddr_in *)a;
        e->family = AF_INET;
        e->port = s->sin_port;
        e->u.addr = s->sin_addr.s_addr;
    }
}

easy_addr_t* easy_connection_get_local_addr(easy_connection_t* c, easy_addr_t* eaddr)
{
    socklen_t len;
    struct sockaddr_storage addr;
    len = sizeof(addr);
    memset(eaddr, 0, sizeof(*eaddr));

    if (getsockname(c->fd, (struct sockaddr *) &addr, &len) == 0) {
        easy_inet_atoe(&addr, eaddr);
    }
    return eaddr;
}

char *easy_connection_str(easy_connection_t *c)
{
    static __thread char buffer[192];
    char local_addr[64], dest_addr[64];
    easy_addr_t local_eaddr;

    if (!c) {
        return const_cast<char*>("null");
    }

    easy_connection_get_local_addr(c, &local_eaddr);
    lnprintf(buffer, 192, "%s_%s_%d_%p tp=%d t=%ld-%ld s=%d r=%d io=%ld/%ld sq=%ld",
        easy_inet_addr_to_str(&local_eaddr, local_addr, sizeof(local_addr)),
        easy_inet_addr_to_str(&c->addr, dest_addr, sizeof(dest_addr)), c->fd, c, c->type,
        (int64_t)(1000000LL * c->start_time), (int64_t)(1000000LL * c->last_time),
        c->status, c->doing_request_count, c->recv_bytes, c->send_bytes, c->ack_bytes);
    return buffer;
}

#if defined(__x86_64__)
uint64_t get_cpufreq_khz()
{
    char line[256];
    FILE *stream = NULL;
    double freq_ghz = 0.0;
    double freq_mhz = 0.0;
    uint64_t freq_khz = 0;
    const char *freq_str = NULL;

    stream = fopen("/proc/cpuinfo", "r");
    if (NULL == stream) {
    } else {
        while (fgets(line, sizeof(line), stream)) {
            if ((freq_str = strstr(line, "CPU @ ")) != NULL
                 && strstr(freq_str, "GHz") != NULL) {
                sscanf(freq_str, "CPU @ %lfGHz", &freq_ghz);
                freq_khz = (uint64_t)(freq_ghz * 1000UL * 1000UL);
                break;
            }
        }
        if (0 == freq_khz) {
            //The name model is not in a format like "Intel(R) Xeon(R) Platinum 8163 CPU @ 2.50GHz"
            //fetch the freq_khz from cpu MHz
            rewind(stream);
            while (fgets(line, sizeof(line), stream)) {
                if (sscanf(line, "cpu MHz\t: %lf", &freq_mhz) == 1) {
                    freq_khz = (uint64_t)(freq_mhz * 1000UL);
                    break;
                }
            }
        }
        fclose(stream);
    }

    return freq_khz;
}
#else
uint64_t get_cpufreq_khz(void)
{
    uint64_t timer_frequency;
    asm volatile("mrs %0, cntfrq_el0":"=r"(timer_frequency));
    return timer_frequency / 1000;
}
#endif

char *easy_string_toupper(char *str)
{
    char *p = str;

    while (*p) {
        if ((*p) >= 'a' && (*p) <= 'z')
            (*p) -= 32;

        p ++;
    }

    return str;
}

int easy_vsnprintf(char *buf, size_t size, const char *fmt, va_list args)
{
    char *p, zero;
    double f, scale;
    int64_t i64;
    uint64_t ui64;
    int width, sign, hex, frac_width, slen, width_sign;
    char *last, *start, *fstart;
    char length_modifier;

    start = buf;
    last = buf + size - 1;

    while (*fmt && buf < last) {

        if (*fmt == '%') {

            zero = (char)((*++fmt == '0') ? '0' : ' ');
            width_sign = ((*fmt == '-') ? (fmt++, -1) : 1);
            width = 0;
            sign = 1;
            hex = 0;
            frac_width = 6;
            slen = -1;
            length_modifier = '0';
            fstart = buf;

            while (*fmt >= '0' && *fmt <= '9') {
                width = width * 10 + *fmt++ - '0';
            }

            width *= width_sign;

            // width
            switch (*fmt) {
            case '.':
                fmt++;

                if (*fmt != '*') {
                    frac_width = 0;

                    while (*fmt >= '0' && *fmt <= '9') {
                        frac_width = frac_width * 10 + *fmt++ - '0';
                    }

                    break;
                }

            case '*':
                slen = va_arg(args, size_t);
                fmt++;
                break;

            case 'l':
                fmt++;
#ifdef _LP64
                length_modifier ++;
#endif

                if (*fmt == 'l') {
                    length_modifier ++;
                    fmt ++;
                }

                break;

            default:
                break;
            }

            // type
            switch (*fmt) {
            case 's':
                p = va_arg(args, char *);

                if (slen < 0) {
                    slen = last - buf;
                } else {
                    slen = easy_min(((size_t)(last - buf)), slen);
                }

                if (p == NULL) {
                    p = (char *) "(null)";
                }

                while (slen-- && buf < last) {
                    *buf++ = *p++;
                }
                break;

            case 'c':
                *buf++ = (char) va_arg(args, int);
                break;

            case 'd':
                i64 = (length_modifier == 'l') ? va_arg(args, int64_t) : va_arg(args, int);
                if (i64 < 0) {
                    *buf++ = '-';
                    i64 = -i64;
                }
                p = easy_sprintf_num(buf, last, i64, zero, 0, width, sign);
                buf = p;
                break;

            case 'u':
                ui64 = (length_modifier == 'l') ? va_arg(args, uint64_t) : va_arg(args, unsigned int);
                p = easy_sprintf_num(buf, last, ui64, zero, 0, width, 0);
                buf = p;
                break;

            case 'x':
                ui64 = (length_modifier == 'l') ? va_arg(args, uint64_t) : va_arg(args, unsigned int);
                p = easy_sprintf_num(buf, last, ui64, zero, 1, width, 0);
                buf = p;
                break;

            case 'X':
                ui64 = (length_modifier == 'l') ? va_arg(args, uint64_t) : va_arg(args, unsigned int);
                p = easy_sprintf_num(buf, last, ui64, zero, 1, width, 0);
                buf = p;
                break;

            case 'f':
                f = va_arg(args, double);
                if (f < 0) {
                    *buf++ = '-';
                    f = -f;
                }
                p = easy_sprintf_num(buf, last, (uint64_t) f, zero, 0, width, 0);
                buf = p;
                break;

            case 'p':
                ui64 = (uintptr_t) va_arg(args, void *);
                p = easy_sprintf_num(buf, last, ui64, zero, 1, width, 0);
                buf = p;
                break;

            case '%':
                *buf++ = '%';
                break;

            default:
                *buf++ = '%';
                buf--;
                break;
            }

            fmt++;
        } else {
            *buf++ = *fmt++;
        }
    }

    *buf = '\0';
    return buf - start;
}

int lnprintf(char *str, size_t size, const char *fmt, ...)
{
    int ret;
    va_list args;

    va_start(args, fmt);
    ret = easy_vsnprintf(str, size, fmt, args);
    va_end(args);

    return ret;
}

void easy_buf_set_data(easy_pool_t *pool, easy_buf_t *b, const void *data, uint32_t size)
{
    b->data = (char *)data;
    b->pos = b->data;
    b->last = b->pos + size;
    b->end = b->last;
    b->cleanup = NULL;
    b->args = pool;
    b->flags = 0;
    easy_list_init(&b->node);
}

void easy_request_addbuf(easy_request_t *r, easy_buf_t *b)
{
    easy_message_session_t *ms = r->ms;
    b->session = NULL;
    // Used at the timeout time
    if ((ms->type == EASY_TYPE_SESSION) ||
            (ms->type == EASY_TYPE_KEEPALIVE_SESSION) ||
            (ms->type == EASY_TYPE_RL_SESSION)) {
        easy_session_t *s = (easy_session_t *)ms;
        b->session = s;
        s->nextb = &b->node;
        s->buf_count++;
        if (unlikely(s->enable_trace)) {
            easy_debug_log("request add buffer, session=%p, count=%ld", s, s->buf_count);
        }
        easy_debug_log("request add buffer, session=%p, count=%ld", s, s->buf_count);
    }

    easy_list_add_tail(&b->node, &ms->c->output);
}
// easy_pool_alloc_ex function
void *easy_pool_alloc_ex(easy_pool_t *pool, uint32_t size, int align)
{
    uint8_t *m;
    easy_pool_t *p;
    int dsize;

    // init
    dsize = 0;

    if (size > pool->max) {
        dsize = size;
        size = sizeof(easy_pool_large_t);
    }

    EASY_POOL_LOCK(pool);

    p = pool->current;

    do {
        m = reinterpret_cast<uint8_t*>(easy_align_ptr(reinterpret_cast<uintptr_t>(p->last), align));

        if (m + size <= p->end) {
            p->last = m + size;
            break;
        }

        p = p->next;
    } while (p);

    easy_cur_mod_stat = pool->mod_stat;
    // Reallocate a block out
    if (p == NULL) {
        m = (uint8_t *)easy_pool_alloc_block(pool, size);
    }

    if (m && dsize) {
        m = (uint8_t *)easy_pool_alloc_large(pool, (easy_pool_large_t *)m, dsize);
    }
    easy_cur_mod_stat = NULL;

    EASY_POOL_UNLOCK(pool);

    return m;
}

static char *easy_sprintf_num(char *buf, char *last, uint64_t ui64, char zero, int hexadecimal, int width, int sign);
static char *easy_fill_space(int width, char *buf, char *fstart, char *last);

static char *easy_sprintf_num(char *buf, char *last, uint64_t ui64, char zero, int hexadecimal, int width, int sign)
{
    char *p, temp[EASY_NUM_LEN + 1];
    int len;

    p = temp + EASY_NUM_LEN;

    if (hexadecimal == 0) {
        if (ui64 == 0) {
            *--p = '0';
        } else {
            while (ui64) {
                *--p = (char) (ui64 % 10 + '0');
                ui64 /= 10;
            }
        }
    } else if (hexadecimal == 1) {
        if (ui64 == 0) {
            *--p = '0';
        } else {
            while (ui64) {
                *--p = (char) (ui64 % 16 + '0');
                ui64 /= 16;
            }
        }
    }

    len = (temp + EASY_NUM_LEN) - p;

    while (len++ < width && buf < last) {
        *buf++ = zero;
    }

    len = (temp + EASY_NUM_LEN) - p;
    if (buf + len > last) {
        len = last - buf;
    }

    return reinterpret_cast<char*>(easy_memcpy(buf, p, len));
}


} // extern "C"

using namespace oceanbase::rpc;
using namespace oceanbase::rpc::frame;
using namespace oceanbase::common;

namespace oceanbase
{
namespace common
{
void update_easy_log_level()
{
}
 
};
};




