#pragma once
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace running {
inline std::string env(const char *name, std::string fallback = "") {
    const char *v = std::getenv(name);
    return v && *v ? v : fallback;
}
inline std::string trim(std::string s) {
    auto a = s.find_first_not_of(" \t\r\n");
    return a == std::string::npos ? "" : s.substr(a, s.find_last_not_of(" \t\r\n") - a + 1);
}
class Icons {
    std::map<std::string, std::string> exact, aliases, folded_icons;
    static std::string folded(std::string value) {
        for (char &c : value)
            if (static_cast<unsigned char>(c) < 128)
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return value;
    }

  public:
    explicit Icons(const std::vector<std::string> &roots) {
        namespace fs = std::filesystem;
        std::set<std::string> seen;
        // Roots are in XDG precedence order. Desktop files are data, never executed.
        size_t count = 0;
        for (const auto &root : roots) {
            std::error_code ec;
            auto dir = fs::path(root) / "applications";
            std::vector<fs::path> files;
            fs::recursive_directory_iterator it(dir, fs::directory_options::skip_permission_denied,
                                                ec),
                end;
            for (; !ec && it != end && count < 20000; it.increment(ec), ++count) {
                if (it.depth() >= 8)
                    it.disable_recursion_pending();
                if (it->path().extension() == ".desktop" && it->is_regular_file(ec))
                    files.push_back(it->path());
            }
            std::sort(files.begin(), files.end());
            for (const auto &file : files) {
                auto id = file.lexically_relative(dir).generic_string();
                std::replace(id.begin(), id.end(), '/', '-');
                id.resize(id.size() - 8);
                if (!seen.insert(id).second)
                    continue;
                std::ifstream in(file);
                std::string line, icon, wmclass, type;
                bool section = false, hidden = false;
                size_t bytes = 0;
                while (std::getline(in, line) && (bytes += line.size()) < 262144) {
                    line = trim(line);
                    if (line.empty() || line[0] == '#')
                        continue;
                    if (line[0] == '[') {
                        section = line == "[Desktop Entry]";
                        continue;
                    }
                    if (!section)
                        continue;
                    auto pos = line.find('=');
                    if (pos == std::string::npos)
                        continue;
                    auto name = trim(line.substr(0, pos)), value = trim(line.substr(pos + 1));
                    if (name == "Icon")
                        icon = value;
                    if (name == "StartupWMClass")
                        wmclass = value;
                    if (name == "Hidden")
                        hidden = value == "true";
                    if (name == "Type")
                        type = value;
                }
                // Keep the mask for Hidden=true even if the lower-priority entry exists.
                if (hidden || type != "Application") {
                    exact.emplace(id, "sparkles");
                    continue;
                }
                // ABI promises themed names only. Absolute files need a host image API.
                if (icon.empty() || icon.find('/') != std::string::npos || icon.size() > 4096 ||
                    icon.find('\0') != std::string::npos)
                    icon = "sparkles";
                exact.emplace(id, icon);
                if (!wmclass.empty())
                    aliases.emplace(wmclass, icon);
            }
        }
        // Precompute once; missing icon IDs must not scan all desktop entries every frame.
        for (const auto *entries : {&exact, &aliases})
            for (const auto &[name, icon] : *entries) {
                auto [it, added] = folded_icons.emplace(folded(name), icon);
                if (!added && it->second != icon)
                    it->second.clear();
            }
    }
    std::string resolve(std::string app) const {
        if (app.size() > 8 && app.compare(app.size() - 8, 8, ".desktop") == 0)
            app.resize(app.size() - 8);
        if (auto it = exact.find(app); it != exact.end())
            return it->second;
        if (auto it = aliases.find(app); it != aliases.end())
            return it->second;
        // Accept casing differences only when all matching entries agree. Avoid
        // guessing between unrelated applications with ambiguous desktop names.
        if (auto it = folded_icons.find(folded(app));
            it != folded_icons.end() && !it->second.empty())
            return it->second;
        return "sparkles"; // Guaranteed bundled semantic fallback in the inspected host.
    }
    static std::vector<std::string> roots() {
        std::vector<std::string> out;
        auto home = env("HOME");
        out.push_back(env("XDG_DATA_HOME", home + "/.local/share"));
        std::istringstream dirs(env("XDG_DATA_DIRS", "/usr/local/share:/usr/share"));
        std::string part;
        while (std::getline(dirs, part, ':'))
            if (!part.empty() && part[0] == '/')
                out.push_back(part);
        // Common Flatpak export locations may be absent from a custom niri session's XDG_DATA_DIRS.
        out.push_back(home + "/.local/share/flatpak/exports/share");
        out.push_back("/var/lib/flatpak/exports/share");
        return out;
    }
};
}
