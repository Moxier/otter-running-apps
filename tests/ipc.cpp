#include "ipc.hpp"
#include <cassert>
#include <iostream>
using namespace running;
int main() {
    char directory[] = "/tmp/running-apps-ipc-XXXXXX";
    assert(mkdtemp(directory));
    std::string path = std::string(directory) + "/socket";
    int server = socket(AF_UNIX, SOCK_STREAM, 0);
    assert(server >= 0);
    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    std::strcpy(addr.sun_path, path.c_str());
    assert(bind(server, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) == 0);
    assert(listen(server, 1) == 0);
    std::vector<std::string> logs;
    auto check = [&](const char *payload, bool stream,
                     const std::function<void(const Json &)> &callback, const char *expected,
                     IpcLog logger) {
        logs.clear();
        Channel channel(std::move(logger));
        assert(channel.start(path, "", 0));
        int peer = accept(server, nullptr, nullptr);
        assert(peer >= 0);
        assert(channel.pump(1, stream, callback)); // EAGAIN is quiet.
        assert(logs.empty());
        assert(send(peer, payload, std::strlen(payload), MSG_NOSIGNAL) ==
               static_cast<ssize_t>(std::strlen(payload)));
        bool ok = channel.pump(2, stream, callback);
        assert(ok == (expected == nullptr));
        assert(channel.open() == (ok && stream));
        if (expected) {
            assert(logs.size() == 1 && logs[0].find(expected) != std::string::npos);
            assert(logs[0].size() < 384);
        } else
            assert(logs.empty());
        ::close(peer);
    };
    IpcLog logger = [&](const char *message) { logs.emplace_back(message); };
    auto noop = [](const Json &) {};
    check("not-json\n", true, noop, "malformed JSON", logger);
    check("{\"Err\":\"rejected\"}\n", true, noop, "niri Err reply", logger);
    check(
        "{}\n", true, [](const Json &) { throw std::runtime_error("bad event data"); },
        "bad event data", logger);
    check("{}\n", true, [](const Json &) { throw 42; }, "unknown IPC processing exception", logger);
    check(
        "{}\n", false, [](const Json &) { throw std::runtime_error("invalid action reply"); },
        "action reply processing failed", logger);
    check(
        "{}\n", true, [](const Json &) { throw std::runtime_error(std::string(4096, 'x')); },
        "event processing failed", logger);
    check("{}\n", true, noop, nullptr, logger);
    check("{\"Ok\":null}\n", false, noop, nullptr, logger);
    // Missing and throwing loggers must not interfere with failure/close semantics.
    for (IpcLog sink : {IpcLog{}, IpcLog([](const char *) { throw 42; })}) {
        Channel channel(sink);
        assert(channel.start(path, "", 0));
        int peer = accept(server, nullptr, nullptr);
        assert(peer >= 0);
        assert(send(peer, "!\n", 2, MSG_NOSIGNAL) == 2);
        assert(!channel.pump(1, true, noop) && !channel.open());
        ::close(peer);
    }
    {
        Client client(path);
        int64_t now = 0;
        auto tick = [&] { client.tick(++now); };
        auto accept_ready = [&] {
            pollfd ready{server, POLLIN, 0};
            assert(poll(&ready, 1, 1000) == 1);
            int peer = accept(server, nullptr, nullptr);
            assert(peer >= 0);
            return peer;
        };
        auto no_action = [&] {
            pollfd ready{server, POLLIN, 0};
            assert(poll(&ready, 1, 0) == 0);
        };
        auto send_line = [](int peer, const char *text) {
            const auto size = std::strlen(text);
            assert(send(peer, text, size, MSG_NOSIGNAL) == static_cast<ssize_t>(size));
        };
        auto read_line = [](int peer) {
            std::string text;
            while (text.empty() || text.back() != '\n') {
                pollfd ready{peer, POLLIN, 0};
                assert(poll(&ready, 1, 1000) == 1);
                char buffer[512];
                auto count = recv(peer, buffer, sizeof(buffer), 0);
                assert(count > 0);
                text.append(buffer, static_cast<size_t>(count));
            }
            return Json::parse(text);
        };
        const char *snapshot =
            R"({"WindowsChanged":{"windows":[{"id":1,"app_id":"a","is_focused":true,"workspace_id":10},{"id":2,"app_id":"b","is_focused":false,"workspace_id":10},{"id":3,"app_id":"c","is_focused":false,"workspace_id":10}]}})"
            "\n";
        tick();
        int events = accept_ready();
        assert(read_line(events) == "EventStream");
        send_line(events, snapshot);
        send_line(events, "{\"WorkspaceActivated\":{\"id\":10,\"focused\":true}}\n");
        tick();
        assert(client.connected);
        auto start_action = [&](Id id) {
            client.focus(id);
            tick();
            tick();
            int peer = accept_ready();
            assert(read_line(peer).at("Action").at("FocusWindow").at("id") == id);
            return peer;
        };
        int action = start_action(1);
        client.focus(2);
        tick();
        client.focus(3);
        tick();
        no_action();
        send_line(action, "{\"Ok\":\"Handled\"}\n");
        tick();
        tick();
        ::close(action);
        action = accept_ready();
        assert(read_line(action).at("Action").at("FocusWindow").at("id") == 3);
        send_line(action, "{\"Ok\":\"Handled\"}\n");
        tick();
        ::close(action);
        tick();
        no_action(); // Superseded target 2 is never replayed.

        // A pending target that closes or leaves the current workspace is dropped.
        for (const char *event : {"{\"WindowClosed\":{\"id\":2}}\n",
                                  "{\"WorkspaceActivated\":{\"id\":20,\"focused\":true}}\n"}) {
            action = start_action(1);
            client.focus(2);
            tick();
            send_line(events, event);
            tick();
            send_line(action, "{\"Ok\":\"Handled\"}\n");
            tick();
            tick();
            ::close(action);
            no_action();
            send_line(events, snapshot);
            send_line(events, "{\"WorkspaceActivated\":{\"id\":10,\"focused\":true}}\n");
            tick();
        }
        action = start_action(1);
        client.focus(3);
        tick();
        ::close(events);
        tick();
        assert(!client.connected);
        ::close(action);
        now += 1000;
        tick();
        events = accept_ready();
        assert(read_line(events) == "EventStream");
        send_line(events, snapshot);
        send_line(events, "{\"WorkspaceActivated\":{\"id\":10,\"focused\":true}}\n");
        tick();
        tick();
        assert(client.connected);
        no_action(); // Reconnect cannot replay stale intent.
        ::close(events);
    }
    ::close(server);
    unlink(path.c_str());
    rmdir(directory);
    std::cout << "IPC diagnostic and focus-coalescing tests passed\n";
}
