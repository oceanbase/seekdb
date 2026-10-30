/* Copyright (c) 2025 OceanBase. Licensed under the Apache License, Version 2.0. */
#include "common/rowkey/ob_rowkey.h"
#include "common/datum/datum_parameter_codec.h"
#include "common/datum/datum_sql_print.h"
#include "common/json_type/ob_json_bin.h"
#include "lib/worker.h"
#include "lib/utility/ob_hang_fatal_error.h"
namespace oceanbase { namespace common {
namespace {
DatumValue marker(bool minimum) { DatumValue value; if(minimum)value.set_min_value();else value.set_max_value();return value; }
}
DatumValue ObRowkey::MIN_VALUE = marker(true);
DatumValue ObRowkey::MAX_VALUE = marker(false);
ObRowkey ObRowkey::MIN_ROWKEY(&ObRowkey::MIN_VALUE,1);
ObRowkey ObRowkey::MAX_ROWKEY(&ObRowkey::MAX_VALUE,1);

int ObRowkey::copy_values(ObRowkey &output,DatumValue *buffer,int64_t capacity) const
{
  int ret=OB_SUCCESS;
  if(buffer==nullptr||capacity<count()){ret=OB_INVALID_ARGUMENT;}
  else if(!is_legal()){ret=OB_INVALID_DATA;}
  else {for(int64_t i=0;i<count();++i)buffer[i]=get_cell(i);output.assign(buffer,count());}
  return ret;
}
int ObRowkey::deep_copy(char *buffer,int64_t size) const
{ ObRowkey output;return deep_copy(output,buffer,size); }
int ObRowkey::deep_copy(ObRowkey &output,char *buffer,int64_t size) const
{
  PartitionBoundRow candidate;int64_t pos=0;
  const int ret=candidate.deep_copy(values_,buffer,size,pos);
  if(ret==OB_SUCCESS)output.values_=candidate;
  return ret;
}
int ObRowkey::deep_copy(const ObRowkey &source,char *buffer,int64_t size,int64_t &pos)
{ return values_.deep_copy(source.values_,buffer,size,pos); }

int ObRowkey::serialize(char *buffer,int64_t size,int64_t &pos) const
{ return values_.serialize(buffer,size,pos); }
int64_t ObRowkey::get_serialize_size() const
{ return values_.get_serialize_size(); }
int ObRowkey::serialize_datums(char *buffer,int64_t size,int64_t &pos) const
{
  int ret=OB_SUCCESS;
  if(buffer==nullptr||size<=0)ret=OB_INVALID_ARGUMENT;
  else if(!is_legal())ret=OB_INVALID_DATA;
  for(int64_t i=0;ret==OB_SUCCESS&&i<count();++i)
    ret=DatumParameterCodec::encode_value(get_cell(i).datum_,get_cell(i).meta_,buffer,size,pos);
  return ret;
}
int64_t ObRowkey::get_serialize_datums_size() const
{
  int64_t result=0;
  for(int64_t i=0;i<count();++i){int64_t size=0;if(DatumParameterCodec::value_size(get_cell(i).datum_,get_cell(i).meta_,size)!=OB_SUCCESS)return 0;result+=size;}
  return result;
}
int ObRowkey::deserialize(ObIAllocator &owner,const char *buffer,int64_t size,int64_t &pos)
{
  // The historical owning overload reserves the public rowkey maximum before
  // decoding; the pre-reserved view overload below has no implicit width cap.
  DatumValue reserved[OB_MAX_ROWKEY_COLUMN_NUMBER];ObRowkey candidate(reserved,OB_MAX_ROWKEY_COLUMN_NUMBER);
  int64_t next=pos;int ret=candidate.decode(owner,buffer,size,next,false);
  if(ret==OB_SUCCESS)ret=candidate.deep_copy(*this,owner);
  if(ret==OB_SUCCESS)pos=next;
  return ret;
}
int ObRowkey::deserialize(const char *buffer,int64_t size,int64_t &pos,bool check_zero)
{ return decode(lib::this_worker().get_sql_arena_allocator(),buffer,size,pos,check_zero); }
int ObRowkey::decode(ObIAllocator &owner,const char *buffer,int64_t size,int64_t &pos,bool check_zero)
{
  int ret=OB_SUCCESS;int64_t next=pos,n=0;
  if(buffer==nullptr||size<=0)ret=OB_INVALID_ARGUMENT;
  else if((ret=serialization::decode_vi64(buffer,size,next,&n))!=OB_SUCCESS){}
  else if(n<=0){if(check_zero&&n==0)set_length(0);pos=next;}
  else if(n>INT64_MAX/static_cast<int64_t>(sizeof(DatumValue)))ret=OB_SIZE_OVERFLOW;
  else if(get_values()!=nullptr&&count()<n)ret=OB_BUF_NOT_ENOUGH;
  else {
    ObSEArray<DatumValue,4> staged;
    ret=staged.prepare_allocate(n);
    for(int64_t i=0;ret==OB_SUCCESS&&i<n;++i){
      if(get_values()!=nullptr)staged.at(i)=get_cell(i);
      ret=DatumParameterCodec::decode_unis_value(buffer,size,next,owner,staged.at(i).datum_,staged.at(i).meta_);
    }
    DatumValue *destination=get_values();
    if(ret==OB_SUCCESS&&destination==nullptr){
      destination=static_cast<DatumValue *>(owner.alloc(sizeof(DatumValue)*n));
      if(destination==nullptr)ret=OB_ALLOCATE_MEMORY_FAILED;
      else new(destination) DatumValue[n];
    }
    if(ret==OB_SUCCESS){for(int64_t i=0;i<n;++i)destination[i]=staged.at(i);assign(destination,n);pos=next;}
  }
  return ret;
}
int ObRowkey::deserialize_datums(const char *buffer,int64_t size,int64_t &pos)
{
  int ret=OB_SUCCESS;int64_t next=pos;ObSEArray<DatumValue,4> staged;
  if(get_values()==nullptr)ret=OB_ERR_UNEXPECTED;
  else ret=staged.prepare_allocate(count());
  for(int64_t i=0;ret==OB_SUCCESS&&i<count();++i){
    staged.at(i)=get_cell(i);
    ret=DatumParameterCodec::decode_unis_value(buffer,size,next,lib::this_worker().get_sql_arena_allocator(),staged.at(i).datum_,staged.at(i).meta_);
  }
  if(ret==OB_SUCCESS){for(int64_t i=0;i<count();++i)get_cell(i)=staged.at(i);pos=next;}
  return ret;
}
int ObRowkey::compare_from(const ObRowkey &other, int64_t index) const
{
  int cmp = 0;
  for (int64_t i = index; cmp == 0 && i < MIN(count(), other.count()); ++i) {
    const int ret = compare_key_value(get_cell(i), other.get_cell(i), cmp);
    if (ret != OB_SUCCESS) {
      COMMON_LOG_RET(ERROR, ret, "native rowkey comparison failed", K(*this), K(other));
      right_to_die_or_duty_to_live();
    }
  }
  if (cmp == 0) {
    cmp = (count() > other.count()) - (count() < other.count());
  }
  return cmp;
}
int ObRowkey::compare_prefix(const ObRowkey &other,int &cmp) const
{
  int ret=OB_SUCCESS;cmp=0;
  const int left=is_min_row()?-1:is_max_row()?1:0,right=other.is_min_row()?-1:other.is_max_row()?1:0;
  if(left!=0||right!=0)cmp=left-right;
  else for(int64_t i=0;ret==OB_SUCCESS&&cmp==0&&i<MIN(count(),other.count());++i)
    ret=compare_key_value(get_cell(i),other.get_cell(i),cmp,true);
  return ret;
}
int ObRowkey::compare_prefix(const ObRowkey &other) const
{
  int cmp=0;const int ret=compare_prefix(other,cmp);
  if(ret!=OB_SUCCESS){COMMON_LOG_RET(ERROR,ret,"native prefix comparison failed");right_to_die_or_duty_to_live();}
  return cmp;
}
int ObRowkey::equal(const ObRowkey &other,bool &equal) const
{
  int ret=OB_SUCCESS;equal=count()==other.count();
  if(get_values()!=other.get_values())for(int64_t i=0;ret==OB_SUCCESS&&equal&&i<count();++i)
    ret=equal_key_value(get_cell(i),other.get_cell(i),equal);
  return ret;
}
bool ObRowkey::simple_equal(const ObRowkey &other) const
{
  bool same=count()==other.count();
  if(get_values()!=other.get_values()){
    for(int64_t i=0;same&&i<count();++i){
      const auto tc=get_cell(i).get_type_class();
      if(tc!=other.get_cell(i).get_type_class())same=false;
      else {
        int ret=OB_SUCCESS;
        switch(tc){
          case ObNullTC:case ObIntTC:case ObUIntTC:case ObDateTimeTC:
          case ObMySQLDateTimeTC:case ObTimeTC:case ObExtendTC:case ObBitTC:
          case ObEnumSetTC:case ObDateTC:case ObMySQLDateTC:case ObYearTC:
          case ObFloatTC:case ObDoubleTC:case ObStringTC:case ObRawTC:
          case ObNumberTC:case ObDecimalIntTC:ret=equal_key_value(get_cell(i),other.get_cell(i),same);break;
          default:ret=OB_ERR_UNEXPECTED;break;
        }
        if(ret!=OB_SUCCESS){
          COMMON_LOG_RET(ERROR,ret,"unsupported native simple key equality",K(tc));
          right_to_die_or_duty_to_live();
        }
      }
    }
  }
  return same;
}
int ObRowkey::get_common_prefix_length(const ObRowkey &left,const ObRowkey &right,int64_t &length)
{
  int ret=OB_SUCCESS;length=0;
  if(!left.is_valid()||!right.is_valid())ret=OB_INVALID_ARGUMENT;
  for(int64_t i=0;ret==OB_SUCCESS&&i<MIN(left.count(),right.count());++i){
    const DatumValue &a=left.get_cell(i),&b=right.get_cell(i);
    if(a.is_min_value()||a.is_max_value()||b.is_min_value()||b.is_max_value())break;
    bool same=false;
    ret=sql_equal_key_value(a,b,same);
    if(ret!=OB_SUCCESS){
      COMMON_LOG_RET(ERROR,ret,"native prefix equality failed",K(a),K(b));
      right_to_die_or_duty_to_live();
    }
    if(ret==OB_SUCCESS&&same)++length;else break;
  }
  return ret;
}
uint64_t ObRowkey::murmurhash(uint64_t seed) const
{
  int ret=OB_SUCCESS;uint64_t hash=seed;
  if(!is_legal())ret=OB_INVALID_DATA;
  else for(int64_t i=0;i<(is_min_row()||is_max_row()?MIN(count(),int64_t(1)):count());++i)
    ret=hash_key_value(get_cell(i),hash,hash);
  if(ret!=OB_SUCCESS)COMMON_LOG_RET(ERROR,ret,"native rowkey hash failed");
  return hash;
}
int ObRowkey::checksum(ObBatchChecksum &checksum) const
{
  int ret=is_legal()?OB_SUCCESS:OB_INVALID_DATA;
  for(int64_t i=0;ret==OB_SUCCESS&&i<count();++i)ret=checksum_key_value(get_cell(i),checksum);
  return ret;
}
int ObRowkey::need_transform_to_collation_free(bool &required) const
{
  int ret=OB_SUCCESS;required=false;
  if(!is_valid())ret=OB_INVALID_ARGUMENT;
  for(int64_t i=0;ret==OB_SUCCESS&&!required&&i<count();++i)required=get_cell(i).meta_.is_collation_free_compatible();
  return ret;
}
// BEGIN NATIVE KEY VALUE PRINTERS: no Rowkey layout or legacy value dependency.
namespace {
ObString key_text_print_data(const DatumValue &value, int64_t maximum)
{
  ObString bytes = value.get_string();
  if (!bytes.empty() && value.has_lob_header()) {
    ObLobLocatorV2 locator(bytes, true);
    if (!locator.is_valid(false)) {
    } else if (!locator.has_inrow_data()) {
      bytes = ObString::make_string("outrow");
    } else if (locator.get_inrow_data(bytes) == OB_SUCCESS) {
      bytes.assign_ptr(bytes.ptr(), MIN(bytes.length(), maximum));
    }
  }
  return bytes;
}
void print_native_key_repeated_text(const DatumValue &value, char *buf, int64_t buf_len, int64_t &pos)
{
  const ObString text = value.is_lob_storage() ? key_text_print_data(value, buf_len - pos) : value.get_string();
  const auto *bytes = reinterpret_cast<const unsigned char *>(text.ptr());
  int32_t length = text.length(), repeats = 0;
  int8_t spaces = 0;
  if (bytes != nullptr && length > 0) {
    while (length > 0 && bytes[length - 1] == ' ') { --length; ++spaces; }
    while (length - 2 > 0 && bytes[length - 1] == 0xBF && bytes[length - 2] == 0xBF && bytes[length - 3] == 0xEF) {
      length -= 3; ++repeats;
    }
  }
  if (repeats == 0) { length = text.length(); }
  BUF_PRINTO(ObString(0, length, text.ptr()));
  if (repeats > 0) {
    BUF_PRINTF(" '<%X%X%X><repeat %d times>' ", bytes[length], bytes[length + 1], bytes[length + 2], repeats);
    if (spaces == 1) { BUF_PRINTO(" "); }
    else if (spaces == 2) { BUF_PRINTO("  "); }
  }
}
int print_native_key_hex(const DatumValue &value, char *buf, int64_t buf_len, int64_t &pos)
{
  int ret = databuff_printf(buf, buf_len, pos, "X'");
  if (ret == OB_SUCCESS) { ret = hex_print(value.get_string_ptr(), value.get_string_len(), buf, buf_len, pos); }
  if (ret == OB_SUCCESS) { ret = databuff_printf(buf, buf_len, pos, "'"); }
  return ret;
}
int print_native_key_json(const DatumValue &value, bool range_style, char *buf, int64_t buf_len, int64_t &pos)
{
  int ret = OB_SUCCESS;
  if (value.is_json()) {
    ObString bytes = value.get_string();
    const auto *header = reinterpret_cast<const ObLobCommon *>(bytes.ptr());
    if (value.has_lob_header() || (bytes.length() >= sizeof(ObLobCommon) && header->is_valid())) {
      ObLobLocatorV2 locator(bytes, true);
      if (!locator.is_valid(false)) {
      } else if (!locator.has_inrow_data()) {
        databuff_printf(buf, buf_len, pos, "'outrow json'");
        ret = OB_INVALID_ARGUMENT;
      } else { ret = locator.get_inrow_data(bytes); }
    }
    if (ret == OB_SUCCESS && !bytes.empty()) {
      ObArenaAllocator owner;
      ObJsonBin json(bytes.ptr(), bytes.length(), &owner);
      ObJsonBuffer printed(&owner);
      if ((ret = json.reset_iter()) == OB_SUCCESS && (ret = json.print(printed, false, bytes.length())) == OB_SUCCESS) {
        ret = databuff_printf(buf, buf_len, pos, "%.*s", static_cast<int>(MIN(printed.length(), buf_len - pos)), printed.ptr());
      }
    }
    return ret;
  }
  J_OBJ_START(); BUF_PRINTO(sql_value_type_to_string(value.get_type())); J_COLON();
  const auto type_class = value.get_type_class();
  if (value.is_null()) { BUF_PRINTO("NULL"); }
  else if (type_class == ObStringTC || type_class == ObTextTC || type_class == ObRawTC || type_class == ObGeometryTC) {
    const ObString text = value.is_lob_storage() ? key_text_print_data(value, buf_len - pos) : value.get_string();
    if (range_style && value.is_string_type()) {
      print_native_key_repeated_text(value, buf, buf_len, pos);
    } else if (value.get_type() == ObHexStringType) {
      J_QUOTE(); ret = hex_print(value.get_string_ptr(), value.get_string_len(), buf, buf_len, pos); J_QUOTE();
    } else if (value.is_binary() || ObCharset::charset_type_by_coll(value.get_collation_type()) == CHARSET_UTF16
               || ObCharset::charset_type_by_coll(value.get_collation_type()) == CHARSET_UTF16LE) {
      ret = hex_print(text.ptr(), text.length(), buf, buf_len, pos);
    } else { BUF_PRINTO(text); }
    if (value.get_type() != ObHexStringType) {
      J_COMMA(); J_KV(N_COLLATION, ObCharset::collation_name(value.get_collation_type()));
      if (!range_style || !value.is_string_type()) {
        J_COMMA(); J_KV(N_COERCIBILITY, ObCharset::collation_level(value.get_collation_level()));
      }
    }
  } else if (type_class == ObIntTC) { BUF_PRINTO(value.get_int()); }
  else if (type_class == ObUIntTC || type_class == ObBitTC || type_class == ObEnumSetTC) { BUF_PRINTO(value.get_uint64()); }
  else if (type_class == ObFloatTC) { BUF_PRINTO(value.get_float()); }
  else if (type_class == ObDoubleTC) { BUF_PRINTO(value.get_double()); }
  else if (type_class == ObNumberTC) { BUF_PRINTO(value.get_number().format()); }
  else if (type_class == ObDecimalIntTC) { ret = value.print_plain_str_literal(buf, buf_len, pos); }
  else if (value.is_temporal_type()) {
    J_QUOTE(); ret = value.print_plain_str_literal(buf, buf_len, pos); J_QUOTE();
  } else if (type_class == ObUnknownTC) { BUF_PRINTO(value.get_unknown()); }
  else if (type_class == ObExtendTC) {
    J_QUOTE();
    if (value.is_nop_value()) { BUF_PRINTF("__OB__NOP__"); }
    else if (value.get_ext() == ObActionFlag::OP_DEFAULT_NOW_FLAG) { BUF_PRINTF("CURRENT_TIMESTAMP"); }
    else { BUF_PRINTO(value.get_ext()); }
    J_QUOTE();
  } else { ret = OB_NOT_SUPPORTED; }
  J_OBJ_END();
  return ret;
}
// style: range JSON / smart JSON / SQL log-tool / plain range endpoints.
int print_native_key_cell(const DatumValue &value, int style, char *buf, int64_t buf_len, int64_t &pos)
{
  int ret = OB_SUCCESS;
  if (value.is_min_value()) { ret = databuff_printf(buf, buf_len, pos, "MIN"); }
  else if (value.is_max_value()) { ret = databuff_printf(buf, buf_len, pos, "MAX"); }
  else if (style == 3) {
    ObValuePrintParams params; params.refine_range_max_value_ = true;
    ret = value.print_plain_str_literal(buf, buf_len, pos, params);
  } else if (style == 0) { ret = print_native_key_json(value, true, buf, buf_len, pos); }
  else if (style == 1 || style == 2) {
    bool printable = true;
    const bool check = style == 1
        ? (value.meta_.is_string_or_lob_locator_type() && value.get_type() != ObHexStringType) || value.is_json() || value.is_geometry()
        : value.is_string_type() && value.get_type() != ObHexStringType;
    if (check) { ret = is_printable(value.get_string_ptr(), value.get_string_len(), printable); }
    if (ret == OB_SUCCESS && printable) {
      ret = style == 1 ? print_native_key_json(value, false, buf, buf_len, pos) : value.print_sql_literal(buf, buf_len, pos);
    } else if (ret == OB_SUCCESS && style == 2) { ret = print_native_key_hex(value, buf, buf_len, pos); }
    else if (ret == OB_SUCCESS) {
      J_OBJ_START(); BUF_PRINTO(sql_value_type_to_string(value.get_type())); J_COLON();
      if ((ret = print_native_key_hex(value, buf, buf_len, pos)) == OB_SUCCESS) {
        J_COMMA(); J_KV(N_COLLATION, ObCharset::collation_name(value.get_collation_type())); J_OBJ_END();
      }
    }
  }
  return ret;
}
}
// END NATIVE KEY VALUE PRINTERS.

namespace {
int64_t print_key(const ObRowkey &key, char *buffer, int64_t size, int style)
{
  int64_t pos = 0; int ret = OB_SUCCESS;
  const char *separator = style == 1 ? "   " : style == 2 ? " " : ",";
  if (buffer != nullptr && key.get_values() != nullptr) {
    for (int64_t i = 0; ret == OB_SUCCESS && i < key.count(); ++i) {
      if (pos < size) {
        const int value_ret = print_native_key_cell(key.get_cell(i), style, buffer, size, pos);
        if (style != 0) { ret = value_ret; }
        if (ret == OB_SUCCESS && i < key.count() - 1) { ret = databuff_printf(buffer, size, pos, "%s", separator); }
      } else if (style == 2) { ret = OB_SIZE_OVERFLOW; }
      else if (style == 3) { break; }
    }
  }
  return pos;
}
}
int64_t ObRowkey::to_string(char *buffer,int64_t size) const {return print_key(*this,buffer,size,0);}
int64_t ObRowkey::to_smart_string(char *buffer,int64_t size) const {return print_key(*this,buffer,size,1);}
int64_t ObRowkey::to_format_string(char *buffer,int64_t size) const {return print_key(*this,buffer,size,2);}
int64_t ObRowkey::to_plain_string(char *buffer,int64_t size) const {return print_key(*this,buffer,size,3);}
} }
