#include "Icons.hpp"

#include <hyprgraphics/image/Image.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprland/src/render/Texture.hpp>
#include <hyprland/src/render/pass/TexPassElement.hpp>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <optional>
#include <span>
#include <sstream>
#include <string_view>
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

    struct SDesktopEntry {
        std::string icon;
        std::string category; // first FreeDesktop main category, lowercase
    };

    struct SDesktopIndex {
        bool                                         built = false;
        std::unordered_map<std::string, SDesktopEntry> byWmClass, byFileName; // lowercased key
    };

    // FreeDesktop main categories we accept as tray auto-names (first match in Categories= wins)
    std::string mainCategorySlug(const std::string& categoriesLine) {
        static constexpr std::string_view MAINS[] = {"audiovideo", "audio", "video", "development", "education", "game", "graphics",
                                                     "network",    "office", "science", "settings",  "system",   "utility"};
        std::stringstream                 all{categoriesLine};
        for (std::string part; std::getline(all, part, ';');) {
            auto cat = lower(part);
            while (!cat.empty() && (cat.front() == ' ' || cat.front() == '\t'))
                cat.erase(cat.begin());
            while (!cat.empty() && (cat.back() == ' ' || cat.back() == '\t'))
                cat.pop_back();
            if (cat.empty())
                continue;
            for (const auto MAIN : MAINS) {
                if (cat == MAIN)
                    return cat;
            }
        }
        return {};
    }

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
                std::string   icon, wmClass, categories;
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
                    else if (line.starts_with("Categories="))
                        categories = line.substr(11);
                }

                SDesktopEntry entry{.icon = icon, .category = mainCategorySlug(categories)};
                // data dirs are ordered by priority (user first), so the first entry wins
                const auto STEM = lower(it->path().stem().string());
                if (!icon.empty() || !entry.category.empty())
                    index.byFileName.try_emplace(STEM, entry);
                if (!wmClass.empty() && (!icon.empty() || !entry.category.empty()))
                    index.byWmClass.try_emplace(lower(wmClass), entry);
            }
        }
        return index;
    }

    const SDesktopEntry* desktopEntryForAppID(const std::string& appID) {
        const auto& index = desktopIndex();
        const auto  KEY   = lower(appID);

        if (const auto it = index.byWmClass.find(KEY); it != index.byWmClass.end())
            return &it->second;
        if (const auto it = index.byFileName.find(KEY); it != index.byFileName.end())
            return &it->second;
        // reverse-DNS desktop ids ending in the app id, e.g. org.gnome.Calculator.desktop for "calculator"
        for (const auto& [fileName, entry] : index.byFileName) {
            if (fileName.ends_with("." + KEY))
                return &entry;
        }
        return nullptr;
    }

    std::string iconNameForAppID(const std::string& appID) {
        if (const auto* entry = desktopEntryForAppID(appID); entry && !entry->icon.empty())
            return entry->icon;
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

namespace {
    // keep in sync with assets/icons/*.svg (embedded so hyprpm's .so ships them)
    constexpr std::string_view UI_GRIP = R"SVG(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="#FFFFFF">
  <circle cx="8.5" cy="5.5" r="2.4"/>
  <circle cx="15.5" cy="5.5" r="2.4"/>
  <circle cx="8.5" cy="12" r="2.4"/>
  <circle cx="15.5" cy="12" r="2.4"/>
  <circle cx="8.5" cy="18.5" r="2.4"/>
  <circle cx="15.5" cy="18.5" r="2.4"/>
</svg>)SVG";

    constexpr std::string_view UI_CLOSE = R"SVG(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="#FFFFFF" stroke-width="2.5" stroke-linecap="round">
  <path d="M6 6l12 12M18 6L6 18"/>
</svg>)SVG";

    constexpr std::string_view UI_PLUS = R"SVG(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="#FFFFFF" stroke-width="2.5" stroke-linecap="round">
  <path d="M12 5v14M5 12h14"/>
