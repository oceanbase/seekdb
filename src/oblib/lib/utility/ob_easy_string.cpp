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

// Shared diagnostic formatting must not depend on the native RPC transport.
#include <cstdint>
#include <cstring>
#include "util/easy_string.h"


extern "C" {
static char *easy_sprintf_num(char *buf, char *last, uint64_t ui64, char zero, int hexadecimal, int width, int sign);

static char *easy_sprintf_num(char *buf, char *last, uint64_t ui64, char zero, int hexadecimal, int width, int sign)
{
    constexpr int format_num_len = 32;
    char *p, temp[format_num_len + 1];
    int len;

    p = temp + format_num_len;

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
        static const char hex_digits[] = "0123456789abcdef";
        if (ui64 == 0) {
            *--p = '0';
        } else {
            while (ui64) {
                *--p = hex_digits[ui64 % 16];
                ui64 /= 16;
            }
        }
    }

    len = (temp + format_num_len) - p;

    while (len++ < width && buf < last) {
        *buf++ = zero;
    }

    len = (temp + format_num_len) - p;
    if (buf + len > last) {
        len = last - buf;
    }

    return reinterpret_cast<char*>(std::memcpy(buf, p, len));
}


int easy_vsnprintf(char *buf, size_t size, const char *fmt, va_list args)
{
    char *p, zero;
    double f, scale;
    int64_t i64;
    uint64_t ui64;
    int width, sign, hex, frac_width, slen, width_sign;
    char *last, *start, *fstart;
    int length_modifier;

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
            length_modifier = 0;
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

                while (slen-- && *p && buf < last) {
                    *buf++ = *p++;
                }
                break;

            case 'c':
                *buf++ = (char) va_arg(args, int);
                break;

            case 'd':
                i64 = (length_modifier >= 1) ? va_arg(args, int64_t) : va_arg(args, int);
                if (i64 < 0) {
                    *buf++ = '-';
                    i64 = -i64;
                }
                p = easy_sprintf_num(buf, last, i64, zero, 0, width, sign);
                buf = p;
                break;

            case 'u':
                ui64 = (length_modifier >= 1) ? va_arg(args, uint64_t) : va_arg(args, unsigned int);
                p = easy_sprintf_num(buf, last, ui64, zero, 0, width, 0);
                buf = p;
                break;

            case 'x':
                ui64 = (length_modifier >= 1) ? va_arg(args, uint64_t) : va_arg(args, unsigned int);
                p = easy_sprintf_num(buf, last, ui64, zero, 1, width, 0);
                buf = p;
                break;

            case 'X':
                ui64 = (length_modifier >= 1) ? va_arg(args, uint64_t) : va_arg(args, unsigned int);
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


} // extern "C"
