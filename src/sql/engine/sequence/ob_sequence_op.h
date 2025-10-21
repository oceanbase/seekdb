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

#ifndef _SRC_SQL_ENGINE_SEQENCE_OB_SEQUENCE_OP_H
#define _SRC_SQL_ENGINE_SEQENCE_OB_SEQUENCE_OP_H 1
#include "sql/engine/ob_operator.h"
#include "share/sequence/ob_sequence_cache.h"
#include "share/schema/ob_schema_struct.h"
#include "lib/mysqlclient/ob_isql_connection_pool.h"

namespace oceanbase
{
namespace obrpc
{
class ObGAISNextSequenceValRpcResult;
}
namespace share
{
class ObGAISNextSequenceValReq;
}
namespace sql
{
class ObSequenceSpec : public ObOpSpec
{
  OB_UNIS_VERSION_V(1);
public:
 

  ObSequenceSpec(common::ObIAllocator &alloc, const ObPhyOperatorType type);

  INHERIT_TO_STRING_KV("op_spec", ObOpSpec, K_(nextval_seq_ids));

  /*
   * Add nextval sequence id to ObSequence,
   * iterate one row at a time, take nextval for these ids, save to session,
   * for ObSeqNextvalExpr to read
   *
   * Note: To avoid duplicate calculations, each id can only be added once.
   * For example: Query select s.nextval as c1, s.nextval as c2 from dual;
   * The output values must satisfy c1 = c2
   */
  int add_uniq_nextval_sequence_id(uint64_t seq_id);
  common::ObFixedArray<uint64_t, common::ObIAllocator> nextval_seq_ids_;
};

class ObSequenceExecutor {
  public:
    ObSequenceExecutor()
      : dblink_id_(OB_INVALID_ID)
    {
      seq_schemas_.set_attr(ObMemAttr(OB_SYS_TENANT_ID, "SeqSchema"));
      seq_ids_.set_attr(ObMemAttr(OB_SYS_TENANT_ID, "SeqId"));
    }
    ~ObSequenceExecutor() { destroy(); }
    virtual int init(ObExecContext &ctx)=0;
    virtual void reset() { seq_ids_.reset(); seq_schemas_.reset();}
    virtual void destroy() { seq_ids_.reset(); seq_schemas_.reset(); }
    virtual int get_nextval(ObExecContext &ctx)=0;
    int add_sequence_id(uint64_t id) { return seq_ids_.push_back(id); }
    TO_STRING_KV(K_(seq_ids), K_(dblink_id));
  protected:
    // schema put into context is to utilize its cache capability
    common::ObSEArray<share::schema::ObSequenceSchema, 1> seq_schemas_;
    common::ObSEArray<uint64_t, 2> seq_ids_;
    uint64_t dblink_id_;
};

class ObLocalSequenceExecutor : public ObSequenceExecutor {
  public:
    ObLocalSequenceExecutor();
    ~ObLocalSequenceExecutor();
    virtual int init(ObExecContext &ctx) override;
    virtual void reset() override;
    virtual void destroy() override;
    virtual int get_nextval(ObExecContext &ctx) override;
    int handle_gais_request(const share::ObGAISNextSequenceValReq &request,
                                  obrpc::ObGAISNextSequenceValRpcResult &result);
  private:
    // sequence exposes to user layer is a cache
    // cache underlying responsible for sequence cache update and global coordination
    share::ObSequenceCache *sequence_cache_;
};

class ObSequenceOp : public ObOperator
{
public:
  ObSequenceOp(ObExecContext &exec_ctx, const ObOpSpec &spec, ObOpInput *input);
  ~ObSequenceOp();

  virtual int inner_get_next_row() override;
  virtual int inner_open() override;
  virtual int inner_close() override;

  void reset() { }

  virtual void destroy() override;
private:
  int init_op();
  /**
   * For select, update statements, sequence has child
   * For insert statement, sequence does not have child
   * This function decides whether to take the next row from child based on the number of children
   */
  int try_get_next_row();
private:
  common::ObSEArray<ObSequenceExecutor*, 1> seq_executors_;
};

} // end namespace sql
} // end namespace oceanbase

#endif /* _SRC_SQL_ENGINE_SEQENCE_OB_SEQUENCE_OP_H */
