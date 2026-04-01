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

#ifndef _OCEABASE_LIB_ALLOC_ALLOC_ASSIST_H_
#define _OCEABASE_LIB_ALLOC_ALLOC_ASSIST_H_

#include <stdint.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include "lib/utility/ob_macro_utils.h"

// Windows compatibility for case-insensitive string functions
#ifdef _WIN32
#define strcasecmp _stricmp
#define strncasecmp _strnicmp
#ifndef strcasestr
// Windows doesn't have strcasestr, provide a simple implementation
static inline char* strcasestr(const char* haystack, const char* needle) {
  if (!haystack || !needle) return NULL;
  size_t needle_len = strlen(needle);
  while (*haystack) {
    if (_strnicmp(haystack, needle, needle_len) == 0) {
      return (char*)haystack;
    }
    haystack++;
  }
  return NULL;
}
#endif
#endif

#define MEMSET(s, c, n) memset(s, c, n)
#define MEMCPY(dest, src, n) memcpy(dest, src, n)
#define MEMCCPY(dest, src, c, n) memccpy(dest, src, c, n)
#define MEMMOVE(dest, src, n) memmove(dest, src, n)
#define BCOPY(src, dest, n) bcopy(src, dest, n)
#define MEMCMP(s1, s2, n) memcmp(s1, s2, n)
#ifdef _WIN32
static inline void *ob_memmem(const void *haystack, size_t haystacklen,
                              const void *needle, size_t needlelen)
{
  if (needlelen == 0) return (void *)haystack;
  if (haystacklen < needlelen) return NULL;
  const char *h = (const char *)haystack;
  const char *n = (const char *)needle;
  for (size_t i = 0; i <= haystacklen - needlelen; ++i) {
    if (memcmp(h + i, n, needlelen) == 0) return (void *)(h + i);
  }
  return NULL;
}
#define MEMMEM(s1, n1, s2, n2) ob_memmem(s1, n1, s2, n2)
#else
#define MEMMEM(s1, n1, s2, n2) memmem(s1, n1, s2, n2)
#endif
#define STRCPY(dest, src) strcpy(dest, src)
#define STRNCPY(dest, src, n) strncpy(dest, src, n)
#define STRCMP(s1, s2) strcmp(s1, s2)
#define STRNCMP(s1, s2, n) strncmp(s1, s2, n)
#define STRCASECMP(s1, s2) strcasecmp(s1, s2)
#define STRNCASECMP(s1, s2, n) strncasecmp(s1, s2, n)
#define STRCOLL(s1, s2) strcoll(s1, s2)
#define STRSTR(haystack, needle) strstr(haystack, needle)
#define STRCASESTR(haystack, needle) strcasestr(haystack, needle)
#define MEMCHR(s, c, n) memchr(s, c, n)
#define MEMRCHR(s, c, n) memrchr(s, c, n)
#define RAWMEMCHR(s, c) rawmemchr(s, c)
#define STRCHR(s, c) strchr(s, c)
#define STRRCHR(s, c) strrchr(s, c)
#define STRCHRNUL(s, c) strchrnul(s, c)
#define STRLEN(s) strlen(s)
#define STRPBRK(s, accept) strpbrk(s, accept)
#define STRSEP(stringp, delim) strsep(stringp, delim)
#define STRSPN(s, accept) strspn(s, accept)
#define STRCSPN(s, reject) strcspn(s, reject)
#define STRTOK(str, delim) strtok(str, delim)
#define STRTOK_R(str, delim, saveptr) strtok_r(str, delim, saveptr)

static const uint32_t ACHUNK_PRESERVE_SIZE = 17L << 10;

// memory operation wrappers

#endif /* _OCEABASE_LIB_ALLOC_ALLOC_ASSIST_H_ */
