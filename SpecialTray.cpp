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
#include <sstream>
#include <fstream>
#include <filesystem>
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
static constexpr float       TRAY_GRIP_WIDTH = 14.F; // grip on the right of the name pill
static constexpr float       TRAY_GRIP_GAP   = 8.F;  // between the name and the grip
static constexpr std::string_view SPECIAL_PREFIX = "special:";

static std::string specialTrayName(std::string name) {
    const auto FIRST = name.find_first_not_of(" \t");
    const auto LAST  = name.find_last_not_of(" \t");
    name             = FIRST == std::string::npos ? "" : name.substr(FIRST, LAST - FIRST + 1);
    if (name.starts_with(SPECIAL_PREFIX))
        name = name.substr(SPECIAL_PREFIX.size());
    return name;
}

// remembered card order (comma list); special_workspaces still decides which empty cards always appear
static std::vector<std::string> g_trayOrder;
static bool                     g_trayOrderLoaded = false;

static std::filesystem::path trayOrderPath() {
    if (const char* state = std::getenv("XDG_STATE_HOME"); state && *state)
        return std::filesystem::path{state} / "gridoverview" / "tray_order";
    const char* home = std::getenv("HOME");
    return std::filesystem::path{home && *home ? home : "."} / ".local" / "state" / "gridoverview" / "tray_order";
}

static void loadTrayOrder() {
    if (g_trayOrderLoaded)
        return;
    g_trayOrderLoaded = true;
    g_trayOrder.clear();
    std::ifstream file{trayOrderPath()};
    if (!file)
        return;
    std::string line;
    std::getline(file, line);
    std::stringstream ss{line};
    for (std::string entry; std::getline(ss, entry, ',');) {
        const auto NAME = specialTrayName(entry);
        if (!NAME.empty() && std::ranges::find(g_trayOrder, NAME) == g_trayOrder.end())
            g_trayOrder.push_back(NAME);
    }
}

