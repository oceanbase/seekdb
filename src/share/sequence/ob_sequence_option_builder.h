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

#ifndef __OB_SHARE_SEQUENCE_SEQUENCE_OPTION_CHECKER_H__
#define __OB_SHARE_SEQUENCE_SEQUENCE_OPTION_CHECKER_H__

#include "lib/container/ob_bit_set.h"

namespace oceanbase
{

namespace share
{
class ObSequenceOption;
class ObSequenceOptionBuilder
{
public:
  ObSequenceOptionBuilder() = default;
  ~ObSequenceOptionBuilder() = default;
  static int build_create_sequence_option(const common::ObBitSet<> &opt_bitset,
                                          share::ObSequenceOption &opt_new);
  static int build_alter_sequence_option(const common::ObBitSet<> &opt_bitset,
                                         const share::ObSequenceOption &opt_old,
                                         share::ObSequenceOption &opt_new,
                                         bool can_alter_start_with);
private:
  static int pre_check_sequence_option(const share::ObSequenceOption &opt);
  static int check_sequence_option(const common::ObBitSet<> &opt_bitset,
                                   const share::ObSequenceOption &opt);
  static int check_sequence_option_integer(const common::ObBitSet<> &opt_bitset,
                                           const share::ObSequenceOption &option);
  DISALLOW_COPY_AND_ASSIGN(ObSequenceOptionBuilder);
};

}
}
#endif /* __OB_SHARE_SEQUENCE_SEQUENCE_OPTION_CHECKER_H__ */
//// end of header file

