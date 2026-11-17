// Extracted from scrollOverview.cpp for readability; methods remain on CScrollOverview.
#include "scrollOverview.hpp"
#include "ScrollOverviewInternal.hpp"
#include "Icons.hpp"
#include "Window.hpp"
#include "OverviewRender.hpp"

#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprland/src/render/pass/RectPassElement.hpp>
#include <hyprland/src/render/pass/TexPassElement.hpp>
#include <hyprland/src/render/pass/BorderPassElement.hpp>
#include <hyprland/src/desktop/state/FocusState.hpp>
#include <hyprland/src/desktop/view/window/Window.hpp>
#include <hyprland/src/desktop/view/window/WindowGroupMembership.hpp>
#include <hyprland/src/desktop/view/Group.hpp>
#include <hyprland/src/config/shared/actions/ConfigActions.hpp>
#include <hyprland/src/managers/input/InputManager.hpp>
#include <hyprland/src/managers/fullscreen/FullscreenController.hpp>

#include <algorithm>
#include <cmath>
#include <unordered_set>

namespace {

struct SChromeButtonsBounds {
    float left = 0.F, top = 0.F, right = 0.F, bot = 0.F;
};

template <typename Buttons>
SChromeButtonsBounds chromeButtonsBounds(const Buttons& buttons) {
    SChromeButtonsBounds b;
    if (buttons.empty())
        return b;
    b.left = b.right = sc<float>(buttons.front().box.x);
    b.top = b.bot = sc<float>(buttons.front().box.y);
    for (const auto& btn : buttons) {
        b.left  = std::min(b.left, sc<float>(btn.box.x));
        b.top   = std::min(b.top, sc<float>(btn.box.y));
        b.right = std::max(b.right, sc<float>(btn.box.x + btn.box.w));
        b.bot   = std::max(b.bot, sc<float>(btn.box.y + btn.box.h));
    }
    return b;
}

} // namespace

void CScrollOverview::renderWindowIcons(PHLMONITOR monitor, size_t activeIdx, float workspacePitch, float renderScale) {
    if (!ScrollOverview::Config::getValue<bool>("plugin:gridoverview:window_icons", monitor))
        return;

    // fade in with the overview zoom like the card shadows
    const float PROGRESS = std::clamp((1.F - renderScale) / (1.F - ScrollOverview::Config::getScale(monitor)), 0.F, 1.F);
    const float ALPHA    = PROGRESS * PROGRESS * (3.F - 2.F * PROGRESS);
    if (ALPHA <= 0.001F)
        return;

    // fixed on-screen size (not scaled with the overview zoom), but never more than 60 % of a window's short side
    const float SIZE    = ScrollOverview::Config::getValue<int>("plugin:gridoverview:window_icon_size", monitor) * monitor->m_scale;
    auto        plate   = ScrollOverview::Config::getWindowIconPlateColor(monitor);
    plate.a            *= ALPHA;
    const auto  THEME   = ScrollOverview::Config::getValue<std::string>("plugin:gridoverview:icon_theme", monitor);
    const auto  DRAGGED = getOverviewWindowToShow(dragActiveWindow.lock());

    const auto drawIconOnBox = [&](const PHLWINDOW& WINDOW, const CBox& BOX) {
        if (!overviewBoxIntersectsMonitor(BOX, monitor))
            return;

        const auto TEXTURE = GridIcons::iconForAppID(WINDOW->metadata().appID(), THEME);
        if (!TEXTURE)
            return;

        const float ICON  = std::min(SIZE, sc<float>(std::min(BOX.width, BOX.height)) * 0.6F / 1.5F);
        const float PLATE = ICON * 1.5F;
        const auto  MID   = BOX.middle();

        if (plate.a > 0.F) {
            CRectPassElement::SRectData data;
            data.box   = CBox{MID.x - PLATE / 2.F, MID.y - PLATE / 2.F, PLATE, PLATE}.round();
            data.color = plate;
            data.round = sc<int>(PLATE * 0.25F);
            Render::IHyprRenderer::currentPass(g_pHyprRenderer->context()).add(makeUnique<CRectPassElement>(data));
        }

        CTexPassElement::SRenderData data;
        data.tex = TEXTURE;
        data.box = CBox{MID.x - ICON / 2.F, MID.y - ICON / 2.F, ICON, ICON}.round();
        data.a   = ALPHA;
        Render::IHyprRenderer::currentPass(g_pHyprRenderer->context()).add(makeUnique<CTexPassElement>(std::move(data)));
    };

    for (size_t workspaceIdx = 0; workspaceIdx < images.size(); ++workspaceIdx) {
        if (!images[workspaceIdx])
            continue;

        const auto OFFSET = workspaceOverviewOffset(workspaceIdx, activeIdx, workspacePitch);
        for (const auto& windowRef : images[workspaceIdx]->windows) {
            const auto WINDOW = getOverviewWindowToShow(windowRef.lock());
            if (!shouldShowOverviewWindow(WINDOW) || WINDOW == DRAGGED || !windowShowsOverviewIcon(WINDOW) || isPinMorphWindow(WINDOW))
                continue;

            drawIconOnBox(WINDOW, getOverviewWindowBox(WINDOW, monitor, renderScale, viewOffset->value(), OFFSET, layout));
        }
    }
    // pinned icons: renderPinnedWindowOverlays (after the pinned preview itself)
}


