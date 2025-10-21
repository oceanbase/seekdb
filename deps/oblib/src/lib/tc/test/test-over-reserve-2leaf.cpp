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
ROOT(root);
SHARED(t1, root, 1);
FIFO(g1, t1, 1);
FIFO(g2, t1, 2);
LIMIT_SET(t1, 10 * 1000 * 1000L);
RESERVE_SET(g1, 4 * 1000 * 1000L);
RESERVE_SET(g2, 4 * 1000L * 1000L);
SCHED();
FILL_SOME_TIME(g1);
FILL(g2);
