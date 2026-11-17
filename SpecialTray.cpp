// Scratchpad tray (special_tray): special workspaces as cards at a screen edge (right or left), in screen
// coordinates (not zoomed or panned with the grid). Hit tests, layout and drawing; input and drag handling call
// into it from scrollOverview.cpp.
#include "scrollOverview.hpp"
#include <algorithm>
#include <any>
#include <array>
#include <chrono>
#include <cstdlib>
#include <dlfcn.h>
#include <functional>
#include <limits>
#include <optional>
#include <unordered_set>
#include <linux/input-event-codes.h>
#include <state/MonitorState.hpp>
#include <state/WorkspaceState.hpp>
#include <hyprland/src/workspace/RegularWorkspace.hpp>
#include <hyprland/src/workspace/query/Query.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprland/src/render/SceneResources.hpp>
#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/config/ConfigValue.hpp>
#include <hyprland/src/config/ConfigManager.hpp>
#include <hyprland/src/config/shared/actions/ConfigActions.hpp>
#include <hyprland/src/config/shared/animation/AnimationTree.hpp>
#include <hyprland/src/config/shared/complex/ComplexDataTypes.hpp>
#include <hyprland/src/event/EventBus.hpp>
#include <hyprland/src/animation/AnimationManager.hpp>
#include <hyprland/src/ipc/s2/S2.hpp>
#include <hyprland/src/managers/input/InputManager.hpp>
#include <hyprland/src/keybinds/Manager.hpp>
#include <hyprland/src/managers/SeatManager.hpp>
#include <hyprland/src/managers/eventLoop/EventLoopManager.hpp>
#include <hyprland/src/config/supplementary/executor/Executor.hpp>
#include <hyprland/src/config/shared/workspace/WorkspaceRuleManager.hpp>
#include <hyprland/src/layout/LayoutManager.hpp>
#include <hyprland/src/layout/algorithm/Algorithm.hpp>
#include <hyprland/src/layout/space/Space.hpp>
#include <hyprland/src/layout/target/Target.hpp>
#include <hyprland/src/layout/algorithm/tiled/scrolling/ScrollingAlgorithm.hpp>
#include <hyprland/src/layout/algorithm/tiled/dwindle/DwindleAlgorithm.hpp>
#include <hyprland/src/desktop/state/GlobalWindowController.hpp>
#include <hyprland/src/pointer/cursor/CursorShapeOverrideController.hpp>
#include <hyprland/src/desktop/state/FocusState.hpp>
#include <hyprland/src/desktop/view/Group.hpp>
#include <hyprland/src/desktop/view/window/Window.hpp>
#include <hyprland/src/desktop/view/window/WindowGroupMembership.hpp>
#include <hyprland/src/desktop/view/window/WindowPresentation.hpp>
#include <hyprland/src/desktop/view/WLSurface.hpp>
#include <hyprland/src/desktop/view/LayerSurface.hpp>
#include <hyprland/src/desktop/view/Popup.hpp>
#include <hyprland/src/desktop/state/WindowState.hpp>
#include <hyprland/src/desktop/state/ViewState.hpp>
#include <hyprland/src/managers/fullscreen/FullscreenController.hpp>
#include <hyprland/src/protocols/LayerShell.hpp>
#include <hyprland/src/devices/IKeyboard.hpp>
#include <hyprland/src/helpers/math/Math.hpp>
#include <hyprland/src/helpers/time/Time.hpp>
#include <hyprland/src/plugins/PluginSystem.hpp>
#include <hyprland/src/render/pass/BorderPassElement.hpp>
#include <hyprland/src/render/pass/Pass.hpp>
#include <hyprland/src/render/pass/ClearPassElement.hpp>
#include <hyprland/src/render/pass/PreBlurElement.hpp>
#include <hyprland/src/render/pass/RectPassElement.hpp>
#include <hyprland/src/render/pass/RendererHintsPassElement.hpp>
#include <hyprland/src/render/pass/SurfacePassElement.hpp>
#include <hyprland/src/render/pass/TexPassElement.hpp>
#include <hyprland/src/render/types.hpp>
#include <hyprland/src/render/decorations/CHyprGroupBarDecoration.hpp>
#include <hyprland/src/render/decorations/DecorationPositioner.hpp>
#include <hyprutils/utils/ScopeGuard.hpp>
#include "Config.hpp"
#include "DropIndicator.hpp"
#include "Icons.hpp"
#include "NativeDrag.hpp"
#include "OverviewOpen.hpp"
#include "OverviewPassElement.hpp"
#include "OverviewRender.hpp"
#include "Window.hpp"
#include "ScrollOverviewInternal.hpp"
#include "SpecialTray.hpp"
#include "IOverview.hpp"
#include <sstream>
#include <fstream>
#include <filesystem>
#include <cctype>
#include <hyprland/src/input/Keys.hpp>
#include <xkbcommon/xkbcommon.h>

static constexpr float       TRAY_MARGIN     = 24.F; // logical px: screen edge to cards, and between grid and cards
static constexpr float       TRAY_GAP        = 20.F; // between a card's label and the next card
static constexpr float       TRAY_LABEL      = 50.F; // band under every card for its name (centred in it)
static constexpr float       TRAY_ICON_PLATE = 1.3F; // icon plate size relative to the icon
static constexpr float       TRAY_PILL       = 32.F; // height of the name pill (15 px text + padding)
static constexpr int         TRAY_LABEL_SIZE = 15;
static constexpr float       TRAY_ADD_HEIGHT = 56.F; // the "+" box below the cards
static constexpr float       TRAY_DOCK_PAD   = 14.F; // dock around the cards (within TRAY_MARGIN)
static constexpr float       TRAY_DOCK_ROUNDING = 18.F;
// max depth foreshortening when the overview is fully open (also used by strip perspective)
static constexpr float       TRAY_PERSPECTIVE_SHRINK = 0.28F;
static constexpr float       TRAY_GRIP_WIDTH   = 16.F; // grip after the pencil
static constexpr float       TRAY_PENCIL_WIDTH = 16.F; // pencil after the name
static constexpr float       TRAY_ACTION_GAP   = 16.F; // gap between name and pencil/grip cluster
static constexpr float       TRAY_ICON_GAP     = 6.F;  // gap between pencil and grip
static constexpr float       TRAY_CLOSE_SIZE   = 36.F; // × hitbox; centre sits on the card's top-right corner
static constexpr std::string_view SPECIAL_PREFIX = "special:";

static std::string specialTrayName(std::string name) {
    const auto FIRST = name.find_first_not_of(" \t");
    const auto LAST  = name.find_last_not_of(" \t");
    name             = FIRST == std::string::npos ? "" : name.substr(FIRST, LAST - FIRST + 1);
    if (name.starts_with(SPECIAL_PREFIX))
        name = name.substr(SPECIAL_PREFIX.size());
    return name;
}

// remembered card order + living tray roster (disk). Config special_workspaces only seeds the roster once.
static std::vector<std::string> g_trayOrder;
static std::vector<std::string> g_trayRoster;
static bool                     g_trayOrderLoaded  = false;
static bool                     g_trayRosterLoaded = false;

static std::filesystem::path trayStateDir() {
    if (const char* state = std::getenv("XDG_STATE_HOME"); state && *state)
        return std::filesystem::path{state} / "gridoverview";
    const char* home = std::getenv("HOME");
    return std::filesystem::path{home && *home ? home : "."} / ".local" / "state" / "gridoverview";
}

static std::vector<std::string> readNameList(const std::filesystem::path& path) {
    std::vector<std::string> names;
    std::ifstream            file{path};
    if (!file)
        return names;
    std::string line;
    std::getline(file, line);
    std::stringstream ss{line};
    for (std::string entry; std::getline(ss, entry, ',');) {
        const auto NAME = specialTrayName(entry);
        if (!NAME.empty() && std::ranges::find(names, NAME) == names.end())
            names.push_back(NAME);
    }
    return names;
}

static void writeNameList(const std::filesystem::path& path, const std::vector<std::string>& names) {
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    std::ofstream file{path, std::ios::trunc};
    if (!file)
        return;
    for (size_t i = 0; i < names.size(); ++i) {
        if (i)
            file << ',';
        file << names[i];
    }
}

static void loadTrayOrder() {
    // re-read when the file changes (also after an external edit without reloading the plugin)
    static std::filesystem::file_time_type orderTime{};
    const auto                             ORDER = trayStateDir() / "tray_order";
    std::error_code                        ec;
    const auto                             ot = std::filesystem::exists(ORDER, ec) ? std::filesystem::last_write_time(ORDER, ec) : std::filesystem::file_time_type{};
    if (g_trayOrderLoaded && ot == orderTime)
        return;
    g_trayOrderLoaded = true;
    orderTime         = ot;
    g_trayOrder       = readNameList(ORDER);
}

static void saveTrayOrder() {
    writeNameList(trayStateDir() / "tray_order", g_trayOrder);
}

static void saveTrayRoster() {
    writeNameList(trayStateDir() / "special_workspaces", g_trayRoster);
}

static void loadTrayRoster(PHLMONITOR monitor) {
    static std::filesystem::file_time_type rosterTime{};
    const auto                             PATH = trayStateDir() / "special_workspaces";
    std::error_code                        ec;
    const bool                             EXISTS = std::filesystem::exists(PATH, ec);
    const auto                             rt     = EXISTS ? std::filesystem::last_write_time(PATH, ec) : std::filesystem::file_time_type{};
    if (g_trayRosterLoaded && EXISTS && rt == rosterTime)
        return;

    g_trayRosterLoaded = true;
    rosterTime         = rt;
    if (EXISTS) {
        g_trayRoster = readNameList(PATH);
        return;
    }

    // first run: seed from config, then persist so × / rename / + survive restarts
    g_trayRoster.clear();
    std::stringstream configured(ScrollOverview::Config::getValue<std::string>("plugin:gridoverview:special_workspaces", monitor));
    for (std::string entry; std::getline(configured, entry, ',');) {
        const auto NAME = specialTrayName(entry);
        if (!NAME.empty() && std::ranges::find(g_trayRoster, NAME) == g_trayRoster.end())
            g_trayRoster.push_back(NAME);
    }
    saveTrayRoster();
}

static bool isAnonymousTrayName(const std::string& name) {
    if (name == "scratch")
        return true;
    if (!name.starts_with("scratch-"))
        return false;
    const auto REST = name.substr(8);
    return !REST.empty() && std::ranges::all_of(REST, [](unsigned char c) { return std::isdigit(c) != 0; });
}

static void addTrayRosterName(const std::string& name) {
    if (name.empty())
        return;
    loadTrayRoster(nullptr);
    if (std::ranges::find(g_trayRoster, name) == g_trayRoster.end()) {
        g_trayRoster.push_back(name);
        saveTrayRoster();
    }
    loadTrayOrder();
    if (std::ranges::find(g_trayOrder, name) == g_trayOrder.end()) {
        g_trayOrder.push_back(name);
        saveTrayOrder();
    }
}

static void removeTrayRosterName(const std::string& name) {
    loadTrayRoster(nullptr);
    std::erase(g_trayRoster, name);
    saveTrayRoster();
    loadTrayOrder();
    std::erase(g_trayOrder, name);
    saveTrayOrder();
}

static std::string sanitizeTrayName(std::string name) {
    name = specialTrayName(std::move(name));
    std::string out;
    for (const char C : name) {
        // keep A–Z/a–z/0–9/_/- (case preserved); everything else becomes a single '-'
        if (std::isalnum(sc<unsigned char>(C)) || C == '_' || C == '-')
            out += C;
        else if (!out.empty() && out.back() != '-')
            out += '-';
    }
    while (!out.empty() && out.back() == '-')
        out.pop_back();
    return out;
}

template <typename TCard>
static void applyTrayOrder(std::vector<TCard>& cards) {
    loadTrayOrder();
    if (cards.empty())
        return;

    if (g_trayOrder.empty()) {
        for (const auto& card : cards)
            g_trayOrder.push_back(card.name);
        return;
    }

    std::vector<TCard> ordered;
    ordered.reserve(cards.size());
    for (const auto& name : g_trayOrder) {
        const auto IT = std::ranges::find_if(cards, [&name](const auto& card) { return card.name == name; });
        if (IT == cards.end())
            continue;
        ordered.push_back(*IT);
        cards.erase(IT);
    }
    ordered.insert(ordered.end(), cards.begin(), cards.end());
    for (const auto& card : ordered) {
        if (std::ranges::find(g_trayOrder, card.name) == g_trayOrder.end())
            g_trayOrder.push_back(card.name);
    }
    cards = std::move(ordered);
}