CBox CScrollOverview::windowActionChromeHostBox(const PHLWINDOW& window) const {
    const auto MONITOR = pMonitor.lock();
    const auto WINDOW  = getOverviewWindowToShow(window);
    if (!MONITOR || !WINDOW)
        return {};

    if (shouldShowPinnedFloatingOverviewWindow(WINDOW))
        return pinnedRailSlotBox(WINDOW); // chrome anchors to the settled icon, not the morphing live box

    if (!shouldShowOverviewWindow(WINDOW) || !WINDOW->m_workspace)
        return {};

    for (size_t workspaceIdx = 0; workspaceIdx < images.size(); ++workspaceIdx) {
        if (!images[workspaceIdx] || images[workspaceIdx]->pWorkspace != WINDOW->m_workspace)
            continue;
        const auto OFFSET = workspaceOverviewOffset(workspaceIdx, activeWorkspaceIndex(), getWorkspaceRenderedPitch(MONITOR, scale->value(), layout));
        return getOverviewWindowBox(WINDOW, MONITOR, scale->value(), viewOffset->value(), OFFSET, layout);
    }
    return {};
}


CBox CScrollOverview::windowOverviewIconPlateBox(const PHLWINDOW& window) const {
    const auto MONITOR = pMonitor.lock();
    const auto WINDOW  = getOverviewWindowToShow(window);
    if (!MONITOR || !WINDOW)
        return {};

    const auto BOX = windowActionChromeHostBox(WINDOW);
    if (BOX.empty())
        return {};

    // same geometry as renderWindowIcons plate
    const float SIZE  = ScrollOverview::Config::getValue<int>("plugin:gridoverview:window_icon_size", MONITOR) * MONITOR->m_scale;
    const float ICON  = std::min(SIZE, sc<float>(std::min(BOX.width, BOX.height)) * 0.6F / 1.5F);
    const float PLATE = ICON * 1.5F;
    const auto  MID   = BOX.middle();
    return CBox{MID.x - PLATE / 2.F, MID.y - PLATE / 2.F, PLATE, PLATE}.round();
}


