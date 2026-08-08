ob_unittest(test_vector_index_serialize)
ob_unittest(test_hybrid_search)
# ob_vsag_adaptor.cpp is empty when OB_BUILD_CDC_DISABLE_VSAG (BUILD_EMBED_MODE / BUILD_CDC_ONLY / Windows).
if(NOT BUILD_EMBED_MODE AND NOT BUILD_CDC_ONLY AND NOT WIN32)
  ob_unittest(test_vsag_adaptor)
endif()