CScrollOverview::ESpecialTrayEdge CScrollOverview::specialTrayEdge() const {
    const auto EDGE = ScrollOverview::Config::getValue<std::string>("plugin:gridoverview:special_tray_edge", pMonitor.lock());
    if (EDGE == "left")
        return ESpecialTrayEdge::Left;
    if (EDGE == "bottom")
        return ESpecialTrayEdge::Bottom;
    return ESpecialTrayEdge::Right;
}

// logical px from a card's bottom edge to the middle of its name: below the app icons when they are shown
float CScrollOverview::specialTrayLabelOffset() const {
    if (!ScrollOverview::Config::getValue<bool>("plugin:gridoverview:special_tray_icons", pMonitor.lock()))
        return TRAY_LABEL / 2.F;
    const float ICONHALF = ScrollOverview::Config::getValue<int>("plugin:gridoverview:special_tray_icon_size", pMonitor.lock()) * TRAY_ICON_PLATE / 2.F;
    return std::max(TRAY_LABEL / 2.F, ICONHALF + 6.F + TRAY_PILL / 2.F);
}

// logical px of the band under every card (name, and the lower half of the icons)
float CScrollOverview::specialTrayLabelBand() const {
    return std::max(TRAY_LABEL, specialTrayLabelOffset() + TRAY_PILL / 2.F + 4.F);
}

std::vector<CScrollOverview::STrayCard> CScrollOverview::specialTrayCards() const {
    std::vector<STrayCard> cards;
    const auto             MONITOR = pMonitor.lock();
    if (!MONITOR || !ScrollOverview::Config::getValue<bool>("plugin:gridoverview:special_tray", MONITOR))
        return cards;

    loadTrayRoster(MONITOR);
    for (const auto& NAME : g_trayRoster) {
        if (NAME.empty() || std::ranges::any_of(cards, [&NAME](const auto& card) { return card.name == NAME; }))
            continue;
        cards.push_back({.name = NAME});
    }

    std::vector<STrayCard> others;
    for (const auto& w : State::workspaceState()->workspaces()) {
        const auto WORKSPACE = w.lock();
        if (!valid(WORKSPACE) || WORKSPACE->type() != Workspace::eWorkspaceType::SPECIAL)
            continue;

        const auto NAME = specialTrayName(WORKSPACE->addressableName());
        const bool HASWINDOWS = std::ranges::any_of(Desktop::windowState()->windows(), [&WORKSPACE](const auto& window) {
            return window && window->m_workspace == WORKSPACE && shouldShowOverviewWindow(window);
        });
        // specials with windows that are not on the roster still appear (keybind-created etc.)
        if (!HASWINDOWS && std::ranges::find(g_trayRoster, NAME) == g_trayRoster.end())
            continue;
        const auto IT = std::ranges::find_if(cards, [&NAME](const auto& card) { return card.name == NAME; });
        if (IT != cards.end())
            IT->workspace = WORKSPACE;
        else if (HASWINDOWS)
            others.push_back({.name = NAME, .workspace = WORKSPACE});
    }
    std::ranges::sort(others, {}, &STrayCard::name);
    cards.insert(cards.end(), others.begin(), others.end());
    if (cards.empty())
        return cards;

    applyTrayOrder(cards);

    const auto  SIZE   = MONITOR->m_size;
    const float ASPECT = sc<float>(SIZE.y / std::max(SIZE.x, 1.0));
    const float N      = sc<float>(cards.size());
    float       width  = sc<float>(ScrollOverview::Config::getValue<int>("plugin:gridoverview:special_tray_width", MONITOR));
    const float BAND   = specialTrayLabelBand();

    if (specialTrayOnBottom()) {
        // one row at the bottom edge, horizontally centred; shrink when they do not fit
        const float AVAILW    = sc<float>(SIZE.x) - 2.F * TRAY_MARGIN;
        const float EXTRA     = (N - 1.F) * TRAY_GAP + TRAY_GAP + TRAY_ADD_HEIGHT; // gaps between cards + "+" after
        const auto  widthFor  = [&](float w) { return N * w + EXTRA; };
        if (widthFor(width) > AVAILW)
            width = std::max(20.F, (AVAILW - EXTRA) / N);

        const float HEIGHT = width * ASPECT;
        const float ROW    = HEIGHT + BAND;
        // keep a full TRAY_MARGIN below the dock (dock pad sits inside the tray, not in the screen margin)
        const float EDGE   = TRAY_MARGIN + TRAY_DOCK_PAD;
        const float HIDDEN = specialTrayAutohide() && trayReveal ? (1.F - trayReveal->value()) * (ROW + EDGE + TRAY_MARGIN) : 0.F;
        const float Y      = sc<float>(SIZE.y) - EDGE - ROW + HIDDEN;
        float       x      = TRAY_MARGIN + std::max(0.F, (AVAILW - widthFor(width)) / 2.F);
        for (auto& card : cards) {
            card.box = CBox{x, Y, width, HEIGHT}.scale(MONITOR->m_scale).round();
            x += width + TRAY_GAP;
        }
        return cards;
    }

    // one column at the left/right edge, vertically centred in the work area
    const float TOP    = gridWorkAreaTop(MONITOR) + TRAY_MARGIN;
    const float AVAILH = sc<float>(SIZE.y) - TOP - TRAY_MARGIN;
    const float EXTRA  = N * BAND + N * TRAY_GAP + TRAY_ADD_HEIGHT;
    const auto  heightFor = [&](float w) { return N * w * ASPECT + EXTRA; };
    if (heightFor(width) > AVAILH)
        width = std::max(20.F, (AVAILH - EXTRA) / N / ASPECT);

    const float HEIGHT = width * ASPECT;
    float       y      = TOP + std::max(0.F, (AVAILH - heightFor(width)) / 2.F);
    const float HIDDEN = specialTrayAutohide() && trayReveal ? (1.F - trayReveal->value()) * (width + 2.F * TRAY_MARGIN) : 0.F;
    const float X      = specialTrayOnLeft() ? TRAY_MARGIN - HIDDEN : sc<float>(SIZE.x) - TRAY_MARGIN - width + HIDDEN;
    for (auto& card : cards) {
        card.box = CBox{X, y, width, HEIGHT}.scale(MONITOR->m_scale).round();
        y += HEIGHT + BAND + TRAY_GAP;
    }

    return cards;
}

bool CScrollOverview::specialTrayAutohide() const {
    return ScrollOverview::Config::getValue<bool>("plugin:gridoverview:special_tray_autohide", pMonitor.lock());
}

// a dragged window approaching the tray shrinks (around the grab point) from its grid size to the size it has in
// a card, reaching it at the cards' inner edge (toward the grid)
float CScrollOverview::specialTrayDragScale(const Vector2D& cursorGlobal, float gridScale) const {
    static constexpr float RANGE = 300.F; // logical px on the grid side of the cards
    const auto             MONITOR = pMonitor.lock();
    if (!MONITOR || closing)
        return 1.F;

    const auto CARDS = specialTrayCards();
    const auto ADD   = specialTrayAddBox(CARDS);
    if (ADD.empty())
        return 1.F;

    const double LOCAL_X = (cursorGlobal.x - MONITOR->m_position.x) * MONITOR->m_scale;
    const double LOCAL_Y = (cursorGlobal.y - MONITOR->m_position.y) * MONITOR->m_scale;
    float        progress = 0.F;

    if (specialTrayOnBottom()) {
        const double CARD_T = CARDS.empty() ? ADD.y : CARDS.front().box.y;
        const double INNER  = CARD_T; // top of the row faces the grid
        const double START  = INNER - RANGE * MONITOR->m_scale;
        progress            = std::clamp(sc<float>((LOCAL_Y - START) / (INNER - START)), 0.F, 1.F);
    } else {
        const bool   LEFT   = specialTrayOnLeft();
        const double CARD_L = CARDS.empty() ? ADD.x : CARDS.front().box.x;
        const double CARD_R = CARDS.empty() ? ADD.x + ADD.w : CARDS.front().box.x + CARDS.front().box.w;
        const double INNER  = LEFT ? CARD_R : CARD_L;
        const double START  = LEFT ? INNER + RANGE * MONITOR->m_scale : INNER - RANGE * MONITOR->m_scale;
        progress =
            LEFT ? std::clamp(sc<float>((START - LOCAL_X) / (START - INNER)), 0.F, 1.F) : std::clamp(sc<float>((LOCAL_X - START) / (INNER - START)), 0.F, 1.F);
    }
    progress = progress * progress * (3.F - 2.F * progress);
    if (specialTrayAutohide() && trayReveal)
        progress *= trayReveal->value();

    const float CARDSCALE = (CARDS.empty() ? sc<float>(ADD.w) / sc<float>(std::max(MONITOR->m_size.x, 1.0) * MONITOR->m_scale)
                                           : specialTrayRenderScale(CARDS.front())) /
                            std::max(gridScale, 0.01F);
    return 1.F + (CARDSCALE - 1.F) * progress;
}

float CScrollOverview::specialTrayPerspectiveDepth() const {
    const auto MONITOR = pMonitor.lock();
    if (!MONITOR || !scale || !ScrollOverview::Config::getValue<bool>("plugin:gridoverview:special_tray_perspective", MONITOR))
        return 0.F;
    const float PROGRESS = std::clamp((1.F - scale->value()) / (1.F - ScrollOverview::Config::getScale(MONITOR)), 0.F, 1.F);
    return TRAY_PERSPECTIVE_SHRINK * PROGRESS;
}

// Two geometries per card:
//   card.box              — Render/FB rect: flat live window contents (and perspective strip source)
//   specialTrayVisualBox  — UI silhouette after depth foreshortening: dock, +, empty outline, hits, pills, icons
// Flat mode (perspective off / depth≈0): visual == card.box.
CBox CScrollOverview::specialTrayVisualBox(const CBox& cardBox) const {
    const float DEPTH = specialTrayPerspectiveDepth();
    if (DEPTH <= 0.001F || cardBox.empty())
        return cardBox;

    if (specialTrayOnBottom()) {
        const float PROJH = sc<float>(cardBox.h) * (1.F - DEPTH);
        return CBox{cardBox.x, cardBox.y + cardBox.h - PROJH, cardBox.w, PROJH}.round();
    }
    const float PROJW = sc<float>(cardBox.w) * (1.F - DEPTH);
    if (specialTrayOnLeft())
        return CBox{cardBox.x, cardBox.y, PROJW, cardBox.h}.round(); // near = left (outer)
    return CBox{cardBox.x + cardBox.w - PROJW, cardBox.y, PROJW, cardBox.h}.round(); // near = right (outer)
}

float CScrollOverview::specialTrayShownWidth() const {
    if (specialTrayOnBottom() || (specialTrayAutohide() && trayReveal && trayReveal->goal() < 0.5F))
        return 0.F;
    const auto MONITOR = pMonitor.lock();
    if (!MONITOR || !ScrollOverview::Config::getValue<bool>("plugin:gridoverview:special_tray", MONITOR))
        return 0.F;
    const auto CARDS = specialTrayCards();
    if (!CARDS.empty()) {
        const auto VIS = specialTrayVisualBox(CARDS.front().box);
        return sc<float>(VIS.w) / std::max(MONITOR->m_scale, 0.01F) + TRAY_MARGIN;
    }
    const float WIDTH = sc<float>(ScrollOverview::Config::getValue<int>("plugin:gridoverview:special_tray_width", MONITOR));
    return WIDTH * (1.F - specialTrayPerspectiveDepth()) + TRAY_MARGIN;
}

