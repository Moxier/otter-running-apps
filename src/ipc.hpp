#pragma once
#include "core.hpp"
#include <cerrno>
#include <chrono>
#include <cstring>
#include <cstdio>
#include <functional>
#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

namespace running {
inline int64_t now_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}
// Main-thread only. No blocking reads, worker callbacks, subprocesses, or signals.
using IpcLog = std::function<void(const char*)>;
class Channel {
    IpcLog log;
    void report(const char* context, const char* detail) const noexcept {
        if (!log) return;
        char message[384];
        std::snprintf(message, sizeof(message), "running-apps IPC %s: %.256s", context, detail);
        // Logging must never prevent channel teardown, even if a callback throws.
        try { log(message); } catch (...) {}
    }
    int fd = -1;
    bool connecting = false;
    std::string output, input;
    size_t sent = 0;
    int64_t deadline = 0;
public:
    explicit Channel(IpcLog logger = {}) : log(std::move(logger)) {}
    Channel(const Channel&) = delete;
    Channel& operator=(const Channel&) = delete;
    ~Channel() { close(); }
    bool open() const { return fd >= 0; }
    void close() {
        if (fd >= 0) ::close(fd);
        fd = -1; input.clear(); output.clear(); sent = 0; connecting = false;
    }
    bool start(const std::string& path, std::string request, int64_t now) {
        close();
        sockaddr_un addr{}; addr.sun_family = AF_UNIX;
        if (path.empty() || path.size() >= sizeof(addr.sun_path)) return false;
        std::memcpy(addr.sun_path, path.c_str(), path.size() + 1);
        fd = socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
        if (fd < 0) return false;
        int rc = connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
        if (rc < 0 && errno != EINPROGRESS) { close(); return false; }
        connecting = rc < 0;
        output = std::move(request); sent = 0; deadline = now + 1500;
        return true;
    }
    bool pump(int64_t now, bool stream, const std::function<void(const Json&)>& line) {
        if (fd < 0) return false;
        if (deadline && now >= deadline) { close(); return false; }
        if (connecting) {
            pollfd p{fd, POLLOUT, 0};
            if (poll(&p, 1, 0) <= 0) return true;
            int error = 0; socklen_t len = sizeof(error);
            if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &len) || error) { close(); return false; }
            connecting = false;
        }
        while (sent < output.size()) {
            ssize_t n = send(fd, output.data() + sent, output.size() - sent, MSG_NOSIGNAL);
            if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) return true;
            if (n <= 0) { close(); return false; }
            sent += static_cast<size_t>(n);
        }
        // Bounded work per callback, including buffered complete lines.
        size_t bytes = 0, lines = 0;
        while (lines < 128) {
            auto end = input.find('\n');
            if (end != std::string::npos) {
                auto payload = input.substr(0, end); input.erase(0, end + 1);
                const char* context = "malformed JSON from niri";
                try {
                    auto j = Json::parse(payload);
                    context = "niri Err reply";
                    if (j.contains("Err")) throw std::runtime_error("niri rejected request");
                    context = stream ? "event processing failed" : "action reply processing failed";
                    line(j);
                } catch (const std::exception& e) {
                    report(context, e.what());
                    close(); return false;
                } catch (...) {
                    report(context, "unknown IPC processing exception");
                    close(); return false;
                }
                ++lines;
                if (!stream) { close(); return true; }
                deadline = 0; // Event streams may be silent indefinitely.
                continue;
            }
            if (bytes >= 65536) break;
            char buffer[8192];
            ssize_t n = recv(fd, buffer, sizeof(buffer), 0);
            if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) break;
            if (n <= 0) { close(); return false; }
            bytes += static_cast<size_t>(n); input.append(buffer, static_cast<size_t>(n));
            if (input.size() > 4 * 1024 * 1024) { close(); return false; }
        }
        return true;
    }
};
class Client {
    Channel events, action;
    int64_t retry = 0;
    // Pending focus requests intentionally coalesce: the most recent click wins.
    std::optional<Id> queued;
public:
    Model model;
    bool connected = false;
    std::string path;
    explicit Client(std::string socket_path, IpcLog log = {})
        : events(log), action(std::move(log)), path(std::move(socket_path)) {}
    void tick(int64_t now) {
        if (!events.open() && now >= retry) {
            events.start(path, "\"EventStream\"\n", now);
            retry = now + 1000;
        }
        if (events.open()) {
            bool ok = events.pump(now, true, [&](const Json& j) {
                if (model.apply(j) && j.contains("WindowsChanged")) connected = true;
            });
            if (!ok) { connected = false; model.clear(); queued.reset(); action.close(); retry = now + 1000; }
        }
        if (action.open()) action.pump(now, false, [](const Json& j) {
            if (!j.contains("Ok")) throw std::runtime_error("invalid action reply");
        });
        if (queued && !action.open() && connected) {
            Id id = *queued; queued.reset();
            if (model.is_current(id)) action.start(path,
                Json{{"Action", {{"FocusWindow", {{"id", id}}}}}}.dump() + "\n", now);
        }
    }
    void focus(Id id) { if (connected) queued = id; }
};
}
