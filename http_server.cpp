#include <atomic>
#include <chrono>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <mutex>
#include <algorithm>
#include <string>
#include <sys/stat.h>
#include <sys/sysinfo.h>
#include <thread>
#include <time.h>
#include <unistd.h>
#include <vector>

#include "file_io.h"
#include "fpga_io.h"
#include "httplib.h"
#include "menu.h"
#include "user_io.h"
#include "support/arcade/mra_loader.h"
#include "support/neogeo/neogeo_loader.h"
#include "version.h"

extern const char *version;

namespace {

constexpr const char *ROOT_DIR = "/media/fat";
constexpr const char *GAMES_ROOT_DIR = "/media/fat/games";
constexpr const char *NEOGEO_GAMES_DIR = "/media/fat/games/NEOGEO";
constexpr const char *HTTP_LOG_PATH = "/media/fat/mister-http-startup.log";
constexpr const char *STARTUP_GAME_PATH = "/media/fat/mister-http-startup-game.txt";
constexpr const char *STARTUP_GAME_NOT_CONFIGURED_ERROR = "startup game not configured";
constexpr const char *PENDING_NEO_ENV = "MISTER_HTTP_PENDING_NEO_PATH";
constexpr int STARTUP_GAME_AUTOLAUNCH_DELAY_SECONDS = 15;
constexpr const char *NEOGEO_CORE_SUBDIR = "_Console";
constexpr const char *NEOGEO_CORE_HINTS[] = {
  "/media/fat/_Console/NeoGeo_20250909.rbf",
  "/media/fat/_Console/NeoGeo.rbf",
};

std::atomic<bool> server_started{false};
std::atomic<bool> restart_requested{false};
std::atomic<bool> startup_game_autolaunch_checked{false};
std::thread server_thread;
std::mutex launch_queue_mutex;
std::chrono::steady_clock::time_point startup_game_autolaunch_not_before;

struct game_inventory_entry {
  std::string file_name;
  std::string relative_path;
  std::string bucket;
  std::string extension;
  long long size_bytes;
};

struct queued_launch_command {
  std::string path;
  bool is_neo;
};

struct process_memory_snapshot {
  long long vm_rss_kb = -1;
  long long vm_hwm_kb = -1;
  long long vm_size_kb = -1;
  long long vm_data_kb = -1;
  long long vm_swap_kb = -1;
  int threads = -1;
};

struct system_memory_snapshot {
  long long total_kb = -1;
  long long free_kb = -1;
  long long available_kb = -1;
  long long buffers_kb = -1;
  long long cached_kb = -1;
  long long shared_kb = -1;
  long long swap_total_kb = -1;
  long long swap_free_kb = -1;
};

std::vector<queued_launch_command> launch_queue;

void resolve_device_identifier(std::string &device_id, std::string &device_id_source);
bool read_startup_game_path(std::string &path, bool &is_neo, std::string &error);

bool should_skip_inventory_directory(const char *name)
{
  return !strcasecmp(name, "_disabled")
      || !strcasecmp(name, "exclusion-list")
      || !strcasecmp(name, "block-list")
      || !strcasecmp(name, "quarantine");
}

bool path_has_extension(const char *name, const char *const *extensions, size_t extension_count)
{
  const char *extension = std::strrchr(name, '.');
  if (!extension) {
    return false;
  }

  for (size_t index = 0; index < extension_count; ++index) {
    if (!strcasecmp(extension, extensions[index])) {
      return true;
    }
  }

  return false;
}

std::string get_path_extension_lower(const char *name)
{
  const char *extension = std::strrchr(name, '.');
  if (!extension) {
    return {};
  }

  std::string value = extension;
  std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
    return static_cast<char>(std::tolower(ch));
  });
  return value;
}

std::string get_inventory_bucket(const std::string &relative_path)
{
  const size_t separator = relative_path.find('/');
  if (separator == std::string::npos) {
    return {};
  }

  return relative_path.substr(0, separator);
}

void append_json_escaped(std::string &body, const std::string &value)
{
  body.push_back('"');
  for (char ch : value) {
    switch (ch) {
      case '\\': body += "\\\\"; break;
      case '"': body += "\\\""; break;
      case '\b': body += "\\b"; break;
      case '\f': body += "\\f"; break;
      case '\n': body += "\\n"; break;
      case '\r': body += "\\r"; break;
      case '\t': body += "\\t"; break;
      default:
        if (static_cast<unsigned char>(ch) < 0x20) {
          char encoded[7] = {};
          std::snprintf(encoded, sizeof(encoded), "\\u%04x", static_cast<unsigned char>(ch));
          body += encoded;
        } else {
          body.push_back(ch);
        }
        break;
    }
  }
  body.push_back('"');
}

