#include "core.hpp"
#include "icons.hpp"
#include "ipc.hpp"
#include <cassert>
#include <iostream>
using namespace running;
int main() {
    Model m;
    auto feed = [&](const char* s) { return m.apply(Json::parse(s)); };
    feed(R"({"WindowsChanged":{"windows":[{"id":9007199254740993,"app_id":"firefox","is_focused":true},{"id":42,"app_id":"firefox","is_focused":false},{"id":8,"app_id":null,"is_focused":false},{"id":9,"app_id":null,"is_focused":false}]}})");
    assert(m.groups().size() == 3);
    assert(m.target("app:firefox") == 42);
    feed(R"({"WindowFocusChanged":{"id":null}})");
    assert(m.target("app:firefox") == 9007199254740993ULL);
    feed(R"({"WindowOpenedOrChanged":{"window":{"id":42,"app_id":"firefox","is_focused":true,"future":12}}})");
    assert(!m.windows.at(9007199254740993ULL).focused);
    assert(m.target("app:firefox") == 9007199254740993ULL);
    feed(R"({"WindowClosed":{"id":9007199254740993}})");
    assert(m.target("app:firefox") == 42);
    assert(!feed(R"({"FutureEvent":{"id":42}})"));
    auto before = m.windows.size();
    try { feed(R"({"WindowsChanged":{"windows":[{"id":1,"app_id":"x","is_focused":false},{"id":-1,"app_id":"bad","is_focused":false}]}})"); assert(false); }
    catch (const std::exception&) {}
    assert(m.windows.size() == before);
    feed(R"({"WindowOpenedOrChanged":{"window":{"id":42,"app_id":"renamed","is_focused":true}}})");
    assert(!m.target("app:firefox"));
    assert(m.target("app:renamed") == 42);
    feed(R"({"WindowsChanged":{"windows":[]}})");
    assert(m.groups().empty());
    feed(R"({"WindowsChanged":{"windows":[{"id":1,"app_id":"left","is_focused":false,"workspace_id":10,"layout":{"pos_in_scrolling_layout":[1,1]}},{"id":2,"app_id":"right","is_focused":true,"workspace_id":10,"layout":{"pos_in_scrolling_layout":[2,1]}},{"id":3,"app_id":"left","is_focused":false,"workspace_id":10,"layout":{"pos_in_scrolling_layout":[3,1]}}]}})");
    assert(m.groups()[0].app == "left");
    feed(R"({"WindowLayoutsChanged":{"changes":[[1,{"pos_in_scrolling_layout":[3,1]}],[2,{"pos_in_scrolling_layout":[1,1]}],[3,{"pos_in_scrolling_layout":[2,1]}]]}})");
    assert(m.groups()[0].app == "right");
    assert(m.groups()[1].windows.front() == 3);
    feed(R"({"WindowFocusChanged":{"id":1}})");
    assert(m.groups()[0].app == "right"); // Focus alone must not reorder app groups.
    assert(m.target("app:left") == 3);
    try { feed(R"({"WindowLayoutsChanged":{"changes":[[2,{"pos_in_scrolling_layout":[8,1]}],[3,{"pos_in_scrolling_layout":[-1,1]}]]}})"); assert(false); }
    catch (const std::exception&) {}
    assert(m.groups()[0].app == "right"); // Malformed batches are atomic.
    feed(R"({"WindowOpenedOrChanged":{"window":{"id":2,"app_id":"right","is_focused":false,"workspace_id":20,"layout":{"pos_in_scrolling_layout":[1,1]}}}})");
    feed(R"({"WorkspacesChanged":{"workspaces":[{"id":10,"idx":2,"output":"DP-1"},{"id":20,"idx":1,"output":"DP-1"}]}})");
    assert(m.groups()[0].app == "right");
    feed(R"({"WorkspacesChanged":{"workspaces":[{"id":10,"idx":1,"output":"DP-1"},{"id":20,"idx":2,"output":"DP-1"}]}})");
    assert(m.groups()[0].app == "left");
    feed(R"({"WindowLayoutsChanged":{"changes":[[999,{"pos_in_scrolling_layout":null}]]}})");
    assert(m.groups().size() == 2);
    assert(m.groups(true).empty()); // No focused workspace in the prior snapshots.
    feed(R"({"WorkspaceActivated":{"id":10,"focused":true}})");
    assert(m.groups(true).size() == 1 && m.groups(true)[0].app == "left");
    assert(!m.target("app:right", true));
    feed(R"({"WorkspaceActivated":{"id":20,"focused":false}})");
    assert(m.groups(true)[0].app == "left");
    feed(R"({"WorkspaceActivated":{"id":20,"focused":true}})");
    assert(m.groups(true).size() == 1 && m.target("app:right", true) == 2);
    feed(R"({"WindowFocusChanged":{"id":null}})");
    assert(m.groups(true).size() == 1); // Launcher focus must not hide workspace apps.
    feed(R"({"WorkspaceActivated":{"id":30,"focused":true}})");
    assert(m.groups(true).empty());
    feed(R"({"WorkspacesChanged":{"workspaces":[{"id":10,"idx":1,"output":"DP-1","is_focused":true}]}})");
    assert(m.groups(true).size() == 1);
    m.clear(); assert(!m.focused_workspace && m.groups(true).empty());
    namespace fs = std::filesystem;
    auto root = fs::temp_directory_path() / ("running-apps-icons-" + std::to_string(getpid()));
    fs::create_directories(root / "user/applications");
    fs::create_directories(root / "system/applications");
    std::ofstream(root / "system/applications/browser.desktop") << "[Desktop Entry]\nType=Application\nIcon=old\n";
    std::ofstream(root / "user/applications/browser.desktop") << "[Desktop Entry]\nType=Application\nIcon=firefox\nStartupWMClass=Browser\n[Desktop Action Test]\nIcon=wrong\n";
    std::ofstream(root / "user/applications/hidden.desktop") << "[Desktop Entry]\nHidden=true\n";
    std::ofstream(root / "system/applications/hidden.desktop") << "[Desktop Entry]\nType=Application\nIcon=wrong\n";
    std::ofstream(root / "user/applications/absolute.desktop") << "[Desktop Entry]\nType=Application\nIcon=/tmp/icon.png\n";
    Icons icons({(root / "user").string(), (root / "system").string()});
    assert(icons.resolve("browser.desktop") == "firefox");
    assert(icons.resolve("Browser") == "firefox");
    assert(icons.resolve("BROWSER") == "firefox");
    assert(icons.resolve("hidden") == "sparkles");
    assert(icons.resolve("absolute") == "sparkles");
    assert(icons.resolve("missing") == "sparkles");
    fs::remove_all(root);
    std::cout << "Model and desktop-icon tests passed\n";
}
