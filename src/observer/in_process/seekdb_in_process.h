// Copyright (c) 2026 OceanBase.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

int seekdb_in_process_start(const char *absolute_directory, const char *const *startup_parameters,
                            const char *const *initial_parameters);

const char *seekdb_in_process_get_socket_path(void);

const char *seekdb_in_process_get_start_error(void);

#ifdef __cplusplus
}
#endif