float CScrollOverview::specialTrayShownHeight() const {
    if (!specialTrayOnBottom() || (specialTrayAutohide() && trayReveal && trayReveal->goal() < 0.5F))
        return 0.F;
    const auto MONITOR = pMonitor.lock();
    if (!MONITOR || !ScrollOverview::Config::getValue<bool>("plugin:gridoverview:special_tray", MONITOR))
        return 0.F;
    const auto CARDS = specialTrayCards();
    if (!CARDS.empty()) {
        const float SCALE = std::max(MONITOR->m_scale, 0.01F);
        const auto  VIS   = specialTrayVisualBox(CARDS.front().box);
        return sc<float>(VIS.h) / SCALE + specialTrayLabelBand() + TRAY_MARGIN + TRAY_DOCK_PAD;
    }
    const float WIDTH = sc<float>(ScrollOverview::Config::getValue<int>("plugin:gridoverview:special_tray_width", MONITOR));
    const float HEIGHT = WIDTH * sc<float>(MONITOR->m_size.y / std::max(MONITOR->m_size.x, 1.0)) * (1.F - specialTrayPerspectiveDepth());
    return HEIGHT + specialTrayLabelBand() + TRAY_MARGIN + TRAY_DOCK_PAD;
}

// a hidden (autohide) tray lies over the grid when shown instead of keeping space free
float CScrollOverview::specialTrayReserveX() const {
    return specialTrayAutohide() || specialTrayOnBottom() ? 0.F : specialTrayShownWidth();
}

float CScrollOverview::specialTrayReserveY() const {
    return specialTrayAutohide() || specialTrayOnSide() ? 0.F : specialTrayShownHeight();
}

float CScrollOverview::specialTrayReserve() const {
    return specialTrayReserveX();
}

// autohide: shown when the pointer touches the tray's screen edge, hidden once it leaves the tray. While a window
// is dragged it comes up as soon as the pointer enters the edge scrolling zone; pushing on to the very edge scrolls
// further (updateDragEdgeScroll)
void CScrollOverview::updateSpecialTrayReveal(const Vector2D& pointLocal) {
    const auto MONITOR = pMonitor.lock();
    if (!MONITOR || !trayReveal || closing || !specialTrayAutohide())
        return;
    if (!ScrollOverview::Config::getValue<bool>("plugin:gridoverview:special_tray", MONITOR))
        return;

    // rename field lives on the tray: keep it revealed until the prompt is done
    if (specialTrayRenameBusy) {
        if (trayReveal->goal() < 0.5F)
            *trayReveal = 1.F;
        return;
    }

    const auto CARDS = specialTrayCards();
    const auto ADD   = specialTrayAddBox(CARDS);
    if (ADD.empty())
        return;

    const auto   SIZE     = MONITOR->m_size * MONITOR->m_scale;
    const bool   DRAGGING = dragActiveWindow || (g_pointerGrabOverview && g_pointerGrabOverview->dragActiveWindow);
    const double SCROLL   = std::min(SIZE.x, SIZE.y) * 0.04;
    const bool   SHOWN    = trayReveal->goal() > 0.5F;
    bool         SHOW     = false;
    if (specialTrayOnBottom()) {
        const double BAND = (CARDS.empty() ? ADD.h : CARDS.front().box.h + specialTrayLabelBand() * MONITOR->m_scale) +
            2.0 * TRAY_MARGIN * MONITOR->m_scale;
        const double KEEP = SIZE.y - BAND;
        const double EDGE = DRAGGING ? SIZE.y - SCROLL : SIZE.y - 2.0 * MONITOR->m_scale;
        SHOW              = isOverviewPointerOnMonitor(MONITOR) && (pointLocal.y >= EDGE || (SHOWN && pointLocal.y >= KEEP));
    } else if (specialTrayOnLeft()) {
        const double BAND = ADD.w + 2.0 * TRAY_MARGIN * MONITOR->m_scale;
        const double KEEP = BAND;
        const double EDGE = DRAGGING ? SCROLL : 2.0 * MONITOR->m_scale;
        SHOW              = isOverviewPointerOnMonitor(MONITOR) && (pointLocal.x <= EDGE || (SHOWN && pointLocal.x <= KEEP));
    } else {
        const double BAND = ADD.w + 2.0 * TRAY_MARGIN * MONITOR->m_scale;
        const double KEEP = SIZE.x - BAND;
        const double EDGE = DRAGGING ? SIZE.x - SCROLL : SIZE.x - 2.0 * MONITOR->m_scale;
        SHOW              = isOverviewPointerOnMonitor(MONITOR) && (pointLocal.x >= EDGE || (SHOWN && pointLocal.x >= KEEP));
    }
    if (SHOW != SHOWN)
        *trayReveal = SHOW ? 1.F : 0.F;
}

// the whole band around the cards (gaps and labels included): nothing under it reacts while the tray is shown
// render px; empty when there is no tray or it is hidden (autohide)
CBox CScrollOverview::specialTrayBandBox() const {
    const auto MONITOR = pMonitor.lock();
    if (!MONITOR || closing || (specialTrayAutohide() && trayReveal && trayReveal->goal() < 0.5F))
        return {};

    const auto CARDS = specialTrayCards();
    const auto ADD   = specialTrayAddBox(CARDS);
    if (ADD.empty())
        return {};

    const double MARGIN = TRAY_MARGIN * MONITOR->m_scale;
    const double SCREENW = MONITOR->m_size.x * MONITOR->m_scale;
    const double SCREENH = MONITOR->m_size.y * MONITOR->m_scale;
    if (specialTrayOnBottom()) {
        // flat card top (not visual.y) — same rule as the dock; foreshortening must not shrink the band
        const double TOP = (CARDS.empty() ? ADD.y : CARDS.front().box.y) - MARGIN;
        return CBox{0, TOP, SCREENW, SCREENH - TOP};
    }
    const auto   FRONT  = CARDS.empty() ? ADD : specialTrayVisualBox(CARDS.front().box);
    const double TOP    = (CARDS.empty() ? ADD.y : CARDS.front().box.y) - MARGIN;
    const double HEIGHT = ADD.y + ADD.h + MARGIN - TOP;
    if (specialTrayOnLeft())
        return CBox{0, TOP, std::max(FRONT.x + FRONT.w, ADD.x + ADD.w) + MARGIN, HEIGHT};
    const double LEFT = std::min(FRONT.x, ADD.x) - MARGIN;
    return CBox{LEFT, TOP, SCREENW - LEFT, HEIGHT};
}

bool CScrollOverview::specialTrayBandAt(const Vector2D& pointLocal) const {
    const auto BAND = specialTrayBandBox();
    return !BAND.empty() && BAND.containsPoint(pointLocal);
}

// the "+" box after the last card (below on side trays, to the right on the bottom tray), or alone when empty
CBox CScrollOverview::specialTrayAddBox(const std::vector<STrayCard>& cards) const {
    const auto MONITOR = pMonitor.lock();
    if (!MONITOR || !ScrollOverview::Config::getValue<bool>("plugin:gridoverview:special_tray", MONITOR))
        return {};

    if (!cards.empty()) {
        const auto& LAST = cards.back().box;
        const auto  VIS  = specialTrayVisualBox(LAST); // hug foreshortened card, not the flat render box
        if (specialTrayOnBottom()) {
            // vertical pill: same thickness as the side-tray + bar, height capped to the card (not card-wide —
            // that overshoots the bottom tray)
            const double ADD_W = TRAY_ADD_HEIGHT * MONITOR->m_scale;
            return CBox{VIS.x + VIS.w + TRAY_GAP * MONITOR->m_scale, VIS.y, ADD_W, VIS.h}.round();
        }
        return CBox{VIS.x, LAST.y + LAST.h + (specialTrayLabelBand() + TRAY_GAP) * MONITOR->m_scale, VIS.w, TRAY_ADD_HEIGHT * MONITOR->m_scale}.round();
    }

    const auto  SIZE  = MONITOR->m_size;
    float       width = sc<float>(ScrollOverview::Config::getValue<int>("plugin:gridoverview:special_tray_width", MONITOR));
    if (specialTrayOnBottom()) {
        const float ASPECT = sc<float>(SIZE.y / std::max(SIZE.x, 1.0));
        const float HEIGHT = width * ASPECT;
        const float ROW    = HEIGHT + specialTrayLabelBand();
        const float EDGE   = TRAY_MARGIN + TRAY_DOCK_PAD;
        const float HIDDEN = specialTrayAutohide() && trayReveal ? (1.F - trayReveal->value()) * (ROW + EDGE + TRAY_MARGIN) : 0.F;
        const float ADD_W  = TRAY_ADD_HEIGHT;
        const float X      = (sc<float>(SIZE.x) - ADD_W) / 2.F;
        const float Y      = sc<float>(SIZE.y) - EDGE - ROW + HIDDEN;
        return CBox{X, Y, ADD_W, HEIGHT}.scale(MONITOR->m_scale).round();
    }

    // no cards: a lone "+" in the tray column, vertically centred
    const float TOP    = gridWorkAreaTop(MONITOR) + TRAY_MARGIN;
    const float AVAILH = sc<float>(SIZE.y) - TOP - TRAY_MARGIN;
    const float HIDDEN = specialTrayAutohide() && trayReveal ? (1.F - trayReveal->value()) * (width + 2.F * TRAY_MARGIN) : 0.F;
    const float X      = specialTrayOnLeft() ? TRAY_MARGIN - HIDDEN : sc<float>(SIZE.x) - TRAY_MARGIN - width + HIDDEN;
    const float Y      = TOP + std::max(0.F, (AVAILH - TRAY_ADD_HEIGHT) / 2.F);
    return CBox{X, Y, width, TRAY_ADD_HEIGHT}.scale(MONITOR->m_scale).round();
}

bool CScrollOverview::specialTrayAddAt(const Vector2D& pointLocal) const {
    if (closing || (specialTrayAutohide() && trayReveal && trayReveal->goal() < 0.5F))
        return false;
    const auto ADD = specialTrayAddBox(specialTrayCards());
    return !ADD.empty() && ADD.containsPoint(pointLocal);
}

// a free name for a new scratchpad. Default / "+": anonymous scratch / scratch-N. Drop with special_tray_auto_name:
// category (desktop Categories=, else app) or app; "-2", "-3", ... when taken
std::string CScrollOverview::newSpecialWorkspaceName(const PHLWINDOW& window) const {
    const auto MODE = ScrollOverview::Config::getValue<std::string>("plugin:gridoverview:special_tray_auto_name", pMonitor.lock());
    std::string base = "scratch";
    if (window && MODE != "off" && !MODE.empty()) {
        if (MODE == "category") {
            base = GridIcons::categoryForAppID(window->metadata().appID());
            if (base.empty())
                base = sanitizeTrayName(window->metadata().appID().substr(window->metadata().appID().find_last_of('.') == std::string::npos
                                                                              ? 0
                                                                              : window->metadata().appID().find_last_of('.') + 1));
        } else if (MODE == "app") {
            auto app = window->metadata().appID();
            if (const auto DOT = app.find_last_of('.'); DOT != std::string::npos && DOT + 1 < app.size())
                app = app.substr(DOT + 1);
            base = sanitizeTrayName(app);
        }
        if (base.empty())
            base = "scratch";
    }

    std::vector<std::string> taken;
    loadTrayRoster(pMonitor.lock());
    taken.insert(taken.end(), g_trayRoster.begin(), g_trayRoster.end());
    for (const auto& card : specialTrayCards())
        taken.push_back(card.name);
    for (const auto& w : State::workspaceState()->workspaces()) {
        if (const auto WORKSPACE = w.lock(); valid(WORKSPACE) && WORKSPACE->type() == Workspace::eWorkspaceType::SPECIAL)
            taken.push_back(specialTrayName(WORKSPACE->addressableName()));
    }

    for (int i = 1;; ++i) {
        const auto NAME = i == 1 ? base : base + "-" + std::to_string(i);
        if (std::ranges::find(taken, NAME) == taken.end())
            return NAME;
    }
}

std::optional<size_t> CScrollOverview::specialTrayCardAt(const Vector2D& pointLocal) const {
    if (closing || (specialTrayAutohide() && trayReveal && trayReveal->goal() < 0.5F))
        return std::nullopt;

    const auto CARDS = specialTrayCards();
    for (size_t i = 0; i < CARDS.size(); ++i) {
        // visual box: with perspective the foreshortened strip; flat mode == card.box
        if (specialTrayVisualBox(CARDS[i].box).containsPoint(pointLocal))
            return i;
    }
    return std::nullopt;
}

