#include <atomic>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <string>
#include <thread>
#include <time.h>
#include <vector>

#include "file_io.h"
#include "fpga_io.h"
#include "httplib.h"
#include "menu.h"
#include "user_io.h"
#include "support/arcade/mra_loader.h"
#include "support/neogeo/neogeo_loader.h"

extern const char *version;

#ifndef BUILD_STAMP
#define BUILD_STAMP "unknown"
#endif

#ifndef BUILD_HASH
#define BUILD_HASH "unknown"
#endif

namespace {

constexpr const char *ROOT_DIR = "/media/fat";
constexpr const char *HTTP_LOG_PATH = "/media/fat/mister-http-startup.log";
constexpr const char *PENDING_NEO_ENV = "MISTER_HTTP_PENDING_NEO_PATH";
constexpr const char *NEOGEO_CORE_SUBDIR = "_Console";
constexpr const char *NEOGEO_CORE_HINTS[] = {
  "/media/fat/_Console/NeoGeo_20250909.rbf",
  "/media/fat/_Console/NeoGeo.rbf",
};

std::atomic<bool> server_started{false};
std::thread server_thread;
std::mutex launch_queue_mutex;

struct queued_launch_command {
  std::string path;
  bool is_neo;
};

std::vector<queued_launch_command> launch_queue;

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

} // namespace

void http_server_process_pending_launches()
{
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

  server_thread = std::thread([]() {
    httplib::Server svr;

    svr.Get("/health", [](const httplib::Request &, httplib::Response &res) {
      char body[256];
      std::snprintf(body, sizeof(body),
                    "{\"ok\":true,\"port\":8080,\"mode\":\"native-runtime\",\"build_version\":\"%s\",\"build_stamp\":\"%s\",\"build_hash\":\"%s\"}",
                    version + 5,
                    BUILD_STAMP,
                    BUILD_HASH);
      res.set_content(body, "application/json");
    });

    svr.Get("/status", [](const httplib::Request &, httplib::Response &res) {
      res.set_content("{\"ok\":true,\"service\":\"mister-runtime-http\",\"launch\":\"native\"}", "application/json");
    });

    svr.Get("/launch", [](const httplib::Request &req, httplib::Response &res) {
      std::string path;
      std::string error;
      if (!parse_path_param(req, path, error)) {
        res.status = 400;
        res.set_content(std::string("{\"ok\":false,\"error\":\"") + error + "\"}", "application/json");
        return;
      }

      const auto ext_pos = path.find_last_of('.');
      const std::string ext = ext_pos == std::string::npos ? std::string() : path.substr(ext_pos);
      const bool is_neo = ext == ".neo";
      const bool is_supported = ext == ".rbf" || ext == ".mra" || ext == ".mgl" || is_neo;
      if (!is_supported) {
        res.status = 400;
        res.set_content("{\"ok\":false,\"error\":\"unsupported extension\"}", "application/json");
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