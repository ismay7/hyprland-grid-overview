// Scratchpad tray (special_tray): special workspaces as cards at the right screen edge, in screen coordinates
// (not zoomed or panned with the grid). Hit tests, layout and drawing; input and drag handling call into it from
// scrollOverview.cpp.
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
#include <cstdio>
#include <thread>
#include <hyprland/src/input/Keys.hpp>

static constexpr float       TRAY_MARGIN     = 24.F; // logical px: screen edge to cards, and between grid and cards
static constexpr float       TRAY_GAP        = 20.F; // between a card's label and the next card
static constexpr float       TRAY_LABEL      = 50.F; // band under every card for its name (centred in it)
static constexpr float       TRAY_ICON_PLATE = 1.3F; // icon plate size relative to the icon
static constexpr float       TRAY_PILL       = 32.F; // height of the name pill (15 px text + padding)
static constexpr int         TRAY_LABEL_SIZE = 15;
static constexpr float       TRAY_ADD_HEIGHT = 56.F; // the "+" box below the cards
static constexpr float       TRAY_DOCK_PAD   = 14.F; // dock around the cards (within TRAY_MARGIN)
static constexpr float       TRAY_DOCK_ROUNDING = 18.F;
static constexpr float       TRAY_GRIP_WIDTH   = 16.F; // grip on the right of the name pill
static constexpr float       TRAY_PENCIL_WIDTH = 16.F; // pencil on the left of the name pill
static constexpr float       TRAY_GRIP_GAP     = 8.F;  // between the name / icons and the grip or pencil
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
        const char LOWER = sc<char>(std::tolower(sc<unsigned char>(C)));
        if (std::isalnum(sc<unsigned char>(LOWER)) || LOWER == '_' || LOWER == '-')
            out += LOWER;
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

    // one column at the right edge, vertically centred in the work area; shrink the cards when they do not fit
    const auto  SIZE   = MONITOR->m_size;
    const float ASPECT = sc<float>(SIZE.y / std::max(SIZE.x, 1.0));
    const float TOP    = gridWorkAreaTop(MONITOR) + TRAY_MARGIN;
    const float AVAILH = sc<float>(SIZE.y) - TOP - TRAY_MARGIN;
    const float N      = sc<float>(cards.size());
    float       width  = sc<float>(ScrollOverview::Config::getValue<int>("plugin:gridoverview:special_tray_width", MONITOR));
    // the cards, then the "+" box (new scratchpad) below the last one
    const float BAND      = specialTrayLabelBand();
    const float EXTRA     = N * BAND + N * TRAY_GAP + TRAY_ADD_HEIGHT;
    const auto  heightFor = [&](float w) { return N * w * ASPECT + EXTRA; };
    if (heightFor(width) > AVAILH)
        width = std::max(20.F, (AVAILH - EXTRA) / N / ASPECT);

    const float HEIGHT = width * ASPECT;
    float       y      = TOP + std::max(0.F, (AVAILH - heightFor(width)) / 2.F);
    // autohide: slides in from beyond the right edge
    const float HIDDEN = specialTrayAutohide() && trayReveal ? (1.F - trayReveal->value()) * (width + 2.F * TRAY_MARGIN) : 0.F;
    const float X      = sc<float>(SIZE.x) - TRAY_MARGIN - width + HIDDEN;
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
// a card, reaching it at the cards' left edge
float CScrollOverview::specialTrayDragScale(const Vector2D& cursorGlobal, float gridScale) const {
    static constexpr float RANGE = 300.F; // logical px left of the cards
    const auto             MONITOR = pMonitor.lock();
    if (!MONITOR || closing)
        return 1.F;

    const auto CARDS = specialTrayCards();
    const auto ADD   = specialTrayAddBox(CARDS);
    if (ADD.empty())
        return 1.F;

    const double EDGE_X   = CARDS.empty() ? ADD.x : CARDS.front().box.x;
    const double X        = (cursorGlobal.x - MONITOR->m_position.x) * MONITOR->m_scale;
    const double START    = EDGE_X - RANGE * MONITOR->m_scale;
    float        progress = std::clamp(sc<float>((X - START) / (EDGE_X - START)), 0.F, 1.F);
    progress              = progress * progress * (3.F - 2.F * progress);
    if (specialTrayAutohide() && trayReveal)
        progress *= trayReveal->value();

    const float CARDSCALE = (CARDS.empty() ? sc<float>(ADD.w) / sc<float>(std::max(MONITOR->m_size.x, 1.0) * MONITOR->m_scale)
                                           : specialTrayRenderScale(CARDS.front())) /
                            std::max(gridScale, 0.01F);
    return 1.F + (CARDSCALE - 1.F) * progress;
}