// name pill under the card: name ··· pencil + grip (larger gap after the name)
CScrollOverview::STrayPill CScrollOverview::specialTrayPill(const STrayCard& card, const Vector2D& textSize) const {
    const auto MONITOR = pMonitor.lock();
    STrayPill  out;
    if (!MONITOR)
        return out;

    const bool  NAMED      = !isAnonymousTrayName(card.name) && textSize.x > 0;
    const float UNIT       = MONITOR->m_scale;
    const float PAD        = 6.F * UNIT;
    const float GRIP       = TRAY_GRIP_WIDTH * UNIT;
    const float PENCIL     = TRAY_PENCIL_WIDTH * UNIT;
    const float ACTION_GAP = TRAY_ACTION_GAP * UNIT;
    const float ICON_GAP   = TRAY_ICON_GAP * UNIT;
    const float TEXTH      = NAMED ? sc<float>(textSize.y) : 0.F;
    const float TEXTW      = NAMED ? sc<float>(textSize.x) : 0.F;
    const float HEIGHT     = std::max(TRAY_PILL * UNIT, TEXTH + 2.F * PAD);
    const float ACTIONS_W  = PENCIL + ICON_GAP + GRIP;
    const float WIDTH      = PAD + (NAMED ? TEXTW + ACTION_GAP : 0.F) + ACTIONS_W + PAD;
    const auto  VIS        = specialTrayVisualBox(card.box);
    const float CENTERX    = VIS.middle().x;
    const float CENTERY    = card.box.y + card.box.h + specialTrayLabelOffset() * UNIT;
    out.pill               = CBox{CENTERX - WIDTH / 2.F, CENTERY - HEIGHT / 2.F, WIDTH, HEIGHT}.round();
    float x                = out.pill.x + PAD;
    if (NAMED) {
        out.text = CBox{x, CENTERY - TEXTH / 2.F, TEXTW, TEXTH}.round();
        x += TEXTW + ACTION_GAP;
    }
    out.pencil = CBox{x, out.pill.y, PENCIL + PAD * 0.5F, out.pill.h}.round();
    x += PENCIL + ICON_GAP;
    out.grip = CBox{x - PAD * 0.25F, out.pill.y, GRIP + PAD * 0.5F, out.pill.h}.round();
    return out;
}

// × centred on the card's top-right corner (half inside the card, half outside).
// Always top-right — also on the left tray (users expect × there; outer-edge mirroring is the exception).
CBox CScrollOverview::specialTrayCloseBox(const STrayCard& card) const {
    const auto MONITOR = pMonitor.lock();
    if (!MONITOR)
        return {};
    const float SIZE = TRAY_CLOSE_SIZE * MONITOR->m_scale;
    const auto  VIS  = specialTrayVisualBox(card.box);
    return CBox{VIS.x + VIS.w - SIZE / 2.F, VIS.y - SIZE / 2.F, SIZE, SIZE}.round();
}

std::optional<size_t> CScrollOverview::specialTrayGripAt(const Vector2D& pointLocal) const {
    if (closing || specialTrayRenameBusy || (specialTrayAutohide() && trayReveal && trayReveal->goal() < 0.5F))
        return std::nullopt;

    const auto MONITOR = pMonitor.lock();
    if (!MONITOR)
        return std::nullopt;

    const int  TEXTSIZE = std::max(1, sc<int>(std::round(TRAY_LABEL_SIZE * MONITOR->m_scale)));
    const auto CARDS    = specialTrayCards();
    for (size_t i = 0; i < CARDS.size(); ++i) {
        const auto KEY     = CARDS[i].name + "\n" + std::to_string(TEXTSIZE) + "\ntray";
        const auto TEXTURE = contextLabelTextures.contains(KEY) ? contextLabelTextures.at(KEY) : SP<Render::ITexture>{};
        const auto SIZE    = !isAnonymousTrayName(CARDS[i].name) && TEXTURE && TEXTURE->m_size.x > 0
                                  ? TEXTURE->m_size
                                  : Vector2D{isAnonymousTrayName(CARDS[i].name) ? 0.0 : TRAY_LABEL_SIZE * 4.0 * MONITOR->m_scale, TRAY_LABEL_SIZE * MONITOR->m_scale};
        if (specialTrayPill(CARDS[i], SIZE).grip.containsPoint(pointLocal))
            return i;
    }
    return std::nullopt;
}

std::optional<size_t> CScrollOverview::specialTrayShowingClose() const {
    if (closing || specialTrayRenameBusy || dragActiveWindow || specialTrayReorderActive)
        return std::nullopt;
    if (specialTrayAutohide() && trayReveal && trayReveal->goal() < 0.5F)
        return std::nullopt;

    const auto MONITOR = pMonitor.lock();
    if (!MONITOR || !isOverviewPointerOnMonitor(MONITOR))
        return std::nullopt;

    const auto CARDS = specialTrayCards();
    if (CARDS.empty())
        return std::nullopt;

    const int TEXTSIZE = std::max(1, sc<int>(std::round(TRAY_LABEL_SIZE * MONITOR->m_scale)));

    // protruding × under the pointer wins
    for (size_t i = 0; i < CARDS.size(); ++i) {
        if (specialTrayCloseBox(CARDS[i]).containsPoint(lastMousePosLocal))
            return i;
    }

    if (const auto CARD = specialTrayCardAt(lastMousePosLocal))
        return CARD;

    // name pill counts as hover on that card (pencil / label / grip)
    for (size_t i = 0; i < CARDS.size(); ++i) {
        const auto KEY     = CARDS[i].name + "\n" + std::to_string(TEXTSIZE) + "\ntray";
        const auto TEXTURE = contextLabelTextures.contains(KEY) ? contextLabelTextures.at(KEY) : SP<Render::ITexture>{};
        const auto SIZE    = !isAnonymousTrayName(CARDS[i].name) && TEXTURE && TEXTURE->m_size.x > 0
                                  ? TEXTURE->m_size
                                  : Vector2D{isAnonymousTrayName(CARDS[i].name) ? 0.0 : TRAY_LABEL_SIZE * 4.0 * MONITOR->m_scale, TRAY_LABEL_SIZE * MONITOR->m_scale};
        if (specialTrayPill(CARDS[i], SIZE).pill.containsPoint(lastMousePosLocal))
            return i;
    }

    return std::nullopt;
}

std::optional<size_t> CScrollOverview::specialTrayCloseAt(const Vector2D& pointLocal) const {
    if (closing || specialTrayRenameBusy || (specialTrayAutohide() && trayReveal && trayReveal->goal() < 0.5F))
        return std::nullopt;

    const auto SHOWING = specialTrayShowingClose();
    if (!SHOWING)
        return std::nullopt;

    auto card = specialTrayCards()[*SHOWING];
    if (specialTrayReorderActive && specialTrayReorderFrom == *SHOWING)
        card.box.y += lastMousePosLocal.y - dragStartMouseLocal.y;
    if (specialTrayCloseBox(card).containsPoint(pointLocal))
        return *SHOWING;

    return std::nullopt;
}

void CScrollOverview::removeSpecialTrayCard(size_t index) {
    const auto CARDS = specialTrayCards();
    if (index >= CARDS.size())
        return;

    const auto CARD = CARDS[index];
    for (const auto& window : specialTrayWindows(CARD)) {
        if (window)
            window->sendClose();
    }

    if (const auto MONITOR = pMonitor.lock(); MONITOR && CARD.workspace && MONITOR->m_activeSpecialWorkspace == CARD.workspace)
        MONITOR->setSpecialWorkspace(nullptr);

    removeTrayRosterName(CARD.name);
    rebuildPending = true;
    damage();
}

std::optional<size_t> CScrollOverview::specialTrayPencilAt(const Vector2D& pointLocal) const {
    if (closing || specialTrayRenameBusy || (specialTrayAutohide() && trayReveal && trayReveal->goal() < 0.5F))
        return std::nullopt;

    const auto MONITOR = pMonitor.lock();
    if (!MONITOR)
        return std::nullopt;

    const int  TEXTSIZE = std::max(1, sc<int>(std::round(TRAY_LABEL_SIZE * MONITOR->m_scale)));
    const auto CARDS    = specialTrayCards();
    for (size_t i = 0; i < CARDS.size(); ++i) {
        const auto KEY     = CARDS[i].name + "\n" + std::to_string(TEXTSIZE) + "\ntray";
        const auto TEXTURE = contextLabelTextures.contains(KEY) ? contextLabelTextures.at(KEY) : SP<Render::ITexture>{};
        const auto SIZE    = !isAnonymousTrayName(CARDS[i].name) && TEXTURE && TEXTURE->m_size.x > 0
                                  ? TEXTURE->m_size
                                  : Vector2D{isAnonymousTrayName(CARDS[i].name) ? 0.0 : TRAY_LABEL_SIZE * 4.0 * MONITOR->m_scale, TRAY_LABEL_SIZE * MONITOR->m_scale};
        if (specialTrayPill(CARDS[i], SIZE).pencil.containsPoint(pointLocal))
            return i;
    }
    return std::nullopt;
}

static bool applySpecialTrayRename(const std::string& oldName, const std::string& rawNew) {
    const auto NEW = sanitizeTrayName(rawNew);
    if (NEW.empty() || NEW == oldName)
        return false;

    loadTrayRoster({});
    if (std::ranges::find(g_trayRoster, NEW) != g_trayRoster.end())
        return false;
    for (const auto& w : State::workspaceState()->workspaces()) {
        if (const auto WORKSPACE = w.lock(); valid(WORKSPACE) && WORKSPACE->type() == Workspace::eWorkspaceType::SPECIAL &&
            specialTrayName(WORKSPACE->addressableName()) == NEW)
            return false;
    }

    PHLWORKSPACE oldWs;
    for (const auto& w : State::workspaceState()->workspaces()) {
        if (const auto WS = w.lock(); valid(WS) && WS->type() == Workspace::eWorkspaceType::SPECIAL && specialTrayName(WS->addressableName()) == oldName) {
            oldWs = WS;
            break;
        }
    }

    // Hyprland's CHLWorkspace::rename is a no-op for SPECIAL. Migrate windows onto a new special instead;
    // empty / roster-only cards only need the roster name updated (new special is created on first use).
    if (oldWs) {
        std::vector<PHLWINDOW> windows;
        for (const auto& w : Desktop::windowState()->windows()) {
            if (w && w->m_workspace == oldWs)
                windows.push_back(w);
        }

        if (!windows.empty()) {
            const auto MONITOR = oldWs->m_monitor ? oldWs->m_monitor.lock() : Desktop::focusState()->monitor();
            if (!MONITOR)
                return false;

            const auto ADDRESS = std::string{SPECIAL_PREFIX} + NEW;
            PHLWORKSPACE newWs;
            for (const auto& w : State::workspaceState()->workspaces()) {
                if (const auto WS = w.lock(); valid(WS) && WS->type() == Workspace::eWorkspaceType::SPECIAL && WS->addressableName() == ADDRESS) {
                    newWs = WS;
                    break;
                }
            }
            if (!newWs)
                newWs = State::workspaceState()->createSpecial(ADDRESS, MONITOR);
            if (!newWs)
                return false;

            for (const auto& window : windows)
                Desktop::globalWindowController()->moveWindowToWorkspace(window, newWs);

            for (const auto& mon : State::monitorState()->monitors()) {
                if (mon && mon->m_activeSpecialWorkspace == oldWs)
                    mon->setSpecialWorkspace(newWs);
            }
        }
    }

    if (const auto IT = std::ranges::find(g_trayRoster, oldName); IT != g_trayRoster.end())
        *IT = NEW;
    else
        g_trayRoster.push_back(NEW);
    saveTrayRoster();

    loadTrayOrder();
    if (const auto IT = std::ranges::find(g_trayOrder, oldName); IT != g_trayOrder.end())
        *IT = NEW;
    else if (std::ranges::find(g_trayOrder, NEW) == g_trayOrder.end())
        g_trayOrder.push_back(NEW);
    saveTrayOrder();
    return true;
}

void cancelSpecialTrayRenameIfAny(CScrollOverview* overview) {
    if (overview)
        overview->cancelSpecialTrayRename();
}

void CScrollOverview::cancelSpecialTrayRename() {
    specialTrayRenameBusy = false;
    renameContextIndex    = 0;
    specialTrayRenameOld.clear();
    specialTrayRenameText.clear();
}

