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

#ifndef OCEANBASE_SHARE_OB_ID_GENERATOR_H_
#define OCEANBASE_SHARE_OB_ID_GENERATOR_H_

#include "share/ob_define.h"

namespace oceanbase
{
namespace share
{

// Generates the inclusive sequence [start_id, end_id] with a fixed positive step.
class ObIDGenerator
{
public:
  ObIDGenerator()
    : inited_(false),
      step_(0),
      start_id_(common::OB_INVALID_ID),
      end_id_(common::OB_INVALID_ID),
      current_id_(common::OB_INVALID_ID)
  {}
  ObIDGenerator(const uint64_t step)
    : inited_(false),
      step_(step),
      start_id_(common::OB_INVALID_ID),
      end_id_(common::OB_INVALID_ID),
      current_id_(common::OB_INVALID_ID)
  {}

  virtual ~ObIDGenerator() {}
  void reset();

  int init(const uint64_t step,
           const uint64_t start_id,
           const uint64_t end_id);
  int next(uint64_t &current_id);

  int get_start_id(uint64_t &start_id) const;
  int get_current_id(uint64_t &current_id) const;
  int get_end_id(uint64_t &end_id) const;
  int get_id_cnt(uint64_t &cnt) const;
  TO_STRING_KV(K_(inited), K_(step), K_(start_id), K_(end_id), K_(current_id));

protected:
  bool inited_;
  uint64_t step_;
  uint64_t start_id_;
  uint64_t end_id_;
  uint64_t current_id_;
};

} // namespace share
} // namespace oceanbase

#endif // OCEANBASE_SHARE_OB_ID_GENERATOR_H_
