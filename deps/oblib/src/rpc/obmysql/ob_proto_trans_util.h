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

#ifndef _OB_PROTO_TRANS_UTILL_H_
#define _OB_PROTO_TRANS_UTILL_H_

#include <stdint.h>
#include "lib/utility/ob_macro_utils.h"

namespace oceanbase
{
namespace common
{
  class ObProtoTransUtil
  {
    public:
      static const int64_t TYPE_LEN = 2;
      static const int64_t VAL_LENGTH_LEN = 4;
      // for encode
      static int store_type_and_len(char *buf, int64_t len, int64_t &pos, int16_t type, int32_t v_len);
      static int store_str(char *buf, int64_t len, int64_t &pos, const char *str, const uint64_t str_len, int16_t type);

      //@{Serialize integer data, save the data in v to the position of buf+pos, and update pos
      static int store_int1(char *buf, int64_t len, int64_t &pos, int8_t v, int16_t type);
      static int store_int8(char *buf, int64_t len, int64_t &pos, int64_t v, int16_t type);

      static int store_double(char *buf, const int64_t len, int64_t &pos, double val, int16_t type);

      // for decode
      static int resolve_type(const char *buf, int64_t len, int64_t &pos, int16_t &type);
      static int resolve_type_and_len(const char *buf, int64_t len, int64_t &pos, int16_t &type, int32_t &v_len);
      static int get_str(const char *buf, int64_t len, int64_t &pos, int64_t str_len, char *&str);
      //@{ Signed integer in reverse sequence, write the result(the position of buf+pos) to v, and update pos
      static int get_int1(const char *buf, int64_t len, int64_t &pos, int64_t v_len, int8_t &val);
      static int get_int4(const char *buf, int64_t len, int64_t &pos, int64_t v_len, int32_t &val);
      static int get_int8(const char *buf, int64_t len, int64_t &pos, int64_t v_len, int64_t &val);
      static int get_double(const char *buf, int64_t len, int64_t &pos, int64_t v_len, double &val);

      // get serialize size 
      static int get_serialize_size(int64_t seri_sz);
    private:
      DISALLOW_COPY_AND_ASSIGN(ObProtoTransUtil);
  };

}
}

#endif /* _OB_PROTO_TRANS_UTILL_H_ */