void CScrollOverview::beginSpecialTrayRename(size_t index) {
    if (specialTrayRenameBusy || closing)
        return;
    const auto CARDS = specialTrayCards();
    if (index >= CARDS.size())
        return;

    renameContextIndex    = 0;
    specialTrayRenameOld  = CARDS[index].name;
    // anonymous cards start empty; named ones keep the current name for editing
    specialTrayRenameText = isAnonymousTrayName(CARDS[index].name) ? "" : CARDS[index].name;
    specialTrayRenameBusy = true;
    if (specialTrayAutohide() && trayReveal && trayReveal->goal() < 0.5F)
        *trayReveal = 1.F;
    damage();
}

void CScrollOverview::beginContextRename(int contextIndex) {
    if (specialTrayRenameBusy || closing || contextIndex < 1)
        return;
    const auto NAMES = ScrollOverview::Config::getContextNames(pMonitor.lock());
    renameContextIndex    = contextIndex;
    specialTrayRenameOld  = sc<size_t>(contextIndex) <= NAMES.size() ? NAMES[sc<size_t>(contextIndex - 1)] : "";
    specialTrayRenameText = specialTrayRenameOld;
    specialTrayRenameBusy = true;
    damage();
}

void CScrollOverview::finishSpecialTrayRename() {
    if (!specialTrayRenameBusy)
        return;
    const auto OLD  = specialTrayRenameOld;
    const auto TEXT = specialTrayRenameText;
    const int  CTX  = renameContextIndex;
    cancelSpecialTrayRename();
    if (CTX > 0) {
        if (!TEXT.empty())
            (void)ScrollOverview::Config::renameContext(CTX, TEXT);
        damage();
        return;
    }
    if (!TEXT.empty()) {
        renameSpecialTrayCard(OLD, TEXT);
        return;
    }
    // empty field: put named cards back into the anonymous scratch / scratch-N series
    if (!isAnonymousTrayName(OLD))
        renameSpecialTrayCard(OLD, newSpecialWorkspaceName(nullptr));
    else
        damage();
}

bool CScrollOverview::handleSpecialTrayRenameKey(xkb_keysym_t keysym) {
    if (!specialTrayRenameBusy || closing)
        return false;

    if (keysym == XKB_KEY_Escape) {
        cancelSpecialTrayRename();
        damage();
        return true;
    }
    if (keysym == XKB_KEY_Return || keysym == XKB_KEY_KP_Enter) {
        finishSpecialTrayRename();
        return true;
    }
    if (keysym == XKB_KEY_BackSpace) {
        if (!specialTrayRenameText.empty()) {
            // erase one UTF-8 codepoint from the end
            do {
                specialTrayRenameText.pop_back();
            } while (!specialTrayRenameText.empty() && (sc<unsigned char>(specialTrayRenameText.back()) & 0xC0) == 0x80);
            damage();
        }
        return true;
    }

    char utf8[8] = {};
    const int N  = xkb_keysym_to_utf8(keysym, utf8, sizeof(utf8));
    // N includes the trailing NUL; tray: A–Z/a–z/0–9/_/- ; context rename also allows spaces
    if (N > 1) {
        for (int i = 0; i < N - 1; ++i) {
            const char C = utf8[i];
            const bool OK = std::isalnum(sc<unsigned char>(C)) || C == '_' || C == '-' || (renameContextIndex > 0 && C == ' ');
            if (OK && specialTrayRenameText.size() < 48)
                specialTrayRenameText.push_back(C);
        }
        damage();
        return true;
    }
    return true; // swallow other keys while the field is open
}

void CScrollOverview::renderSpecialTrayRenameOverlay(PHLMONITOR monitor, float alpha) {
    if (!specialTrayRenameBusy || !monitor || alpha <= 0.001F || !g_pHyprRenderer)
        return;

    const auto  SIZE   = monitor->m_size * monitor->m_scale;
    const float UNIT   = monitor->m_scale;
    const float BOX_W  = 420.F * UNIT;
    const float BOX_H  = 64.F * UNIT;
    const float BOX_X  = (SIZE.x - BOX_W) / 2.F;
    const float BOX_Y  = (SIZE.y - BOX_H) / 2.F;
    const auto  FIELD  = CBox{BOX_X, BOX_Y, BOX_W, BOX_H}.round();

    // dim the overview a bit so the field reads as a modal
    CRectPassElement::SRectData dim;
    dim.box   = CBox{0, 0, SIZE.x, SIZE.y};
    dim.color = CHyprColor{0.F, 0.F, 0.F, 0.35F * alpha};
    Render::IHyprRenderer::currentPass(g_pHyprRenderer->context()).add(makeUnique<CRectPassElement>(dim));

    CRectPassElement::SRectData plate;
    plate.box   = FIELD;
    // darker than the tray pills so the modal field reads clearly against the dimmed overview
    plate.color = CHyprColor{0.F, 0.F, 0.F, 0.92F * alpha};
    plate.round = sc<int>(FIELD.h / 2.0);
    Render::IHyprRenderer::currentPass(g_pHyprRenderer->context()).add(makeUnique<CRectPassElement>(plate));

    auto borderColor = ScrollOverview::Config::getEmptyColor(monitor);
    borderColor.a    = std::min(1.F, sc<float>(borderColor.a) * 2.F) * alpha;
    Config::CGradientValueData gradient;
    gradient.m_colors = {borderColor};
    gradient.updateColorsOk();
    Render::IHyprRenderer::currentPass(g_pHyprRenderer->context()).add(makeUnique<CBorderPassElement>(CBorderPassElement::SBorderData{
        .box        = FIELD,
        .grad1      = gradient,
        .round      = sc<int>(FIELD.h / 2.0),
        .borderSize = std::max(1, sc<int>(std::round(2.F * UNIT))),
    }));

    const auto  CHROME   = ScrollOverview::Config::getContextLabelColor(monitor);
    const int   TEXTSIZE = std::max(1, sc<int>(std::round(18.F * UNIT)));
    const float PAD      = 18.F * UNIT;
    // show a placeholder when empty
    const bool  PLACEHOLDER = specialTrayRenameText.empty();
    const auto  DISPLAY     = PLACEHOLDER ? std::string{"Name"} : specialTrayRenameText;
    const auto  KEY         = DISPLAY + "\n" + std::to_string(TEXTSIZE) + (PLACEHOLDER ? "\nrename-ph" : "\nrename");
    auto&       texture     = contextLabelTextures[KEY];
    if (!texture)
        texture = g_pHyprRenderer->renderText(DISPLAY, PLACEHOLDER ? CHROME.modifyA(0.35F) : CHROME, TEXTSIZE, false, "", 0, 600);
    if (texture && texture->m_size.x > 0) {
        const double MAX_W = FIELD.w - 2.0 * PAD - 8.0 * UNIT;
        double       tw    = texture->m_size.x;
        double       th    = texture->m_size.y;
        // if the name is long, keep the right end (caret side) visible
        double       tx    = FIELD.x + PAD;
        if (tw > MAX_W)
            tx = FIELD.x + FIELD.w - PAD - tw;
        CTexPassElement::SRenderData data;
        data.tex = texture;
        data.box = CBox{tx, FIELD.middle().y - th / 2.0, tw, th}.round();
        data.a   = alpha * (PLACEHOLDER ? 0.45F : 1.F);
        Render::IHyprRenderer::currentPass(g_pHyprRenderer->context()).add(makeUnique<CTexPassElement>(std::move(data)));

        // caret at the end of the text (or at the placeholder start)
        const double CARET_X = PLACEHOLDER ? FIELD.x + PAD : std::min(tx + tw + 2.0 * UNIT, FIELD.x + FIELD.w - PAD);
        CRectPassElement::SRectData caret;
        caret.box   = CBox{CARET_X, FIELD.middle().y - th / 2.0, std::max(2.0, 2.0 * UNIT), th}.round();
        caret.color = CHROME.modifyA(alpha);
        Render::IHyprRenderer::currentPass(g_pHyprRenderer->context()).add(makeUnique<CRectPassElement>(caret));
    }
}

void CScrollOverview::renameSpecialTrayCard(const std::string& oldName, const std::string& rawNew) {
    if (!applySpecialTrayRename(oldName, rawNew))
        return;
    rebuildPending = true;
    damage();
}

size_t CScrollOverview::specialTrayReorderInsertAt(const Vector2D& pointLocal, size_t from) const {
    const auto CARDS = specialTrayCards();
    if (CARDS.empty())
        return 0;

    const bool BOTTOM = specialTrayOnBottom();
    size_t best = CARDS.size();
    double bestDist = std::numeric_limits<double>::max();
    for (size_t slot = 0; slot <= CARDS.size(); ++slot) {
        double mark = 0;
        if (BOTTOM) {
            const auto FRONT = specialTrayVisualBox(CARDS.front().box);
            const auto BACK  = specialTrayVisualBox(CARDS.back().box);
            if (slot == 0)
                mark = FRONT.x;
            else if (slot == CARDS.size())
                mark = BACK.x + BACK.w;
            else {
                const auto A = specialTrayVisualBox(CARDS[slot - 1].box);
                const auto B = specialTrayVisualBox(CARDS[slot].box);
                mark         = (A.x + A.w + B.x) / 2.0;
            }
        } else {
            if (slot == 0)
                mark = CARDS.front().box.y;
            else if (slot == CARDS.size())
                mark = CARDS.back().box.y + CARDS.back().box.h;
            else
                mark = (CARDS[slot - 1].box.y + CARDS[slot - 1].box.h + CARDS[slot].box.y) / 2.0;
        }
        const double DIST = std::abs((BOTTOM ? pointLocal.x : pointLocal.y) - mark);
        if (DIST < bestDist) {
            bestDist = DIST;
            best     = slot;
        }
    }

    // same visual place as "before self" / "after self" is a no-op once applied
    if (best == from || best == from + 1)
        return from;
    return best;
}

void CScrollOverview::beginSpecialTrayReorder(size_t index) {
    specialTrayReorderFrom   = index;
    specialTrayReorderInsert = index;
    specialTrayReorderActive = false;
    dragStartMouseLocal      = lastMousePosLocal;
    if (Pointer::Cursor::overrideController)
        Pointer::Cursor::overrideController->setOverride("grab", Pointer::Cursor::CURSOR_OVERRIDE_SPECIAL_ACTION);
}

void CScrollOverview::updateSpecialTrayReorder() {
    if (!specialTrayReorderFrom)
        return;
    static constexpr float DRAGTHRESHOLD = 6.F;
    if (!specialTrayReorderActive && lastMousePosLocal.distanceSq(dragStartMouseLocal) > DRAGTHRESHOLD * DRAGTHRESHOLD) {
        specialTrayReorderActive = true;
        if (Pointer::Cursor::overrideController)
            Pointer::Cursor::overrideController->setOverride("grabbing", Pointer::Cursor::CURSOR_OVERRIDE_SPECIAL_ACTION);
    }
    if (!specialTrayReorderActive)
        return;
    specialTrayReorderInsert = specialTrayReorderInsertAt(lastMousePosLocal, *specialTrayReorderFrom);
    damage();
}

void CScrollOverview::applySpecialTrayOrder(std::vector<std::string> order) {
    g_trayOrderLoaded = true;
    g_trayOrder       = std::move(order);
    saveTrayOrder();
    damage();
}

void CScrollOverview::finishSpecialTrayReorder(bool apply) {
    if (apply && specialTrayReorderActive && specialTrayReorderFrom && specialTrayReorderInsert) {
        auto       cards = specialTrayCards();
        const auto FROM  = *specialTrayReorderFrom;
        auto       TO    = *specialTrayReorderInsert;
        if (FROM < cards.size()) {
            std::vector<std::string> order;
            order.reserve(cards.size());
            for (const auto& card : cards)
                order.push_back(card.name);
            const auto NAME = order[FROM];
            order.erase(order.begin() + sc<long>(FROM));
            if (TO > FROM)
                --TO;
            TO = std::min(TO, order.size());
            order.insert(order.begin() + sc<long>(TO), NAME);
            applySpecialTrayOrder(std::move(order));
        }
    }
    specialTrayReorderFrom.reset();
    specialTrayReorderInsert.reset();
    specialTrayReorderActive = false;
    if (Pointer::Cursor::overrideController)
        Pointer::Cursor::overrideController->unsetOverride(Pointer::Cursor::CURSOR_OVERRIDE_SPECIAL_ACTION);
}

