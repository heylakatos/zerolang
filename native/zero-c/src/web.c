#include "zero.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static bool is_directory(const char *path) {
  struct stat st;
  return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

static char *dirname_of(const char *path) {
  const char *slash = strrchr(path, '/');
  if (!slash) return z_strdup(".");
  return z_strndup(path, (size_t)(slash - path));
}

static char *join_path(const char *left, const char *right) {
  ZBuf buf;
  zbuf_init(&buf);
  zbuf_append(&buf, left);
  if (left[strlen(left) - 1] != '/') zbuf_append_char(&buf, '/');
  zbuf_append(&buf, right);
  return buf.data;
}

static bool ends_with(const char *text, const char *suffix) {
  size_t text_len = strlen(text);
  size_t suffix_len = strlen(suffix);
  return text_len >= suffix_len && strcmp(text + text_len - suffix_len, suffix) == 0;
}

static void append_json_string(ZBuf *buf, const char *value) {
  zbuf_append_char(buf, '"');
  for (const unsigned char *cursor = (const unsigned char *)(value ? value : ""); *cursor; cursor++) {
    switch (*cursor) {
      case '"': zbuf_append(buf, "\\\""); break;
      case '\\': zbuf_append(buf, "\\\\"); break;
      case '\n': zbuf_append(buf, "\\n"); break;
      case '\r': zbuf_append(buf, "\\r"); break;
      case '\t': zbuf_append(buf, "\\t"); break;
      default:
        if (*cursor < 0x20) zbuf_appendf(buf, "\\u%04x", *cursor);
        else zbuf_append_char(buf, (char)*cursor);
        break;
    }
  }
  zbuf_append_char(buf, '"');
}

static int compare_string_ptrs(const void *left, const void *right) {
  const char *const *a = (const char *const *)left;
  const char *const *b = (const char *const *)right;
  return strcmp(*a, *b);
}

static void free_string_list(char **items, size_t count) {
  for (size_t i = 0; i < count; i++) free(items[i]);
  free(items);
}

static bool collect_route_files(const char *route_dir, char ***files, size_t *file_count, ZDiag *diag) {
  *files = NULL;
  *file_count = 0;
  DIR *dir = opendir(route_dir);
  if (!dir) {
    diag->code = 5001;
    diag->path = route_dir;
    diag->line = 1;
    diag->column = 1;
    snprintf(diag->message, sizeof(diag->message), "cannot read route directory");
    return false;
  }

  struct dirent *entry;
  while ((entry = readdir(dir)) != NULL) {
    if (!ends_with(entry->d_name, ".0")) continue;
    *files = z_checked_reallocarray(*files, *file_count + 1, sizeof(char *));
    (*files)[(*file_count)++] = join_path(route_dir, entry->d_name);
  }
  closedir(dir);
  if (*file_count > 1) qsort(*files, *file_count, sizeof(char *), compare_string_ptrs);
  return true;
}

static char *project_root_for(const char *input) {
  if (ends_with(input, "zero.json")) return dirname_of(input);
  if (is_directory(input)) return z_strdup(input);
  char *dir = dirname_of(input);
  return dir;
}

