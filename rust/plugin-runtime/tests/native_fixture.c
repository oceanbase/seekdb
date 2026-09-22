/* Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0. */
/* Minimal DSO for mapping tests, deliberately not an installable plugin. */
#if defined(_WIN32)
#include <windows.h>
#define EXPORT __declspec(dllexport)
#else
#define EXPORT __attribute__((visibility("default")))
#endif

static int *unload_count;
static void bind_counter(int *counter) { unload_count = counter; }
static int call(void) { return 42; }
struct fixture_api { void (*bind_counter)(int *); int (*call)(void); };
static const struct fixture_api api = {bind_counter, call};

#if !defined(OMIT_ENTRY)
EXPORT const void *seekdb_plugin_entry_v1(void) { return &api; }
#else
EXPORT int not_a_plugin_entry(void) { return 7; }
#endif

#if defined(_WIN32)
BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
  (void)instance; (void)reserved;
  if (reason == DLL_PROCESS_DETACH && unload_count) ++*unload_count;
  return TRUE;
}
#else
__attribute__((destructor)) static void unloaded(void)
{
  if (unload_count) ++*unload_count;
}
#endif
