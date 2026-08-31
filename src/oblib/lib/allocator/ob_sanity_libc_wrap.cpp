/*
 * Copyright (c) 2026 OceanBase.
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

#if defined(ENABLE_SANITY)

#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <strings.h>

#include <sanity/sanity.h>

namespace {

void check_sanity_range(const void *ptr, size_t size) {
  if (!SanityDisableCheckRangeGuard::tl_check() || nullptr == ptr ||
      0 == size) {
    return;
  }
  const uintptr_t begin = reinterpret_cast<uintptr_t>(ptr);
  if (begin < static_cast<uintptr_t>(sanity_min_addr) ||
      begin >= static_cast<uintptr_t>(sanity_max_addr)) {
    return;
  }
  if (size > static_cast<uintptr_t>(sanity_max_addr) - begin) {
    memory_sanity_abort();
  }

  uintptr_t current = begin;
  const uintptr_t end = begin + size;
  while (current < end) {
    const uint8_t shadow = *reinterpret_cast<volatile uint8_t *>(current >> 3);
    const size_t offset = current & 7;
    const size_t bytes_in_block = static_cast<size_t>(
        ((current | static_cast<uintptr_t>(7)) + 1) - current);
    const size_t checked = bytes_in_block < end - current
                               ? bytes_in_block
                               : static_cast<size_t>(end - current);
    const size_t accessible = 0 == shadow ? 8 : (shadow <= 7 ? shadow : 0);
    if (offset + checked > accessible) {
      memory_sanity_abort();
    }
    current += checked;
  }
}

size_t checked_string_size(const char *str, size_t limit) {
  size_t size = 0;
  while (size < limit) {
    check_sanity_range(str + size, 1);
    if ('\0' == str[size++]) {
      break;
    }
  }
  return size;
}

} // namespace

extern "C" {

void *__real_memcpy(void *, const void *, size_t);
void *__real_memmove(void *, const void *, size_t);
void *__real_memset(void *, int, size_t);
int __real_memcmp(const void *, const void *, size_t);
char *__real_strcpy(char *, const char *);
char *__real_strncpy(char *, const char *, size_t);
int __real_strcasecmp(const char *, const char *);
int __real_strncasecmp(const char *, const char *, size_t);
int __real_vsprintf(char *, const char *, va_list);
int __real_vsnprintf(char *, size_t, const char *, va_list);

void *__wrap_memcpy(void *dst, const void *src, size_t size) {
  check_sanity_range(src, size);
  check_sanity_range(dst, size);
  return __real_memcpy(dst, src, size);
}

void *__wrap_memmove(void *dst, const void *src, size_t size) {
  check_sanity_range(src, size);
  check_sanity_range(dst, size);
  return __real_memmove(dst, src, size);
}

void *__wrap_memset(void *dst, int value, size_t size) {
  check_sanity_range(dst, size);
  return __real_memset(dst, value, size);
}

void __wrap_bzero(void *dst, size_t size) {
  check_sanity_range(dst, size);
  static_cast<void>(__real_memset(dst, 0, size));
}

int __wrap_memcmp(const void *lhs, const void *rhs, size_t size) {
  check_sanity_range(lhs, size);
  check_sanity_range(rhs, size);
  return __real_memcmp(lhs, rhs, size);
}

size_t __wrap_strlen(const char *str) {
  return checked_string_size(str, SIZE_MAX) - 1;
}

size_t __wrap_strnlen(const char *str, size_t max_size) {
  const size_t checked = checked_string_size(str, max_size);
  return checked > 0 && '\0' == str[checked - 1] ? checked - 1 : checked;
}

char *__wrap_strcpy(char *dst, const char *src) {
  const size_t size = checked_string_size(src, SIZE_MAX);
  check_sanity_range(dst, size);
  return __real_strcpy(dst, src);
}

char *__wrap_strncpy(char *dst, const char *src, size_t size) {
  static_cast<void>(checked_string_size(src, size));
  check_sanity_range(dst, size);
  return __real_strncpy(dst, src, size);
}

int __wrap_strcmp(const char *lhs, const char *rhs) {
  for (size_t i = 0;; ++i) {
    check_sanity_range(lhs + i, 1);
    check_sanity_range(rhs + i, 1);
    const unsigned char left = static_cast<unsigned char>(lhs[i]);
    const unsigned char right = static_cast<unsigned char>(rhs[i]);
    if (left != right || '\0' == left) {
      return static_cast<int>(left) - static_cast<int>(right);
    }
  }
}

int __wrap_strncmp(const char *lhs, const char *rhs, size_t size) {
  for (size_t i = 0; i < size; ++i) {
    check_sanity_range(lhs + i, 1);
    check_sanity_range(rhs + i, 1);
    const unsigned char left = static_cast<unsigned char>(lhs[i]);
    const unsigned char right = static_cast<unsigned char>(rhs[i]);
    if (left != right || '\0' == left) {
      return static_cast<int>(left) - static_cast<int>(right);
    }
  }
  return 0;
}

int __wrap_strcasecmp(const char *lhs, const char *rhs) {
  static_cast<void>(checked_string_size(lhs, SIZE_MAX));
  static_cast<void>(checked_string_size(rhs, SIZE_MAX));
  return __real_strcasecmp(lhs, rhs);
}

int __wrap_strncasecmp(const char *lhs, const char *rhs, size_t size) {
  static_cast<void>(checked_string_size(lhs, size));
  static_cast<void>(checked_string_size(rhs, size));
  return __real_strncasecmp(lhs, rhs, size);
}

int __wrap_vsnprintf(char *dst, size_t size, const char *format, va_list args) {
  static_cast<void>(checked_string_size(format, SIZE_MAX));
  check_sanity_range(dst, size);
  return __real_vsnprintf(dst, size, format, args);
}

int __wrap_snprintf(char *dst, size_t size, const char *format, ...) {
  va_list args;
  va_start(args, format);
  const int result = __wrap_vsnprintf(dst, size, format, args);
  va_end(args);
  return result;
}

int __wrap_vsprintf(char *dst, const char *format, va_list args) {
  static_cast<void>(checked_string_size(format, SIZE_MAX));
  va_list size_args;
  va_copy(size_args, args);
  const int output_size = __real_vsnprintf(nullptr, 0, format, size_args);
  va_end(size_args);
  if (output_size >= 0) {
    check_sanity_range(dst, static_cast<size_t>(output_size) + 1);
  }
  return __real_vsprintf(dst, format, args);
}

int __wrap_sprintf(char *dst, const char *format, ...) {
  va_list args;
  va_start(args, format);
  const int result = __wrap_vsprintf(dst, format, args);
  va_end(args);
  return result;
}

} // extern "C"

#endif
