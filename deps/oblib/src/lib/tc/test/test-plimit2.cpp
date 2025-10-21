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
DEF_LIMIT(L1, 1000000);
ROOT(root);
SHARED(tt1, root, 1);
FIFO(tt1g1, tt1, 1);
FIFO(tt1g2, tt1, 2);
SHARED(tt2, root, 2);
FIFO(tt2g1, tt2, 1);
FIFO(tt2g2, tt2, 2);
FIFO(tt2g3, tt2, 3);
LIMIT(tt1, L1);
//LIMIT(tt1g1, L1);
//LIMIT(tt1g2, L1);
LIMIT(tt2g1, L1);
SCHED();
FILL(tt1g1);
FILL(tt1g2);
FILL(tt2g1);
FILL(tt2g2);
FILL(tt2g3);
