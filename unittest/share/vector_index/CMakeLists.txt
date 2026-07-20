ob_unittest(test_vector_index_serialize)
ob_unittest(test_hybrid_search)
# Skip vsag adaptor unit test when VSAG is disabled (BUILD_CDC_ONLY).
if(NOT BUILD_EMBED_MODE AND NOT BUILD_CDC_ONLY AND NOT WIN32)
  ob_unittest(test_vsag_adaptor)
endif()
