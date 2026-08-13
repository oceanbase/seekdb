# Data Plane was extracted from SQL and Storage during the Bazel modularization.
# Its implementation is intentionally small; headers remain available through
# the common src include root used by the compatibility build.
ob_set_subtarget(ob_data_plane runtime
  ${SEEKDB_DATA_PLANE_PARALLEL_RANGE}
  ${SEEKDB_DATA_PLANE_TABLET_SCAN}
)
ob_server_add_target(ob_data_plane)