void collect_game_inventory_entries(const std::string &base_path,
                                    const std::string &current_path,
                                    bool exclude_neogeo_directory,
                                    const char *const *extensions,
                                    size_t extension_count,
                                    std::vector<game_inventory_entry> &entries)
{
  DIR *directory = opendir(current_path.c_str());
  if (!directory) {
    return;
  }

  while (dirent *entry = readdir(directory)) {
    if (!std::strcmp(entry->d_name, ".") || !std::strcmp(entry->d_name, "..")) {
      continue;
    }

    const std::string full_path = current_path + "/" + entry->d_name;
    struct stat file_stat = {};
    if (stat(full_path.c_str(), &file_stat) != 0) {
      continue;
    }

    if (S_ISDIR(file_stat.st_mode)) {
      if (should_skip_inventory_directory(entry->d_name)) {
        continue;
      }

      if (exclude_neogeo_directory && !strcasecmp(full_path.c_str(), NEOGEO_GAMES_DIR)) {
        continue;
      }

      collect_game_inventory_entries(base_path, full_path, exclude_neogeo_directory, extensions, extension_count, entries);
      continue;
    }

    if (!S_ISREG(file_stat.st_mode) || !path_has_extension(entry->d_name, extensions, extension_count)) {
      continue;
    }

    std::string relative_path = full_path;
    if (relative_path.size() > base_path.size() + 1 && !relative_path.compare(0, base_path.size(), base_path)) {
      relative_path.erase(0, base_path.size() + 1);
    }

    entries.push_back({
      entry->d_name,
      relative_path,
      get_inventory_bucket(relative_path),
      get_path_extension_lower(entry->d_name),
      static_cast<long long>(file_stat.st_size)
    });
  }

  closedir(directory);
}

std::vector<game_inventory_entry> get_game_inventory_entries(const char *base_path,
                                                             bool exclude_neogeo_directory,
                                                             const char *const *extensions,
                                                             size_t extension_count)
{
  std::vector<game_inventory_entry> entries;
  if (!PathIsDir(base_path, 0)) {
    return entries;
  }

  collect_game_inventory_entries(base_path, base_path, exclude_neogeo_directory, extensions, extension_count, entries);
  std::sort(entries.begin(), entries.end(), [](const game_inventory_entry &left, const game_inventory_entry &right) {
    return strcasecmp(left.relative_path.c_str(), right.relative_path.c_str()) < 0;
  });
  return entries;
}

void append_inventory_entries_json(std::string &body, const std::vector<game_inventory_entry> &entries)
{
  body += "[";
  for (size_t index = 0; index < entries.size(); ++index) {
    if (index > 0) {
      body += ",";
    }

    const game_inventory_entry &entry = entries[index];
    body += "{";
    body += "\"fileName\":";
    append_json_escaped(body, entry.file_name);
    body += ",\"relativePath\":";
    append_json_escaped(body, entry.relative_path);
    body += ",\"bucket\":";
    append_json_escaped(body, entry.bucket);
    body += ",\"extension\":";
    append_json_escaped(body, entry.extension);
    body += ",\"sizeBytes\":" + std::to_string(entry.size_bytes);
    body += "}";
  }
  body += "]";
}

bool try_parse_inventory_system_filter(const httplib::Request &req, bool &include_arcade, bool &include_neogeo, std::string &error)
{
  include_arcade = true;
  include_neogeo = true;

  auto it = req.params.find("system");
  if (it == req.params.end() || it->second.empty()) {
    return true;
  }

  if (!strcasecmp(it->second.c_str(), "arcade")) {
    include_neogeo = false;
    return true;
  }

  if (!strcasecmp(it->second.c_str(), "neogeo")) {
    include_arcade = false;
    return true;
  }

  if (!strcasecmp(it->second.c_str(), "all")) {
    return true;
  }

  error = "system must be one of arcade, neogeo, or all";
  return false;
}

std::string build_games_inventory_response(bool include_arcade, bool include_neogeo)
{
  static const char *const arcade_extensions[] = { ".zip", ".7z", ".chd" };
  static const char *const neogeo_extensions[] = { ".neo" };

  std::string body = "{\"ok\":true,\"root\":";
  append_json_escaped(body, GAMES_ROOT_DIR);
  body += ",\"systems\":{";

  bool wrote_system = false;
  if (include_arcade) {
    const std::vector<game_inventory_entry> entries = get_game_inventory_entries(
        GAMES_ROOT_DIR,
        true,
        arcade_extensions,
        sizeof(arcade_extensions) / sizeof(arcade_extensions[0]));

    body += "\"arcade\":{";
    body += "\"root\":";
    append_json_escaped(body, GAMES_ROOT_DIR);
    body += ",\"exists\":";
    body += PathIsDir(GAMES_ROOT_DIR, 0) ? "true" : "false";
    body += ",\"count\":" + std::to_string(entries.size());
    body += ",\"entries\":";
    append_inventory_entries_json(body, entries);
    body += "}";
    wrote_system = true;
  }

  if (include_neogeo) {
    const std::vector<game_inventory_entry> entries = get_game_inventory_entries(
        NEOGEO_GAMES_DIR,
        false,
        neogeo_extensions,
        sizeof(neogeo_extensions) / sizeof(neogeo_extensions[0]));

    if (wrote_system) {
      body += ",";
    }

    body += "\"neogeo\":{";
    body += "\"root\":";
    append_json_escaped(body, NEOGEO_GAMES_DIR);
    body += ",\"exists\":";
    body += PathIsDir(NEOGEO_GAMES_DIR, 0) ? "true" : "false";
    body += ",\"count\":" + std::to_string(entries.size());
    body += ",\"entries\":";
    append_inventory_entries_json(body, entries);
    body += "}";
  }

  body += "}}";
  return body;
}

void log_http_server(const char *message)
{
  FILE *log = std::fopen(HTTP_LOG_PATH, "a");
  if (log) {
    time_t now = time(nullptr);
    std::fprintf(log, "[%ld] %s\n", static_cast<long>(now), message);
    std::fclose(log);
  }
  std::fprintf(stderr, "%s\n", message);
  std::fflush(stderr);
}