static void saveTrayOrder() {
    const auto PATH = trayOrderPath();
    std::error_code ec;
    std::filesystem::create_directories(PATH.parent_path(), ec);
    std::ofstream file{PATH, std::ios::trunc};
    if (!file)
        return;
    for (size_t i = 0; i < g_trayOrder.size(); ++i) {
        if (i)
            file << ',';
        file << g_trayOrder[i];
    }
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

    std::stringstream configured(ScrollOverview::Config::getValue<std::string>("plugin:gridoverview:special_workspaces", MONITOR));
    for (std::string entry; std::getline(configured, entry, ',');) {
        const auto NAME = specialTrayName(entry);
        if (!NAME.empty() && std::ranges::none_of(cards, [&NAME](const auto& card) { return card.name == NAME; }))
            cards.push_back({.name = NAME});
    }

    std::vector<STrayCard> others;
    for (const auto& w : State::workspaceState()->workspaces()) {
        const auto WORKSPACE = w.lock();
        if (!valid(WORKSPACE) || WORKSPACE->type() != Workspace::eWorkspaceType::SPECIAL)
            continue;

        const auto NAME = specialTrayName(WORKSPACE->addressableName());
        const auto IT   = std::ranges::find_if(cards, [&NAME](const auto& card) { return card.name == NAME; });
        if (IT != cards.end())
            IT->workspace = WORKSPACE;
        else
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
    const auto             CARDS   = specialTrayCards();
    if (!MONITOR || CARDS.empty() || closing)
        return 1.F;

    const double X        = (cursorGlobal.x - MONITOR->m_position.x) * MONITOR->m_scale;
    const double START    = CARDS.front().box.x - RANGE * MONITOR->m_scale;
    float        progress = std::clamp(sc<float>((X - START) / (CARDS.front().box.x - START)), 0.F, 1.F);
    progress              = progress * progress * (3.F - 2.F * progress);
    if (specialTrayAutohide() && trayReveal)
        progress *= trayReveal->value();

    const float CARDSCALE = specialTrayRenderScale(CARDS.front()) / std::max(gridScale, 0.01F);
    return 1.F + (CARDSCALE - 1.F) * progress;
}

float CScrollOverview::specialTrayShownWidth() const {
    if (specialTrayAutohide() && trayReveal && trayReveal->goal() < 0.5F)
        return 0.F;
    const auto MONITOR = pMonitor.lock();
    const auto CARDS   = specialTrayCards();
    if (!MONITOR || CARDS.empty())
        return 0.F;
    return sc<float>(CARDS.front().box.w) / std::max(MONITOR->m_scale, 0.01F) + TRAY_MARGIN;
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

    const auto CARDS = specialTrayCards();
    if (CARDS.empty())
        return;

    const auto   SIZE     = MONITOR->m_size * MONITOR->m_scale;
    const double RIGHT    = SIZE.x;
    const double KEEP     = RIGHT - (CARDS.front().box.w + 2.0 * TRAY_MARGIN * MONITOR->m_scale);
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
    if (CARDS.empty())
        return {};

    const double MARGIN = TRAY_MARGIN * MONITOR->m_scale;
    const double LEFT   = CARDS.front().box.x - MARGIN;
    const double TOP    = CARDS.front().box.y - MARGIN;
    const auto   ADD    = specialTrayAddBox(CARDS);
    return CBox{LEFT, TOP, MONITOR->m_size.x * MONITOR->m_scale - LEFT, ADD.y + ADD.h + MARGIN - TOP};
}

bool CScrollOverview::specialTrayBandAt(const Vector2D& pointLocal) const {
    const auto BAND = specialTrayBandBox();
    return !BAND.empty() && BAND.containsPoint(pointLocal);
}

// the "+" box below the last card: drop a window there for a new scratchpad named after its app, or click it
CBox CScrollOverview::specialTrayAddBox(const std::vector<STrayCard>& cards) const {
    const auto MONITOR = pMonitor.lock();
    if (!MONITOR || cards.empty())
        return {};
    const auto& LAST = cards.back().box;
    return CBox{LAST.x, LAST.y + LAST.h + (specialTrayLabelBand() + TRAY_GAP) * MONITOR->m_scale, LAST.w, TRAY_ADD_HEIGHT * MONITOR->m_scale}.round();
}

bool CScrollOverview::specialTrayAddAt(const Vector2D& pointLocal) const {
    if (closing || (specialTrayAutohide() && trayReveal && trayReveal->goal() < 0.5F))
        return false;
    const auto ADD = specialTrayAddBox(specialTrayCards());
    return !ADD.empty() && ADD.containsPoint(pointLocal);
}

// a name not taken by an existing or configured scratchpad: the app of the dropped window ("org.gnome.Nautilus" ->
// "nautilus"), else "scratch"; "-2", "-3", ... when taken
std::string CScrollOverview::newSpecialWorkspaceName(const PHLWINDOW& window) const {
    std::string base;
    if (window) {
        auto app = window->metadata().appID();
        if (const auto DOT = app.find_last_of('.'); DOT != std::string::npos && DOT + 1 < app.size())
            app = app.substr(DOT + 1);
        for (const char C : app) {
            const char LOWER = sc<char>(std::tolower(sc<unsigned char>(C)));
            if (std::isalnum(sc<unsigned char>(LOWER)) || LOWER == '_' || LOWER == '-')
                base += LOWER;
            else if (!base.empty() && base.back() != '-')
                base += '-';
        }
        while (!base.empty() && base.back() == '-')
            base.pop_back();
    }
    if (base.empty())
        base = "scratch";

    std::vector<std::string> taken;
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

// name pill under the card, extended to the right for the reorder grip (room for a later pencil / menu)
CScrollOverview::STrayPill CScrollOverview::specialTrayPill(const STrayCard& card, const Vector2D& textSize) const {
    const auto MONITOR = pMonitor.lock();
    STrayPill  out;
    if (!MONITOR || textSize.x <= 0)
        return out;

    const float UNIT    = MONITOR->m_scale;
    const float PAD     = 6.F * UNIT;
    const float GRIP    = TRAY_GRIP_WIDTH * UNIT;
    const float GAP     = TRAY_GRIP_GAP * UNIT;
    const float HEIGHT  = std::max(TRAY_PILL * UNIT, sc<float>(textSize.y) + 2.F * PAD);
    const float WIDTH   = textSize.x + GAP + GRIP + 2.F * PAD + 2.F * PAD; // extra side pad like the old pills
    const float CENTERX = card.box.middle().x;
    const float CENTERY = card.box.y + card.box.h + specialTrayLabelOffset() * UNIT;
    out.pill            = CBox{CENTERX - WIDTH / 2.F, CENTERY - HEIGHT / 2.F, WIDTH, HEIGHT}.round();
    out.text            = CBox{out.pill.x + PAD + PAD, CENTERY - textSize.y / 2.F, textSize.x, textSize.y}.round();
    out.grip            = CBox{out.pill.x + out.pill.w - PAD - PAD - GRIP, out.pill.y, GRIP + PAD, out.pill.h}.round();
    return out;
}

std::optional<size_t> CScrollOverview::specialTrayGripAt(const Vector2D& pointLocal) const {
    if (closing || (specialTrayAutohide() && trayReveal && trayReveal->goal() < 0.5F))
        return std::nullopt;

    const auto MONITOR = pMonitor.lock();
    if (!MONITOR)
        return std::nullopt;

    const int  TEXTSIZE = std::max(1, sc<int>(std::round(TRAY_LABEL_SIZE * MONITOR->m_scale)));
    const auto CARDS    = specialTrayCards();
    for (size_t i = 0; i < CARDS.size(); ++i) {
        const auto KEY     = CARDS[i].name + "\n" + std::to_string(TEXTSIZE) + "\ntray";
        const auto TEXTURE = contextLabelTextures.contains(KEY) ? contextLabelTextures.at(KEY) : SP<Render::ITexture>{};
        const auto SIZE    = TEXTURE && TEXTURE->m_size.x > 0 ? TEXTURE->m_size : Vector2D{TRAY_LABEL_SIZE * 4.0 * MONITOR->m_scale, TRAY_LABEL_SIZE * MONITOR->m_scale};
        if (specialTrayPill(CARDS[i], SIZE).grip.containsPoint(pointLocal))
            return i;
    }
    return std::nullopt;
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
    const auto CARDS = specialTrayCards();
    if (CARDS.empty())
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
    const auto  TEXTCOLOR  = CHyprColor{1.F, 1.F, 1.F, 0.9F};
    const bool  ICONS      = ScrollOverview::Config::getValue<bool>("plugin:gridoverview:special_tray_icons", monitor);

    // one dock behind all cards and the "+": the windows sit on it directly, without card backgrounds or shadows
    const auto  ADD  = specialTrayAddBox(CARDS);
    const float PAD  = TRAY_DOCK_PAD * monitor->m_scale;
    const auto& TOP  = CARDS.front().box;
    const auto  DOCK = CBox{TOP.x - PAD, TOP.y - PAD, TOP.w + 2.F * PAD, ADD.y + ADD.h + PAD - (TOP.y - PAD)}.round();
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

        const auto KEY     = CARD.name + "\n" + std::to_string(TEXTSIZE) + "\ntray";
        auto&      texture = contextLabelTextures[KEY];
        if (!texture)
            texture = g_pHyprRenderer->renderText(CARD.name, TEXTCOLOR, TEXTSIZE, false, "", 0, 600);
        if (!texture || texture->m_size.x <= 0)
            continue;

        auto       pillGeom = specialTrayPill(CARD, texture->m_size);
        if (MOVING) {
            // CARD.box was already shifted; pill was computed from that
        }
        auto pillColor = BACKGROUND.a > 0.F ? BACKGROUND : CHyprColor{0.F, 0.F, 0.F, 0.45F};
        CRectPassElement::SRectData pill;
        pill.box   = pillGeom.pill;
        pill.color = pillColor.modifyA(pillColor.a * CARDALPHA);
        pill.round = sc<int>(pill.box.h / 2.0);
        Render::IHyprRenderer::currentPass(g_pHyprRenderer->context()).add(makeUnique<CRectPassElement>(pill));

        CTexPassElement::SRenderData data;
        data.tex = texture;
        data.box = pillGeom.text;
        data.a   = CARDALPHA * (CARD.workspace ? 1.F : 0.6F);
        Render::IHyprRenderer::currentPass(g_pHyprRenderer->context()).add(makeUnique<CTexPassElement>(std::move(data)));

        // six-dot grip on the right of the pill
        const float DOT = std::max(1.5F, 2.2F * monitor->m_scale);
        const auto  GRIPCOLOR = CHyprColor{1.F, 1.F, 1.F, 0.55F * CARDALPHA};
        const float GX = pillGeom.grip.middle().x;
        const float GY = pillGeom.grip.middle().y;
        const float DX = 3.2F * monitor->m_scale;
        const float DY = 4.2F * monitor->m_scale;
        for (int col : {-1, 1}) {
            for (int row : {-1, 0, 1}) {
                CRectPassElement::SRectData dot;
                dot.box   = CBox{GX + col * DX - DOT / 2.F, GY + row * DY - DOT / 2.F, DOT, DOT}.round();
                dot.color = GRIPCOLOR;
                dot.round = sc<int>(DOT);
                Render::IHyprRenderer::currentPass(g_pHyprRenderer->context()).add(makeUnique<CRectPassElement>(dot));
            }
        }
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

    // "+" box: new scratchpad (drop a window on it, or click it); brighter under the pointer or a dragged window
    if (ADD.empty())
        return;

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

    const float ARM   = 11.F * UNIT;
    const float THICK = 3.F * UNIT;
    const auto  MID   = ADD.middle();
    for (const auto& PART : {CBox{MID.x - ARM, MID.y - THICK / 2.F, ARM * 2.F, THICK}, CBox{MID.x - THICK / 2.F, MID.y - ARM, THICK, ARM * 2.F}}) {
        CRectPassElement::SRectData plus;
        plus.box   = PART.copy().round();
        plus.color = color;
        plus.round = sc<int>(THICK / 2.F);
        Render::IHyprRenderer::currentPass(g_pHyprRenderer->context()).add(makeUnique<CRectPassElement>(plus));
    }
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
        const auto KEY      = card.name + "\n" + std::to_string(TEXTSIZE) + "\ntray";
        const auto TEXTURE  = contextLabelTextures.contains(KEY) ? contextLabelTextures.at(KEY) : SP<Render::ITexture>{};
        const auto SIZE     = TEXTURE && TEXTURE->m_size.x > 0 ? TEXTURE->m_size : Vector2D{TRAY_LABEL_SIZE * 4.0 * MONITOR->m_scale, TRAY_LABEL_SIZE * MONITOR->m_scale};
        const auto PILL     = specialTrayPill(card, SIZE);
        auto       box      = global(card.box);
        box.insert(1, std::format("\"name\": {}, \"exists\": {}, \"icons\": [{}], \"grip\": {}, ", jsonString(card.name), card.workspace ? "true" : "false", icons, global(PILL.grip)));
        cards += box;
    }

    const bool SHOWN = !CARDS.empty() && !(specialTrayAutohide() && trayReveal && trayReveal->goal() < 0.5F);
    return std::format("{{\"open\": {}, \"monitor\": {}, \"shown\": {}, \"cards\": [{}], \"add\": {}}}", closing ? "false" : "true", jsonString(MONITOR->m_name),
                       SHOWN ? "true" : "false", cards, CARDS.empty() ? "null" : global(specialTrayAddBox(CARDS)));
}

int trayLayoutLua(lua_State* L) {
    const auto  OVERVIEW = g_pInputManager ? scrollOverviewAt(g_pInputManager->getMouseCoordsInternal()) : SP<IOverview>{};
    const auto* TRAY     = OVERVIEW ? dynamic_cast<CScrollOverview*>(OVERVIEW.get()) : nullptr;
    const auto  JSON     = TRAY ? TRAY->specialTrayLayoutJson() : std::string{"{\"open\": false}"};
    lua_pushstring(L, JSON.c_str());
    return 1;
}
