#pragma once
#include "json.hpp"
#include <algorithm>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>
#include <tuple>
#include <limits>

namespace running {
using Json = nlohmann::json;
using Id = uint64_t;
struct Window { Id id; std::string app; bool focused; uint64_t seen = 0, mru = 0; std::optional<Id> workspace = {}; std::optional<std::pair<Id, Id>> position = {}; };
struct Group { std::string key, app; std::vector<Id> windows; bool active = false; };
inline std::string key(const Window& w) {
    // Unknown app IDs must not collapse unrelated applications into one group.
    return w.app.empty() ? "window:" + std::to_string(w.id) : "app:" + w.app;
}
inline Id id_value(const Json& j) {
    if (!j.is_number_unsigned()) throw std::runtime_error("invalid window id");
    return j.get<Id>();
}
class Model {
    uint64_t serial = 0;
    struct Workspace { std::string output; Id index; };
    std::map<Id, Workspace> workspaces;
    static std::optional<std::pair<Id, Id>> position(const Json& layout) {
        if (!layout.contains("pos_in_scrolling_layout") || layout.at("pos_in_scrolling_layout").is_null()) return {};
        const auto& p = layout.at("pos_in_scrolling_layout");
        if (!p.is_array() || p.size() != 2) throw std::runtime_error("invalid layout position");
        return std::pair<Id, Id>{id_value(p[0]), id_value(p[1])};
    }
    auto location(const Window& w) const {
        const Id unknown = std::numeric_limits<Id>::max();
        auto it = w.workspace ? workspaces.find(*w.workspace) : workspaces.end();
        return std::make_tuple(it == workspaces.end() ? std::string("~") : it->second.output,
            it == workspaces.end() ? w.workspace.value_or(unknown) : it->second.index,
            w.workspace.value_or(unknown), !w.position.has_value(),
            w.position ? w.position->first : unknown, w.position ? w.position->second : unknown,
            w.seen, w.id);
    }
public:
    std::map<Id, Window> windows;
    std::optional<Id> focused_workspace;
    bool is_current(Id id) const {
        auto it = windows.find(id);
        return focused_workspace && it != windows.end() && it->second.workspace == focused_workspace;
    }
    void clear() { windows.clear(); workspaces.clear(); focused_workspace.reset(); }
    void focus(std::optional<Id> id) {
        for (auto& [n, w] : windows) {
            w.focused = id && n == *id;
            if (w.focused) w.mru = ++serial;
        }
    }
    Window parse(const Json& j) {
        Window w{id_value(j.at("id")), "", j.at("is_focused").get<bool>()};
        if (!j.at("app_id").is_null()) w.app = j.at("app_id").get<std::string>();
        if (w.app.size() > 4096 || w.app.find('\0') != std::string::npos)
            throw std::runtime_error("invalid app id");
        auto it = windows.find(w.id);
        w.seen = it == windows.end() ? ++serial : it->second.seen;
        w.mru = it == windows.end() ? 0 : it->second.mru;
        if (j.contains("workspace_id") && !j.at("workspace_id").is_null()) w.workspace = id_value(j.at("workspace_id"));
        if (j.contains("layout")) w.position = position(j.at("layout"));
        return w;
    }
    // Apply on a copy: a malformed snapshot never partially replaces valid state.
    bool apply(const Json& e) {
        if (!e.contains("WindowsChanged") && !e.contains("WindowOpenedOrChanged") &&
            !e.contains("WindowClosed") && !e.contains("WindowFocusChanged") &&
            !e.contains("WindowLayoutsChanged") && !e.contains("WorkspacesChanged") &&
            !e.contains("WorkspaceActivated")) return false;
        Model next = *this;
        if (e.contains("WorkspaceActivated")) {
            const auto& activation = e.at("WorkspaceActivated");
            Id id = id_value(activation.at("id"));
            if (activation.at("focused").get<bool>()) next.focused_workspace = id;
        } else if (e.contains("WindowLayoutsChanged")) {
            const auto& changes = e.at("WindowLayoutsChanged").at("changes");
            if (!changes.is_array() || changes.size() > 16384) throw std::runtime_error("invalid layouts");
            for (const auto& change : changes) {
                if (!change.is_array() || change.size() != 2) throw std::runtime_error("invalid layout change");
                Id id = id_value(change[0]);
                auto pos = position(change[1]);
                if (auto it = next.windows.find(id); it != next.windows.end()) it->second.position = pos;
            }
        } else if (e.contains("WorkspacesChanged")) {
            const auto& list = e.at("WorkspacesChanged").at("workspaces");
            if (!list.is_array() || list.size() > 16384) throw std::runtime_error("invalid workspaces");
            next.workspaces.clear();
            next.focused_workspace.reset();
            for (const auto& j : list) {
                auto output = j.at("output").is_null() ? std::string("~") : j.at("output").get<std::string>();
                Id id = id_value(j.at("id"));
                next.workspaces.emplace(id, Workspace{output, id_value(j.at("idx"))});
                if (j.value("is_focused", false)) next.focused_workspace = id;
            }
        } else if (e.contains("WindowsChanged")) {
            const auto& list = e.at("WindowsChanged").at("windows");
            if (!list.is_array() || list.size() > 16384) throw std::runtime_error("invalid snapshot");
            std::map<Id, Window> replacement;
            std::optional<Id> active;
            for (const auto& j : list) {
                auto w = next.parse(j);
                if (w.focused) active = w.id;
                if (!replacement.emplace(w.id, w).second) throw std::runtime_error("duplicate id");
            }
            next.windows = std::move(replacement);
            next.focus(active);
        } else if (e.contains("WindowOpenedOrChanged")) {
            auto w = next.parse(e.at("WindowOpenedOrChanged").at("window"));
            if (next.windows.size() >= 16384 && !next.windows.count(w.id)) throw std::runtime_error("too many windows");
            next.windows[w.id] = w;
            if (w.focused) next.focus(w.id);
        } else if (e.contains("WindowClosed")) {
            next.windows.erase(id_value(e.at("WindowClosed").at("id")));
        } else if (e.contains("WindowFocusChanged")) {
            const auto& id = e.at("WindowFocusChanged").at("id");
            next.focus(id.is_null() ? std::nullopt : std::optional<Id>(id_value(id)));
        } else return false; // Future niri events and extra fields are harmless.
        *this = std::move(next);
        return true;
    }
    std::vector<Group> groups(bool current_only = false) const {
        std::map<std::string, Group> by_key;
        for (const auto& [id, w] : windows) {
            if (current_only && !is_current(id)) continue;
            auto k = key(w);
            auto [it, added] = by_key.try_emplace(k, Group{k, w.app, {}, false});
            auto& g = it->second;
            g.windows.push_back(id);
            g.active |= w.focused;
        }
        std::vector<Group> out;
        for (auto& [k, g] : by_key) {
            std::sort(g.windows.begin(), g.windows.end(), [&](Id a, Id b) { return location(windows.at(a)) < location(windows.at(b)); });
            out.push_back(std::move(g));
        }
        std::sort(out.begin(), out.end(), [&](const auto& a, const auto& b) {
            return location(windows.at(a.windows.front())) < location(windows.at(b.windows.front()));
        });
        return out;
    }
    std::optional<Id> target(const std::string& group_key, bool current_only = false) const {
        for (const auto& g : groups(current_only)) if (g.key == group_key) {
            // An active group cycles in layout order; an inactive group restores MRU.
            for (size_t i = 0; i < g.windows.size(); ++i)
                if (windows.at(g.windows[i]).focused) return g.windows[(i + 1) % g.windows.size()];
            return *std::max_element(g.windows.begin(), g.windows.end(), [&](Id a, Id b) {
                return windows.at(a).mru < windows.at(b).mru;
            });
        }
        return std::nullopt;
    }
};
}