float CScrollOverview::specialTrayShownWidth() const {
    if (specialTrayAutohide() && trayReveal && trayReveal->goal() < 0.5F)
        return 0.F;
    const auto MONITOR = pMonitor.lock();
    if (!MONITOR || !ScrollOverview::Config::getValue<bool>("plugin:gridoverview:special_tray", MONITOR))
        return 0.F;
    const auto CARDS = specialTrayCards();
    if (!CARDS.empty())
        return sc<float>(CARDS.front().box.w) / std::max(MONITOR->m_scale, 0.01F) + TRAY_MARGIN;
    // no cards left: still keep the column for the lone "+"
    return sc<float>(ScrollOverview::Config::getValue<int>("plugin:gridoverview:special_tray_width", MONITOR)) + TRAY_MARGIN;
}

// a hidden (autohide) tray lies over the grid when shown instead of keeping space free
float CScrollOverview::specialTrayReserve() const {
    return specialTrayAutohide() ? 0.F : specialTrayShownWidth();
}

// autohide: shown when the pointer touches the right edge, hidden once it leaves the tray. While a window is dragged
// it comes up as soon as the pointer enters the edge scrolling zone; pushing on to the very edge scrolls further
// (updateDragEdgeScroll)
void CScrollOverview::updateSpecialTrayReveal(const Vector2D& pointLocal) {
    const auto MONITOR = pMonitor.lock();
    if (!MONITOR || !trayReveal || closing || !specialTrayAutohide())
        return;
    if (!ScrollOverview::Config::getValue<bool>("plugin:gridoverview:special_tray", MONITOR))
        return;

    const auto CARDS = specialTrayCards();
    const auto ADD   = specialTrayAddBox(CARDS);
    if (ADD.empty())
        return;

    const auto   SIZE     = MONITOR->m_size * MONITOR->m_scale;
    const double RIGHT    = SIZE.x;
    const double KEEP     = RIGHT - (ADD.w + 2.0 * TRAY_MARGIN * MONITOR->m_scale);
    const bool   DRAGGING = dragActiveWindow || (g_pointerGrabOverview && g_pointerGrabOverview->dragActiveWindow);
    // dragging: the edge scrolling zone of updateDragEdgeScroll()
    const double EDGE  = DRAGGING ? RIGHT - std::min(SIZE.x, SIZE.y) * 0.04 : RIGHT - 2.0 * MONITOR->m_scale;
    const bool   SHOWN = trayReveal->goal() > 0.5F;
    const bool   SHOW  = isOverviewPointerOnMonitor(MONITOR) && (pointLocal.x >= EDGE || (SHOWN && pointLocal.x >= KEEP));
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
    const double LEFT   = ADD.x - MARGIN;
    const double TOP    = (CARDS.empty() ? ADD.y : CARDS.front().box.y) - MARGIN;
    return CBox{LEFT, TOP, MONITOR->m_size.x * MONITOR->m_scale - LEFT, ADD.y + ADD.h + MARGIN - TOP};
}

bool CScrollOverview::specialTrayBandAt(const Vector2D& pointLocal) const {
    const auto BAND = specialTrayBandBox();
    return !BAND.empty() && BAND.containsPoint(pointLocal);
}