bool CScrollOverview::windowIsTopmostAtIconAnchor(const PHLWINDOW& window) const {
    const auto WINDOW = getOverviewWindowToShow(window);
    if (!shouldShowOverviewWindow(WINDOW))
        return false;

    const auto PLATE = windowOverviewIconPlateBox(WINDOW);
    if (PLATE.empty())
        return false;

    // hide as soon as any sample on the plate is owned by another window (clipped plate)
    const Vector2D MID = PLATE.middle();
    const Vector2D SAMPLES[] = {
        MID,
        {PLATE.x + 1.F, PLATE.y + 1.F},
        {PLATE.x + PLATE.w - 2.F, PLATE.y + 1.F},
        {PLATE.x + 1.F, PLATE.y + PLATE.h - 2.F},
        {PLATE.x + PLATE.w - 2.F, PLATE.y + PLATE.h - 2.F},
        {MID.x, PLATE.y + 1.F},
        {MID.x, PLATE.y + PLATE.h - 2.F},
        {PLATE.x + 1.F, MID.y},
        {PLATE.x + PLATE.w - 2.F, MID.y},
    };

    for (const auto& P : SAMPLES) {
        const auto HIT = windowAtOverviewPoint(P);
        if (HIT && HIT != WINDOW)
            return false; // another window already clips the plate
    }
    return windowAtOverviewPoint(MID) == WINDOW;
}


bool CScrollOverview::windowShowsOverviewIcon(const PHLWINDOW& window) const {
    const auto WINDOW = getOverviewWindowToShow(window);
    if (!WINDOW || closing || dragActiveWindow)
        return false;
    if (WINDOW == getOverviewWindowToShow(dragActiveWindow.lock()))
        return false;

    // no hover exception for covered windows — only a free icon plate
    return windowIsTopmostAtIconAnchor(WINDOW);
}


bool CScrollOverview::windowShowsActionChrome(const PHLWINDOW& window) const {
    const auto WINDOW = getOverviewWindowToShow(window);
    if (!WINDOW || closing || dragActiveWindow)
        return false;
    if (!ScrollOverview::Config::getValue<bool>("plugin:gridoverview:window_chrome", pMonitor.lock()))
        return false;

    // pin rail: still hover/focus based (icons don't occlude each other the same way)
    if (shouldShowPinnedFloatingOverviewWindow(WINDOW))
        return closeOnWindow == WINDOW || hoveredChromeWindow.lock() == WINDOW;

    if (closeOnWindow != WINDOW && hoveredChromeWindow.lock() != WINDOW)
        return false;

    // covered windows stay without chrome; topmost + focus/hover only
    return windowIsTopmostAtIconAnchor(WINDOW);
}


void CScrollOverview::updateHoveredChromeWindow() {
    if (closing || dragActiveWindow || !ScrollOverview::Config::getValue<bool>("plugin:gridoverview:window_chrome", pMonitor.lock())) {
        hoveredChromeWindow.reset();
        return;
    }

    const auto MONITOR = pMonitor.lock();
    PHLWINDOW  next    = windowAtOverviewPoint(lastMousePosLocal);

    const auto stillOverChrome = [&](const PHLWINDOW& window) -> bool {
        if (!window || !MONITOR)
            return false;
        const auto BOX     = windowActionChromeHostBox(window);
        const auto BUTTONS = windowActionChromeButtons(window, BOX, MONITOR);
        for (const auto& btn : BUTTONS) {
            if (btn.box.containsPoint(lastMousePosLocal))
                return true;
        }
        // pin rail only: icon + bridge to chrome (never use a normal window's full card — that steals pin hover)
        if (!shouldShowPinnedFloatingOverviewWindow(window) || BOX.empty())
            return false;
        if (BOX.containsPoint(lastMousePosLocal))
            return true;
        if (BUTTONS.empty())
            return false;
        const auto  EXT = chromeButtonsBounds(BUTTONS);
        const float TOP = std::min(sc<float>(BOX.y), EXT.top);
        const float BOT = std::max(sc<float>(BOX.y + BOX.h), EXT.bot);
        const CBox  BRIDGE{BOX.x, TOP, EXT.right - BOX.x, BOT - TOP};
        return BRIDGE.containsPoint(lastMousePosLocal);
    };

    // keep chrome while on its buttons / pin-rail bridge (pinned stillOverChrome never uses a full window card)
    if (const auto PREV = hoveredChromeWindow.lock(); stillOverChrome(PREV))
        next = PREV;
    else if (stillOverChrome(closeOnWindow.lock()))
        next = closeOnWindow.lock();

    if (hoveredChromeWindow.lock() != next) {
        hoveredChromeWindow = next;
        damage();
    }
}


