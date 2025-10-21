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

#ifndef OCEANBASE_ENCODING_OB_CONST_ENCODER_H_
#define OCEANBASE_ENCODING_OB_CONST_ENCODER_H_

#include "ob_icolumn_encoder.h"
#include "ob_encoding_util.h"
#include "ob_dict_encoder.h"

namespace oceanbase
{
namespace blocksstable
{

class ObEncodingHashTable;
struct ObEncodingHashNodeList;

struct ObConstMetaHeader
{
  static constexpr uint8_t OB_CONST_META_HEADER_V1 = 0;
  uint8_t version_;
  // count of the except rows
  uint8_t count_;
  // the offset of dict
  uint8_t const_ref_;
  union {
    struct {
      uint8_t row_id_byte_:3;
    };
    uint8_t attr_;
  };
  uint16_t offset_;
  char payload_[0];

  ObConstMetaHeader() { reset(); }
  void reset() { memset(this, 0, sizeof(*this)); }

  TO_STRING_KV(K_(version), K_(count), K_(const_ref), K_(row_id_byte), K_(attr), K_(offset));
}__attribute__((packed));

class ObConstEncoder : public ObIColumnEncoder
{
public:
  static const int64_t MAX_EXCEPTION_SIZE = 32; // Maybe need tune up for pure columnar store
  static const int64_t MAX_EXCEPTION_PCT = 10;
  static const ObColumnHeader::Type type_ = ObColumnHeader::CONST;

  ObConstEncoder();
  virtual ~ObConstEncoder();

  virtual int init(const ObColumnEncodingCtx &ctx_,
                   const int64_t column_index,
                   const ObConstDatumRowArray &rows) override;
  virtual int store_meta(ObBufferWriter &buf_writer) override;
  virtual int store_data(const int64_t row_id, ObBitStream &bs,
                         char *buf, const int64_t len) override;
  virtual int get_row_checksum(int64_t &checksum) const override;
  virtual int traverse(bool &suitable) override;
  virtual int64_t calc_size() const override;
  virtual int get_encoding_store_meta_need_space(int64_t &need_size) const override;
  virtual ObColumnHeader::Type get_type() const override { return type_; }

  virtual void reuse() override ;
  virtual int store_fix_data(ObBufferWriter &buf_writer) override;
  INHERIT_TO_STRING_KV("ObIColumnEncoder", ObIColumnEncoder, K_(count), K_(row_id_byte), KPC_(const_meta_header), K_(dict_encoder));
private:
  int store_meta_without_dict(ObBufferWriter &buf_writer);
  int get_cell_len(const common::ObDatum &datum, int64_t &length) const;
  int store_value(const common::ObDatum &datum, char *buf);

private:
  ObObjTypeStoreClass sc_;
  int64_t count_;
  int64_t row_id_byte_;
  ObConstMetaHeader *const_meta_header_;
  ObEncodingHashNode *const_list_header_;
  ObEncodingHashTable *ht_;
  ObDictEncoder dict_encoder_;

  DISALLOW_COPY_AND_ASSIGN(ObConstEncoder);
};

} // end namespace blocksstable
} // end namespace oceanbase

#endif // OCEANBASE_ENCODING_OB_CONST_ENCODER_H_