// the "+" box below the last card (or alone when every scratchpad was closed): drop / click for a new one
CBox CScrollOverview::specialTrayAddBox(const std::vector<STrayCard>& cards) const {
    const auto MONITOR = pMonitor.lock();
    if (!MONITOR || !ScrollOverview::Config::getValue<bool>("plugin:gridoverview:special_tray", MONITOR))
        return {};

    if (!cards.empty()) {
        const auto& LAST = cards.back().box;
        return CBox{LAST.x, LAST.y + LAST.h + (specialTrayLabelBand() + TRAY_GAP) * MONITOR->m_scale, LAST.w, TRAY_ADD_HEIGHT * MONITOR->m_scale}.round();
    }

    // no cards: a lone "+" in the tray column, vertically centred
    const auto  SIZE   = MONITOR->m_size;
    const float TOP    = gridWorkAreaTop(MONITOR) + TRAY_MARGIN;
    const float AVAILH = sc<float>(SIZE.y) - TOP - TRAY_MARGIN;
    float       width  = sc<float>(ScrollOverview::Config::getValue<int>("plugin:gridoverview:special_tray_width", MONITOR));
    const float HIDDEN = specialTrayAutohide() && trayReveal ? (1.F - trayReveal->value()) * (width + 2.F * TRAY_MARGIN) : 0.F;
    const float X      = sc<float>(SIZE.x) - TRAY_MARGIN - width + HIDDEN;
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
        if (CARDS[i].box.containsPoint(pointLocal))
            return i;
    }
    return std::nullopt;
}

// name pill under the card: pencil (rename) + optional text + grip (reorder)
CScrollOverview::STrayPill CScrollOverview::specialTrayPill(const STrayCard& card, const Vector2D& textSize) const {
    const auto MONITOR = pMonitor.lock();
    STrayPill  out;
    if (!MONITOR)
        return out;

    const bool  NAMED   = !isAnonymousTrayName(card.name) && textSize.x > 0;
    const float UNIT    = MONITOR->m_scale;
    const float PAD     = 6.F * UNIT;
    const float GRIP    = TRAY_GRIP_WIDTH * UNIT;
    const float PENCIL  = TRAY_PENCIL_WIDTH * UNIT;
    const float GAP     = TRAY_GRIP_GAP * UNIT;
    const float TEXTH   = NAMED ? sc<float>(textSize.y) : 0.F;
    const float TEXTW   = NAMED ? sc<float>(textSize.x) : 0.F;
    const float HEIGHT  = std::max(TRAY_PILL * UNIT, TEXTH + 2.F * PAD);
    const float WIDTH   = PAD + PAD + PENCIL + GAP + (NAMED ? TEXTW + GAP : 0.F) + GRIP + PAD + PAD;
    const float CENTERX = card.box.middle().x;
    const float CENTERY = card.box.y + card.box.h + specialTrayLabelOffset() * UNIT;
    out.pill            = CBox{CENTERX - WIDTH / 2.F, CENTERY - HEIGHT / 2.F, WIDTH, HEIGHT}.round();
    out.pencil          = CBox{out.pill.x + PAD, out.pill.y, PENCIL + PAD, out.pill.h}.round();
    if (NAMED)
        out.text = CBox{out.pencil.x + out.pencil.w + GAP - PAD, CENTERY - TEXTH / 2.F, TEXTW, TEXTH}.round();
    out.grip = CBox{out.pill.x + out.pill.w - PAD - PAD - GRIP, out.pill.y, GRIP + PAD, out.pill.h}.round();
    return out;
}

