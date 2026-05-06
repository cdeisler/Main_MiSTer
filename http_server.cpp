#include <atomic>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <string>
#include <thread>
#include <time.h>
#include <vector>

#include "fpga_io.h"
#include "httplib.h"
#include "menu.h"
#include "support/arcade/mra_loader.h"
#include "support/neogeo/neogeo_loader.h"

namespace {

constexpr const char *ROOT_DIR = "/media/fat";
constexpr const char *HTTP_LOG_PATH = "/media/fat/mister-http-startup.log";

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
      neogeo_romset_tx(const_cast<char *>(command.path.c_str()), 0);
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
      res.set_content("{\"ok\":true,\"port\":8080,\"mode\":\"native-runtime\"}", "application/json");
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