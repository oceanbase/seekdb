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

#ifndef OBDEV_SRC_SQL_ENGINE_CONNECTOR_OB_JNI_CONNECTOR_H_
#define OBDEV_SRC_SQL_ENGINE_CONNECTOR_OB_JNI_CONNECTOR_H_

#include <jni.h>
#include "lib/string/ob_string.h"
#include "lib/utility/ob_print_utils.h"
#include "lib/container/ob_array.h"

namespace oceanbase {
namespace common {
class ObSqlString;
}

namespace sql {
class ObJniConnector {
public:
  enum OdpsType {
    /**
     * 8-byte signed integer
     */
    BIGINT,
    /**
     * double precision floating point
     */
    DOUBLE,
    /**
     * Boolean
     */
    BOOLEAN,
    /**
     * Date type
     */
    DATETIME,
    /**
     * string type
     */
    STRING,
    /**
     * precise decimal type
     */
    DECIMAL,
    /**
     * MAP type
     */
    MAP,
    /**
     * ARRAY type
     */
    ARRAY,
    /**
     * empty
     */
    VOID,
    /**
     * 1 byte signed integer
     */
    TINYINT,
    /**
     * 2-byte signed integer
     */
    SMALLINT,
    /**
     * 4-byte signed integer
     */
    INT,
    /**
     * single precision float
     */
    FLOAT,
    /**
     * Fixed length string
     */
    CHAR,
    /**
     * variable length string
     */
    VARCHAR,
    /**
     * Time type
     */
    DATE,
    /**
     * timestamp
     */
    TIMESTAMP,
    /**
     * byte array
     */
    BINARY,
    /**
     * Date interval
     */
    INTERVAL_DAY_TIME,
    /**
     * Year interval
     */
    INTERVAL_YEAR_MONTH,
    /**
     * structure
     */
    STRUCT,
    /**
     * JSON type
     */
    JSON,
    /**
     * Time zone agnostic timestamp
     */
    TIMESTAMP_NTZ,
    /**
     * Unsupported types from external systems
     */
    UNKNOWN
  };

  struct MirrorOdpsJniColumn{
    MirrorOdpsJniColumn():
      name_(""),
      type_(OdpsType::UNKNOWN),
      precision_(-1),
      scale_(-1),
      length_(-1),
      type_size_(-1),
      type_expr_("") {}
    
    // simple primitive
    MirrorOdpsJniColumn(ObString name, OdpsType type, int32_t type_size,
                        ObString type_expr)
        : name_(name), type_(type), type_size_(type_size),
          type_expr_(type_expr) {}

    // char/varchar (default is -1 (in java side))
    MirrorOdpsJniColumn(ObString name, OdpsType type, int32_t length,
                        int32_t type_size, ObString type_expr)
        : name_(name), type_(type), length_(length), type_size_(type_size),
          type_expr_(type_expr) {}

    // decimal
    MirrorOdpsJniColumn(ObString name, OdpsType type, int32_t precision,
                        int32_t scale, int32_t type_size, ObString type_expr)
        : name_(name), type_(type), precision_(precision), scale_(scale),
          type_size_(type_size), type_expr_(type_expr) {}

    // array
    MirrorOdpsJniColumn(ObString name, OdpsType type)
    : name_(name),
      type_(type),
      type_size_(-1),
      type_expr_(),
      child_columns_() {}

    virtual ~MirrorOdpsJniColumn() {
      child_columns_.reset();
    }
    int assign(const MirrorOdpsJniColumn &other);

    ObString name_;
    OdpsType type_;
    int32_t precision_ = -1;
    int32_t scale_ = -1;
    int32_t length_ = -1;
    int32_t type_size_; // type size is useful to calc offset on memory
    ObString type_expr_;
    ObArray<MirrorOdpsJniColumn> child_columns_;

    TO_STRING_KV(K(name_), K(type_), K(precision_), K(scale_), K(length_),
                 K(type_size_), K(type_expr_), K(child_columns_));
  };

  struct OdpsJNIColumn {
    OdpsJNIColumn():
      name_(""),
      type_("") {}

    OdpsJNIColumn(ObString name, ObString type) :
      name_(name),
      type_(type) {}

    ObString name_;
    ObString type_;
    TO_STRING_KV(K(name_), K(type_));
  };

  struct OdpsJNIPartition {
    OdpsJNIPartition() {}

    OdpsJNIPartition(ObString partition_spec, int record_count)
        : partition_spec_(partition_spec), record_count_(record_count) {}

    ObString partition_spec_;
    int record_count_;

    TO_STRING_KV(K(partition_spec_), K(record_count_));
  };

  class JniTableMeta {
    private:
      long *batch_meta_ptr_;
      int batch_meta_index_;

    public:
      JniTableMeta() {
        batch_meta_ptr_ = nullptr;
        batch_meta_index_ = 0;
      }

      JniTableMeta(long meta_addr) {
        batch_meta_ptr_ =
            static_cast<long *>(reinterpret_cast<void *>(meta_addr));
        batch_meta_index_ = 0;
      }

      void set_meta(long meta_addr) {
        batch_meta_ptr_ =
            static_cast<long *>(reinterpret_cast<void *>(meta_addr));
        batch_meta_index_ = 0;
      }

      long next_meta_as_long() { 
        return batch_meta_ptr_[batch_meta_index_++]; 
      }

      void *next_meta_as_ptr() {
        return reinterpret_cast<void *>(batch_meta_ptr_[batch_meta_index_++]);
      }
  };

public:
  static bool is_java_env_inited();
  static int get_jni_env(JNIEnv *&env);
  static int check_jni_exception_(JNIEnv *env);
  static OdpsType get_odps_type_by_string(ObString type);
};

}
}


#endif