std::vector<CScrollOverview::SWindowChromeButton> CScrollOverview::windowActionChromeButtons(const PHLWINDOW& window, const CBox& windowBox,
                                                                                              PHLMONITOR monitor) const {
    std::vector<SWindowChromeButton> buttons;
    if (!window || !monitor || windowBox.empty())
        return buttons;

    static constexpr int   COLS = 3;
    static constexpr int   ROWS = 2;
    static constexpr float GAP  = 6.F;
    const bool PINNEDHOST = shouldShowPinnedFloatingOverviewWindow(window);
    const bool ICONS = ScrollOverview::Config::getValue<bool>("plugin:gridoverview:window_icons", monitor);
    const float SIZE  = ScrollOverview::Config::getValue<int>("plugin:gridoverview:window_icon_size", monitor) * monitor->m_scale;
    const float ICON  = std::min(SIZE, sc<float>(std::min(windowBox.width, windowBox.height)) * 0.6F / 1.5F);
    const float PLATE = ICON * 1.5F;
    const float GAPPX = GAP * monitor->m_scale;
    // pin rail: chrome opens to the right of the icon at a fixed readable size (not cramped to the plate)
    float BTN = PINNEDHOST ? std::clamp(28.F * monitor->m_scale, 22.F * monitor->m_scale, 34.F * monitor->m_scale) :
                             std::clamp(PLATE * 0.72F, 18.F * monitor->m_scale, 40.F * monitor->m_scale);
    float WIDTH  = COLS * BTN + (COLS - 1) * GAPPX;
    float HEIGHT = ROWS * BTN + (ROWS - 1) * GAPPX;
    if (!PINNEDHOST) {
        const float MAXW = std::max(sc<float>(windowBox.width) * 0.92F, 1.F);
        if (WIDTH > MAXW) {
            BTN = (MAXW - (COLS - 1) * GAPPX) / COLS;
            if (BTN < 14.F * monitor->m_scale)
                return buttons;
            WIDTH  = COLS * BTN + (COLS - 1) * GAPPX;
            HEIGHT = ROWS * BTN + (ROWS - 1) * GAPPX;
        }
    }

    const auto MID = windowBox.middle();
    float      X   = MID.x - WIDTH / 2.F;
    float      Y   = ICONS ? MID.y + PLATE / 2.F + 8.F * monitor->m_scale : MID.y - HEIGHT / 2.F;
    if (PINNEDHOST) {
        // flush against the icon so hover can travel onto the dock without a dead gap
        X = sc<float>(windowBox.x + windowBox.w) + 2.F * monitor->m_scale;
        Y = MID.y - HEIGHT / 2.F;
    }

    const bool FLOATING   = window->isFloating();
    const bool PINNED     = FLOATING && (window->m_state & Desktop::View::WINDOW_STATE_PINNED);
    const bool GROUPED    = !!window->grouping().group();
    const auto FS         = Fullscreen::controller()->getFullscreenModes(window);
    const bool MAXIMIZED  = FS.internal == Fullscreen::FSMODE_MAXIMIZED || FS.client == Fullscreen::FSMODE_MAXIMIZED;
    const bool FULLSCREEN = FS.internal == Fullscreen::FSMODE_FULLSCREEN || FS.client == Fullscreen::FSMODE_FULLSCREEN;

    // row1: layout — row2: specials
    static constexpr EWindowChromeAction ORDER[ROWS * COLS] = {
        EWindowChromeAction::Float, EWindowChromeAction::Maximize, EWindowChromeAction::Fullscreen,
        EWindowChromeAction::Group, EWindowChromeAction::Pin,       EWindowChromeAction::Close,
    };
    for (int i = 0; i < ROWS * COLS; ++i) {
        const int row = i / COLS;
        const int col = i % COLS;
        SWindowChromeButton b;
        b.action = ORDER[i];
        b.box    = CBox{X + col * (BTN + GAPPX), Y + row * (BTN + GAPPX), BTN, BTN}.round();
        b.active = (b.action == EWindowChromeAction::Float && FLOATING) || (b.action == EWindowChromeAction::Maximize && MAXIMIZED) ||
            (b.action == EWindowChromeAction::Fullscreen && FULLSCREEN) || (b.action == EWindowChromeAction::Group && GROUPED) ||
            (b.action == EWindowChromeAction::Pin && PINNED);
        buttons.push_back(b);
    }
    return buttons;
}


