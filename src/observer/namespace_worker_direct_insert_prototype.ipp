// Native direct-insert DAGs and slice writers stay beside shared tablets.
#include "data_plane/ddl/ob_direct_insert.h"
#include "data_plane/ddl/ob_ddl_schedule.h"
#include "sql/engine/basic/ob_temp_column_spill_spool.h"
#include "query/engine/vector/ob_i_vector.h"
#include "share/ob_ddl_checksum.h"
#include <map>
#include <mutex>
#include <shared_mutex>
namespace oceanbase { namespace observer { namespace namespace_worker_prototype {
using namespace data_plane;

#include "observer/namespace_inprocess_direct_insert_owner.ipp"

#include "observer/namespace_inprocess_direct_insert_route.ipp"

#include "observer/namespace_inprocess_direct_insert_service.ipp"
} } }
