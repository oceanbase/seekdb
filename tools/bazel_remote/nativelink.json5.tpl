// Single-node Bazel REAPI service for a trusted internal network.
// Replace @ROOT@ with an absolute installation directory before starting.
{
  stores: [
    {
      name: "CAS_MAIN_STORE",
      filesystem: {
        content_path: "@ROOT@/data/content_path-cas",
        temp_path: "@ROOT@/data/tmp_path-cas",
        eviction_policy: { max_bytes: 20000000000 },
      },
    },
    {
      name: "AC_MAIN_STORE",
      filesystem: {
        content_path: "@ROOT@/data/content_path-ac",
        temp_path: "@ROOT@/data/tmp_path-ac",
        eviction_policy: { max_bytes: 200000000 },
      },
    },
    {
      name: "WORKER_FAST_SLOW_STORE",
      fast_slow: {
        fast: {
          filesystem: {
            content_path: "@ROOT@/data/content_path-worker",
            temp_path: "@ROOT@/data/tmp_path-worker",
            eviction_policy: { max_bytes: 20000000000 },
          },
        },
        slow: { ref_store: { name: "CAS_MAIN_STORE" } },
      },
    },
  ],
  schedulers: [
    {
      name: "MAIN_SCHEDULER",
      simple: {
        supported_platform_properties: {
          cpu_count: "minimum",
          OSFamily: "priority",
          "container-image": "priority",
        },
      },
    },
  ],
  workers: [
    {
      local: {
        worker_api_endpoint: { uri: "grpc://127.0.0.1:50061" },
        cas_fast_slow_store: "WORKER_FAST_SLOW_STORE",
        upload_action_result: { ac_store: "AC_MAIN_STORE" },
        work_directory: "@ROOT@/work",
        use_namespaces: true,
        use_mount_namespace: true,
        platform_properties: {
          cpu_count: { values: ["1"] },
          OSFamily: { values: [""] },
          "container-image": { values: [""] },
        },
      },
    },
  ],
  servers: [
    {
      name: "internal_reapi",
      listener: {
        http: { socket_address: "0.0.0.0:50051" },
      },
      services: {
        cas: [{ cas_store: "CAS_MAIN_STORE" }],
        ac: [{ ac_store: "AC_MAIN_STORE" }],
        bytestream: [{ cas_store: "CAS_MAIN_STORE" }],
        execution: [
          { cas_store: "CAS_MAIN_STORE", scheduler: "MAIN_SCHEDULER" },
        ],
        capabilities: [
          { remote_execution: { scheduler: "MAIN_SCHEDULER" } },
        ],
      },
    },
    {
      name: "worker_api",
      listener: {
        http: { socket_address: "127.0.0.1:50061" },
      },
      services: {
        worker_api: { scheduler: "MAIN_SCHEDULER" },
        health: {},
      },
    },
  ],
  global: { max_open_files: 24576 },
}