void CScrollOverview::ensureOverviewFloatSize(const PHLWINDOW& window) {
    const auto MONITOR = pMonitor.lock();
    if (!window || !MONITOR || !window->isFloating())
        return;

    static constexpr float FRACTION = 0.65F;
    static constexpr float HUGE     = 0.85F;
    const auto             KEY      = overviewWindowKey(window);
    const auto             BOX      = window->geometricBox(Desktop::View::IGeometric::GEOMETRIC_CURRENT);
    Vector2D               size     = BOX.size();

    if (const auto it = rememberedFloatSizes.find(KEY); it != rememberedFloatSizes.end())
        size = it->second;
    else if (size.x >= MONITOR->m_size.x * HUGE && size.y >= MONITOR->m_size.y * HUGE) {
        size = MONITOR->m_size * FRACTION;
        rememberedFloatSizes[KEY] = size;
    } else if (size.x > 1 && size.y > 1)
        rememberedFloatSizes[KEY] = size;
    else {
        size = MONITOR->m_size * FRACTION;
        rememberedFloatSizes[KEY] = size;
    }

    // layout target is reliable while the overview owns input; Actions::center can leave floats at (0,0)
    const Vector2D POS = MONITOR->m_position + (MONITOR->m_size - size) * 0.5;
    if (const auto TARGET = window->layoutTarget()) {
        TARGET->rememberFloatingSize(size);
        TARGET->setPositionGlobal(CBox{POS, size});
        TARGET->warpPositionSize();
        TARGET->damageEntire();
    } else {
        (void)Config::Actions::resize(size, false, window);
        (void)Config::Actions::center(window);
    }
}