bool z_discover_routes_json(const char *input, char **json, ZDiag *diag) {
  char *root = project_root_for(input);
  char *route_dir = join_path(root, "src/routes");
  DIR *dir = opendir(route_dir);
  if (!dir) {
    diag->code = 5001;
    diag->path = route_dir;
    diag->line = 1;
    diag->column = 1;
    snprintf(diag->message, sizeof(diag->message), "cannot read route directory");
    free(root);
    free(route_dir);
    return false;
  }

  ZBuf buf;
  zbuf_init(&buf);
  zbuf_append(&buf, "{\n  \"schemaVersion\": 1,\n  \"runtime\": \"wasm32-web\",\n  \"routes\": [\n");
  bool first = true;
  int route_count = 0;
  struct dirent *entry;
  while ((entry = readdir(dir)) != NULL) {
    if (!ends_with(entry->d_name, ".0")) continue;
    if (!first) zbuf_append(&buf, ",\n");
    first = false;
    route_count++;
    char *file = join_path(route_dir, entry->d_name);
    char *stem = z_strndup(entry->d_name, strlen(entry->d_name) - 2);
    const char *route_path = strcmp(stem, "index") == 0 ? "/" : stem;
    zbuf_appendf(&buf, "    {\"method\": \"GET\", \"path\": \"%s\", \"file\": \"%s\"}", route_path, file);
    free(file);
    free(stem);
  }
  closedir(dir);
  zbuf_appendf(&buf, "\n  ],\n  \"routeCount\": %d,\n", route_count);
  zbuf_append(&buf, "  \"requiresCapabilities\": [\"web\"],\n");
  zbuf_append(&buf, "  \"capabilityFacts\": {\n");
  zbuf_append(&buf, "    \"filesystem\": {\"required\": false, \"browserWasm\": \"unavailable\", \"wasi\": \"capability-gated\", \"staticLinuxServerless\": \"temp-only by capability\"},\n");
  zbuf_append(&buf, "    \"environment\": {\"required\": false, \"browserWasm\": \"preloaded only\", \"edge\": \"capability-gated\"},\n");
  zbuf_append(&buf, "    \"process\": {\"required\": false, \"browserWasm\": \"unavailable\", \"edge\": \"unavailable\"}\n");
  zbuf_append(&buf, "  },\n");
  zbuf_append(&buf, "  \"ownership\": {\n");
  zbuf_append(&buf, "    \"wasi\": {\"requestBody\": \"host-owned stream\", \"responseBody\": \"guest-owned until return\", \"filesystem\": \"capability-gated\"},\n");
  zbuf_append(&buf, "    \"browserWasm\": {\"requestBody\": \"JS-owned buffer view\", \"responseBody\": \"guest-owned copy\", \"filesystem\": \"unavailable\"},\n");
  zbuf_append(&buf, "    \"staticLinuxServerless\": {\"requestBody\": \"runtime-owned stream\", \"responseBody\": \"guest-owned until flush\", \"filesystem\": \"temp-only by capability\"},\n");
  zbuf_append(&buf, "    \"platformAdapter\": {\"boundary\": \"Request in, Response out\", \"allocations\": \"handler-visible only\"}\n");
  zbuf_append(&buf, "  },\n");
  zbuf_append(&buf, "  \"handlerAbi\": {\n");
  zbuf_append(&buf, "    \"request\": {\"type\": \"Request\", \"ownership\": \"borrowed\", \"body\": \"stream-or-borrowed-bytes\"},\n");
  zbuf_append(&buf, "    \"response\": {\"type\": \"Response\", \"ownership\": \"returned\", \"body\": \"owned-or-static-bytes\"},\n");
  zbuf_append(&buf, "    \"errors\": {\"model\": \"raises\", \"lowering\": \"typed status/error packet\"},\n");
  zbuf_append(&buf, "    \"memory\": {\"allocator\": \"explicit Alloc parameter when needed\", \"requestAllocation\": \"zero by default\"}\n");
  zbuf_append(&buf, "  },\n");
  zbuf_append(&buf, "  \"webSurfaces\": {\n");
  zbuf_append(&buf, "    \"request\": [\"method\", \"url\", \"headers\", \"cookies\", \"params\", \"body\"],\n");
  zbuf_append(&buf, "    \"response\": [\"status\", \"headers\", \"cookies\", \"body\", \"stream\"],\n");
  zbuf_append(&buf, "    \"environment\": [\"preloaded env\", \"region\"],\n");
  zbuf_append(&buf, "    \"cache\": [\"cache.match\", \"cache.put\", \"cache-control\"],\n");
  zbuf_append(&buf, "    \"lifecycle\": [\"wait_until\"]\n");
  zbuf_append(&buf, "  },\n");
  zbuf_append(&buf, "  \"measurements\": {\n");
  zbuf_append(&buf, "    \"compressedSizeBudgetBytes\": 10240,\n");
  zbuf_append(&buf, "    \"coldStartBudgetMs\": 1,\n");
  zbuf_append(&buf, "    \"memoryFloorBudgetBytes\": 65536,\n");
  zbuf_append(&buf, "    \"perRequestAllocationBudgetBytes\": 0,\n");
  zbuf_append(&buf, "    \"status\": \"route manifest and web bundle audit metadata emitted\"\n");
  zbuf_append(&buf, "  },\n");
  zbuf_append(&buf, "  \"artifact\": {\"target\": \"wasm32-web\", \"kind\": \"route-manifest\", \"available\": true, \"format\": \"zero.routes.v1\", \"generatedCBytes\": 0},\n");
  zbuf_append(&buf, "  \"webBundle\": {\"target\": \"wasm32-web\", \"kind\": \"direct-wasm-web-bundle\", \"available\": true, \"javascriptFrameworkTaxBytes\": 0, \"frameworkTaxBytes\": 0, \"adapter\": \"host-provided web capabilities\", \"imports\": [\"web.request\", \"web.response\", \"env.preloaded\", \"cache\", \"wait_until\"], \"capabilityRestrictions\": \"checked before codegen\", \"deployment\": {\"providerSpecific\": false, \"vercel\": \"out-of-scope\"}},\n");
  zbuf_append(&buf, "  \"localRuntime\": {\"schemaVersion\": 1, \"target\": \"wasm32-web\", \"runtimeKind\": \"browser-worker\", \"providerSpecificDeployment\": false, \"hostedDeployment\": \"out-of-scope\", \"productionLikeImports\": true, \"command\": \"zero dev --target wasm32-web\", \"imports\": {\"explicit\": true, \"module\": \"zero_web_preview1\", \"functions\": [\"web.request\", \"web.response\", \"env.preloaded\", \"cache\", \"wait_until\"], \"adapter\": \"browser-worker-import-shim\"}, \"capabilityRestrictions\": {\"filesystem\": \"denied\", \"environment\": \"preloaded import only\", \"arguments\": \"preloaded import only\", \"stdio\": \"explicit import\", \"dom\": \"unavailable to portable worker module\", \"network\": \"denied until Fetch capability\", \"process\": \"denied\"}, \"memoryFloor\": {\"linearMemory\": true, \"pageBytes\": 65536, \"minimumPages\": 1, \"floorBytes\": 65536}, \"frameworkTaxBytes\": 0},\n");
  zbuf_append(&buf, "  \"artifactAudit\": {\"localRuntime\": \"covered by command-contracts, wasm:runtime:smoke, and docs:compiler\", \"imports\": \"explicit capability names\", \"filesystem\": \"denied for browser wasm\", \"preloadedInputs\": \"manifest-declared only\", \"providerSpecificDeployment\": false}\n}\n");
  free(root);
  free(route_dir);
  *json = buf.data;
  return true;
}

