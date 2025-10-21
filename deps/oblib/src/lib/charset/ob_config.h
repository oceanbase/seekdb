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

#ifndef OB_CONFIG_H
#define OB_CONFIG_H

#define _GNU_SOURCE 1


#define SIZEOF_SIZE_T 8
#define SIZEOF_CHARP  8
#define SIZEOF_VOIDP  8
#define SIZEOF_LONG   8

#define SIZEOF_CHAR 1
#define HAS_CHAR 1
#define HAS_LONG 1

#define HAS_CHARP 1
#define SIZEOF_SHORT 2
#define HAS_SHORT 1
#define SIZEOF_INT 4
#define HAS_INT 1
#define SIZEOF_LONG_LONG 8
#define HAS_LONG_LONG 1
#define SIZEOF_OFF_T 8
#define HAS_OFF_T 1
#define SIZEOF_SIGSET_T 128
#define HAS_SIGSET_T 1
#define HAS_SIZE_T 1
#define SIZEOF_UINT 4
#define HAS_UINT 1
#define SIZEOF_ULONG 8
#define HAS_ULONG 1
#define HAS_U_INT32_T 1
#define SIZEOF_U_INT32_T 4
#define HAS_MBSTATE_T
#define MAX_INDEXES 64U
#define QSORT_TYPE_IS_VOID 1
#define SIGNAL_RETURN_TYPE_IS_VOID 1
#define VOID_SIGHANDLER 1
#define RETSIGTYPE void
#define RETQSORTTYPE void
#define STRUCT_RLIMIT struct rlimit
#define SOCKET_SIZE_TYPE socklen_t


#endif
