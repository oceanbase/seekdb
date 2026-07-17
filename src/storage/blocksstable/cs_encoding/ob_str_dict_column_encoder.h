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

#ifndef OCEANBASE_ENCODING_OB_STR_DICT_COLUMN_ENCODER_H_
#define OCEANBASE_ENCODING_OB_STR_DICT_COLUMN_ENCODER_H_

#include "ob_dict_column_encoder.h"

namespace oceanbase
{
namespace blocksstable
{
class ObStrDictColumnEncoder : public ObDictColumnEncoder
{
public:
  static const ObCSColumnHeader::Type type_ = ObCSColumnHeader::STR_DICT;
  ObStrDictColumnEncoder()
    : ObDictColumnEncoder(),
      string_dict_enc_ctx_() { }
  virtual ~ObStrDictColumnEncoder() {}

  int init(
    const ObColumnCSEncodingCtx &ctx, const int64_t column_index, const int64_t row_count) override;
  void reuse() override;
  int store_column(ObMicroBufferWriter &buf_writer) override;
  int store_column_meta(ObMicroBufferWriter &buf_writer) override;
  int64_t estimate_store_size() const override;
  ObCSColumnHeader::Type get_type() const override { return type_; }
  int get_maximal_encoding_store_size(int64_t &size) const override;
  int get_string_data_len(uint32_t &len) const override;

  INHERIT_TO_STRING_KV("ObDictColumnEncoder", ObDictColumnEncoder, K_(string_dict_enc_ctx));

private:
  int build_string_dict_encoder_ctx_();
  int sort_dict_();
  int store_dict_(ObMicroBufferWriter &buf_writer);

private:
  ObStringStreamEncoderCtx string_dict_enc_ctx_;
};

}  // end namespace blocksstable
}  // end namespace oceanbase

#endif  // OCEANBASE_ENCODING_OB_STR_DICT_COLUMN_ENCODER_H_
