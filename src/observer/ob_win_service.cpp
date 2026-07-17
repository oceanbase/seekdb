/*
 * Copyright (c) 2025 OceanBase.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifdef _WIN32

#include "observer/ob_win_service.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>
#include <vector>

#define MPRINT(format, ...) fprintf(stderr, format "\n", ##__VA_ARGS__)

namespace oceanbase {
namespace observer {

static SERVICE_STATUS        g_service_status = {};
static SERVICE_STATUS_HANDLE g_status_handle  = nullptr;
static ObServiceMainFunc     g_main_func      = nullptr;
static int                   g_saved_argc     = 0;
static char                **g_saved_argv     = nullptr;
static HANDLE                g_stop_event     = nullptr;

static void set_service_status(DWORD state, DWORD exit_code, DWORD wait_hint)
{
  static DWORD check_point = 1;
  g_service_status.dwCurrentState  = state;
  g_service_status.dwWin32ExitCode = exit_code;
  g_service_status.dwWaitHint      = wait_hint;
  if (state == SERVICE_START_PENDING) {
    g_service_status.dwControlsAccepted = 0;
  } else {
    g_service_status.dwControlsAccepted = SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN;
  }
  if (state == SERVICE_RUNNING || state == SERVICE_STOPPED) {
    g_service_status.dwCheckPoint = 0;
  } else {
    g_service_status.dwCheckPoint = check_point++;
  }
  if (g_status_handle != nullptr) {
    SetServiceStatus(g_status_handle, &g_service_status);
  }
}

static DWORD WINAPI delayed_terminate(LPVOID param)
{
  (void)param;
  Sleep(500);
  TerminateProcess(GetCurrentProcess(), 0);
  return 0;
}

static DWORD WINAPI service_ctrl_handler(DWORD control, DWORD event_type,
                                         LPVOID event_data, LPVOID context)
{
  (void)event_type;
  (void)event_data;
  (void)context;
  switch (control) {
    case SERVICE_CONTROL_STOP:
    case SERVICE_CONTROL_SHUTDOWN:
      set_service_status(SERVICE_STOPPED, NO_ERROR, 0);
      CreateThread(nullptr, 0, delayed_terminate, nullptr, 0, nullptr);
      return NO_ERROR;
    case SERVICE_CONTROL_INTERROGATE:
      return NO_ERROR;
    default:
      return ERROR_CALL_NOT_IMPLEMENTED;
  }
}

static void WINAPI service_main_entry(DWORD argc, LPWSTR *argv)
{
  (void)argc;
  (void)argv;

  g_status_handle = RegisterServiceCtrlHandlerExA(
      OB_DEFAULT_SERVICE_NAME, service_ctrl_handler, nullptr);
  if (g_status_handle == nullptr) {
    return;
  }

  g_service_status.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
  set_service_status(SERVICE_START_PENDING, NO_ERROR, 60000);

  g_stop_event = CreateEventA(nullptr, TRUE, FALSE, nullptr);

  set_service_status(SERVICE_RUNNING, NO_ERROR, 0);

  int ret = 0;
  if (g_main_func != nullptr) {
    ret = g_main_func(g_saved_argc, g_saved_argv);
  }

  set_service_status(SERVICE_STOPPED, ret == 0 ? NO_ERROR : ERROR_SERVICE_SPECIFIC_ERROR, 0);

  if (g_stop_event != nullptr) {
    CloseHandle(g_stop_event);
    g_stop_event = nullptr;
  }
}

static std::string build_service_binary_path(int argc, char *argv[])
{
  char exe_path[MAX_PATH] = {};
  GetModuleFileNameA(nullptr, exe_path, MAX_PATH);

  std::string cmd = "\"";
  cmd += exe_path;
  cmd += "\" --service";

  for (int i = 1; i < argc; i++) {
    const char *arg = argv[i];
    if (strcmp(arg, "--install-service") == 0) {
      if (i + 1 < argc && argv[i + 1][0] != '-') {
        i++;
      }
      continue;
    }
    cmd += " ";
    if (strchr(arg, ' ') != nullptr) {
      cmd += "\"";
      cmd += arg;
      cmd += "\"";
    } else {
      cmd += arg;
    }
  }
  return cmd;
}

int ob_install_win_service(const char *service_name, int argc, char *argv[])
{
  if (service_name == nullptr || service_name[0] == '\0') {
    service_name = OB_DEFAULT_SERVICE_NAME;
  }

  SC_HANDLE sc_manager = OpenSCManagerA(nullptr, nullptr, SC_MANAGER_CREATE_SERVICE);
  if (sc_manager == nullptr) {
    DWORD err = GetLastError();
    if (err == ERROR_ACCESS_DENIED) {
      MPRINT("Error: Access denied. Please run as Administrator to install the service.");
    } else {
      MPRINT("Error: Failed to open Service Control Manager (error %lu).", err);
    }
    return 1;
  }

  std::string binary_path = build_service_binary_path(argc, argv);

  SC_HANDLE service = CreateServiceA(
      sc_manager,
      service_name,
      OB_SERVICE_DISPLAY_NAME,
      SERVICE_ALL_ACCESS,
      SERVICE_WIN32_OWN_PROCESS,
      SERVICE_AUTO_START,
      SERVICE_ERROR_NORMAL,
      binary_path.c_str(),
      nullptr, nullptr, nullptr,
      nullptr,  // LocalSystem account
      nullptr);

  if (service == nullptr) {
    DWORD err = GetLastError();
    if (err == ERROR_SERVICE_EXISTS) {
      MPRINT("Error: Service '%s' already exists. Use --remove-service first.", service_name);
    } else {
      MPRINT("Error: Failed to create service '%s' (error %lu).", service_name, err);
    }
    CloseServiceHandle(sc_manager);
    return 1;
  }

  SERVICE_DESCRIPTIONA desc;
  desc.lpDescription = const_cast<char *>(OB_SERVICE_DESCRIPTION);
  ChangeServiceConfig2A(service, SERVICE_CONFIG_DESCRIPTION, &desc);

  SERVICE_FAILURE_ACTIONSA failure_actions = {};
  SC_ACTION actions[3];
  actions[0].Type  = SC_ACTION_RESTART;
  actions[0].Delay = 5000;
  actions[1].Type  = SC_ACTION_RESTART;
  actions[1].Delay = 10000;
  actions[2].Type  = SC_ACTION_NONE;
  actions[2].Delay = 0;
  failure_actions.dwResetPeriod = 86400;
  failure_actions.cActions      = 3;
  failure_actions.lpsaActions   = actions;
  ChangeServiceConfig2A(service, SERVICE_CONFIG_FAILURE_ACTIONS, &failure_actions);

  MPRINT("Service '%s' installed successfully.", service_name);
  MPRINT("Binary path: %s", binary_path.c_str());
  MPRINT("");
  MPRINT("To start the service:");
  MPRINT("  net start %s", service_name);
  MPRINT("To stop the service:");
  MPRINT("  net stop %s", service_name);

  CloseServiceHandle(service);
  CloseServiceHandle(sc_manager);
  return 0;
}

int ob_remove_win_service(const char *service_name)
{
  if (service_name == nullptr || service_name[0] == '\0') {
    service_name = OB_DEFAULT_SERVICE_NAME;
  }

  SC_HANDLE sc_manager = OpenSCManagerA(nullptr, nullptr, SC_MANAGER_CONNECT);
  if (sc_manager == nullptr) {
    DWORD err = GetLastError();
    if (err == ERROR_ACCESS_DENIED) {
      MPRINT("Error: Access denied. Please run as Administrator to remove the service.");
    } else {
      MPRINT("Error: Failed to open Service Control Manager (error %lu).", err);
    }
    return 1;
  }

  SC_HANDLE service = OpenServiceA(sc_manager, service_name, SERVICE_STOP | DELETE | SERVICE_QUERY_STATUS);
  if (service == nullptr) {
    DWORD err = GetLastError();
    if (err == ERROR_SERVICE_DOES_NOT_EXIST) {
      MPRINT("Error: Service '%s' does not exist.", service_name);
    } else {
      MPRINT("Error: Failed to open service '%s' (error %lu).", service_name, err);
    }
    CloseServiceHandle(sc_manager);
    return 1;
  }

  SERVICE_STATUS status;
  if (QueryServiceStatus(service, &status) && status.dwCurrentState != SERVICE_STOPPED) {
    MPRINT("Stopping service '%s'...", service_name);
    ControlService(service, SERVICE_CONTROL_STOP, &status);
    int wait_count = 0;
    while (wait_count < 30) {
      Sleep(1000);
      if (QueryServiceStatus(service, &status) && status.dwCurrentState == SERVICE_STOPPED) {
        break;
      }
      wait_count++;
    }
    if (status.dwCurrentState != SERVICE_STOPPED) {
      MPRINT("Warning: Service did not stop within 30 seconds.");
    }
  }

  if (!DeleteService(service)) {
    DWORD err = GetLastError();
    MPRINT("Error: Failed to delete service '%s' (error %lu).", service_name, err);
    CloseServiceHandle(service);
    CloseServiceHandle(sc_manager);
    return 1;
  }

  MPRINT("Service '%s' removed successfully.", service_name);
  CloseServiceHandle(service);
  CloseServiceHandle(sc_manager);
  return 0;
}

int ob_start_as_win_service(const char *service_name, ObServiceMainFunc main_func,
                            int argc, char *argv[])
{
  if (service_name == nullptr || service_name[0] == '\0') {
    service_name = OB_DEFAULT_SERVICE_NAME;
  }

  g_main_func = main_func;
  g_saved_argc = argc;
  g_saved_argv = argv;

  SERVICE_TABLE_ENTRYA dispatch_table[] = {
    { const_cast<char *>(service_name), (LPSERVICE_MAIN_FUNCTIONA)service_main_entry },
    { nullptr, nullptr }
  };

  if (!StartServiceCtrlDispatcherA(dispatch_table)) {
    DWORD err = GetLastError();
    if (err == ERROR_FAILED_SERVICE_CONTROLLER_CONNECT) {
      MPRINT("Error: This program is being run as a service but could not connect to the");
      MPRINT("Service Control Manager. If running from the command line, use --nodaemon");
      MPRINT("instead of --service.");
    } else {
      MPRINT("Error: StartServiceCtrlDispatcher failed (error %lu).", err);
    }
    return 1;
  }
  return 0;
}

void ob_report_win_service_running()
{
  if (g_status_handle != nullptr) {
    set_service_status(SERVICE_RUNNING, NO_ERROR, 0);
  }
}

void ob_report_win_service_stopped(DWORD exit_code)
{
  if (g_status_handle != nullptr) {
    set_service_status(SERVICE_STOPPED, exit_code, 0);
  }
}

} // namespace observer
} // namespace oceanbase

#endif // _WIN32