void CScrollOverview::applyWindowChromeAction(const PHLWINDOW& window, EWindowChromeAction action) {
    if (!window)
        return;

    const auto focusChromeWindow = [&]() {
        size_t workspaceIdx = viewportCurrentWorkspace;
        for (size_t i = 0; i < images.size(); ++i) {
            if (images[i] && images[i]->pWorkspace == window->m_workspace) {
                workspaceIdx = i;
                break;
            }
        }
        // maximize/fullscreen need a focused window; float/pin/close already work without this
        selectOverviewWindow(window, workspaceIdx, true);
    };

    using A = EWindowChromeAction;
    switch (action) {
        case A::Float: {
            const bool ON = !window->isFloating();
            (void)Config::Actions::floatWindow(ON ? Config::Actions::TOGGLE_ACTION_ENABLE : Config::Actions::TOGGLE_ACTION_DISABLE, window);
            if (ON)
                ensureOverviewFloatSize(window);
            closeOnWindow = window;
            break;
        }
        case A::Maximize: {
            focusChromeWindow();
            const auto FS = Fullscreen::controller()->getFullscreenModes(window);
            const bool ON = !(FS.internal == Fullscreen::FSMODE_MAXIMIZED || FS.client == Fullscreen::FSMODE_MAXIMIZED);
            (void)Config::Actions::fullscreenWindow(ON ? Fullscreen::FSMODE_MAXIMIZED : Fullscreen::FSMODE_NONE, true, window);
            break;
        }
        case A::Fullscreen: {
            focusChromeWindow();
            const auto FS = Fullscreen::controller()->getFullscreenModes(window);
            const bool ON = !(FS.internal == Fullscreen::FSMODE_FULLSCREEN || FS.client == Fullscreen::FSMODE_FULLSCREEN);
            (void)Config::Actions::fullscreenWindow(ON ? Fullscreen::FSMODE_FULLSCREEN : Fullscreen::FSMODE_NONE, true, window);
            break;
        }
        case A::Group: {
            focusChromeWindow();
            (void)Config::Actions::toggleGroup(window);
            closeOnWindow = window;
            break;
        }
        case A::Pin: {
            const bool PINNED = window->isFloating() && (window->m_state & Desktop::View::WINDOW_STATE_PINNED);
            const CBox FROM   = windowActionChromeHostBox(window);
            if (PINNED)
                (void)Config::Actions::pinWindow(Config::Actions::TOGGLE_ACTION_DISABLE, window);
            else {
                if (!window->isFloating()) {
                    (void)Config::Actions::floatWindow(Config::Actions::TOGGLE_ACTION_ENABLE, window);
                    ensureOverviewFloatSize(window);
                }
                (void)Config::Actions::pinWindow(Config::Actions::TOGGLE_ACTION_ENABLE, window);
            }
            beginPinMorph(window, FROM, !PINNED);
            closeOnWindow = window;
            break;
        }
        case A::Close: window->sendClose(); break;
    }
    rebuildPending = true;
    damage();
}


std::optional<std::pair<PHLWINDOW, CScrollOverview::EWindowChromeAction>> CScrollOverview::windowChromeActionAt(const Vector2D& pointLocal) const {
    const auto MONITOR = pMonitor.lock();
    if (!MONITOR || closing || dragActiveWindow)
        return std::nullopt;

    const auto DRAGGED = getOverviewWindowToShow(dragActiveWindow.lock());
    const auto UNDER   = windowAtOverviewPoint(pointLocal);

    const auto hitButtons = [&](const PHLWINDOW& WINDOW) -> std::optional<std::pair<PHLWINDOW, EWindowChromeAction>> {
        if (!windowShowsActionChrome(WINDOW) || WINDOW == DRAGGED)
            return std::nullopt;
        if (!shouldShowOverviewWindow(WINDOW) && !shouldShowPinnedFloatingOverviewWindow(WINDOW))
            return std::nullopt;
        const auto BOX = windowActionChromeHostBox(WINDOW);
        for (const auto& btn : windowActionChromeButtons(WINDOW, BOX, MONITOR)) {
            if (btn.box.containsPoint(pointLocal))
                return std::pair{WINDOW, btn.action};
        }
        return std::nullopt;
    };

    // chrome is drawn above the pinned preview / card — hit its buttons even when a workspace sits under them
    if (const auto HIT = hitButtons(closeOnWindow.lock()))
        return HIT;
    if (const auto HIT = hitButtons(hoveredChromeWindow.lock()))
        return HIT;
    for (const auto& windowRef : pinnedFloatingWindows) {
        if (const auto HIT = hitButtons(getOverviewWindowToShow(windowRef.lock())))
            return HIT;
    }
    if (const auto HIT = hitButtons(UNDER))
        return HIT;
    return std::nullopt;
}


