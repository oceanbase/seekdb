// In-process scan schema, storage execution, and iterator adapters.
#include "data_plane/access/ob_table_scan_param.h"
#include "storage/tx_storage/ob_access_service.h"
#include "storage/access/ob_table_scan_iterator.h"
#include "data_plane/access/ob_table_param.h"
#include "sql/engine/basic/ob_pushdown_filter.h"
#include "data_plane/transaction/ob_tx_desc_access.h"
namespace oceanbase { namespace observer { namespace namespace_worker_prototype {
using namespace common;
using namespace share;
using namespace share::schema;
using namespace storage;
#include "observer/namespace_inprocess_scan_schema.ipp"
#include "observer/namespace_inprocess_scan_engine.ipp"
#include "observer/namespace_inprocess_scan_service.ipp"
} } }