// a card shows the scratchpad's own monitor (it can live on another one), scaled to the card
float CScrollOverview::specialTrayRenderScale(const STrayCard& card) const {
    const auto MONITOR = pMonitor.lock();
    const auto SOURCE  = card.workspace && card.workspace->m_monitor ? card.workspace->m_monitor.lock() : MONITOR;
    if (!MONITOR || !SOURCE)
        return 0.F;
    return sc<float>(card.box.w / (std::max(SOURCE->m_size.x, 1.0) * std::max(MONITOR->m_scale, 0.01F)));
}

CBox CScrollOverview::specialTrayWindowBox(const STrayCard& card, const PHLWINDOW& window) const {
    const auto MONITOR = pMonitor.lock();
    const auto SOURCE  = card.workspace && card.workspace->m_monitor ? card.workspace->m_monitor.lock() : MONITOR;
    if (!MONITOR || !SOURCE || !window)
        return {};

    const float FACTOR = specialTrayRenderScale(card) * MONITOR->m_scale;
    const auto  BOX    = window->geometricBox(Desktop::View::IGeometric::GEOMETRIC_CURRENT);
    return CBox{card.box.pos() + (BOX.pos() - SOURCE->m_position) * FACTOR, BOX.size() * FACTOR}.round();
}

std::vector<PHLWINDOW> CScrollOverview::specialTrayWindows(const STrayCard& card) const {
    std::vector<PHLWINDOW> tiled, floating;
    if (!card.workspace)
        return tiled;

    for (const auto& w : Desktop::windowState()->windows()) {
        const auto WINDOW = getOverviewWindowToShow(w);
        if (!WINDOW || WINDOW->m_workspace != card.workspace || !shouldShowOverviewWindow(WINDOW))
            continue;
        auto& list = WINDOW->isFloating() ? floating : tiled;
        if (std::ranges::find(list, WINDOW) == list.end())
            list.push_back(WINDOW);
    }
    tiled.insert(tiled.end(), floating.begin(), floating.end());
    return tiled;
}

PHLWINDOW CScrollOverview::specialTrayWindowAt(const Vector2D& pointLocal) const {
    if (const auto ICON = specialTrayIconAt(pointLocal))
        return ICON->second;

    const auto IDX = specialTrayCardAt(pointLocal);
    if (!IDX)
        return nullptr;

    const auto CARDS   = specialTrayCards();
    const auto WINDOWS = specialTrayWindows(CARDS[*IDX]);
    for (auto it = WINDOWS.rbegin(); it != WINDOWS.rend(); ++it) {
        if (specialTrayWindowBox(CARDS[*IDX], *it).containsPoint(pointLocal))
            return *it;
    }
    return nullptr;
}

// a click on a workspace goes there: a scratchpad that was open when the overview started would come back on top
void CScrollOverview::hideActiveSpecialWorkspace() {
    if (const auto MONITOR = pMonitor.lock(); MONITOR && MONITOR->m_activeSpecialWorkspace)
        MONITOR->setSpecialWorkspace(nullptr);
}

PHLWORKSPACE CScrollOverview::ensureSpecialWorkspace(const STrayCard& card) const {
    if (!card.name.empty())
        addTrayRosterName(card.name);

    if (card.workspace && valid(card.workspace))
        return card.workspace;

    const auto ADDRESS = std::string{SPECIAL_PREFIX} + card.name;
    for (const auto& w : State::workspaceState()->workspaces()) {
        const auto WORKSPACE = w.lock();
        if (valid(WORKSPACE) && WORKSPACE->type() == Workspace::eWorkspaceType::SPECIAL && WORKSPACE->addressableName() == ADDRESS)
            return WORKSPACE;
    }

    return State::workspaceState()->createSpecial(ADDRESS, pMonitor.lock());
}

// app icons on the bottom edge of a card, half over it, side by side from the outer screen edge, like macOS Stage
// Manager: one per app, the topmost window's outermost, as many as fit (the name sits below them, see
// specialTrayLabelOffset)
// an app without an icon still gets something to take it by: its first letter
static SP<Render::ITexture> appLetterTexture(const std::string& appID, int sizePx) {
    static std::unordered_map<std::string, SP<Render::ITexture>> cache;

    const auto NAME   = appID.substr(appID.find_last_of('.') == std::string::npos ? 0 : appID.find_last_of('.') + 1);
    const auto LETTER = NAME.empty() || !std::isalnum(sc<unsigned char>(NAME.front())) ? std::string{"?"} : std::string(1, sc<char>(std::toupper(sc<unsigned char>(NAME.front()))));
    auto&      texture = cache[LETTER + "\n" + std::to_string(sizePx)];
    if (!texture)
        texture = g_pHyprRenderer->renderText(LETTER, CHyprColor{1.F, 1.F, 1.F, 0.9F}, std::max(1, sizePx), false, "", 0, 700);
    return texture;
}

std::vector<CScrollOverview::STrayIcon> CScrollOverview::specialTrayIcons(const STrayCard& card, const std::vector<PHLWINDOW>& windows) const {
    const auto MONITOR = pMonitor.lock();
    if (!MONITOR)
        return {};

    const auto  VIS   = specialTrayVisualBox(card.box);
    const float ICON  = ScrollOverview::Config::getValue<int>("plugin:gridoverview:special_tray_icon_size", MONITOR) * MONITOR->m_scale;
    const float PLATE = ICON * TRAY_ICON_PLATE;
    const float GAP   = 6.F * MONITOR->m_scale;
    const float INSET = 10.F * MONITOR->m_scale;
    const int   FIT   = std::max(1, sc<int>((VIS.w - 2.F * INSET + GAP) / (PLATE + GAP)));
    const auto  THEME = ScrollOverview::Config::getValue<std::string>("plugin:gridoverview:icon_theme", MONITOR);

    std::vector<std::string> apps;
    std::vector<STrayIcon>   icons;
    // side trays: pack from the outer screen edge; bottom tray: pack from the left
    const bool               FROM_LEFT = specialTrayOnLeft() || specialTrayOnBottom();
    float                    cursorX   = FROM_LEFT ? VIS.x + INSET : VIS.x + VIS.w - INSET;
    const float              CENTER    = VIS.y + VIS.h;
    for (auto it = windows.rbegin(); it != windows.rend() && sc<int>(icons.size()) < FIT; ++it) {
        const auto APP = (*it)->metadata().appID();
        if (std::ranges::find(apps, APP) != apps.end())
            continue;
        apps.push_back(APP);

        auto       texture = GridIcons::iconForAppID(APP, THEME);
        const bool LETTER  = !texture;
        if (LETTER)
            texture = appLetterTexture(APP, sc<int>(ICON * 0.6F));
        if (!texture || texture->m_size.x <= 0)
            continue;

        const float    MIDX = FROM_LEFT ? cursorX + PLATE / 2.F : cursorX - PLATE / 2.F;
        const Vector2D SIZE = LETTER ? texture->m_size : Vector2D{ICON, ICON};
        icons.push_back({
            .window  = *it,
            .texture = texture,
            .plate   = CBox{MIDX - PLATE / 2.F, CENTER - PLATE / 2.F, PLATE, PLATE}.round(),
            .icon    = CBox{MIDX - SIZE.x / 2.0, CENTER - SIZE.y / 2.0, SIZE.x, SIZE.y}.round(),
        });
        cursorX += FROM_LEFT ? PLATE + GAP : -(PLATE + GAP);
    }
    return icons;
}

// an icon picks its app's window out of a crowded card: a drag takes it, a click opens the scratchpad on it
std::optional<std::pair<size_t, PHLWINDOW>> CScrollOverview::specialTrayIconAt(const Vector2D& pointLocal) const {
    if (closing || (specialTrayAutohide() && trayReveal && trayReveal->goal() < 0.5F) ||
        !ScrollOverview::Config::getValue<bool>("plugin:gridoverview:special_tray_icons", pMonitor.lock()))
        return std::nullopt;

    const auto CARDS = specialTrayCards();
    for (size_t i = 0; i < CARDS.size(); ++i) {
        for (const auto& icon : specialTrayIcons(CARDS[i], specialTrayWindows(CARDS[i]))) {
            if (icon.plate.containsPoint(pointLocal))
                return std::pair{i, icon.window};
        }
    }
    return std::nullopt;
}

void CScrollOverview::renderSpecialTrayIcons(PHLMONITOR monitor, const STrayCard& card, const std::vector<PHLWINDOW>& windows, float alpha) {
    auto plate = ScrollOverview::Config::getWindowIconPlateColor(monitor);
    plate.a *= alpha;

    for (const auto& icon : specialTrayIcons(card, windows)) {
        if (plate.a > 0.F) {
            CRectPassElement::SRectData data;
            data.box   = icon.plate;
            data.color = plate;
            data.round = sc<int>(icon.plate.w * 0.25F);
            Render::IHyprRenderer::currentPass(g_pHyprRenderer->context()).add(makeUnique<CRectPassElement>(data));
        }

        CTexPassElement::SRenderData data;
        data.tex = icon.texture;
        data.box = icon.icon;
        data.a   = alpha;
        Render::IHyprRenderer::currentPass(g_pHyprRenderer->context()).add(makeUnique<CTexPassElement>(std::move(data)));
    }
}

// special_tray_perspective: Hyprland's tex pass only draws axis-aligned rectangles, so window contents go into a
// monitor-sized buffer first and come back as strips that taper toward the screen centre. Icons / name pills stay
// flat afterwards. Returns false if the buffer is unavailable (caller then draws the windows flat).
bool CScrollOverview::renderSpecialTrayPerspective(PHLMONITOR monitor, const std::vector<CBox>& cards, float shrink,
                                                   ETrayPerspectiveTaper taper, const std::function<void()>& drawContents) {
    static constexpr int STRIPS = 24;

    if (!monitor || !g_pHyprRenderer || cards.empty() || shrink <= 0.001F || !drawContents)
        return false;
    if (g_pHyprRenderer->type() != Render::IHyprRenderer::RT_GL)
        return false;

    auto& ctx = g_pHyprRenderer->context();
    if (!ctx.m_data.currentFB)
        return false;

    const auto SIZE   = monitor->m_transformedSize;
    const auto FORMAT = ctx.m_data.currentFB->m_drmFormat;
    if (!trayPerspectiveFB)
        trayPerspectiveFB = g_pHyprRenderer->createFB("gridoverview_tray_perspective");
    if (!trayPerspectiveFB)
        return false;
    if (!trayPerspectiveFB->isAllocated() || trayPerspectiveFB->m_size != SIZE || trayPerspectiveFB->m_drmFormat != FORMAT) {
        trayPerspectiveFB->release();
        if (!trayPerspectiveFB->alloc(sc<int>(SIZE.x), sc<int>(SIZE.y), FORMAT))
            return false;
    }
    trayPerspectiveFB->setImageDescription(monitor->workBufferImageDescription());

    OverviewRender::flushPass(ctx, monitor);
    {
        auto bind = g_pHyprRenderer->bindTempFB(ctx, trayPerspectiveFB);
        g_pHyprRenderer->draw(ctx, CClearPassElement::SClearData{CHyprColor{0.F, 0.F, 0.F, 0.F}}, CRegion{CBox{{}, SIZE}});
        drawContents();
        OverviewRender::flushPass(ctx, monitor);
    }

    const auto TEXTURE = trayPerspectiveFB->getTexture();
    if (!TEXTURE)
        return true;

    const auto restoreUV = Hyprutils::Utils::CScopeGuard([&ctx] {
        ctx.m_data.primarySurfaceUVTopLeft     = Vector2D{-1, -1};
        ctx.m_data.primarySurfaceUVBottomRight = Vector2D{-1, -1};
    });

    const bool HORIZONTAL = taper == ETrayPerspectiveTaper::TowardTop;
    // Depth foreshortening: a plane tipped away is not only tapered on the far edge — its projected
    // span along the depth axis also shrinks (otherwise side trays keep flat width and look stretched).
    const double DEPTH = std::clamp(sc<double>(shrink), 0.0, 0.9);
    for (const auto& box : cards) {
        if (box.w <= 1 || box.h <= 1)
            continue;
        for (int i = 0; i < STRIPS; ++i) {
            const double U0 = sc<double>(i) / STRIPS, U1 = sc<double>(i + 1) / STRIPS;
            const double ALONG = (U0 + U1) / 2.0;
            CTexPassElement::SRenderData data;
            data.tex           = TEXTURE;
            data.allowCustomUV = true;
            if (HORIZONTAL) {
                // bottom tray: tip back → top (toward centre) is far. Anchor near edge at the bottom.
                const double projH = box.h * (1.0 - DEPTH);
                const double yNear = box.y + box.h;
                const double yFar  = yNear - projH;
                const double Y0 = yFar + projH * U0, Y1 = yFar + projH * U1;
                // far (U→0) narrower; UV still covers the full flat card (vertical squash = foreshortening)
                const double W = box.w * (1.0 - DEPTH * (1.0 - ALONG));
                ctx.m_data.primarySurfaceUVTopLeft     = Vector2D{box.x / SIZE.x, (box.y + box.h * U0) / SIZE.y};
                ctx.m_data.primarySurfaceUVBottomRight = Vector2D{(box.x + box.w) / SIZE.x, (box.y + box.h * U1) / SIZE.y};
                data.box = CBox{box.x + (box.w - W) / 2.0, Y0, W, std::max(1.0, Y1 - Y0)};
            } else {
                // left/right tray: yaw around the outer (near) edge; far edge toward screen centre
                const double projW = box.w * (1.0 - DEPTH);
                const bool   farIsLeft = taper == ETrayPerspectiveTaper::TowardLeft;
                const double xNear = farIsLeft ? box.x + box.w : box.x;
                const double xFar  = farIsLeft ? xNear - projW : xNear + projW;
                const double xMin  = std::min(xNear, xFar);
                const double X0 = xMin + projW * U0, X1 = xMin + projW * U1;
                // TowardLeft: U=0 at far (left); TowardRight: U=0 at near (left) — match strip order to UV
                const double T = farIsLeft ? (1.0 - ALONG) : ALONG;
                const double H = box.h * (1.0 - DEPTH * T);
                ctx.m_data.primarySurfaceUVTopLeft     = Vector2D{(box.x + box.w * U0) / SIZE.x, box.y / SIZE.y};
                ctx.m_data.primarySurfaceUVBottomRight = Vector2D{(box.x + box.w * U1) / SIZE.x, (box.y + box.h) / SIZE.y};
                data.box = CBox{X0, box.y + (box.h - H) / 2.0, std::max(1.0, X1 - X0), H};
            }
            Render::IHyprRenderer::currentPass(ctx).add(makeUnique<CTexPassElement>(std::move(data)));
            OverviewRender::flushPass(ctx, monitor);
        }
    }
    return true;
}

