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
#pragma once
#include <stdint.h>
#include <errno.h>
#include <time.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "atomic.h"
#include "define.h"
#include "futex.h"
#include "single_waiter_cond.h"
#include "link.h"
#include "batch_pop_queue.h"
#include "simple_link_queue.h"
#include "lock.h"
#include "str_format.h"
#include "get_us.h"