void CScrollOverview::drawWindowActionChromeButtons(const PHLWINDOW& window, const CBox& windowBox, PHLMONITOR monitor, float alpha, const CHyprColor& chrome) {
    const auto BUTTONS = windowActionChromeButtons(window, windowBox, monitor);
    if (BUTTONS.empty())
        return;

    const auto EXT = chromeButtonsBounds(BUTTONS);

    const float PAD        = 5.F * monitor->m_scale;
    const bool  PINNEDHOST = shouldShowPinnedFloatingOverviewWindow(window);
    CRectPassElement::SRectData dock;
    // pin rail: dock starts at the icon edge so chrome reads as attached; cover the full 2×3 grid
    if (PINNEDHOST)
        dock.box = CBox{windowBox.x + windowBox.w - PAD, EXT.top - PAD, EXT.right - (windowBox.x + windowBox.w - PAD) + PAD, EXT.bot - EXT.top + 2.F * PAD}.round();
    else
        dock.box = CBox{EXT.left - PAD, EXT.top - PAD, EXT.right - EXT.left + 2.F * PAD, EXT.bot - EXT.top + 2.F * PAD}.round();
    dock.color = CHyprColor{0.F, 0.F, 0.F, 0.55F * alpha};
    dock.round = sc<int>(std::min(dock.box.w, dock.box.h) * 0.22);
    Render::IHyprRenderer::currentPass(g_pHyprRenderer->context()).add(makeUnique<CRectPassElement>(dock));

    for (const auto& btn : BUTTONS) {
        if (btn.active) {
            CRectPassElement::SRectData plate;
            plate.box   = btn.box.copy().scaleFromCenter(0.92).round();
            plate.color = CHyprColor{1.F, 1.F, 1.F, 0.16F * alpha};
            plate.round = sc<int>(plate.box.h * 0.28F);
            Render::IHyprRenderer::currentPass(g_pHyprRenderer->context()).add(makeUnique<CRectPassElement>(plate));
        }
        auto color = chrome;
        color.a    = 1.F;
        const char* icon = "close";
        switch (btn.action) {
            case EWindowChromeAction::Float: icon = "float"; break;
            case EWindowChromeAction::Maximize: icon = "maximize"; break;
            case EWindowChromeAction::Fullscreen: icon = "fullscreen"; break;
            case EWindowChromeAction::Group: icon = "group"; break;
            case EWindowChromeAction::Pin: icon = "pin"; break;
            case EWindowChromeAction::Close: icon = "close"; break;
        }
        GridIcons::drawUiIcon(icon, btn.box.copy().scaleFromCenter(0.62), alpha, color);
    }
}


void CScrollOverview::renderWindowActionChrome(PHLMONITOR monitor, size_t activeIdx, float workspacePitch, float renderScale) {
    if (!monitor || closing || !ScrollOverview::Config::getValue<bool>("plugin:gridoverview:window_chrome", monitor))
        return;

    const float PROGRESS = std::clamp((1.F - renderScale) / (1.F - ScrollOverview::Config::getScale(monitor)), 0.F, 1.F);
    const float ALPHA    = PROGRESS * PROGRESS * (3.F - 2.F * PROGRESS);
    if (ALPHA <= 0.001F)
        return;

    const auto DRAGGED = getOverviewWindowToShow(dragActiveWindow.lock());
    const auto CHROME  = ScrollOverview::Config::getContextLabelColor(monitor);

    for (size_t workspaceIdx = 0; workspaceIdx < images.size(); ++workspaceIdx) {
        if (!images[workspaceIdx])
            continue;
        const auto OFFSET = workspaceOverviewOffset(workspaceIdx, activeIdx, workspacePitch);
        for (const auto& windowRef : images[workspaceIdx]->windows) {
            const auto WINDOW = getOverviewWindowToShow(windowRef.lock());
            if (!shouldShowOverviewWindow(WINDOW) || WINDOW == DRAGGED || !windowShowsActionChrome(WINDOW) || isPinMorphWindow(WINDOW))
                continue;

            const auto BOX = getOverviewWindowBox(WINDOW, monitor, renderScale, viewOffset->value(), OFFSET, layout);
            drawWindowActionChromeButtons(WINDOW, BOX, monitor, ALPHA, CHROME);
        }
    }
    // pinned chrome: renderPinnedWindowOverlays (after the pinned preview itself)
}

