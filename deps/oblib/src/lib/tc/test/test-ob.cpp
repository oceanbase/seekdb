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
#define MAX_GROUP_STAT 30
DEF_COUNT_LIMIT(B1R, 10000);
ROOT(root);
SHARED(net_in, root, 1);
SHARED(net_out, root, 1);
SHARED(tt1r, net_in, 1);
SHARED(tt2r, net_in, 2);
SHARED(tt1w, net_out, 1);
SHARED(tt2w, net_out, 2);
FIFO(tt1r1, tt1r, 1);
FIFO(tt1r2, tt1r, 2);
FIFO(tt2r1, tt2r, 1);
FIFO(tt2r2, tt2r, 2);
FIFO(tt1w1, tt1w, 1);
FIFO(tt1w2, tt1w, 2);
FIFO(tt2w1, tt2w, 1);
FIFO(tt2w2, tt2w, 2);
LIMIT(tt1r1, B1R);
LIMIT(tt2r1, B1R);
SCHED();
FILL(tt1r1);
FILL(tt1r2);
FILL(tt2r1);
FILL(tt2r2);
FILL(tt1w1);
FILL(tt1w2);
FILL(tt2w1);
FILL(tt2w2);