// × centred on the card's top-right corner (half inside the card, half outside)
CBox CScrollOverview::specialTrayCloseBox(const STrayCard& card) const {
    const auto MONITOR = pMonitor.lock();
    if (!MONITOR)
        return {};
    const float SIZE = TRAY_CLOSE_SIZE * MONITOR->m_scale;
    return CBox{card.box.x + card.box.w - SIZE / 2.F, card.box.y - SIZE / 2.F, SIZE, SIZE}.round();
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

std::optional<size_t> CScrollOverview::specialTrayCloseAt(const Vector2D& pointLocal) const {
    if (closing || specialTrayRenameBusy || (specialTrayAutohide() && trayReveal && trayReveal->goal() < 0.5F))
        return std::nullopt;

    const auto CARDS = specialTrayCards();
    for (size_t i = 0; i < CARDS.size(); ++i) {
        auto card = CARDS[i];
        if (specialTrayReorderActive && specialTrayReorderFrom == i)
            card.box.y += lastMousePosLocal.y - dragStartMouseLocal.y;
        if (specialTrayCloseBox(card).containsPoint(pointLocal))
            return i;
    }
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

    PHLWORKSPACE workspace;
    for (const auto& w : State::workspaceState()->workspaces()) {
        if (const auto WS = w.lock(); valid(WS) && WS->type() == Workspace::eWorkspaceType::SPECIAL && specialTrayName(WS->addressableName()) == oldName) {
            workspace = WS;
            break;
        }
    }

    if (workspace) {
        // Hyprland specials keep the special: prefix in their addressable name
        (void)Config::Actions::renameWorkspace(workspace, std::string{SPECIAL_PREFIX} + NEW);
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

void CScrollOverview::beginSpecialTrayRename(size_t index) {
    if (specialTrayRenameBusy || closing)
        return;
    const auto CARDS = specialTrayCards();
    if (index >= CARDS.size())
        return;

    const auto OLD = CARDS[index].name;
    auto       cmd = ScrollOverview::Config::getValue<std::string>("plugin:gridoverview:special_tray_rename_cmd", pMonitor.lock());
    if (cmd.empty())
        cmd = "tofi --require-match=false --prompt-text=\"Name \" --width=360 --height=48 --anchor=center";
    // $NAME -> current name (for prompts that show or pre-fill it)
    for (size_t pos = 0; (pos = cmd.find("$NAME", pos)) != std::string::npos;)
        cmd.replace(pos, 5, OLD);

    specialTrayRenameBusy = true;
    damage();

    // run off the compositor thread so the overlay can take keyboard focus; apply on the main loop
    std::thread([OLD, cmd] {
        std::string result;
        if (FILE* pipe = popen(cmd.c_str(), "r")) {
            char buf[256];
            while (fgets(buf, sizeof(buf), pipe))
                result += buf;
            pclose(pipe);
        }
        while (!result.empty() && (result.back() == '\n' || result.back() == '\r'))
            result.pop_back();

        if (!g_pEventLoopManager)
            return;
        g_pEventLoopManager->doLater([OLD, result] {
            if (!result.empty())
                applySpecialTrayRename(OLD, result);
            const auto OVERVIEW = activeScrollOverview();
            if (auto* tray = OVERVIEW ? dynamic_cast<CScrollOverview*>(OVERVIEW.get()) : nullptr) {
                tray->specialTrayRenameBusy = false;
                tray->rebuildPending        = true;
                tray->damage();
            }
        });
    }).detach();
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

    size_t best = CARDS.size();
    double bestDist = std::numeric_limits<double>::max();
    for (size_t slot = 0; slot <= CARDS.size(); ++slot) {
        double y = 0;
        if (slot == 0)
            y = CARDS.front().box.y;
        else if (slot == CARDS.size())
            y = CARDS.back().box.y + CARDS.back().box.h;
        else
            y = (CARDS[slot - 1].box.y + CARDS[slot - 1].box.h + CARDS[slot].box.y) / 2.0;
        const double DIST = std::abs(pointLocal.y - y);
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

// app icons on the bottom edge of a card, half over it, side by side from the right edge (the tray is at the right
// screen edge), like macOS Stage Manager: one per app, the topmost window's on the right, as many as fit (the name
// sits below them, see specialTrayLabelOffset)
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

    const float ICON  = ScrollOverview::Config::getValue<int>("plugin:gridoverview:special_tray_icon_size", MONITOR) * MONITOR->m_scale;
    const float PLATE = ICON * TRAY_ICON_PLATE;
    const float GAP   = 6.F * MONITOR->m_scale;
    const float INSET = 10.F * MONITOR->m_scale;
    const int   FIT   = std::max(1, sc<int>((card.box.w - 2.F * INSET + GAP) / (PLATE + GAP)));
    const auto  THEME = ScrollOverview::Config::getValue<std::string>("plugin:gridoverview:icon_theme", MONITOR);

    std::vector<std::string> apps;
    std::vector<STrayIcon>   icons;
    float                    right  = card.box.x + card.box.w - INSET;
    const float              CENTER = card.box.y + card.box.h;
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

        const float    MIDX = right - PLATE / 2.F;
        const Vector2D SIZE = LETTER ? texture->m_size : Vector2D{ICON, ICON};
        icons.push_back({
            .window  = *it,
            .texture = texture,
            .plate   = CBox{MIDX - PLATE / 2.F, CENTER - PLATE / 2.F, PLATE, PLATE}.round(),
            .icon    = CBox{MIDX - SIZE.x / 2.0, CENTER - SIZE.y / 2.0, SIZE.x, SIZE.y}.round(),
        });
        right -= PLATE + GAP;
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

    // one dock behind all cards and the "+" (or the "+" alone when every scratchpad was closed)
    const float PAD  = TRAY_DOCK_PAD * monitor->m_scale;
    const auto  TOPY = CARDS.empty() ? ADD.y : CARDS.front().box.y;
    const auto  DOCK = CBox{ADD.x - PAD, TOPY - PAD, ADD.w + 2.F * PAD, ADD.y + ADD.h + PAD - (TOPY - PAD)}.round();
    const bool  BLUR = ScrollOverview::Config::getValue<bool>("plugin:gridoverview:special_tray_blur", monitor) && ScrollOverview::Config::getValue<int>("decoration:blur:enabled");
    CRectPassElement::SRectData dock;
    dock.box   = DOCK;
    dock.color = CHyprColor{0.F, 0.F, 0.F, (BLUR ? 0.4F : 0.5F) * ALPHA};
    dock.round = sc<int>(std::round(TRAY_DOCK_ROUNDING * monitor->m_scale));
    dock.blur  = BLUR;
    dock.blurA = ALPHA;
    Render::IHyprRenderer::currentPass(g_pHyprRenderer->context()).add(makeUnique<CRectPassElement>(dock));

    const bool  REORDERING = specialTrayReorderActive && specialTrayReorderFrom;
    const double REORDERY  = REORDERING ? lastMousePosLocal.y - dragStartMouseLocal.y : 0.0;

    for (size_t i = 0; i < CARDS.size(); ++i) {
        auto        CARD     = CARDS[i];
        const bool  MOVING   = REORDERING && *specialTrayReorderFrom == i;
        if (MOVING)
            CARD.box.y += REORDERY;
        const bool  TARGET   = !REORDERING && HOVERED == i && (!DRAGGED || DRAGGED->m_workspace != CARD.workspace);
        const float SCALE    = specialTrayRenderScale(CARD);
        const auto  ROUNDING = sc<int>(std::round(ScrollOverview::Config::getValue<int>("decoration:rounding") * monitor->m_scale * SCALE));
        const float CARDALPHA = ALPHA * (MOVING ? 0.85F : REORDERING ? 0.55F : 1.F);

        auto windows = specialTrayWindows(CARD);
        std::erase(windows, DRAGGED);

        if (TARGET || MOVING) {
            CRectPassElement::SRectData card;
            card.box   = CARD.box;
            card.color = CHyprColor{1.F, 1.F, 1.F, (MOVING ? 0.12F : 0.08F) * CARDALPHA};
            card.round = ROUNDING;
            Render::IHyprRenderer::currentPass(g_pHyprRenderer->context()).add(makeUnique<CRectPassElement>(card));
        }

        if (windows.empty())
            renderEmptyWorkspaceMarker(monitor, CARD.box, scale->value(), CARDALPHA, TARGET || MOVING);
        else {
            const auto WORKSPACE         = CARD.workspace;
            const bool WASVISIBLE        = WORKSPACE->visible();
            const bool WASFORCERENDERING = WORKSPACE->m_forceRendering;
            // a scratchpad that was just left fades out (workspace alpha -> 0), also after the overview opened.
            // Window contents follow the workspace alpha, borders and shadows the render alpha: both get the
            // tray's own fade. Only the drawn value is replaced, Hyprland's fade keeps running.
            const float WASALPHA = WORKSPACE->m_alpha ? WORKSPACE->m_alpha->value() : 1.F;
            WORKSPACE->setVisible(true);
            WORKSPACE->m_forceRendering = true;
            if (WORKSPACE->m_alpha)
                WORKSPACE->m_alpha->value() = CARDALPHA;
            auto restoreWorkspaceState = Hyprutils::Utils::CScopeGuard([WORKSPACE, WASVISIBLE, WASFORCERENDERING, WASALPHA] {
                WORKSPACE->setVisible(WASVISIBLE);
                WORKSPACE->m_forceRendering = WASFORCERENDERING;
                if (WORKSPACE->m_alpha)
                    WORKSPACE->m_alpha->value() = WASALPHA;
            });

            for (const auto& window : windows)
                renderWindowLive(monitor, window, specialTrayWindowBox(CARD, window), SCALE, now, &CARD.box, false, CARDALPHA, false);

            if (TARGET || MOVING) {
                auto color = ScrollOverview::Config::getEmptyColor(monitor);
                color.a    = std::min(1.F, sc<float>(color.a) * 2.F) * CARDALPHA;
                Config::CGradientValueData gradient;
                gradient.m_colors = {color};
                gradient.updateColorsOk();
                Render::IHyprRenderer::currentPass(g_pHyprRenderer->context()).add(makeUnique<CBorderPassElement>(CBorderPassElement::SBorderData{
                    .box        = CARD.box,
                    .grad1      = gradient,
                    .round      = ROUNDING,
                    .borderSize = std::max(1, sc<int>(std::round(2.F * monitor->m_scale))),
                }));
            }

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

        // × centred on the card's top-right corner (after windows so it stays on top)
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

    // drop line while reordering
    if (REORDERING && specialTrayReorderInsert && !CARDS.empty()) {
        const auto SLOT = *specialTrayReorderInsert;
        double     y    = 0;
        if (SLOT == 0)
            y = CARDS.front().box.y - 4.F * monitor->m_scale;
        else if (SLOT >= CARDS.size())
            y = CARDS.back().box.y + CARDS.back().box.h + 4.F * monitor->m_scale;
        else
            y = (CARDS[SLOT - 1].box.y + CARDS[SLOT - 1].box.h + CARDS[SLOT].box.y) / 2.0;
        auto color = ScrollOverview::Config::getEmptyColor(monitor);
        color.a    = std::min(1.F, sc<float>(color.a) * 2.F) * ALPHA;
        CRectPassElement::SRectData line;
        line.box   = CBox{CARDS.front().box.x, y - 1.5F * monitor->m_scale, CARDS.front().box.w, 3.F * monitor->m_scale}.round();
        line.color = color;
        line.round = sc<int>(line.box.h / 2.0);
        Render::IHyprRenderer::currentPass(g_pHyprRenderer->context()).add(makeUnique<CRectPassElement>(line));
    }

    // "+" box: new scratchpad (drop a window on it, or click it); also the sole tray content when every card was closed
    const bool ADDHOVERED = isOverviewPointerOnMonitor(monitor) && specialTrayAddAt(getOverviewMousePosLocal(monitor));
    const bool ADDTARGET  = ADDHOVERED && DRAGGED;
    const auto UNIT       = monitor->m_scale;

    if (ADDHOVERED) {
        CRectPassElement::SRectData addBackground;
        addBackground.box   = ADD;
        addBackground.color = CHyprColor{1.F, 1.F, 1.F, 0.08F * ALPHA};
        addBackground.round = sc<int>(ADD.h / 2.0);
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
        .round      = sc<int>(ADD.h / 2.0),
        .borderSize = std::max(1, sc<int>(std::round((ADDTARGET ? 3.F : 2.F) * UNIT))),
    }));

    const float PLUS = 22.F * UNIT;
    GridIcons::drawUiIcon("plus", CBox{ADD.middle().x - PLUS / 2.F, ADD.middle().y - PLUS / 2.F, PLUS, PLUS}, 1.F, color);
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
        const auto PILL = specialTrayPill(card, size);
        auto       box  = global(card.box);
        box.insert(1, std::format("\"name\": {}, \"anonymous\": {}, \"exists\": {}, \"icons\": [{}], \"close\": {}, \"pencil\": {}, \"grip\": {}, ",
                                  jsonString(card.name), NAMED ? "false" : "true", card.workspace ? "true" : "false", icons, global(specialTrayCloseBox(card)),
                                  global(PILL.pencil), global(PILL.grip)));
        cards += box;
    }

    const auto ADD   = specialTrayAddBox(CARDS);
    const bool SHOWN = !ADD.empty() && !(specialTrayAutohide() && trayReveal && trayReveal->goal() < 0.5F);
    return std::format("{{\"open\": {}, \"monitor\": {}, \"shown\": {}, \"cards\": [{}], \"add\": {}}}", closing ? "false" : "true", jsonString(MONITOR->m_name),
                       SHOWN ? "true" : "false", cards, ADD.empty() ? "null" : global(ADD));
}

int trayLayoutLua(lua_State* L) {
    const auto  OVERVIEW = g_pInputManager ? scrollOverviewAt(g_pInputManager->getMouseCoordsInternal()) : SP<IOverview>{};
    const auto* TRAY     = OVERVIEW ? dynamic_cast<CScrollOverview*>(OVERVIEW.get()) : nullptr;
    const auto  JSON     = TRAY ? TRAY->specialTrayLayoutJson() : std::string{"{\"open\": false}"};
    lua_pushstring(L, JSON.c_str());
    return 1;
}
