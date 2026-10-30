/* Copyright (c) 2025 OceanBase. Licensed under the Apache License, Version 2.0. */
#include "common/datum/datum_key_ops.h"
#include "common/wide_integer/ob_wide_integer_helper.h"
#include "common/json_type/ob_json_bin.h"
#include "lib/charset/ob_charset.h"
#include <cmath>
namespace oceanbase { namespace common {
namespace {
uint64_t key_hash_bytes(const void *bytes,uint64_t length,uint64_t seed) { return murmurhash(bytes,static_cast<int32_t>(length),seed); }
bool null_value(const DatumValue &v) { return v.is_null()||v.meta_.is_null(); }
bool fixed_double(const DatumValue &v) { return ob_is_double_type(v.get_type())&&v.get_scale()>SCALE_UNKNOWN_YET&&v.get_scale()<=OB_MAX_DOUBLE_FLOAT_SCALE; }
template<class L,class R>int order(L left,R right) { return left<right?-1:left>right?1:0; }
int real_order(double left,double right) {
  return std::isnan(left)||std::isnan(right)?std::isnan(left)==std::isnan(right)?0:std::isnan(left)?1:-1:order(left,right);
}
int fixed_order(const DatumValue &left,const DatumValue &right) {
  const int scale=MAX(left.get_scale(),right.get_scale())+1;
  const double tolerance=5.0/std::pow(10.0,scale<=0||scale>OB_NOT_FIXED_SCALE?OB_NOT_FIXED_SCALE:scale);
  const double l=left.get_double(),r=right.get_double();
  return std::isnan(l)||std::isnan(r)?real_order(l,r):l==r||std::fabs(l-r)<tolerance?0:order(l,r);
}
int inrow(const DatumValue &value,ObString &bytes) {
  int ret=OB_SUCCESS;bytes=value.get_string();
  if(value.has_lob_header()&&!bytes.empty()){
    ObLobLocatorV2 locator(bytes,true);
    if(!locator.is_valid()||!locator.has_inrow_data())ret=OB_ERR_UNEXPECTED;
    else ret=locator.get_inrow_data(bytes);
  }
  return ret;
}
int sortkey_order(const DatumValue &left,const DatumValue &right,int &result) {
  int ret=OB_SUCCESS;result=0;
  if(left.get_collation_type()!=CS_TYPE_COLLATION_FREE||right.get_collation_type()!=CS_TYPE_COLLATION_FREE
      ||left.get_type()!=right.get_type()||!left.meta_.is_character_type())ret=OB_ERR_UNEXPECTED;
  else {
    const int32_t n=MIN(left.get_string_len(),right.get_string_len());
    result=MEMCMP(left.get_string_ptr(),right.get_string_ptr(),n);
    if(result==0&&left.get_string_len()!=right.get_string_len()){
      const bool more_left=left.get_string_len()>n;
      const unsigned char *tail=reinterpret_cast<const unsigned char *>(more_left?left.get_string_ptr():right.get_string_ptr())+n;
      const int64_t size=(more_left?left.get_string_len():right.get_string_len())-n;
      for(int64_t i=0;i<size;++i)if(tail[i]!=' '){result=(tail[i]<' '?-1:1)*(more_left?1:-1);break;}
    }
  }
  return ret;
}
int decimal_order(const DatumValue &left,const DatumValue &right,int &result) {
  int ret=OB_SUCCESS;ObDecimalIntBuilder l,r,conversion;ObDecimalInt *right_integer=nullptr;int32_t right_bytes=0;ObScale right_scale=0;
  const ObDecimalInt *right_coefficient=nullptr;
  if(right.is_decimal_int()){right_coefficient=right.get_decimal_int();right_bytes=right.get_int_bytes();right_scale=right.get_scale();}
  else {
    switch(right.get_type_class()){
      case ObIntTC:ret=wide::from_integer(right.get_int(),conversion,right_integer,right_bytes);break;
      case ObUIntTC:case ObEnumSetTC:ret=wide::from_integer(right.get_uint64(),conversion,right_integer,right_bytes);break;
      case ObNumberTC:{number::ObNumber number=right.get_number();right_scale=number.get_scale();ret=wide::from_number(number,conversion,right_scale,right_integer,right_bytes);break;}
      default:ret=OB_ERR_UNEXPECTED;break;
    }
    right_coefficient=right_integer;
  }
  const int32_t zero=0;
  const ObDecimalInt *left_coefficient=left.get_int_bytes()==0?reinterpret_cast<const ObDecimalInt *>(&zero):left.get_decimal_int();
  const int32_t left_bytes=left.get_int_bytes()==0?sizeof(zero):left.get_int_bytes();
  if(right_bytes==0){right_coefficient=reinterpret_cast<const ObDecimalInt *>(&zero);right_bytes=sizeof(zero);}
  if(ret==OB_SUCCESS){
    if(left.get_scale()<right_scale){ret=wide::common_scale_decimalint(left_coefficient,left_bytes,left.get_scale(),right_scale,l);if(ret==OB_SUCCESS)r.from(right_coefficient,right_bytes);}
    else if(left.get_scale()>right_scale){ret=wide::common_scale_decimalint(right_coefficient,right_bytes,right_scale,left.get_scale(),r);if(ret==OB_SUCCESS)l.from(left_coefficient,left_bytes);}
    else {l.from(left_coefficient,left_bytes);r.from(right_coefficient,right_bytes);}
    if(ret==OB_SUCCESS)ret=wide::compare(l,r,result);
  }
  return ret;
}
int number_order(const DatumValue &left,const DatumValue &right,int &result) {
  const number::ObNumber number=left.get_number();
  switch(right.get_type_class()){
    case ObIntTC:result=number.compare(right.get_int());break;
    case ObUIntTC:case ObEnumSetTC:result=number.compare(right.get_uint64());break;
    case ObNumberTC:result=number.compare(right.get_number());break;
    default:return OB_ERR_UNEXPECTED;
  }
  return OB_SUCCESS;
}
bool integer_class(SqlTypeClass tc) {return tc==ObIntTC||tc==ObUIntTC||tc==ObEnumSetTC;}
bool real_class(SqlTypeClass tc) {return tc==ObFloatTC||tc==ObDoubleTC;}
int numeric_order(const DatumValue &left,const DatumValue &right,int &result) {
  const auto l=left.get_type_class(),r=right.get_type_class();
  if(left.is_decimal_int())return decimal_order(left,right,result);
  if(right.is_decimal_int()){int ret=decimal_order(right,left,result);result=-result;return ret;}
  if(l==ObNumberTC)return number_order(left,right,result);
  if(r==ObNumberTC){int ret=number_order(right,left,result);result=-result;return ret;}
  if(real_class(l)&&real_class(r)){
    if(l==r){result=l==ObFloatTC?real_order(left.get_float(),right.get_float()):fixed_double(left)&&fixed_double(right)?fixed_order(left,right):real_order(left.get_double(),right.get_double());}
    else result=order(l==ObFloatTC?static_cast<double>(left.get_float()):left.get_double(),
                      r==ObFloatTC?static_cast<double>(right.get_float()):right.get_double());
  }else if(real_class(l)&&integer_class(r)){
    const double real=l==ObFloatTC?static_cast<double>(left.get_float()):left.get_double();
    // The historical mixed integer/REAL comparator casts both operands to
    // double, even when the REAL source is FLOAT. NaN is unordered here.
    result=order(real,r==ObIntTC?static_cast<double>(right.get_int()):static_cast<double>(right.get_uint64()));
  }else if(integer_class(l)&&real_class(r)){
    int ret=numeric_order(right,left,result);result=-result;return ret;
  }else if(integer_class(l)&&integer_class(r)){
    if(l==ObIntTC&&r==ObIntTC)result=order(left.get_int(),right.get_int());
    else if(l==ObIntTC)result=left.get_int()<0?-1:order(static_cast<uint64_t>(left.get_int()),right.get_uint64());
    else if(r==ObIntTC)result=right.get_int()<0?1:order(left.get_uint64(),static_cast<uint64_t>(right.get_int()));
    else result=order(left.get_uint64(),right.get_uint64());
  }else return OB_ERR_UNEXPECTED;
  return OB_SUCCESS;
}
int strings_order(const DatumValue &left,const DatumValue &right,int &result) {
  int ret=OB_SUCCESS;ObString l,r;
  const ObCollationType cs=left.get_collation_type();
  if(cs==CS_TYPE_INVALID||cs!=right.get_collation_type())ret=OB_ERR_UNEXPECTED;
  else if((ret=inrow(left,l))!=OB_SUCCESS){}
  else if((ret=inrow(right,r))!=OB_SUCCESS){}
  else result=ObCharset::strcmpsp(cs,l.ptr(),l.length(),r.ptr(),r.length(),false);
  return ret;
}
}
int validate_key_value(const DatumValue &value)
{
  if(!value.meta_.is_valid())return OB_ERR_UNEXPECTED;
  if(null_value(value))return OB_SUCCESS;
  const auto &datum=value.datum_;
  if(datum.len_>0&&datum.ptr_==nullptr)return OB_INVALID_ARGUMENT;
  if(value.is_ext())return !value.is_ext_sql_array()&&datum.has_ext_value()?OB_SUCCESS:OB_ERR_UNEXPECTED;
  switch(ObDatum::get_sql_value_datum_map_type(value.get_type())){
    case OBJ_DATUM_8BYTE_DATA:return datum.len_==sizeof(int64_t)?OB_SUCCESS:OB_INVALID_ARGUMENT;
    case OBJ_DATUM_4BYTE_DATA:return datum.len_==sizeof(int32_t)?OB_SUCCESS:OB_INVALID_ARGUMENT;
    case OBJ_DATUM_1BYTE_DATA:return datum.len_==sizeof(uint8_t)?OB_SUCCESS:OB_INVALID_ARGUMENT;
    case OBJ_DATUM_NUMBER:return datum.len_>=sizeof(number::ObNumber::Desc)&&datum.len_==sizeof(number::ObNumber::Desc)+sizeof(uint32_t)*datum.get_number_desc().len_?OB_SUCCESS:OB_INVALID_ARGUMENT;
    case OBJ_DATUM_DECIMALINT:return datum.len_==0||datum.len_==4||datum.len_==8||datum.len_==16||datum.len_==32||datum.len_==64?OB_SUCCESS:OB_INVALID_ARGUMENT;
    case OBJ_DATUM_STRING:return OB_SUCCESS;
    default:return OB_ERR_UNEXPECTED;
  }
}
int compare_key_value(const DatumValue &left,const DatumValue &right,int &result,bool collation_free)
{
  int ret=validate_key_value(left);result=0;
  if(ret==OB_SUCCESS)ret=validate_key_value(right);
  if(ret!=OB_SUCCESS)return ret;
  if(left.is_min_value()||left.is_max_value()||right.is_min_value()||right.is_max_value())
    result=(left.is_min_value()&&right.is_min_value())||(left.is_max_value()&&right.is_max_value())?0:left.is_min_value()||right.is_max_value()?-1:1;
  else if(left.is_ext()||right.is_ext())ret=OB_ERR_UNEXPECTED;
  else if(null_value(left)||null_value(right))result=null_value(left)==null_value(right)?0:null_value(left)?-1:1;
  else if(collation_free&&(left.get_collation_type()==CS_TYPE_COLLATION_FREE||right.get_collation_type()==CS_TYPE_COLLATION_FREE))return sortkey_order(left,right,result);
  else {
    const auto l=left.get_type_class(),r=right.get_type_class();
    if(integer_class(l)||real_class(l)||l==ObNumberTC||l==ObDecimalIntTC)ret=numeric_order(left,right,result);
    else if((l==ObStringTC||l==ObTextTC)&&(r==ObStringTC||r==ObTextTC))ret=strings_order(left,right,result);
    else if(l!=r)ret=OB_ERR_UNEXPECTED;
    else switch(l){
      case ObDateTimeTC:if(left.get_type()!=right.get_type())ret=OB_ERR_UNEXPECTED;else result=order(left.get_datetime(),right.get_datetime());break;
      case ObMySQLDateTimeTC:result=order(left.get_datetime(),right.get_datetime());break;
      case ObDateTC:case ObMySQLDateTC:result=order(left.get_date(),right.get_date());break;
      case ObTimeTC:result=order(left.get_time(),right.get_time());break;
      case ObYearTC:result=order(left.get_year(),right.get_year());break;
      case ObBitTC:result=order(left.get_bit(),right.get_bit());break;
      case ObRawTC:case ObCollectionSQLTC:case ObUserDefinedSQLTC:ret=strings_order(left,right,result);break;
      case ObGeometryTC:{ObString a,b;if((ret=inrow(left,a))==OB_SUCCESS&&(ret=inrow(right,b))==OB_SUCCESS)result=ObCharset::strcmpsp(CS_TYPE_BINARY,a.ptr(),a.length(),b.ptr(),b.length(),false);break;}
      case ObJsonTC:{
        ObString a,b;
        if((ret=inrow(left,a))==OB_SUCCESS&&(ret=inrow(right,b))==OB_SUCCESS){
          ObJsonBinCtx ac,bc;ObJsonBin av(a.ptr(),a.length(),&ac),bv(b.ptr(),b.length(),&bc);
          if((ret=av.reset_iter())==OB_SUCCESS&&(ret=bv.reset_iter())==OB_SUCCESS)ret=av.compare(bv,result);
        }
        break;
      }
      default:ret=OB_ERR_UNEXPECTED;break;
    }
  }
  if(ret==OB_SUCCESS)result=(result>0)-(result<0);
  return ret;
}
int equal_key_value(const DatumValue &left,const DatumValue &right,bool &equal)
{
  int ret=validate_key_value(left);equal=true;
  if(ret==OB_SUCCESS)ret=validate_key_value(right);
  if(ret!=OB_SUCCESS){equal=false;return ret;}
  const auto l=null_value(left)?ObNullTC:left.get_type_class(),r=null_value(right)?ObNullTC:right.get_type_class();
  if(l!=r)equal=false;
  else switch(l){
    case ObNullTC:break;
    case ObIntTC:case ObUIntTC:case ObEnumSetTC:case ObDateTimeTC:case ObMySQLDateTimeTC:case ObTimeTC:case ObBitTC:equal=left.get_int()==right.get_int();break;
    case ObExtendTC:equal=left.get_ext()==right.get_ext();break;
    case ObDateTC:case ObMySQLDateTC:equal=left.get_date()==right.get_date();break;
    case ObYearTC:equal=left.get_year()==right.get_year();break;
    case ObFloatTC:equal=left.get_float()==right.get_float();break;
    case ObDoubleTC:equal=fixed_double(left)&&fixed_double(right)?fixed_order(left,right)==0:left.get_double()==right.get_double();break;
    case ObStringTC:case ObRawTC:case ObTextTC:{
      ObString a,b;
      if((ret=inrow(left,a))==OB_SUCCESS&&(ret=inrow(right,b))==OB_SUCCESS){
        equal=a.length()==b.length()&&MEMCMP(a.ptr(),b.ptr(),a.length())==0;
        if(!equal)equal=ObCharset::strcmpsp(left.get_collation_type(),a.ptr(),a.length(),b.ptr(),b.length(),false)==0;
      }else equal=false;
      break;
    }
    case ObNumberTC:{
      const auto a=left.get_number(),b=right.get_number();
      equal=a.get_desc().se_==b.get_desc().se_&&a.get_desc().len_==b.get_desc().len_
          &&MEMCMP(a.get_digits(),b.get_digits(),sizeof(uint32_t)*a.get_desc().len_)==0;break;
    }
    case ObDecimalIntTC:{
      // Rowkey equal/simple_equal historically compare stored coefficients;
      // SQL comparison and common-prefix equality align scales separately.
      int cmp=0;ret=wide::compare(left,right,cmp);equal=ret==OB_SUCCESS&&cmp==0;break;
    }
    default:ret=OB_ERR_UNEXPECTED;equal=false;break;
  }
  return ret;
}
int sql_equal_key_value(const DatumValue &left,const DatumValue &right,bool &equal)
{
  int cmp=0;int ret=compare_key_value(left,right,cmp);equal=false;
  if(ret==OB_SUCCESS){
    const auto l=left.get_type_class(),r=right.get_type_class();
    if(!null_value(left)&&!null_value(right)&&real_class(l)&&real_class(r)){
      const double a=l==ObFloatTC?static_cast<double>(left.get_float()):left.get_double();
      const double b=r==ObFloatTC?static_cast<double>(right.get_float()):right.get_double();
      equal=l==r?(std::isnan(a)||std::isnan(b)?std::isnan(a)&&std::isnan(b):cmp==0)
          :!std::isnan(a)&&!std::isnan(b)&&cmp==0;
    }else if(!null_value(left)&&!null_value(right)
        &&((real_class(l)&&integer_class(r))||(integer_class(l)&&real_class(r)))){
      const DatumValue &real=real_class(l)?left:right;
      const double number=real.get_type_class()==ObFloatTC?static_cast<double>(real.get_float()):real.get_double();
      equal=!std::isnan(number)&&cmp==0;
    }else equal=cmp==0;
  }
  return ret;
}
int hash_key_value(const DatumValue &value,uint64_t seed,uint64_t &hash)
{
  int ret=validate_key_value(value);hash=seed;
  if(ret!=OB_SUCCESS)return ret;
  if(null_value(value)){const int type=ObNullType;hash=murmurhash(&type,sizeof(type),seed);}
  else switch(value.get_type_class()){
    case ObIntTC:case ObDateTimeTC:case ObMySQLDateTimeTC:case ObTimeTC:{const int64_t n=value.get_int();hash=murmurhash(&n,sizeof(n),seed);break;}
    case ObUIntTC:case ObEnumSetTC:case ObBitTC:{const uint64_t n=value.get_uint64();hash=murmurhash(&n,sizeof(n),seed);break;}
    case ObDateTC:case ObMySQLDateTC:case ObYearTC:{const int64_t n=value.get_type_class()==ObYearTC?value.get_year():value.get_date();hash=murmurhash(&n,sizeof(n),seed);break;}
    case ObExtendTC:{const int64_t n=value.get_ext();hash=murmurhash(&n,sizeof(n),seed);break;}
    case ObUnknownTC:{const int64_t n=value.get_unknown();hash=murmurhash(&n,sizeof(n),seed);break;}
    case ObFloatTC:case ObDoubleTC:{
      double number=value.get_type_class()==ObFloatTC?static_cast<double>(value.get_float()):value.get_double();
      if(number==0.0)number=0.0;else if(std::isnan(number))number=NAN;
      else if(fixed_double(value)){
        char bytes[OB_CAST_TO_VARCHAR_MAX_LENGTH];const int64_t n=ob_fcvt(number,value.get_scale(),sizeof(bytes)-1,bytes,nullptr);hash=murmurhash(bytes,n,seed);break;
      }
      hash=murmurhash(&number,sizeof(number),seed);break;
    }
    case ObNumberTC:hash=murmurhash(&value.datum_.get_number_desc().se_,1,seed);hash=murmurhash(value.datum_.get_number_digits(),sizeof(uint32_t)*value.datum_.get_number_desc().len_,hash);break;
    case ObDecimalIntTC:hash=murmurhash(value.get_decimal_int(),value.get_int_bytes(),seed);break;
    case ObStringTC:case ObRawTC:case ObTextTC:{ObString bytes;if((ret=inrow(value,bytes))==OB_SUCCESS)hash=ObCharset::hash(value.get_collation_type(),bytes.ptr(),bytes.length(),seed,false,nullptr);break;}
    case ObGeometryTC:{ObString bytes;if((ret=inrow(value,bytes))==OB_SUCCESS&&!bytes.empty())hash=ObCharset::hash(CS_TYPE_BINARY,bytes.ptr(),bytes.length(),seed,false,nullptr);break;}
    case ObJsonTC:{ObString bytes;if((ret=inrow(value,bytes))==OB_SUCCESS&&!bytes.empty()){ObJsonBinCtx ctx;ObJsonBin json(bytes.ptr(),bytes.length(),&ctx);if((ret=json.reset_iter())==OB_SUCCESS)ret=json.calc_json_hash_value(seed,key_hash_bytes,hash);}break;}
    default:hash=0;ret=OB_NOT_SUPPORTED;break;
  }
  return ret;
}
namespace {
template<class T>void fill_scalar(ObBatchChecksum &checksum,T value){checksum.fill(&value,sizeof(value));}
}
int checksum_key_value(const DatumValue &value,ObBatchChecksum &checksum)
{
  const int status=validate_key_value(value);if(status!=OB_SUCCESS)return status;
  const int type=null_value(value)?ObNullType:value.get_type();
  if(type==ObUnknownType){checksum.fill(&value.meta_,sizeof(value.meta_));return OB_SUCCESS;}
  checksum.fill(&type,sizeof(type));
  if(type==ObNullType)return OB_SUCCESS;
  switch(type){
    case ObTinyIntType:case ObUTinyIntType:fill_scalar(checksum,static_cast<int8_t>(value.get_int()));break;
    case ObSmallIntType:case ObUSmallIntType:fill_scalar(checksum,static_cast<int16_t>(value.get_int()));break;
    case ObMediumIntType:case ObInt32Type:case ObUMediumIntType:case ObUInt32Type:fill_scalar(checksum,static_cast<int32_t>(value.get_int()));break;
    case ObIntType:case ObUInt64Type:case ObBitType:case ObEnumType:case ObSetType:case ObDateTimeType:case ObTimestampType:case ObMySQLDateTimeType:case ObTimeType:fill_scalar(checksum,value.get_int());break;
    case ObExtendType:fill_scalar(checksum,value.get_ext());break;
    case ObDateType:case ObMySQLDateType:fill_scalar(checksum,value.get_date());break;
    case ObYearType:fill_scalar(checksum,value.get_year());break;
    case ObFloatType:case ObUFloatType:fill_scalar(checksum,value.get_float());break;
    case ObDoubleType:case ObUDoubleType:fill_scalar(checksum,value.get_double());break;
    case ObNumberType:case ObUNumberType:checksum.fill(&value.datum_.get_number_desc().se_,1);checksum.fill(value.datum_.get_number_digits(),sizeof(uint32_t)*value.datum_.get_number_desc().len_);break;
    case ObDecimalIntType:{const ObScale scale=value.get_scale();checksum.fill(&scale,sizeof(scale));checksum.fill(value.get_decimal_int(),value.get_int_bytes());break;}
    default:{
      if(value.get_type_class()!=ObUserDefinedSQLTC&&value.get_type_class()!=ObCollectionSQLTC){const int cs=value.get_collation_type();checksum.fill(&cs,sizeof(cs));}
      checksum.fill(value.get_string_ptr(),value.get_string_len());break;
    }
  }
  return OB_SUCCESS;
}
int transform_key_sortkey(const DatumValue &source,DatumValue &output,bool &valid_sortkey,ObIAllocator &owner)
{
  int ret=OB_SUCCESS;valid_sortkey=true;
  if(!source.meta_.is_character_type())ret=OB_INVALID_ARGUMENT;
  else {
    const int64_t size=source.get_string_len();const bool variable=source.get_collation_type()==CS_TYPE_UTF8MB4_GENERAL_CI;
    const int64_t capacity=variable?size*3+2:size*2;
    DatumValue candidate=source;
    if(size>0&&source.get_string_ptr()!=nullptr){
      char *bytes=static_cast<char *>(owner.alloc(capacity));
      if(bytes==nullptr)ret=OB_ALLOCATE_MEMORY_FAILED;
      else {
        const size_t n=variable?ObCharset::sortkey_var_len(source.get_collation_type(),source.get_string_ptr(),size,bytes,capacity,false,valid_sortkey):ObCharset::sortkey(source.get_collation_type(),source.get_string_ptr(),size,bytes,capacity,valid_sortkey);
        candidate.datum_.set_string(bytes,n);
      }
    }
    if(ret==OB_SUCCESS){candidate.set_collation_type(CS_TYPE_COLLATION_FREE);output=candidate;}
  }
  return ret;
}
} }
