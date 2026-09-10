#include "core.hpp"
#include "icons.hpp"
#include "ipc.hpp"
#include <cassert>
#include <iostream>
using namespace running;
// Include ordering/targets and the next serial value in rollback comparisons.
static Json state(Model model) {
    Json out;
    out["workspace"] = model.focused_workspace ? Json(*model.focused_workspace) : Json();
    for (const auto &[id, w] : model.windows)
        out["windows"].push_back(Json::array(
            {id, w.app, w.focused, w.seen, w.mru, w.workspace ? Json(*w.workspace) : Json(),
             w.position ? Json::array({w.position->first, w.position->second}) : Json()}));
    for (bool current : {false, true}) {
        for (const auto &g : model.groups(current)) {
            auto target = model.target(g.key, current);
            out[current ? "current" : "all"].push_back(
                Json::array({g.key, g.app, g.windows, g.active, target ? Json(*target) : Json()}));
        }
    }
    out["next_seen"] =
        model
            .parse(
                Json::parse(R"({"id":18446744073709551615,"app_id":"probe","is_focused":false})"))
            .seen;
    return out;
}
static void incremental_events() {
    Model m;
    auto feed = [&](const char *event) { assert(m.apply(Json::parse(event))); };
    feed(
        R"({"WindowsChanged":{"windows":[{"id":1,"app_id":"a","is_focused":true,"workspace_id":10,"layout":{"pos_in_scrolling_layout":[1,1]}},{"id":2,"app_id":"b","is_focused":false,"workspace_id":20,"layout":{"pos_in_scrolling_layout":[2,1]}}]}})");
    feed(
        R"({"WorkspacesChanged":{"workspaces":[{"id":10,"idx":2,"output":"DP-1","is_focused":true},{"id":20,"idx":1,"output":"DP-1"}]}})");
    auto reject = [&](const Json &event) {
        const auto before = state(m);
        bool threw = false;
        try {
            m.apply(event);
        } catch (const std::exception &) {
            threw = true;
        }
        assert(threw);
        assert(state(m) == before);
    };
    for (
        const char *event :
        {R"({"WorkspaceActivated":{"id":20}})", R"({"WorkspaceActivated":{"id":20,"focused":1}})",
         R"({"WorkspaceActivated":{"id":-1,"focused":false}})", R"({"WindowClosed":{}})",
         R"({"WindowClosed":{"id":-1}})", R"({"WindowClosed":{"id":"1"}})",
         R"({"WindowFocusChanged":{}})", R"({"WindowFocusChanged":{"id":-1}})",
         R"({"WindowFocusChanged":{"id":true}})", R"({"WindowLayoutsChanged":{"changes":{}}})",
         R"({"WindowLayoutsChanged":{"changes":[[1,{"pos_in_scrolling_layout":[9,9]}],[2,{"pos_in_scrolling_layout":[3,-1]}]]}})",
         R"({"WindowLayoutsChanged":{"changes":[[1,{}],[999,{"pos_in_scrolling_layout":[1]}]]}})",
         R"({"WindowLayoutsChanged":{"changes":[[1,{}],[2]]}})",
         R"({"WindowsChanged":{"windows":[{"id":3,"app_id":"c","is_focused":false},{"id":3,"app_id":"c","is_focused":false}]}})",
         R"({"WorkspacesChanged":{"workspaces":[{"id":20,"idx":9,"output":"DP-2"},{"id":10,"idx":-1,"output":"DP-1"}]}})",
         R"({"WindowOpenedOrChanged":{"window":{"id":3,"app_id":"c","is_focused":true,"workspace_id":-1}}})"})
        reject(Json::parse(event));
    Json oversized = Json::parse(R"({"WindowLayoutsChanged":{"changes":[]}})");
    oversized["WindowLayoutsChanged"]["changes"] =
        std::vector<Json>(16385, Json::array({Id(1), Json::object()}));
    reject(oversized);

    feed(R"({"WorkspaceActivated":{"id":20,"focused":true}})");
    assert(m.focused_workspace == 20);
    auto before = state(m);
    feed(R"({"WorkspaceActivated":{"id":10,"focused":false}})");
    assert(state(m) == before);
    const auto seen = m.windows.at(2).seen;
    const auto previous_mru = m.windows.at(1).mru;
    feed(R"({"WindowFocusChanged":{"id":2}})");
    assert(!m.windows.at(1).focused && m.windows.at(2).focused);
    assert(m.windows.at(2).mru == previous_mru + 1 && m.windows.at(2).seen == seen);
    feed(R"({"WindowFocusChanged":{"id":2}})");
    assert(m.windows.at(2).mru == previous_mru + 2);
    feed(R"({"WindowFocusChanged":{"id":null}})");
    assert(!m.windows.at(1).focused && !m.windows.at(2).focused);
    before = state(m);
    feed(R"({"WindowFocusChanged":{"id":999}})");
    assert(state(m) == before);
    feed(
        R"({"WindowLayoutsChanged":{"changes":[[1,{"pos_in_scrolling_layout":[8,1]}],[2,{}],[1,{"pos_in_scrolling_layout":[4,2]}],[999,{}]]}})");
    assert((m.windows.at(1).position == std::make_pair(Id(4), Id(2))));
    assert(!m.windows.at(2).position);
    feed(R"({"WindowClosed":{"id":1}})");
    assert(m.windows.size() == 1 && m.windows.count(2));
    before = state(m);
    feed(R"({"WindowClosed":{"id":999}})");
    assert(state(m) == before);
    assert(!m.apply(Json::parse(R"({"FutureEvent":{}})")));
    assert(state(m) == before);
    // Higher-priority events must still ignore lower-priority malformed fields.
    feed(R"({"WorkspaceActivated":{"id":20,"focused":false},"WindowClosed":{}})");
    feed(R"({"WindowLayoutsChanged":{"changes":[]},"WindowFocusChanged":{}})");
    feed(
        R"({"WindowOpenedOrChanged":{"window":{"id":2,"app_id":"b","is_focused":false,"workspace_id":20}},"WindowClosed":{}})");
    assert(state(m) == before);
}
int main() {
    incremental_events();
    Model m;
    auto feed = [&](const char *s) { return m.apply(Json::parse(s)); };
    feed(
        R"({"WindowsChanged":{"windows":[{"id":9007199254740993,"app_id":"firefox","is_focused":true},{"id":42,"app_id":"firefox","is_focused":false},{"id":8,"app_id":null,"is_focused":false},{"id":9,"app_id":null,"is_focused":false}]}})");
    assert(m.groups().size() == 3);
    assert(m.target("app:firefox") == 42);
    feed(R"({"WindowFocusChanged":{"id":null}})");
    assert(m.target("app:firefox") == 9007199254740993ULL);
    feed(
        R"({"WindowOpenedOrChanged":{"window":{"id":42,"app_id":"firefox","is_focused":true,"future":12}}})");
    assert(!m.windows.at(9007199254740993ULL).focused);
    assert(m.target("app:firefox") == 9007199254740993ULL);
    feed(R"({"WindowClosed":{"id":9007199254740993}})");
    assert(m.target("app:firefox") == 42);
    assert(!feed(R"({"FutureEvent":{"id":42}})"));
    auto before = m.windows.size();
    try {
        feed(
            R"({"WindowsChanged":{"windows":[{"id":1,"app_id":"x","is_focused":false},{"id":-1,"app_id":"bad","is_focused":false}]}})");
        assert(false);
    } catch (const std::exception &) {
    }
    assert(m.windows.size() == before);
    feed(R"({"WindowOpenedOrChanged":{"window":{"id":42,"app_id":"renamed","is_focused":true}}})");
    assert(!m.target("app:firefox"));
    assert(m.target("app:renamed") == 42);
    feed(R"({"WindowsChanged":{"windows":[]}})");
    assert(m.groups().empty());
    feed(
        R"({"WindowsChanged":{"windows":[{"id":1,"app_id":"left","is_focused":false,"workspace_id":10,"layout":{"pos_in_scrolling_layout":[1,1]}},{"id":2,"app_id":"right","is_focused":true,"workspace_id":10,"layout":{"pos_in_scrolling_layout":[2,1]}},{"id":3,"app_id":"left","is_focused":false,"workspace_id":10,"layout":{"pos_in_scrolling_layout":[3,1]}}]}})");
    assert(m.groups()[0].app == "left");
    feed(
        R"({"WindowLayoutsChanged":{"changes":[[1,{"pos_in_scrolling_layout":[3,1]}],[2,{"pos_in_scrolling_layout":[1,1]}],[3,{"pos_in_scrolling_layout":[2,1]}]]}})");
    assert(m.groups()[0].app == "right");
    assert(m.groups()[1].windows.front() == 3);
    feed(R"({"WindowFocusChanged":{"id":1}})");
    assert(m.groups()[0].app == "right"); // Focus alone must not reorder app groups.
    assert(m.target("app:left") == 3);
    try {
        feed(
            R"({"WindowLayoutsChanged":{"changes":[[2,{"pos_in_scrolling_layout":[8,1]}],[3,{"pos_in_scrolling_layout":[-1,1]}]]}})");
        assert(false);
    } catch (const std::exception &) {
    }
    assert(m.groups()[0].app == "right"); // Malformed batches are atomic.
    feed(
        R"({"WindowOpenedOrChanged":{"window":{"id":2,"app_id":"right","is_focused":false,"workspace_id":20,"layout":{"pos_in_scrolling_layout":[1,1]}}}})");
    feed(
        R"({"WorkspacesChanged":{"workspaces":[{"id":10,"idx":2,"output":"DP-1"},{"id":20,"idx":1,"output":"DP-1"}]}})");
    assert(m.groups()[0].app == "right");
    feed(
        R"({"WorkspacesChanged":{"workspaces":[{"id":10,"idx":1,"output":"DP-1"},{"id":20,"idx":2,"output":"DP-1"}]}})");
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
    feed(
        R"({"WorkspacesChanged":{"workspaces":[{"id":10,"idx":1,"output":"DP-1","is_focused":true}]}})");
    assert(m.groups(true).size() == 1);
    m.clear();
    assert(!m.focused_workspace && m.groups(true).empty());
    namespace fs = std::filesystem;
    auto root = fs::temp_directory_path() / ("running-apps-icons-" + std::to_string(getpid()));
    fs::create_directories(root / "user/applications");
    fs::create_directories(root / "system/applications");
    std::ofstream(root / "system/applications/browser.desktop")
        << "[Desktop Entry]\nType=Application\nIcon=old\n";
    std::ofstream(root / "user/applications/browser.desktop")
        << "[Desktop Entry]\nType=Application\nIcon=firefox\nStartupWMClass=Browser\n[Desktop Action Test]\nIcon=wrong\n";
    std::ofstream(root / "user/applications/hidden.desktop") << "[Desktop Entry]\nHidden=true\n";
    std::ofstream(root / "system/applications/hidden.desktop")
        << "[Desktop Entry]\nType=Application\nIcon=wrong\n";
    std::ofstream(root / "user/applications/absolute.desktop")
        << "[Desktop Entry]\nType=Application\nIcon=/tmp/icon.png\n";
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
