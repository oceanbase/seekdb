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

#ifdef RL_DEF
RL_DEF(foo_int, RLInt, "1")
RL_DEF(foo_str, RLStr, "foo")
RL_DEF(foo_cap, RLCap, "1K")
RL_DEF(max_datafile_size, RLCap, "20T")
RL_DEF(max_session_count, RLInt, "100000")
RL_DEF(max_concurrent_query_count, RLInt, "5000")
#endif
