#include "Icons.hpp"

#include <hyprgraphics/image/Image.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprland/src/render/Texture.hpp>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <unordered_map>
#include <vector>

namespace fs = std::filesystem;

namespace {
    // raster size for SVG icons; drawn scaled down to window_icon_size
    constexpr int ICON_RASTER = 256;

    std::string lower(std::string s) {
        std::ranges::transform(s, s.begin(), [](unsigned char c) { return std::tolower(c); });
        return s;
    }

    std::vector<std::string> dataDirs() {
        std::vector<std::string> dirs;
        const char*              home     = std::getenv("HOME");
        const char*              dataHome = std::getenv("XDG_DATA_HOME");
        dirs.emplace_back(dataHome && *dataHome ? std::string{dataHome} : std::string{home ? home : ""} + "/.local/share");

        const char*       dataDirsEnv = std::getenv("XDG_DATA_DIRS");
        std::stringstream all(dataDirsEnv && *dataDirsEnv ? dataDirsEnv : "/usr/local/share:/usr/share");
        for (std::string dir; std::getline(all, dir, ':');) {
            if (!dir.empty())
                dirs.emplace_back(dir);
        }
        return dirs;
    }

    struct SDesktopIndex {
        bool                                         built = false;
        std::unordered_map<std::string, std::string> byWmClass, byFileName; // lowercased key -> Icon=
    };

    SDesktopIndex& desktopIndex() {
        static SDesktopIndex index;
        if (index.built)
            return index;
        index.built = true;

        for (const auto& base : dataDirs()) {
            std::error_code ec;
            const auto      applications = fs::path{base} / "applications";
            if (!fs::is_directory(applications, ec))
                continue;

            for (auto it = fs::recursive_directory_iterator(applications, fs::directory_options::skip_permission_denied, ec); !ec && it != fs::recursive_directory_iterator();
                 it.increment(ec)) {
                if (!it->is_regular_file(ec) || it->path().extension() != ".desktop")
                    continue;

                std::ifstream file(it->path());
                std::string   icon, wmClass;
                bool          inEntry = false;
                for (std::string line; std::getline(file, line);) {
                    if (!line.empty() && line.front() == '[') {
                        inEntry = line == "[Desktop Entry]";
                        continue;
                    }
                    if (!inEntry)
                        continue;
                    if (line.starts_with("Icon="))
                        icon = line.substr(5);
                    else if (line.starts_with("StartupWMClass="))
                        wmClass = line.substr(15);
                }

                if (icon.empty())
                    continue;
                // data dirs are ordered by priority (user first), so the first entry wins
                index.byFileName.try_emplace(lower(it->path().stem().string()), icon);
                if (!wmClass.empty())
                    index.byWmClass.try_emplace(lower(wmClass), icon);
            }
        }
        return index;
    }

    std::string iconNameForAppID(const std::string& appID) {
        const auto& index = desktopIndex();
        const auto  KEY   = lower(appID);

        if (const auto it = index.byWmClass.find(KEY); it != index.byWmClass.end())
            return it->second;
        if (const auto it = index.byFileName.find(KEY); it != index.byFileName.end())
            return it->second;
        // reverse-DNS desktop ids ending in the app id, e.g. org.gnome.Calculator.desktop for "calculator"
        for (const auto& [fileName, icon] : index.byFileName) {
            if (fileName.ends_with("." + KEY))
                return icon;
        }
        return appID;
    }

    // "scalable" beats every fixed size, larger fixed sizes beat smaller ones; anything else is skipped
    int sizeDirScore(const std::string& name) {
        if (name == "scalable")
            return 100000;
        const auto X = name.find('x');
        if (X == std::string::npos)
            return -1;
        try {
            const int SIZE = std::stoi(name.substr(0, X));
            return name.find('@') != std::string::npos ? SIZE * 2 : SIZE;
        } catch (...) { return -1; }
    }

    std::optional<std::string> findIconFile(std::string name, const std::string& iconTheme) {
        if (name.empty())
            return std::nullopt;

        std::error_code ec;
        if (name.front() == '/')
            return fs::is_regular_file(name, ec) ? std::optional{name} : std::nullopt;

        if (name.ends_with(".png") || name.ends_with(".svg"))
            name.resize(name.size() - 4);

        std::vector<fs::path> roots;
        if (const char* home = std::getenv("HOME"))
            roots.emplace_back(std::string{home} + "/.icons");
        for (const auto& base : dataDirs())
            roots.emplace_back(fs::path{base} / "icons");

        std::vector<std::string> themes;
        if (!iconTheme.empty())
            themes.emplace_back(iconTheme);
        themes.emplace_back("hicolor");

        for (const auto& theme : themes) {
            for (const auto& root : roots) {
                const auto themeDir = root / theme;
                if (!fs::is_directory(themeDir, ec))
                    continue;

                std::optional<std::string> best;
                int                        bestScore = -1;
                for (const auto& sizeDir : fs::directory_iterator(themeDir, fs::directory_options::skip_permission_denied, ec)) {
                    const int SCORE = sizeDirScore(sizeDir.path().filename().string());
                    if (SCORE <= bestScore)
                        continue;
                    for (const auto* ext : {".svg", ".png"}) {
                        const auto candidate = sizeDir.path() / "apps" / (name + ext);
                        if (fs::is_regular_file(candidate, ec)) {
                            best      = candidate.string();
                            bestScore = SCORE;
                            break;
                        }
                    }
                }
                if (best)
                    return best;
            }
        }

        for (const auto& base : dataDirs()) {
            for (const auto* ext : {".svg", ".png"}) {
                const auto candidate = fs::path{base} / "pixmaps" / (name + ext);
                if (fs::is_regular_file(candidate, ec))
                    return candidate.string();
            }
        }
        return std::nullopt;
    }
}

SP<Render::ITexture> GridIcons::iconForAppID(const std::string& appID, const std::string& iconTheme) {
    static std::unordered_map<std::string, SP<Render::ITexture>> cache; // also remembers misses (nullptr)

    const auto KEY = iconTheme + "\n" + appID;
    if (const auto it = cache.find(KEY); it != cache.end())
        return it->second;

    SP<Render::ITexture> texture;
    if (const auto FILE = findIconFile(iconNameForAppID(appID), iconTheme)) {
        Hyprgraphics::CImage image(*FILE, {ICON_RASTER, ICON_RASTER});
        if (image.success()) {
            if (const auto SURFACE = image.cairoSurface(); SURFACE && SURFACE->cairo())
                texture = g_pHyprRenderer->createTexture(SURFACE->cairo());
        }
    }

    cache.emplace(KEY, texture);
    return texture;
}
