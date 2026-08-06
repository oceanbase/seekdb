ob_set_subtarget(oblib_rpc common
  ob_lock_wait_node.cpp
  ob_request.cpp
  ob_sql_request_operator.cpp
  ob_sql_mem_pool.cpp
)

ob_set_subtarget(oblib_rpc frame
  frame/ob_result_code.cpp
  frame/ob_net_easy.cpp
  frame/ob_req_deliver.cpp
  frame/ob_req_qhandler.cpp
  frame/ob_req_queue_thread.cpp
  frame/ob_req_translator.cpp
  frame/ob_sql_processor.cpp
)

ob_set_subtarget(oblib_rpc obmysql
  obmysql/ob_mysql_packet.cpp
  obmysql/ob_mysql_util.cpp
  obmysql/ob_nio_abi_check.cpp
  obmysql/ob_sql_nio_server.cpp
  obmysql/ob_sql_sock_handler.cpp
  obmysql/ob_sql_sock_session.cpp
)

ob_set_subtarget(oblib_rpc obmysql_packet
  obmysql/packet/ompk_eof.cpp
  obmysql/packet/ompk_error.cpp
  obmysql/packet/ompk_local_infile.cpp
  obmysql/packet/ompk_ok.cpp
  obmysql/packet/ompk_auth_switch.cpp
)


ob_lib_add_target(oblib_rpc)
