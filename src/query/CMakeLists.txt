# Query owns the narrow runtime seams extracted from SQL and Observer.
ob_set_subtarget(ob_query runtime
  ${SEEKDB_QUERY_MYSQL_PROTOCOL}
  ${SEEKDB_QUERY_SCHEDULER}
  ${SEEKDB_QUERY_VECTOR_EMBEDDING}
)
ob_server_add_target(ob_query)
