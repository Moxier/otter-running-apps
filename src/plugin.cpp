#include "icons.hpp"
#include "ipc.hpp"
#include "otter_plugin_abi.h"
#include <limits>
#include <memory>

namespace {
using namespace running;
constexpr uint64_t more_hit = std::numeric_limits<uint64_t>::max();
constexpr uint32_t item_width = 36;
bool initialized = false;
std::unique_ptr<Icons> icons;
unsigned slots = 8;
struct Instance {
    OtterPluginBarInstance bar{};
    const OtterPluginHost *host;
    Client client;
    std::map<uint64_t, std::string> hits;
    uint64_t next_hit = 1;
    size_t page = 0;
    std::optional<Id> displayed_workspace;
    explicit Instance(const OtterPluginHost *h)
        : host(h), client(env("NIRI_SOCKET"), [h](const char *message) {
              if (h->log)
                  h->log(h->userdata, 2, message);
          }) {}
};
uint32_t width(void *, const OtterPluginSlotConstraints *) {
    // Fixed reservation: current host doesn't remeasure plugin widths on IPC events.
    return slots * item_width;
}
bool contains(const OtterPluginRect &clip, const OtterPluginRect &rect) {
    return rect.x >= clip.x && rect.y >= clip.y &&
           static_cast<int64_t>(rect.x) + rect.width <= static_cast<int64_t>(clip.x) + clip.width &&
           static_cast<int64_t>(rect.y) + rect.height <= static_cast<int64_t>(clip.y) + clip.height;
}
int contribute(void *self, OtterPluginFrameCtx *frame, const OtterPluginSlotConstraints *cons) {
    if (!self || !cons)
        return -1;
    try {
        auto &s = *static_cast<Instance *>(self);
        s.client.tick(now_ms());
        if (s.displayed_workspace != s.client.model.focused_workspace)
            s.page = 0;
        s.displayed_workspace = s.client.model.focused_workspace;
        auto groups = s.client.model.groups(true);
        s.hits.clear();
        const auto &a = cons->area;
        size_t capacity = std::min<size_t>(slots, a.width / item_width);
        if (a.height == 0 || capacity == 0)
            return 0;
        bool overflow = groups.size() > capacity;
        size_t per_page = overflow ? capacity - 1 : capacity;
        size_t pages = per_page ? (groups.size() + per_page - 1) / per_page : 1;
        s.page %= std::max<size_t>(pages, 1);
        size_t start = s.page * per_page;
        size_t visible = std::min(per_page, groups.size() - start);
        size_t cells = visible + (overflow ? 1 : 0);
        // Center this page's occupied cells, including its overflow button.
        int32_t left = a.x + static_cast<int32_t>((a.width - cells * item_width) / 2);
        for (size_t i = 0; i < visible; ++i) {
            const auto &g = groups[start + i];
            OtterPluginRect r{left + static_cast<int32_t>(i * item_width), a.y, item_width,
                              a.height};
            // Hidden portions must not leave clickable targets behind.
            if (!contains(cons->clip, r))
                continue;
            uint64_t hit = s.next_hit++;
            if (s.next_hit == more_hit)
                s.next_hit = 1;
            s.hits.emplace(hit, g.key);
            auto node = "app-" + g.key;
            auto icon = icons->resolve(g.app);
            int rc = s.host->sd_bar_item(
                frame, node.c_str(), &r, "", 0, icon.c_str(),
                g.active ? OTTER_PLUGIN_BAR_ITEM_ACTIVE : OTTER_PLUGIN_BAR_ITEM_NORMAL, hit);
            if (rc)
                return rc;
            // A capped count fits inside the existing cell; the group never expands.
            if (g.windows.size() > 1 && r.height >= 14 && s.host->sd_rect && s.host->sd_label) {
                OtterPluginRect badge{r.x + static_cast<int32_t>(r.width) - 15, r.y + 1, 14, 12};
                auto count =
                    g.windows.size() > 9 ? std::string("9+") : std::to_string(g.windows.size());
                auto bg = node + "-count-bg", label = node + "-count";
                rc = s.host->sd_rect(frame, bg.c_str(), &badge,
                                     OTTER_PLUGIN_COLOR_THEME_BACKGROUND_ALT);
                if (rc)
                    return rc;
                rc = s.host->sd_label(frame, label.c_str(), &badge, count.c_str(), 10,
                                      OTTER_PLUGIN_COLOR_THEME_FOREGROUND);
                if (rc)
                    return rc;
                // Preserve the button hit over the decorative count overlay.
                if (s.host->sd_hit_button) {
                    auto hit_node = node + "-count-hit";
                    rc = s.host->sd_hit_button(frame, hit_node.c_str(), &r, hit);
                    if (rc)
                        return rc;
                }
            }
        }
        if (overflow) {
            OtterPluginRect r{left + static_cast<int32_t>(visible * item_width), a.y, item_width,
                              a.height};
            if (contains(cons->clip, r)) {
                s.hits.emplace(more_hit, "");
                return s.host->sd_bar_item(frame, "more", &r, ">", 0, nullptr, 0, more_hit);
            }
        }
        return 0;
    } catch (...) {
        return -1;
    } // No C++ exception crosses the C ABI.
}
void destroy(void *self) {
    delete static_cast<Instance *>(self);
}
int event(void *self, const OtterPluginEvent *e) {
    if (!self || !e)
        return -1;
    if (e->kind != OTTER_PLUGIN_EVENT_CLICK)
        return 0;
    try {
        auto &s = *static_cast<Instance *>(self);
        auto it = s.hits.find(e->hit_data);
        if (it == s.hits.end())
            return 0;
        s.client.tick(now_ms());
        // A click from a previous workspace must not activate that app elsewhere.
        if (s.displayed_workspace != s.client.model.focused_workspace) {
            if (s.host->request_frame)
                s.host->request_frame(s.host->userdata);
            return 0;
        }
        if (e->hit_data == more_hit)
            ++s.page;
        else {
            std::string key = it->second;
            if (auto id = s.client.model.target(key, true))
                s.client.focus(*id);
            s.client.tick(now_ms());
            s.client.tick(now_ms()); // Flush a newly queued nonblocking request promptly.
        }
        if (s.host->request_frame)
            s.host->request_frame(s.host->userdata);
        return 0;
    } catch (...) {
        return -1;
    }
}
const OtterPluginBarVTable vtable{width, contribute, destroy, event};
}
extern "C" {
__attribute__((visibility("default"))) uint32_t otter_plugin_abi_version() {
    return OTTER_PLUGIN_ABI_VERSION;
}
__attribute__((visibility("default"))) int otter_plugin_init(const OtterPluginHost *host) {
    if (!host || host->abi_version != OTTER_PLUGIN_ABI_VERSION)
        return -1;
    // Require the current full host struct. ABI v1 has no struct-size negotiation;
    // ancient smaller layouts cannot be probed safely. See README compatibility.
    if (!host->sd_bar_item)
        return -1;
    try {
        if (!icons)
            icons = std::make_unique<Icons>(Icons::roots());
        auto value = env("OTTER_RUNNING_APPS_SLOTS", "8");
        char *end = nullptr;
        auto n = std::strtoul(value.c_str(), &end, 10);
        slots = end && *end == '\0' && n >= 2 && n <= 32 ? static_cast<unsigned>(n) : 8;
        initialized = true;
        return 0;
    } catch (...) {
        return -1;
    }
}
__attribute__((visibility("default"))) void otter_plugin_deinit() {
    initialized = false;
    icons.reset();
}
__attribute__((visibility("default"))) int otter_plugin_query(OtterPluginManifest *out) {
    if (!out)
        return -1;
    *out = {"running-apps", "Running applications (niri)", "0.5.0", 1, 1, 0, 0, 0};
    return 0;
}
__attribute__((visibility("default"))) OtterPluginBarInstance *
otter_plugin_bar_attach(const OtterPluginHost *host, const char *) {
    if (!initialized || !host || host->abi_version != 1 || !host->sd_bar_item)
        return nullptr;
    try {
        auto s = std::make_unique<Instance>(host);
        s->bar = {&vtable, s.get()};
        auto *out = &s->bar;
        s.release();
        return out;
    } catch (...) {
        return nullptr;
    }
}
}