bool path_is_under_root(const std::string &path)
{
  return path.rfind(ROOT_DIR, 0) == 0;
}

bool write_pending_neo_launch(const std::string &path)
{
  return setenv(PENDING_NEO_ENV, path.c_str(), 1) == 0;
}

void clear_pending_neo_launch()
{
  unsetenv(PENDING_NEO_ENV);
}

bool read_text_file_line(const char *path, std::string &value)
{
  FILE *file = std::fopen(path, "r");
  if (!file) {
    return false;
  }

  char buffer[2048] = {};
  const bool has_line = std::fgets(buffer, sizeof(buffer), file) != nullptr;
  std::fclose(file);
  if (!has_line) {
    return false;
  }

  buffer[strcspn(buffer, "\r\n")] = 0;
  if (!buffer[0]) {
    return false;
  }

  value = buffer;
  return true;
}

bool read_text_file(const char *path, std::string &value)
{
  FILE *file = std::fopen(path, "r");
  if (!file) {
    return false;
  }

  value.clear();
  char buffer[1024] = {};
  while (std::fgets(buffer, sizeof(buffer), file)) {
    value += buffer;
  }

  std::fclose(file);
  return true;
}

bool try_extract_proc_key_kb(const std::string &content, const char *key, long long &value)
{
  const std::string prefix = std::string(key) + ":";
  const size_t start = content.find(prefix);
  if (start == std::string::npos) {
    return false;
  }

  size_t cursor = start + prefix.size();
  while (cursor < content.size() && (content[cursor] == ' ' || content[cursor] == '\t')) {
    ++cursor;
  }

  char *end_ptr = nullptr;
  const long long parsed = std::strtoll(content.c_str() + cursor, &end_ptr, 10);
  if (end_ptr == content.c_str() + cursor) {
    return false;
  }

  value = parsed;
  return true;
}

bool try_extract_proc_key_int(const std::string &content, const char *key, int &value)
{
  const std::string prefix = std::string(key) + ":";
  const size_t start = content.find(prefix);
  if (start == std::string::npos) {
    return false;
  }

  size_t cursor = start + prefix.size();
  while (cursor < content.size() && (content[cursor] == ' ' || content[cursor] == '\t')) {
    ++cursor;
  }

  char *end_ptr = nullptr;
  const long parsed = std::strtol(content.c_str() + cursor, &end_ptr, 10);
  if (end_ptr == content.c_str() + cursor) {
    return false;
  }

  value = static_cast<int>(parsed);
  return true;
}

bool try_read_process_memory_snapshot(process_memory_snapshot &snapshot)
{
  std::string status;
  if (!read_text_file("/proc/self/status", status)) {
    return false;
  }

  try_extract_proc_key_kb(status, "VmRSS", snapshot.vm_rss_kb);
  try_extract_proc_key_kb(status, "VmHWM", snapshot.vm_hwm_kb);
  try_extract_proc_key_kb(status, "VmSize", snapshot.vm_size_kb);
  try_extract_proc_key_kb(status, "VmData", snapshot.vm_data_kb);
  try_extract_proc_key_kb(status, "VmSwap", snapshot.vm_swap_kb);
  try_extract_proc_key_int(status, "Threads", snapshot.threads);
  return true;
}

bool try_read_system_memory_snapshot(system_memory_snapshot &snapshot)
{
  std::string meminfo;
  if (!read_text_file("/proc/meminfo", meminfo)) {
    return false;
  }

  try_extract_proc_key_kb(meminfo, "MemTotal", snapshot.total_kb);
  try_extract_proc_key_kb(meminfo, "MemFree", snapshot.free_kb);
  try_extract_proc_key_kb(meminfo, "MemAvailable", snapshot.available_kb);
  try_extract_proc_key_kb(meminfo, "Buffers", snapshot.buffers_kb);
  try_extract_proc_key_kb(meminfo, "Cached", snapshot.cached_kb);
  try_extract_proc_key_kb(meminfo, "Shmem", snapshot.shared_kb);
  try_extract_proc_key_kb(meminfo, "SwapTotal", snapshot.swap_total_kb);
  try_extract_proc_key_kb(meminfo, "SwapFree", snapshot.swap_free_kb);
  return true;
}

void append_json_number_or_null(std::string &body, long long value)
{
  if (value >= 0) {
    body += std::to_string(value);
  } else {
    body += "null";
  }
}

void append_json_number_or_null(std::string &body, int value)
{
  if (value >= 0) {
    body += std::to_string(value);
  } else {
    body += "null";
  }
}

void append_json_double(std::string &body, double value)
{
  char buffer[64] = {};
  std::snprintf(buffer, sizeof(buffer), "%.2f", value);
  body += buffer;
}