void CScrollOverview::renderSpecialTray(PHLMONITOR monitor, const Time::steady_tp& now) {
    if (!ScrollOverview::Config::getValue<bool>("plugin:gridoverview:special_tray", monitor))
        return;

    const auto CARDS = specialTrayCards();
    const auto ADD   = specialTrayAddBox(CARDS);
    if (ADD.empty())
        return;

    const float PROGRESS = std::clamp((1.F - scale->value()) / (1.F - ScrollOverview::Config::getScale(monitor)), 0.F, 1.F);
    const float REVEAL   = specialTrayAutohide() && trayReveal ? trayReveal->value() : 1.F;
    const float ALPHA    = PROGRESS * PROGRESS * (3.F - 2.F * PROGRESS) * REVEAL;
    if (ALPHA <= 0.001F)
        return;

    auto*      dragOwner = g_pointerGrabOverview && g_pointerGrabOverview->dragActiveWindow ? g_pointerGrabOverview : this;
    const auto DRAGGED   = getOverviewWindowToShow(dragOwner->dragActiveWindow.lock());
    const auto HOVERED   = DRAGGED && isOverviewPointerOnMonitor(monitor) ? specialTrayCardAt(getOverviewMousePosLocal(monitor)) : std::nullopt;

    const auto  BACKGROUND = ScrollOverview::Config::getContextLabelBackground(monitor);
    const int   TEXTSIZE   = std::max(1, sc<int>(std::round(TRAY_LABEL_SIZE * monitor->m_scale)));
    // same default theme for name, × and grip (context_label_color, else white)
    const auto  CHROME     = ScrollOverview::Config::getContextLabelColor(monitor);
    const bool  ICONS      = ScrollOverview::Config::getValue<bool>("plugin:gridoverview:special_tray_icons", monitor);

    // Dock: hug foreshortening only on the depth axis (side: width via visual; bottom: none on height —
    // rotate the side rule 90°). Side uses flat card.y for the long axis so the BG keeps padding;
    // bottom must use flat card.y too — visual.y is the foreshortened top and made the dock too short.
    const float PAD = TRAY_DOCK_PAD * monitor->m_scale;
    CBox        DOCK;
    if (specialTrayOnBottom()) {
        const auto   FLAT   = CARDS.empty() ? ADD : CARDS.front().box;
        const double LEFT   = FLAT.x - PAD;
        const double TOP    = FLAT.y - PAD;
        const double RIGHT  = ADD.x + ADD.w + PAD;
        const double BOTTOM = std::max(ADD.y + ADD.h, FLAT.y + FLAT.h + specialTrayLabelBand() * monitor->m_scale) + PAD;
        DOCK                = CBox{LEFT, TOP, RIGHT - LEFT, BOTTOM - TOP}.round();
    } else {
        const auto FRONT = CARDS.empty() ? ADD : specialTrayVisualBox(CARDS.front().box);
        const auto TOPY  = CARDS.empty() ? ADD.y : CARDS.front().box.y;
        const double LEFT  = std::min(FRONT.x, ADD.x) - PAD;
        const double RIGHT = std::max(FRONT.x + FRONT.w, ADD.x + ADD.w) + PAD;
        DOCK = CBox{LEFT, TOPY - PAD, RIGHT - LEFT, ADD.y + ADD.h + PAD - (TOPY - PAD)}.round();
    }
    const bool BLUR = ScrollOverview::Config::getValue<bool>("plugin:gridoverview:special_tray_blur", monitor) && ScrollOverview::Config::getValue<int>("decoration:blur:enabled");
    CRectPassElement::SRectData dock;
    dock.box   = DOCK;
    dock.color = CHyprColor{0.F, 0.F, 0.F, (BLUR ? 0.4F : 0.5F) * ALPHA};
    dock.round = sc<int>(std::round(TRAY_DOCK_ROUNDING * monitor->m_scale));
    dock.blur  = BLUR;
    dock.blurA = ALPHA;
    Render::IHyprRenderer::currentPass(g_pHyprRenderer->context()).add(makeUnique<CRectPassElement>(dock));

    const bool   REORDERING = specialTrayReorderActive && specialTrayReorderFrom;
    const double REORDERDX  = REORDERING && specialTrayOnBottom() ? lastMousePosLocal.x - dragStartMouseLocal.x : 0.0;
    const double REORDERDY  = REORDERING && !specialTrayOnBottom() ? lastMousePosLocal.y - dragStartMouseLocal.y : 0.0;

    // tilt only the window stack; icons / name pills stay flat. Strength follows the overview open progress.
    const bool WANT_PERSPECTIVE = ScrollOverview::Config::getValue<bool>("plugin:gridoverview:special_tray_perspective", monitor);
    const float PERSPECTIVE_AMOUNT = TRAY_PERSPECTIVE_SHRINK * PROGRESS;

    struct SCardDraw {
        STrayCard              card;
        bool                   target = false;
        bool                   moving = false;
        float                  scale  = 1.F;
        int                    rounding = 0;
        float                  alpha  = 1.F;
        std::vector<PHLWINDOW> windows;
    };
    std::vector<SCardDraw> draws;
    draws.reserve(CARDS.size());
    for (size_t i = 0; i < CARDS.size(); ++i) {
        SCardDraw draw;
        draw.card = CARDS[i];
        draw.moving = REORDERING && *specialTrayReorderFrom == i;
        if (draw.moving) {
            draw.card.box.x += REORDERDX;
            draw.card.box.y += REORDERDY;
        }
        draw.target   = !REORDERING && HOVERED == i && (!DRAGGED || DRAGGED->m_workspace != draw.card.workspace);
        draw.scale    = specialTrayRenderScale(draw.card);
        draw.rounding = sc<int>(std::round(ScrollOverview::Config::getValue<int>("decoration:rounding") * monitor->m_scale * draw.scale));
        draw.alpha    = ALPHA * (draw.moving ? 0.85F : REORDERING ? 0.55F : 1.F);
        draw.windows  = specialTrayWindows(draw.card);
        std::erase(draw.windows, DRAGGED);
        draws.push_back(std::move(draw));
    }

    const auto drawWindows = [&](const SCardDraw& draw) {
        if (draw.windows.empty() || !draw.card.workspace)
            return;
        const auto WORKSPACE         = draw.card.workspace;
        const bool WASVISIBLE        = WORKSPACE->visible();
        const bool WASFORCERENDERING = WORKSPACE->m_forceRendering;
        // a scratchpad that was just left fades out (workspace alpha -> 0), also after the overview opened.
        // Window contents follow the workspace alpha, borders and shadows the render alpha: both get the
        // tray's own fade. Only the drawn value is replaced, Hyprland's fade keeps running.
        // Shadows stay off in the tray (and especially in the perspective FB — they crashed the old experiment).
        const float WASALPHA = WORKSPACE->m_alpha ? WORKSPACE->m_alpha->value() : 1.F;
        WORKSPACE->setVisible(true);
        WORKSPACE->m_forceRendering = true;
        if (WORKSPACE->m_alpha)
            WORKSPACE->m_alpha->value() = draw.alpha;
        auto restoreWorkspaceState = Hyprutils::Utils::CScopeGuard([WORKSPACE, WASVISIBLE, WASFORCERENDERING, WASALPHA] {
            WORKSPACE->setVisible(WASVISIBLE);
            WORKSPACE->m_forceRendering = WASFORCERENDERING;
            if (WORKSPACE->m_alpha)
                WORKSPACE->m_alpha->value() = WASALPHA;
        });

        for (const auto& window : draw.windows)
            renderWindowLive(monitor, window, specialTrayWindowBox(draw.card, window), draw.scale, now, &draw.card.box, false, draw.alpha, false);
    };

    // hover plates under the windows (same order as the flat path) — visual/foreshortened bounds
    for (const auto& draw : draws) {
        if (!(draw.target || draw.moving))
            continue;
        CRectPassElement::SRectData card;
        card.box   = specialTrayVisualBox(draw.card.box);
        card.color = CHyprColor{1.F, 1.F, 1.F, (draw.moving ? 0.12F : 0.08F) * draw.alpha};
        card.round = draw.rounding;
        Render::IHyprRenderer::currentPass(g_pHyprRenderer->context()).add(makeUnique<CRectPassElement>(card));
    }

    std::vector<CBox> perspectiveCards;
    if (WANT_PERSPECTIVE) {
        for (const auto& draw : draws) {
            if (!draw.windows.empty())
                perspectiveCards.push_back(draw.card.box);
        }
    }
    // bottom uses the same yaw as left (toward the right / screen centre from a left-edge tray)
    const auto TAPER = (specialTrayOnLeft() || specialTrayOnBottom()) ? ETrayPerspectiveTaper::TowardRight :
                                                                        ETrayPerspectiveTaper::TowardLeft;
    const bool PERSPECTIVE = WANT_PERSPECTIVE && !perspectiveCards.empty() &&
        renderSpecialTrayPerspective(monitor, perspectiveCards, PERSPECTIVE_AMOUNT, TAPER, [&] {
            for (const auto& draw : draws)
                drawWindows(draw);
        });

    for (size_t i = 0; i < draws.size(); ++i) {
        const auto& draw      = draws[i];
        const auto& CARD      = draw.card;
        const float CARDALPHA = draw.alpha;
        const auto  ROUNDING  = draw.rounding;
        const bool  TARGET    = draw.target;
        const bool  MOVING    = draw.moving;
        const auto& windows   = draw.windows;

        const auto VISUAL = specialTrayVisualBox(CARD.box);

        if (windows.empty())
            renderEmptyWorkspaceMarker(monitor, VISUAL, scale->value(), CARDALPHA, TARGET || MOVING);
        else {
            if (!PERSPECTIVE)
                drawWindows(draw);

            if (TARGET || MOVING) {
                auto color = ScrollOverview::Config::getEmptyColor(monitor);
                color.a    = std::min(1.F, sc<float>(color.a) * 2.F) * CARDALPHA;
                Config::CGradientValueData gradient;
                gradient.m_colors = {color};
                gradient.updateColorsOk();
                Render::IHyprRenderer::currentPass(g_pHyprRenderer->context()).add(makeUnique<CBorderPassElement>(CBorderPassElement::SBorderData{
                    .box        = VISUAL,
                    .grad1      = gradient,
                    .round      = ROUNDING,
                    .borderSize = std::max(1, sc<int>(std::round(2.F * monitor->m_scale))),
                }));
            }

            // icons stay flat on top of the (possibly tilted) window stack
            if (ICONS)
                renderSpecialTrayIcons(monitor, CARD, windows, CARDALPHA);
        }

        const float CHROME_A = CARDALPHA * (CARD.workspace ? 1.F : 0.6F);
        const bool  NAMED    = !isAnonymousTrayName(CARD.name);
        Vector2D    textSize{};
        SP<Render::ITexture> texture;
        if (NAMED) {
            const auto KEY = CARD.name + "\n" + std::to_string(TEXTSIZE) + "\ntray";
            auto&      slot = contextLabelTextures[KEY];
            if (!slot)
                slot = g_pHyprRenderer->renderText(CARD.name, CHROME, TEXTSIZE, false, "", 0, 600);
            texture = slot;
            if (texture && texture->m_size.x > 0)
                textSize = texture->m_size;
        }
        const auto pillGeom  = specialTrayPill(CARD, textSize);
        auto       pillColor = BACKGROUND.a > 0.F ? BACKGROUND : CHyprColor{0.F, 0.F, 0.F, 0.45F};
        CRectPassElement::SRectData pill;
        pill.box   = pillGeom.pill;
        pill.color = pillColor.modifyA(pillColor.a * CARDALPHA);
        pill.round = sc<int>(pill.box.h / 2.0);
        Render::IHyprRenderer::currentPass(g_pHyprRenderer->context()).add(makeUnique<CRectPassElement>(pill));

        auto chrome = CHROME;
        chrome.a    = 1.F;
        const float PENSIZE = std::min(sc<float>(pillGeom.pencil.w), sc<float>(pillGeom.pencil.h)) * 0.72F;
        GridIcons::drawUiIcon("pencil", CBox{pillGeom.pencil.middle().x - PENSIZE / 2.F, pillGeom.pencil.middle().y - PENSIZE / 2.F, PENSIZE, PENSIZE}, CHROME_A, chrome);

        if (NAMED && texture && texture->m_size.x > 0 && !pillGeom.text.empty()) {
            CTexPassElement::SRenderData data;
            data.tex = texture;
            data.box = pillGeom.text;
            data.a   = CHROME_A;
            Render::IHyprRenderer::currentPass(g_pHyprRenderer->context()).add(makeUnique<CTexPassElement>(std::move(data)));
        }

        const float GRIPSIZE = std::min(sc<float>(pillGeom.grip.w), sc<float>(pillGeom.grip.h)) * 0.85F;
        GridIcons::drawUiIcon("grip", CBox{pillGeom.grip.middle().x - GRIPSIZE / 2.F, pillGeom.grip.middle().y - GRIPSIZE / 2.F, GRIPSIZE, GRIPSIZE}, CHROME_A, chrome);

        // × only while this card (or its protruding button / name pill) is hovered
        if (!REORDERING && !DRAGGED && specialTrayShowingClose() == i) {
            const auto CLOSEBOX = specialTrayCloseBox(CARD);
            CRectPassElement::SRectData plate;
            plate.box   = CLOSEBOX.copy().scaleFromCenter(0.92).round();
            plate.color = CHyprColor{0.F, 0.F, 0.F, 0.55F * CARDALPHA};
            plate.round = sc<int>(plate.box.h / 2.0);
            Render::IHyprRenderer::currentPass(g_pHyprRenderer->context()).add(makeUnique<CRectPassElement>(plate));
            auto closeColor = CHROME;
            closeColor.a    = 1.F;
            GridIcons::drawUiIcon("close", CLOSEBOX.copy().scaleFromCenter(0.78), CARDALPHA, closeColor);
        }
    }

    // drop line while reordering
    if (REORDERING && specialTrayReorderInsert && !CARDS.empty()) {
        const auto SLOT = *specialTrayReorderInsert;
        auto       color = ScrollOverview::Config::getEmptyColor(monitor);
        color.a          = std::min(1.F, sc<float>(color.a) * 2.F) * ALPHA;
        CRectPassElement::SRectData line;
        line.color = color;
        if (specialTrayOnBottom()) {
            const auto FRONT = specialTrayVisualBox(CARDS.front().box);
            const auto BACK  = specialTrayVisualBox(CARDS.back().box);
            double     x     = 0;
            if (SLOT == 0)
                x = FRONT.x - 4.F * monitor->m_scale;
            else if (SLOT >= CARDS.size())
                x = BACK.x + BACK.w + 4.F * monitor->m_scale;
            else {
                const auto A = specialTrayVisualBox(CARDS[SLOT - 1].box);
                const auto B = specialTrayVisualBox(CARDS[SLOT].box);
                x            = (A.x + A.w + B.x) / 2.0;
            }
            line.box = CBox{x - 1.5F * monitor->m_scale, FRONT.y, 3.F * monitor->m_scale, FRONT.h}.round();
        } else {
            const auto FRONT = specialTrayVisualBox(CARDS.front().box);
            double     y     = 0;
            if (SLOT == 0)
                y = CARDS.front().box.y - 4.F * monitor->m_scale;
            else if (SLOT >= CARDS.size())
                y = CARDS.back().box.y + CARDS.back().box.h + 4.F * monitor->m_scale;
            else
                y = (CARDS[SLOT - 1].box.y + CARDS[SLOT - 1].box.h + CARDS[SLOT].box.y) / 2.0;
            line.box = CBox{FRONT.x, y - 1.5F * monitor->m_scale, FRONT.w, 3.F * monitor->m_scale}.round();
        }
        line.round = sc<int>(std::min(line.box.w, line.box.h) / 2.0);
        Render::IHyprRenderer::currentPass(g_pHyprRenderer->context()).add(makeUnique<CRectPassElement>(line));
    }

    // "+" box: new scratchpad (drop a window on it, or click it); also the sole tray content when every card was closed
    const bool ADDHOVERED = isOverviewPointerOnMonitor(monitor) && specialTrayAddAt(getOverviewMousePosLocal(monitor));
    const bool ADDTARGET  = ADDHOVERED && DRAGGED;
    const auto UNIT       = monitor->m_scale;

    const auto ADD_ROUND = sc<int>(std::min(ADD.w, ADD.h) / 2.0);
    if (ADDHOVERED) {
        CRectPassElement::SRectData addBackground;
        addBackground.box   = ADD;
        addBackground.color = CHyprColor{1.F, 1.F, 1.F, 0.08F * ALPHA};
        addBackground.round = ADD_ROUND;
        Render::IHyprRenderer::currentPass(g_pHyprRenderer->context()).add(makeUnique<CRectPassElement>(addBackground));
    }

    auto color = ScrollOverview::Config::getEmptyColor(monitor);
    color.a    = std::min(1.F, sc<float>(color.a) * (ADDHOVERED ? 2.F : 1.F)) * ALPHA;
    Config::CGradientValueData gradient;
    gradient.m_colors = {color};
    gradient.updateColorsOk();
    Render::IHyprRenderer::currentPass(g_pHyprRenderer->context()).add(makeUnique<CBorderPassElement>(CBorderPassElement::SBorderData{
        .box        = ADD,
        .grad1      = gradient,
        .round      = ADD_ROUND,
        .borderSize = std::max(1, sc<int>(std::round((ADDTARGET ? 3.F : 2.F) * UNIT))),
    }));

    const float PLUS = 22.F * UNIT;
    GridIcons::drawUiIcon("plus", CBox{ADD.middle().x - PLUS / 2.F, ADD.middle().y - PLUS / 2.F, PLUS, PLUS}, 1.F, color);

    renderSpecialTrayRenameOverlay(monitor, ALPHA);
}