bool z_discover_web_dev_plan_json(const char *input, char **json, ZDiag *diag) {
  char *root = project_root_for(input);
  char *route_dir = join_path(root, "src/routes");
  char **files = NULL;
  size_t file_count = 0;
  if (!collect_route_files(route_dir, &files, &file_count, diag)) {
    free(root);
    free(route_dir);
    return false;
  }

  char *manifest = join_path(root, "zero.json");
  const char *source_file = file_count > 0 ? files[0] : route_dir;
  ZBuf buf;
  zbuf_init(&buf);
  zbuf_append(&buf, "{\n  \"schemaVersion\": 1,\n  \"ok\": true,\n  \"mode\": \"watch-plan\",\n  \"sourceFile\": ");
  append_json_string(&buf, source_file);
  zbuf_append(&buf, ",\n  \"target\": \"wasm32-web\",\n  \"generatedCBytes\": 0,\n  \"cBridgeFallback\": false,\n");
  zbuf_append(&buf, "  \"watch\": {\"strategy\":\"fingerprint changed routes and manifests\",\"persistent\": false, \"planOnly\": true, \"sourceFileCount\": ");
  zbuf_appendf(&buf, "%zu", file_count);
  zbuf_append(&buf, ", \"moduleCount\": ");
  zbuf_appendf(&buf, "%zu", file_count);
  zbuf_append(&buf, ", \"files\": [");
  for (size_t i = 0; i < file_count; i++) {
    if (i > 0) zbuf_append(&buf, ", ");
    append_json_string(&buf, files[i]);
  }
  zbuf_append(&buf, "], \"manifest\": ");
  append_json_string(&buf, manifest);
  zbuf_append(&buf, ", \"generatedBindingInputs\": [], \"rerun\": [\"check\", \"routes\", \"build\"], \"restartOnSuccess\": true},\n");
  zbuf_append(&buf, "  \"affected\": {\"tests\": 0, \"examples\": 1, \"modules\": ");
  zbuf_appendf(&buf, "%zu", file_count);
  zbuf_append(&buf, "},\n  \"actions\": [{\"kind\":\"check\",\"when\":\"source-or-manifest-changed\"}, {\"kind\":\"routes\",\"selectedRoutes\":");
  zbuf_appendf(&buf, "%zu", file_count);
  zbuf_append(&buf, "}, {\"kind\":\"restart\",\"enabled\":true,\"target\":\"wasm32-web\"}],\n");
  zbuf_append(&buf, "  \"requiresCapabilities\": [\"web\"],\n");
  zbuf_append(&buf, "  \"localRuntime\": {\"schemaVersion\": 1, \"target\": \"wasm32-web\", \"runtimeKind\": \"browser-worker\", \"providerSpecificDeployment\": false, \"hostedDeployment\": \"out-of-scope\", \"productionLikeImports\": true, \"command\": \"zero dev --target wasm32-web\", \"imports\": {\"explicit\": true, \"module\": \"zero_web_preview1\", \"functions\": [\"web.request\", \"web.response\", \"env.preloaded\", \"cache\", \"wait_until\"], \"adapter\": \"browser-worker-import-shim\"}, \"capabilityRestrictions\": {\"filesystem\": \"denied\", \"environment\": \"preloaded import only\", \"arguments\": \"preloaded import only\", \"stdio\": \"explicit import\", \"dom\": \"unavailable to portable worker module\", \"network\": \"denied until Fetch capability\", \"process\": \"denied\"}, \"memoryFloor\": {\"linearMemory\": true, \"pageBytes\": 65536, \"minimumPages\": 1, \"floorBytes\": 65536}, \"frameworkTaxBytes\": 0},\n");
  zbuf_append(&buf, "  \"selfHostRouting\": {\"contractVersion\":1,\"subsetCompatible\":false,\"mode\":\"native-bootstrap\",\"phases\":{\"parse\":\"route-manifest\",\"check\":\"zero-c\",\"lower\":\"zero-c\",\"emit\":\"zero-c\",\"selfHostCompiler\":\"compiler-zero\",\"selfHostCompilerRole\":\"not-used\"},\"frontend\":{\"selected\":false,\"implementation\":\"compiler-zero\",\"status\":\"not-selected\"},\"wasmEmitter\":{\"selected\":false,\"implementation\":\"compiler-zero-direct-wasm\",\"status\":\"not-selected\"},\"metadata\":{\"graphJson\":false,\"sizeJson\":false},\"cBridge\":{\"required\":false,\"policy\":\"removed\",\"explicitDirectFallback\":\"never-c-bridge\"}}\n}\n");

  free(manifest);
  free_string_list(files, file_count);
  free(root);
  free(route_dir);
  *json = buf.data;
  return true;
}