std::string build_diagnostics_response()
{
  std::string device_id;
  std::string device_id_source;
  resolve_device_identifier(device_id, device_id_source);

  system_memory_snapshot system_memory;
  const bool has_system_memory = try_read_system_memory_snapshot(system_memory);

  process_memory_snapshot process_memory;
  const bool has_process_memory = try_read_process_memory_snapshot(process_memory);

  struct sysinfo info = {};
  const bool has_sysinfo = sysinfo(&info) == 0;

  double load1 = 0.0;
  double load5 = 0.0;
  double load15 = 0.0;
  if (has_sysinfo) {
    load1 = static_cast<double>(info.loads[0]) / 65536.0;
    load5 = static_cast<double>(info.loads[1]) / 65536.0;
    load15 = static_cast<double>(info.loads[2]) / 65536.0;
  }

  const char *current_core_path = user_io_get_current_rbf_path();
  size_t pending_launches = 0;
  {
    std::lock_guard<std::mutex> lock(launch_queue_mutex);
    pending_launches = launch_queue.size();
  }

  std::string pending_neo_path;
  const char *pending_neo_env = std::getenv(PENDING_NEO_ENV);
  if (pending_neo_env && pending_neo_env[0]) {
    pending_neo_path = pending_neo_env;
  }

  std::string startup_game_path;
  bool startup_game_is_neo = false;
  std::string startup_game_error;
  const bool startup_game_configured = read_startup_game_path(startup_game_path, startup_game_is_neo, startup_game_error);

  std::string body = "{\"ok\":true,\"profile\":\"basic\",\"mode\":\"native-runtime\",\"pid\":";
  body += std::to_string(static_cast<long long>(getpid()));
  body += ",\"build\":{";
  body += "\"number\":";
  append_json_escaped(body, APP_BUILD_NUMBER_STR);
  body += ",\"semanticVersion\":";
  append_json_escaped(body, APP_BUILD_VERSION);
  body += ",\"hash\":";
  append_json_escaped(body, APP_BUILD_HASH);
  body += ",\"date\":";
  append_json_escaped(body, APP_BUILD_DATE);
  body += "},\"device\":{";
  body += "\"id\":";
  append_json_escaped(body, device_id);
  body += ",\"idSource\":";
  append_json_escaped(body, device_id_source);
  body += "},\"runtime\":{";
  body += "\"currentCorePath\":";
  if (current_core_path && current_core_path[0]) {
    append_json_escaped(body, current_core_path);
  } else {
    body += "null";
  }
  body += ",\"isNeoGeoCoreActive\":";
  body += is_neogeo() ? "true" : "false";
  body += ",\"restartPending\":";
  body += restart_requested.load() ? "true" : "false";
  body += ",\"pendingLaunchCount\":" + std::to_string(static_cast<long long>(pending_launches));
  body += ",\"pendingNeoLaunchPath\":";
  if (!pending_neo_path.empty()) {
    append_json_escaped(body, pending_neo_path);
  } else {
    body += "null";
  }
  body += ",\"startupGameConfigured\":";
  body += startup_game_configured ? "true" : "false";
  body += ",\"startupGamePath\":";
  if (startup_game_configured) {
    append_json_escaped(body, startup_game_path);
  } else {
    body += "null";
  }
  body += ",\"startupGameMode\":";
  if (startup_game_configured) {
    append_json_escaped(body, startup_game_is_neo ? "neogeo" : "standard");
  } else {
    body += "null";
  }
  body += "},\"system\":{";
  body += "\"sysinfoAvailable\":";
  body += has_sysinfo ? "true" : "false";
  body += ",\"memoryInfoAvailable\":";
  body += has_system_memory ? "true" : "false";
  body += ",\"uptimeSeconds\":";
  if (has_sysinfo) {
    body += std::to_string(static_cast<long long>(info.uptime));
  } else {
    body += "null";
  }
  body += ",\"loadAverage\":{";
  body += "\"oneMinute\":";
  if (has_sysinfo) {
    append_json_double(body, load1);
  } else {
    body += "null";
  }
  body += ",\"fiveMinute\":";
  if (has_sysinfo) {
    append_json_double(body, load5);
  } else {
    body += "null";
  }
  body += ",\"fifteenMinute\":";
  if (has_sysinfo) {
    append_json_double(body, load15);
  } else {
    body += "null";
  }
  body += "},\"memoryKb\":{";
  body += "\"total\":";
  append_json_number_or_null(body, system_memory.total_kb);
  body += ",\"free\":";
  append_json_number_or_null(body, system_memory.free_kb);
  body += ",\"available\":";
  append_json_number_or_null(body, system_memory.available_kb);
  body += ",\"buffers\":";
  append_json_number_or_null(body, system_memory.buffers_kb);
  body += ",\"cached\":";
  append_json_number_or_null(body, system_memory.cached_kb);
  body += ",\"shared\":";
  append_json_number_or_null(body, system_memory.shared_kb);
  body += ",\"swapTotal\":";
  append_json_number_or_null(body, system_memory.swap_total_kb);
  body += ",\"swapFree\":";
  append_json_number_or_null(body, system_memory.swap_free_kb);
  body += "}},\"process\":{";
  body += "\"memoryInfoAvailable\":";
  body += has_process_memory ? "true" : "false";
  body += ",\"memoryKb\":{";
  body += "\"rss\":";
  append_json_number_or_null(body, process_memory.vm_rss_kb);
  body += ",\"peakRss\":";
  append_json_number_or_null(body, process_memory.vm_hwm_kb);
  body += ",\"virtualSize\":";
  append_json_number_or_null(body, process_memory.vm_size_kb);
  body += ",\"data\":";
  append_json_number_or_null(body, process_memory.vm_data_kb);
  body += ",\"swap\":";
  append_json_number_or_null(body, process_memory.vm_swap_kb);
  body += "},\"threads\":";
  append_json_number_or_null(body, process_memory.threads);
  body += "}}";
  return body;
}

bool is_zero_mac_address(const std::string &value)
{
  for (char ch : value) {
    if (ch == ':' || ch == '-') {
      continue;
    }

    if (ch != '0') {
      return false;
    }
  }

  return !value.empty();
}