</svg>)SVG";

    constexpr std::string_view UI_PENCIL = R"SVG(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="#FFFFFF" stroke-width="2.5" stroke-linecap="round" stroke-linejoin="round">
  <path d="M14.5 4.5l5 5L8 21H3v-5L14.5 4.5z"/>
  <path d="M12.5 6.5l5 5"/>
</svg>)SVG";

    constexpr std::string_view UI_VIEW_CENTER = R"SVG(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="#FFFFFF">
  <rect x="1" y="7" width="4" height="10" rx="1" opacity="0.4"/>
  <rect x="19" y="7" width="4" height="10" rx="1" opacity="0.4"/>
  <rect x="6" y="5" width="12" height="14" rx="2"/>
</svg>)SVG";

    constexpr std::string_view UI_VIEW_GRID = R"SVG(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="#FFFFFF">
  <rect x="2" y="4" width="5.5" height="6.5" rx="1"/>
  <rect x="9.25" y="4" width="5.5" height="6.5" rx="1"/>
  <rect x="16.5" y="4" width="5.5" height="6.5" rx="1"/>
  <rect x="2" y="13" width="5.5" height="6.5" rx="1"/>
  <rect x="9.25" y="13" width="5.5" height="6.5" rx="1"/>
  <rect x="16.5" y="13" width="5.5" height="6.5" rx="1"/>
</svg>)SVG";

    constexpr std::string_view UI_FLOAT = R"SVG(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="#FFFFFF" stroke-width="2.2" stroke-linecap="round" stroke-linejoin="round">
  <rect x="3" y="6" width="12" height="10" rx="2" opacity="0.45"/>
  <rect x="9" y="8" width="12" height="10" rx="2"/>
</svg>)SVG";

    constexpr std::string_view UI_MAXIMIZE = R"SVG(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="#FFFFFF" stroke-width="2.2" stroke-linecap="round" stroke-linejoin="round">
  <rect x="5" y="5" width="14" height="14" rx="2"/>
</svg>)SVG";

    constexpr std::string_view UI_FULLSCREEN = R"SVG(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="#FFFFFF" stroke-width="2.2" stroke-linecap="round" stroke-linejoin="round">
  <path d="M8 4H4v4M16 4h4v4M8 20H4v-4M16 20h4v-4"/>
  <rect x="7" y="7" width="10" height="10" rx="1.5"/>
</svg>)SVG";

    constexpr std::string_view UI_PIN = R"SVG(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="#FFFFFF" stroke-width="2.2" stroke-linecap="round" stroke-linejoin="round">
  <path d="M12 14v7"/>
  <path d="M8.5 4.5l7 0"/>
  <path d="M9 4.5c0 3.5-1.5 5.5-1.5 7.5 0 1.5 1.5 2 4.5 2s4.5-.5 4.5-2c0-2-1.5-4-1.5-7.5"/>
</svg>)SVG";

    std::optional<std::string_view> uiSvgSource(std::string_view name) {
        if (name == "grip")
            return UI_GRIP;
        if (name == "close")
            return UI_CLOSE;
        if (name == "plus")
            return UI_PLUS;
        if (name == "pencil")
            return UI_PENCIL;
        if (name == "view-center")
            return UI_VIEW_CENTER;
        if (name == "view-grid")
            return UI_VIEW_GRID;
        if (name == "float")
            return UI_FLOAT;
        if (name == "maximize")
            return UI_MAXIMIZE;
        if (name == "fullscreen")
            return UI_FULLSCREEN;
        if (name == "pin")
            return UI_PIN;
        return std::nullopt;
    }

    std::string tintUiSvg(std::string svg, const CHyprColor& color) {
        const auto HEX = std::format("#{:02X}{:02X}{:02X}", sc<int>(std::clamp(sc<float>(color.r), 0.F, 1.F) * 255.F + 0.5F),
                                     sc<int>(std::clamp(sc<float>(color.g), 0.F, 1.F) * 255.F + 0.5F), sc<int>(std::clamp(sc<float>(color.b), 0.F, 1.F) * 255.F + 0.5F));
        // only the full six-digit white: replacing "#FFF" would also match inside "#FFFFFF" and yield "#FFFFFFFFF"
        for (const auto* from : {"#FFFFFF", "#ffffff"}) {
            for (size_t pos = 0; (pos = svg.find(from, pos)) != std::string::npos;) {
                svg.replace(pos, std::strlen(from), HEX);
                pos += HEX.size();
            }
        }
        return svg;
    }
}