static std::string jsonString(const std::string& text) {
    std::string out = "\"";
    for (const char C : text) {
        if (C == '"' || C == '\\')
            out += '\\';
        out += C;
    }
    return out + "\"";
}

std::string CScrollOverview::specialTrayLayoutJson() const {
    const auto MONITOR = pMonitor.lock();
    if (!MONITOR)
        return "{\"open\": false}";

    const float SCALE  = std::max(MONITOR->m_scale, 0.01F);
    const auto  global = [&](const CBox& box) {
        return std::format("{{\"x\": {:.0f}, \"y\": {:.0f}, \"w\": {:.0f}, \"h\": {:.0f}}}", box.x / SCALE + MONITOR->m_position.x, box.y / SCALE + MONITOR->m_position.y,
                           box.w / SCALE, box.h / SCALE);
    };

    const auto  CARDS = specialTrayCards();
    std::string cards;
    for (const auto& card : CARDS) {
        if (!cards.empty())
            cards += ", ";
        std::string icons;
        if (ScrollOverview::Config::getValue<bool>("plugin:gridoverview:special_tray_icons", MONITOR)) {
            for (const auto& icon : specialTrayIcons(card, specialTrayWindows(card))) {
                if (!icons.empty())
                    icons += ", ";
                auto iconBox = global(icon.plate);
                iconBox.insert(1, std::format("\"window\": \"0x{:x}\", ", rc<uintptr_t>(icon.window.get())));
                icons += iconBox;
            }
        }
        const int  TEXTSIZE = std::max(1, sc<int>(std::round(TRAY_LABEL_SIZE * MONITOR->m_scale)));
        const bool NAMED    = !isAnonymousTrayName(card.name);
        Vector2D   size{};
        if (NAMED) {
            const auto KEY     = card.name + "\n" + std::to_string(TEXTSIZE) + "\ntray";
            const auto TEXTURE = contextLabelTextures.contains(KEY) ? contextLabelTextures.at(KEY) : SP<Render::ITexture>{};
            size               = TEXTURE && TEXTURE->m_size.x > 0 ? TEXTURE->m_size : Vector2D{TRAY_LABEL_SIZE * 4.0 * MONITOR->m_scale, TRAY_LABEL_SIZE * MONITOR->m_scale};
        }
        const auto PILL    = specialTrayPill(card, size);
        const auto VISUAL  = specialTrayVisualBox(card.box);
        auto       box     = global(card.box);
        box.insert(1, std::format("\"name\": {}, \"anonymous\": {}, \"exists\": {}, \"visual\": {}, \"icons\": [{}], \"close\": {}, \"pencil\": {}, \"grip\": {}, ",
                                  jsonString(card.name), NAMED ? "false" : "true", card.workspace ? "true" : "false", global(VISUAL), icons,
                                  global(specialTrayCloseBox(card)), global(PILL.pencil), global(PILL.grip)));
        cards += box;
    }

    const auto ADD   = specialTrayAddBox(CARDS);
    const bool SHOWN = !ADD.empty() && !(specialTrayAutohide() && trayReveal && trayReveal->goal() < 0.5F);
    const float DEPTH = specialTrayPerspectiveDepth();
    return std::format("{{\"open\": {}, \"monitor\": {}, \"shown\": {}, \"perspective_depth\": {:.4f}, \"cards\": [{}], \"add\": {}}}", closing ? "false" : "true",
                       jsonString(MONITOR->m_name), SHOWN ? "true" : "false", DEPTH, cards, ADD.empty() ? "null" : global(ADD));
}

int trayLayoutLua(lua_State* L) {
    const auto  OVERVIEW = g_pInputManager ? scrollOverviewAt(g_pInputManager->getMouseCoordsInternal()) : SP<IOverview>{};
    const auto* TRAY     = OVERVIEW ? dynamic_cast<CScrollOverview*>(OVERVIEW.get()) : nullptr;
    const auto  JSON     = TRAY ? TRAY->specialTrayLayoutJson() : std::string{"{\"open\": false}"};
    lua_pushstring(L, JSON.c_str());
    return 1;
}