bool is_broadcast_mac_address(const std::string &value)
{
  bool has_hex = false;
  for (char ch : value) {
    if (ch == ':' || ch == '-') {
      continue;
    }

    has_hex = true;
    if (ch != 'f' && ch != 'F') {
      return false;
    }
  }

  return has_hex;
}

bool is_valid_mac_address(const std::string &value)
{
  return !value.empty() && !is_zero_mac_address(value) && !is_broadcast_mac_address(value);
}

bool read_binary_file(const char *path, unsigned char *buffer, size_t size)
{
  FILE *file = std::fopen(path, "rb");
  if (!file) {
    return false;
  }

  const size_t bytes_read = std::fread(buffer, 1, size, file);
  std::fclose(file);
  return bytes_read == size;
}

std::string format_mac_address(const unsigned char *bytes, size_t size)
{
  char formatted[18] = {};
  if (size < 6) {
    return {};
  }

  std::snprintf(formatted,
                sizeof(formatted),
                "%02x:%02x:%02x:%02x:%02x:%02x",
                bytes[0],
                bytes[1],
                bytes[2],
                bytes[3],
                bytes[4],
                bytes[5]);
  return formatted;
}

bool try_read_binary_mac_address(const char *path, std::string &value)
{
  unsigned char bytes[6] = {};
  if (!read_binary_file(path, bytes, sizeof(bytes))) {
    return false;
  }

  value = format_mac_address(bytes, sizeof(bytes));
  if (!is_valid_mac_address(value)) {
    value.clear();
    return false;
  }

  return true;
}

bool try_read_text_mac_address(const char *path, std::string &value)
{
  if (!read_text_file_line(path, value)) {
    return false;
  }

  if (!is_valid_mac_address(value)) {
    value.clear();
    return false;
  }

  return true;
}

bool try_read_interface_mac_address(const char *interface_name, std::string &value, std::string &source)
{
  char path[160] = {};

  std::snprintf(path, sizeof(path), "/sys/class/net/%s/perm_address", interface_name);
  if (try_read_text_mac_address(path, value)) {
    source = std::string("mac-permanent:") + interface_name;
    return true;
  }

  std::snprintf(path, sizeof(path), "/sys/class/net/%s/device/of_node/local-mac-address", interface_name);
  if (try_read_binary_mac_address(path, value)) {
    source = std::string("mac-device-tree-local:") + interface_name;
    return true;
  }

  std::snprintf(path, sizeof(path), "/sys/class/net/%s/device/of_node/mac-address", interface_name);
  if (try_read_binary_mac_address(path, value)) {
    source = std::string("mac-device-tree:") + interface_name;
    return true;
  }

  std::string assign_type;
  std::snprintf(path, sizeof(path), "/sys/class/net/%s/addr_assign_type", interface_name);
  const bool has_assign_type = read_text_file_line(path, assign_type);

  std::snprintf(path, sizeof(path), "/sys/class/net/%s/address", interface_name);
  if (!try_read_text_mac_address(path, value)) {
    return false;
  }

  if (has_assign_type && assign_type != "0") {
    value.clear();
    return false;
  }

  source = std::string("mac-current:") + interface_name;
  return true;
}

void resolve_device_identifier(std::string &device_id, std::string &device_id_source)
{
  if (try_read_interface_mac_address("eth0", device_id, device_id_source)) {
    return;
  }

  if (try_read_interface_mac_address("wlan0", device_id, device_id_source)) {
    return;
  }

  if (read_text_file_line("/etc/machine-id", device_id)) {
    device_id_source = "machine-id";
    return;
  }

  device_id.clear();
  device_id_source.clear();
}

bool path_is_neogeo_core(const char *path)
{
  if (!path) return false;

  const char *name = std::strrchr(path, '/');
  name = name ? name + 1 : path;

  const char *extension = std::strrchr(name, '.');
  if (!extension || strcasecmp(extension, ".rbf")) {
    return false;
  }

  if (strncasecmp(name, "NeoGeo", 6)) {
    return false;
  }

  const char separator = name[6];
  return separator == '_' || separator == '.';
}

bool resolve_neogeo_core_path_via_shell(std::string &path)
{
  const char *command =
      "for f in /media/fat/_Console/NeoGeo*.rbf; do "
      "[ -e \"$f\" ] || continue; "
      "case \"$(basename \"$f\")\" in NeoGeoPocket*) continue ;; esac; "
      "printf '%s\\n' \"$f\"; "
      "done | tail -n 1";

  FILE *pipe = popen(command, "r");
  if (!pipe) {
    return false;
  }

  char buffer[1024] = {};
  const bool has_path = std::fgets(buffer, sizeof(buffer), pipe) != nullptr;
  pclose(pipe);
  if (!has_path) {
    return false;
  }

  buffer[strcspn(buffer, "\r\n")] = 0;
  if (!path_is_neogeo_core(buffer)) {
    return false;
  }

  path = buffer;
  return true;
}

