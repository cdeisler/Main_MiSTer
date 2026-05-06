#pragma once

#include <atomic>
#include <cstdio>
#include <cerrno>
#include <cstring>
#include <functional>
#include <map>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>
#include <cctype>

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

namespace httplib {

inline void debug_log(const char *message) {
  FILE *log = std::fopen("/media/fat/mister-http-startup.log", "a");
  if (log) {
    std::fprintf(log, "%s\n", message);
    std::fclose(log);
  }
  std::fprintf(stderr, "%s\n", message);
  std::fflush(stderr);
}

struct Request {
  std::string method;
  std::string path;
  std::string body;
  std::map<std::string, std::string> params;
};

struct Response {
  int status = 200;
  std::string body;
  std::map<std::string, std::string> headers;

  void set_content(const std::string &content, const char *content_type) {
    body = content;
    headers["Content-Type"] = content_type ? content_type : "text/plain";
  }
};

class Server {
 public:
  using Handler = std::function<void(const Request &, Response &)>;

  void Get(const std::string &pattern, Handler handler) {
    std::lock_guard<std::mutex> lock(handler_mutex_);
    get_handlers_[pattern] = std::move(handler);
  }

  bool listen(const char *host, int port) {
    int sock = ::socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) return false;

    set_close_on_exec(sock);

    int opt = 1;
    ::setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(port));
    if (!host || std::strcmp(host, "0.0.0.0") == 0) {
      addr.sin_addr.s_addr = INADDR_ANY;
    } else if (::inet_pton(AF_INET, host, &addr.sin_addr) != 1) {
      ::close(sock);
      return false;
    }

    if (::bind(sock, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) != 0) {
      ::close(sock);
      return false;
    }

    if (::listen(sock, 8) != 0) {
      ::close(sock);
      return false;
    }

    listen_socket_.store(sock);
    running_.store(true);

    while (running_.load()) {
      sockaddr_in client_addr{};
      socklen_t client_len = sizeof(client_addr);
      int client = ::accept(sock, reinterpret_cast<sockaddr *>(&client_addr), &client_len);
      if (client < 0) {
        if (!running_.load()) break;
        continue;
      }
      set_close_on_exec(client);
      set_client_timeouts(client);
      handle_client(client);
      ::shutdown(client, SHUT_RDWR);
      ::close(client);
    }

    ::close(sock);
    listen_socket_.store(-1);
    return true;
  }

  void stop() {
    running_.store(false);
    int sock = listen_socket_.exchange(-1);
    if (sock >= 0) {
      ::shutdown(sock, SHUT_RDWR);
      ::close(sock);
    }
  }

 private:
  void set_close_on_exec(int fd) {
    if (fd < 0) return;
    const int flags = ::fcntl(fd, F_GETFD);
    if (flags >= 0) {
      ::fcntl(fd, F_SETFD, flags | FD_CLOEXEC);
    }
  }

  void set_client_timeouts(int client) {
    timeval timeout{};
    timeout.tv_sec = 2;
    timeout.tv_usec = 0;
    ::setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    ::setsockopt(client, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
  }

  bool send_all(int client, const std::string &text) {
    size_t sent = 0;
    while (sent < text.size()) {
      ssize_t rc = ::send(client, text.data() + sent, text.size() - sent, MSG_NOSIGNAL);
      if (rc <= 0) return false;
      sent += static_cast<size_t>(rc);
    }
    return true;
  }

  void handle_client(int client) {
    debug_log("httplib: handle_client begin");
    char buffer[4096];
    ssize_t len = ::recv(client, buffer, sizeof(buffer) - 1, 0);
    if (len <= 0) {
      char recv_log[128];
      std::snprintf(recv_log, sizeof(recv_log), "httplib: recv returned %d errno=%d", static_cast<int>(len), errno);
      debug_log(recv_log);
      return;
    }
    buffer[len] = 0;

    std::istringstream request_stream(buffer);
    std::string method;
    std::string path;
    std::string version;
    request_stream >> method >> path >> version;

    Request request;
    request.method = method;
    request.path = path;

    char request_log[256];
    std::snprintf(request_log, sizeof(request_log), "httplib: request method=%s path=%s", method.c_str(), path.c_str());
    debug_log(request_log);

    const auto query_pos = path.find('?');
    if (query_pos != std::string::npos) {
      request.path = path.substr(0, query_pos);
      std::string query = path.substr(query_pos + 1);
      std::istringstream query_stream(query);
      std::string pair;
      while (std::getline(query_stream, pair, '&')) {
        const auto eq = pair.find('=');
        if (eq == std::string::npos) {
          request.params[pair] = "";
        } else {
          request.params[pair.substr(0, eq)] = pair.substr(eq + 1);
        }
      }
    }

    Response response;
    bool handled = false;
    {
      std::lock_guard<std::mutex> lock(handler_mutex_);
      auto it = get_handlers_.find(request.path);
      if (it != get_handlers_.end() && method == "GET") {
        it->second(request, response);
        handled = true;
      }
    }

    if (!handled) {
      response.status = 404;
      response.set_content("not found", "text/plain");
    }

    char handler_log[128];
    std::snprintf(handler_log, sizeof(handler_log), "httplib: response status=%d handled=%d", response.status, handled ? 1 : 0);
    debug_log(handler_log);

    std::ostringstream out;
    out << "HTTP/1.1 " << response.status << " OK\r\n";
    if (response.headers.find("Content-Type") == response.headers.end()) {
      response.headers["Content-Type"] = "text/plain";
    }
    for (const auto &kv : response.headers) {
      out << kv.first << ": " << kv.second << "\r\n";
    }
    out << "Content-Length: " << response.body.size() << "\r\n";
    out << "Connection: close\r\n\r\n";
    out << response.body;
    const auto text = out.str();
    const bool send_ok = send_all(client, text);
    char send_log[128];
    std::snprintf(send_log, sizeof(send_log), "httplib: send_all ok=%d errno=%d", send_ok ? 1 : 0, errno);
    debug_log(send_log);
  }

  std::mutex handler_mutex_;
  std::map<std::string, Handler> get_handlers_;
  std::atomic<bool> running_{false};
  std::atomic<int> listen_socket_{-1};
};

} // namespace httplib