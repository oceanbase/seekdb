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

#ifndef __EASY_MACCEPT_H__
#define __EASY_MACCEPT_H__

#include "easy_define.h"
EASY_CPP_START
extern void easy_ma_init(int port);
extern int easy_ma_start();
extern void easy_ma_stop();
extern int easy_ma_regist(int gid, int idx);
extern int easy_ma_handshake(int fd, int id);

EASY_CPP_END
#endif /* __EASY_MACCEPT_H__ */