bool resolve_neogeo_core_path(std::string &path, std::string &error)
{
  const char *root_dir = getRootDir();
  if (!root_dir || !root_dir[0]) {
    error = "root directory unavailable";
    return false;
  }

  char console_subdir[64] = {};
  std::snprintf(console_subdir, sizeof(console_subdir), "%s", NEOGEO_CORE_SUBDIR);

  std::string best_match;
  if (ScanDirectory(console_subdir, SCANF_INIT, "rbf", 0)) {
    for (int index = 0; index < flist_nDirEntries(); ++index) {
      direntext_t *entry = flist_DirItem(index);
      if (!entry || entry->de.d_type != DT_REG) {
        continue;
      }

      if (!path_is_neogeo_core(entry->de.d_name)) {
        continue;
      }

      const std::string candidate = std::string(root_dir) + "/" + NEOGEO_CORE_SUBDIR + "/" + entry->de.d_name;
      if (best_match.empty() || strcasecmp(candidate.c_str(), best_match.c_str()) > 0) {
        best_match = candidate;
      }
    }
  }

  if (best_match.empty()) {
    if (resolve_neogeo_core_path_via_shell(path)) {
      return true;
    }

    for (const char *candidate : NEOGEO_CORE_HINTS) {
      if (path_is_neogeo_core(candidate)) {
        path = candidate;
        return true;
      }
    }

    error = std::string("no NeoGeo core found in ") + root_dir + "/" + NEOGEO_CORE_SUBDIR;
    return false;
  }

  path = best_match;
  return true;
}

struct quiet_load_scope
{
  quiet_load_scope() { menu_set_quiet_load(1); }
  ~quiet_load_scope() { menu_set_quiet_load(0); }
};

bool queue_launch_command(const std::string &path, bool is_neo, std::string &error)
{
  std::lock_guard<std::mutex> lock(launch_queue_mutex);
  if (launch_queue.size() >= 8) {
    error = "launch queue full";
    return false;
  }

  launch_queue.push_back({path, is_neo});
  return true;
}

bool parse_path_param(const httplib::Request &req, std::string &path, std::string &error)
{
  auto it = req.params.find("path");
  if (it == req.params.end() || it->second.empty()) {
    error = "missing path";
    return false;
  }

  path = it->second;
  if (path.empty() || path[0] != '/') {
    error = "path must be absolute";
    return false;
  }

  if (!path_is_under_root(path)) {
    error = "path must be under /media/fat";
    return false;
  }

  return true;
}

bool try_parse_launch_target(const std::string &path, bool &is_neo, std::string &error)
{
  const auto ext_pos = path.find_last_of('.');
  const std::string ext = ext_pos == std::string::npos ? std::string() : path.substr(ext_pos);
  is_neo = ext == ".neo";

  if (ext != ".rbf" && ext != ".mra" && ext != ".mgl" && !is_neo) {
    error = "unsupported extension";
    return false;
  }

  return true;
}

bool parse_launch_request(const httplib::Request &req, std::string &path, bool &is_neo, std::string &error)
{
  if (!parse_path_param(req, path, error)) {
    return false;
  }

  return try_parse_launch_target(path, is_neo, error);
}

bool read_startup_game_path(std::string &path, bool &is_neo, std::string &error)
{
  if (!read_text_file_line(STARTUP_GAME_PATH, path)) {
    error = STARTUP_GAME_NOT_CONFIGURED_ERROR;
    return false;
  }

  if (path.empty() || path[0] != '/') {
    error = "stored startup path must be absolute";
    return false;
  }

  if (!path_is_under_root(path)) {
    error = "stored startup path must be under /media/fat";
    return false;
  }

  return try_parse_launch_target(path, is_neo, error);
}

bool write_startup_game_path(const std::string &path)
{
  FILE *file = std::fopen(STARTUP_GAME_PATH, "w");
  if (!file) {
    return false;
  }

  const bool ok = std::fprintf(file, "%s\n", path.c_str()) > 0;
  std::fclose(file);
  return ok;
}

bool clear_startup_game_path()
{
  if (std::remove(STARTUP_GAME_PATH) == 0) {
    return true;
  }

  return errno == ENOENT;
}

std::string build_startup_game_response()
{
  std::string path;
  bool is_neo = false;
  std::string error;
  const bool configured = read_startup_game_path(path, is_neo, error);

  std::string body = "{\"ok\":true,\"configured\":";
  body += configured ? "true" : "false";
  body += ",\"path\":";
  if (configured) {
    append_json_escaped(body, path);
  } else {
    body += "null";
  }

  body += ",\"mode\":";
  if (configured) {
    append_json_escaped(body, is_neo ? "neogeo" : "standard");
  } else {
    body += "null";
  }

  body += ",\"storagePath\":";
  append_json_escaped(body, STARTUP_GAME_PATH);

  body += ",\"error\":";
  if (configured || error == STARTUP_GAME_NOT_CONFIGURED_ERROR) {
    body += "null";
  } else {
    append_json_escaped(body, error);
  }

  body += "}";
  return body;
}

bool queue_restart_request(std::string &error)
{
  bool expected = false;
  if (!restart_requested.compare_exchange_strong(expected, true)) {
    return true;
  }

  try {
    std::thread([]() {
      log_http_server("HTTP restart requested; rebooting MiSTer");
      std::this_thread::sleep_for(std::chrono::milliseconds(250));
      std::system("sync >/dev/null 2>&1");

      const int reboot_result = std::system("/sbin/reboot >/dev/null 2>&1 || /bin/busybox reboot >/dev/null 2>&1 || reboot >/dev/null 2>&1");
      if (reboot_result != 0) {
        restart_requested.store(false);
        log_http_server("HTTP restart request failed to invoke reboot command");
      }
    }).detach();
  } catch (...) {
    restart_requested.store(false);
    error = "failed to queue restart request";
    return false;
  }

  return true;
}

