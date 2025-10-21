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

#include <string>
class ObSimpleServerRestartHelper
{
public:
  static bool is_restart_;
  static bool need_restart_;
  static int sleep_sec_;
  static const int name_len = 64;

public:
  ObSimpleServerRestartHelper(int argc,
                              char **argv,
                              const char *test_file_name,
                              const char *born_case_name,
                              const char *restart_case_name,
                              const char *log_level = "INFO")
    : argc_(argc),
      argv_(argv),
      test_file_name_(test_file_name),
      born_case_name_(born_case_name),
      restart_case_name_(restart_case_name),
      log_level_(log_level) {}

  int run();

  void set_sleep_sec(int t) { ObSimpleServerRestartHelper::sleep_sec_ = t; }

private:
  int argc_;
  char **argv_;
  const char *test_file_name_;
  const char *born_case_name_;
  const char *restart_case_name_;
  const char *log_level_;
};