std::string GridIcons::categoryForAppID(const std::string& appID) {
    if (const auto* entry = desktopEntryForAppID(appID); entry && !entry->category.empty())
        return entry->category;
    return {};
}

SP<Render::ITexture> GridIcons::uiIcon(std::string_view name, const CHyprColor& color, int rasterPx) {
    static std::unordered_map<std::string, SP<Render::ITexture>> cache;

    const auto SOURCE = uiSvgSource(name);
    if (!SOURCE || !g_pHyprRenderer)
        return nullptr;

    rasterPx         = std::clamp(rasterPx, 16, 512);
    const auto KEY   = std::format("{}\n{:02X}{:02X}{:02X}\n{}", name, sc<int>(color.r * 255.F + 0.5F), sc<int>(color.g * 255.F + 0.5F), sc<int>(color.b * 255.F + 0.5F), rasterPx);
    if (const auto it = cache.find(KEY); it != cache.end())
        return it->second;

    // load via a temp file: same path Hyprgraphics uses for app icons (in-memory SVG came out black)
    const auto      SVG = tintUiSvg(std::string{*SOURCE}, color);
    const auto      DIR = fs::temp_directory_path() / "gridoverview-icons";
    std::error_code ec;
    fs::create_directories(DIR, ec);
    const auto FILE = DIR / std::format("{}-{:02X}{:02X}{:02X}-{}.svg", name, sc<int>(color.r * 255.F + 0.5F), sc<int>(color.g * 255.F + 0.5F),
                                        sc<int>(color.b * 255.F + 0.5F), rasterPx);
    {
        std::ofstream out{FILE, std::ios::trunc};
        if (!out) {
            cache.emplace(KEY, nullptr);
            return nullptr;
        }
        out << SVG;
    }

    SP<Render::ITexture> texture;
    Hyprgraphics::CImage image(FILE.string(), Vector2D{rasterPx, rasterPx});
    if (image.success()) {
        if (const auto SURFACE = image.cairoSurface(); SURFACE && SURFACE->cairo())
            texture = g_pHyprRenderer->createTexture(SURFACE->cairo());
    }

    cache.emplace(KEY, texture);
    return texture;
}

void GridIcons::drawUiIcon(std::string_view name, const CBox& box, float alpha, const CHyprColor& color) {
    if (alpha <= 0.F || box.empty() || !g_pHyprRenderer)
        return;

    const auto TEXTURE = uiIcon(name, color, std::max(32, sc<int>(std::max(box.w, box.h) * 2.0)));
    if (!TEXTURE || TEXTURE->m_size.x <= 0)
        return;

    const double ASPECT = TEXTURE->m_size.x / std::max(TEXTURE->m_size.y, 1.0);
    double       w = box.w, h = box.h;
    if (w / std::max(h, 1.0) > ASPECT)
        w = h * ASPECT;
    else
        h = w / ASPECT;

    CTexPassElement::SRenderData data;
    data.tex = TEXTURE;
    data.box = CBox{box.middle().x - w / 2.0, box.middle().y - h / 2.0, w, h}.round();
    data.a   = alpha * color.a;
    Render::IHyprRenderer::currentPass(g_pHyprRenderer->context()).add(makeUnique<CTexPassElement>(std::move(data)));
}