void queue_configured_startup_game_if_present();

void queue_configured_startup_game_when_ready()
{
  if (startup_game_autolaunch_checked.load()) {
    return;
  }

  if (std::chrono::steady_clock::now() < startup_game_autolaunch_not_before) {
    return;
  }

  bool expected = false;
  if (!startup_game_autolaunch_checked.compare_exchange_strong(expected, true)) {
    return;
  }

  log_http_server("Startup game autolaunch delay elapsed; evaluating configured startup game");
  queue_configured_startup_game_if_present();
}

void queue_configured_startup_game_if_present()
{
  std::string path;
  bool is_neo = false;
  std::string error;
  if (!read_startup_game_path(path, is_neo, error)) {
    if (error != STARTUP_GAME_NOT_CONFIGURED_ERROR) {
      std::string message = std::string("Ignoring invalid startup game config: ") + error;
      log_http_server(message.c_str());
    }
    return;
  }

  if (!queue_launch_command(path, is_neo, error)) {
    std::string message = std::string("Failed to queue configured startup game: ") + error;
    log_http_server(message.c_str());
    return;
  }

  std::string message = std::string("Queued configured startup game: ") + path;
  log_http_server(message.c_str());
}

} // namespace

void http_server_process_pending_launches()
{
  queue_configured_startup_game_when_ready();

  std::vector<queued_launch_command> pending;
  {
    std::lock_guard<std::mutex> lock(launch_queue_mutex);
    if (launch_queue.empty()) return;
    pending.swap(launch_queue);
  }

  for (const auto &command : pending) {
    quiet_load_scope quiet_load;
    if (command.is_neo) {
      if (is_neogeo()) {
        clear_pending_neo_launch();
        neogeo_romset_tx(const_cast<char *>(command.path.c_str()), 0);
        continue;
      }

      if (!write_pending_neo_launch(command.path)) {
        clear_pending_neo_launch();
        log_http_server("Failed to store pending Neo Geo launch intent; falling back to direct loader");
        neogeo_romset_tx(const_cast<char *>(command.path.c_str()), 0);
        continue;
      }

      if (path_is_neogeo_core(NEOGEO_CORE_HINTS[0])) {
        std::string message = std::string("Switching to Neo Geo core via known path: ") + NEOGEO_CORE_HINTS[0];
        log_http_server(message.c_str());
        if (fpga_load_rbf(NEOGEO_CORE_HINTS[0]) == 0) {
          continue;
        }

        log_http_server("Known Neo Geo core path failed to load; falling back to resolver");
      }

      std::string neogeo_core_path;
      std::string resolve_error;
      if (!resolve_neogeo_core_path(neogeo_core_path, resolve_error)) {
        clear_pending_neo_launch();
        std::string message = std::string("Failed to resolve Neo Geo core (") + resolve_error + "); falling back to direct loader";
        log_http_server(message.c_str());
        neogeo_romset_tx(const_cast<char *>(command.path.c_str()), 0);
        continue;
      }

      std::string message = std::string("Switching to Neo Geo core for pending ROM launch: ") + neogeo_core_path;
      log_http_server(message.c_str());
      fpga_load_rbf(neogeo_core_path.c_str());
    } else if (isXmlName(command.path.c_str())) {
      xml_load(command.path.c_str());
    } else {
      fpga_load_rbf(command.path.c_str());
    }
  }
}

