ob_set_subtarget(oblib_rpc common
  ob_lock_wait_node.cpp
  ob_request.cpp
  ob_req_operator.cpp
  ob_sql_request_operator.cpp
  ob_sql_mem_pool.cpp
)

ob_set_subtarget(oblib_rpc frame
  frame/ob_net_consts.cpp
  frame/ob_result_code.cpp
  frame/ob_req_session_handler.cpp
  frame/ob_net_easy.cpp
  frame/ob_req_deliver.cpp
  frame/ob_req_handler.cpp
  frame/ob_req_processor.cpp
  frame/ob_req_qhandler.cpp
  frame/ob_req_queue_thread.cpp
  frame/ob_req_translator.cpp
  frame/ob_req_transport.cpp
  frame/ob_sql_processor.cpp
)

ob_set_subtarget(oblib_rpc obmysql
  obmysql/ob_2_0_protocol_processor.cpp
  obmysql/ob_2_0_protocol_utils.cpp
  obmysql/ob_easy_sql_request_operator.cpp
  obmysql/ob_mysql_compress_protocol_processor.cpp
  obmysql/ob_mysql_field.cpp
  obmysql/ob_mysql_packet.cpp
  obmysql/ob_mysql_protocol_processor.cpp
  obmysql/ob_mysql_request_utils.cpp
  obmysql/ob_mysql_row.cpp
  obmysql/ob_mysql_translator.cpp
  obmysql/ob_mysql_util.cpp
  obmysql/ob_poc_sql_request_operator.cpp
  obmysql/ob_sql_nio.cpp
  obmysql/ob_sql_nio_server.cpp
  obmysql/ob_sql_nio_win_pipe.cpp
  obmysql/ob_sql_sock_handler.cpp
  obmysql/ob_sql_sock_processor.cpp
  obmysql/ob_sql_sock_session.cpp
  obmysql/ob_virtual_cs_protocol_processor.cpp
  obmysql/obp20_extra_info.cpp
  obmysql/ob_packet_record.cpp
)

ob_set_subtarget(oblib_rpc obmysql_packet
  obmysql/packet/ompk_change_user.cpp
  obmysql/packet/ompk_eof.cpp
  obmysql/packet/ompk_error.cpp
  obmysql/packet/ompk_field.cpp
  obmysql/packet/ompk_handshake.cpp
  obmysql/packet/ompk_handshake_response.cpp
  obmysql/packet/ompk_local_infile.cpp
  obmysql/packet/ompk_ok.cpp
  obmysql/packet/ompk_piece.cpp
  obmysql/packet/ompk_prepare.cpp
  obmysql/packet/ompk_prexecute.cpp
  obmysql/packet/ompk_resheader.cpp
  obmysql/packet/ompk_row.cpp
  obmysql/packet/ompk_ssl_request.cpp
  obmysql/packet/ompk_auth_switch.cpp
)


ob_lib_add_target(oblib_rpc)
