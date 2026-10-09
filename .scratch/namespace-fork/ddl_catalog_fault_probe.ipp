// Local deterministic fault hook, compiled only into the probe binary.
static int ddl_catalog_fault(const char *stage, uint64_t ns_id)
{
  const char *path = getenv("SEEKDB_DDL_CATALOG_CONTROL");
  if (!path) { return OB_SUCCESS; }
  FILE *input = fopen(path, "r");
  if (!input) { return OB_SUCCESS; }
  unsigned long long selected = 0;
  char wanted[64] = {}, action[64] = {};
  const int fields = fscanf(input, "%llu %63s %63s", &selected, wanted, action);
  fclose(input);
  if (fields != 3 || selected != ns_id || strcmp(wanted, stage) != 0) { return OB_SUCCESS; }
  const std::string ready = std::string(path) + ".ready";
  FILE *output = fopen(ready.c_str(), "w");
  if (output) { fprintf(output, "%llu %s %s\n", selected, stage, action); fclose(output); }
  fprintf(stderr, "DDL_CATALOG_FAULT namespace=%llu stage=%s action=%s\n", selected, stage, action);
  if (strcmp(action, "fail") == 0) { return OB_ERR_UNEXPECTED; }
  if (strcmp(action, "unknown") == 0) { return OB_TIMEOUT; }
  const bool once = strcmp(action, "pause_once") == 0;
  const std::string release = std::string(path) + ".release";
  if (once) { unlink(path); }
  auto waiting = [&]() { return once ? access(release.c_str(), F_OK) != 0 : access(path, F_OK) == 0; };
  const int64_t until = ObTimeUtility::current_time() + 60000000;
  while (waiting() && ObTimeUtility::current_time() < until) { usleep(10000); }
  return waiting() ? OB_TIMEOUT : OB_SUCCESS;
}