void http_server_start()
{
  if (server_started.exchange(true)) {
    log_http_server("MiSTer HTTP server already started");
    return;
  }

  log_http_server("MiSTer HTTP server thread starting");
  startup_game_autolaunch_checked.store(false);
  startup_game_autolaunch_not_before = std::chrono::steady_clock::now() + std::chrono::seconds(STARTUP_GAME_AUTOLAUNCH_DELAY_SECONDS);
  char startup_delay_message[160] = {};
  std::snprintf(startup_delay_message,
                sizeof(startup_delay_message),
                "Configured startup game autolaunch deferred for %d seconds after boot",
                STARTUP_GAME_AUTOLAUNCH_DELAY_SECONDS);
  log_http_server(startup_delay_message);

  server_thread = std::thread([]() {
    httplib::Server svr;

    svr.Get("/health", [](const httplib::Request &, httplib::Response &res) {
      std::string device_id;
      std::string device_id_source;
      std::string startup_game_path;
      bool startup_game_is_neo = false;
      std::string startup_game_error;
      const bool startup_game_configured = read_startup_game_path(startup_game_path, startup_game_is_neo, startup_game_error);
      resolve_device_identifier(device_id, device_id_source);

      std::string body = "{\"ok\":true,\"port\":8080,\"mode\":\"native-runtime\",\"build_version\":";
      append_json_escaped(body, APP_BUILD_NUMBER_STR);
      body += ",\"build_number\":";
      append_json_escaped(body, APP_BUILD_NUMBER_STR);
      body += ",\"semantic_version\":";
      append_json_escaped(body, APP_BUILD_VERSION);
      body += ",\"build_stamp\":";
      append_json_escaped(body, APP_BUILD_NUMBER_STR);
      body += ",\"build_hash\":";
      append_json_escaped(body, APP_BUILD_HASH);
      body += ",\"build_date\":";
      append_json_escaped(body, APP_BUILD_DATE);
      body += ",\"device_id\":";
      append_json_escaped(body, device_id);
      body += ",\"device_id_source\":";
      append_json_escaped(body, device_id_source);
      body += ",\"startup_game_configured\":";
      body += startup_game_configured ? "true" : "false";
      body += ",\"startup_game_path\":";
      if (startup_game_configured) {
        append_json_escaped(body, startup_game_path);
      } else {
        body += "null";
      }
      body += ",\"startup_game_mode\":";
      if (startup_game_configured) {
        append_json_escaped(body, startup_game_is_neo ? "neogeo" : "standard");
      } else {
        body += "null";
      }
      body += "}";
      res.set_content(body, "application/json");
    });

    svr.Get("/diagnostics", [](const httplib::Request &, httplib::Response &res) {
      res.set_content(build_diagnostics_response(), "application/json");
    });

    svr.Get("/status", [](const httplib::Request &, httplib::Response &res) {
      res.set_content("{\"ok\":true,\"service\":\"mister-runtime-http\",\"launch\":\"native\"}", "application/json");
    });

    svr.Get("/inventory/games", [](const httplib::Request &req, httplib::Response &res) {
      bool include_arcade = true;
      bool include_neogeo = true;
      std::string error;
      if (!try_parse_inventory_system_filter(req, include_arcade, include_neogeo, error)) {
        res.status = 400;
        res.set_content(std::string("{\"ok\":false,\"error\":\"") + error + "\"}", "application/json");
        return;
      }

      res.set_content(build_games_inventory_response(include_arcade, include_neogeo), "application/json");
    });

    svr.Get("/launch", [](const httplib::Request &req, httplib::Response &res) {
      std::string path;
      bool is_neo = false;
      std::string error;
      if (!parse_launch_request(req, path, is_neo, error)) {
        res.status = 400;
        res.set_content(std::string("{\"ok\":false,\"error\":\"") + error + "\"}", "application/json");
        return;
      }

      if (!queue_launch_command(path, is_neo, error)) {
        res.status = 503;
        res.set_content(std::string("{\"ok\":false,\"error\":\"") + error + "\"}", "application/json");
        return;
      }

      res.set_content(std::string("{\"ok\":true,\"queued\":true,\"path\":\"") + path + "\"}", "application/json");
    });

    svr.Get("/launch-neo-test", [](const httplib::Request &req, httplib::Response &res) {
      std::string path;
      std::string error;
      if (!parse_path_param(req, path, error)) {
        res.status = 400;
        res.set_content(std::string("{\"ok\":false,\"error\":\"") + error + "\"}", "application/json");
        return;
      }

      const auto ext_pos = path.find_last_of('.');
      const std::string ext = ext_pos == std::string::npos ? std::string() : path.substr(ext_pos);
      if (ext != ".neo") {
        res.status = 400;
        res.set_content("{\"ok\":false,\"error\":\"path must target a .neo file\"}", "application/json");
        return;
      }

      if (!queue_launch_command(path, true, error)) {
        res.status = 503;
        res.set_content(std::string("{\"ok\":false,\"error\":\"") + error + "\"}", "application/json");
        return;
      }

      res.set_content(std::string("{\"ok\":true,\"queued\":true,\"mode\":\"neo-test\",\"target\":\"") + path + "\"}", "application/json");
    });

    svr.Get("/startup-game", [](const httplib::Request &req, httplib::Response &res) {
      auto clear_it = req.params.find("clear");
      const bool should_clear = clear_it != req.params.end() && clear_it->second == "1";
      auto path_it = req.params.find("path");

      if (should_clear) {
        if (!clear_startup_game_path()) {
          res.status = 500;
          res.set_content("{\"ok\":false,\"error\":\"failed to clear startup game\"}", "application/json");
          return;
        }

        res.set_content(build_startup_game_response(), "application/json");
        return;
      }

      if (path_it == req.params.end() || path_it->second.empty()) {
        res.set_content(build_startup_game_response(), "application/json");
        return;
      }

      std::string path;
      bool is_neo = false;
      std::string error;
      if (!parse_launch_request(req, path, is_neo, error)) {
        res.status = 400;
        res.set_content(std::string("{\"ok\":false,\"error\":\"") + error + "\"}", "application/json");
        return;
      }

      if (!write_startup_game_path(path)) {
        res.status = 500;
        res.set_content("{\"ok\":false,\"error\":\"failed to persist startup game\"}", "application/json");
        return;
      }

      res.set_content(build_startup_game_response(), "application/json");
    });

    svr.Get("/restart", [](const httplib::Request &, httplib::Response &res) {
      std::string error;
      if (!queue_restart_request(error)) {
        res.status = 500;
        res.set_content(std::string("{\"ok\":false,\"error\":\"") + error + "\"}", "application/json");
        return;
      }

      std::string body = "{\"ok\":true,\"queued\":true,\"action\":\"restart\",\"pending\":";
      body += restart_requested.load() ? "true" : "false";
      body += "}";
      res.set_content(body, "application/json");
    });

    svr.Get("/", [](const httplib::Request &, httplib::Response &res) {
      res.set_content("MiSTer HTTP server is running", "text/plain");
    });

    log_http_server("MiSTer HTTP server calling listen(0.0.0.0:8080)");
    const bool listen_ok = svr.listen("0.0.0.0", 8080);
    char buffer[128];
    std::snprintf(buffer, sizeof(buffer), "MiSTer HTTP server listen returned ok=%d errno=%d", listen_ok ? 1 : 0, errno);
    log_http_server(buffer);
  });

  server_thread.detach();
}