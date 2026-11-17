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
#include <hyprland/src/config/shared/actions/ConfigActions.hpp>
#include <hyprland/src/managers/input/InputManager.hpp>
#include <hyprland/src/managers/fullscreen/FullscreenController.hpp>

#include <algorithm>
#include <cmath>
#include <unordered_set>

float CScrollOverview::pinnedFloatingAnimationProgress() const {
    const auto MONITOR = pMonitor.lock();
    if (!MONITOR || !scale)
        return 1.F;
    const auto TARGET = ScrollOverview::Config::getScale(MONITOR);
    return (1.F - TARGET) > 0.001F ? (1.F - scale->value()) / (1.F - TARGET) : 1.F;
}


std::vector<PHLWINDOW> CScrollOverview::pinnedRailWindows(PHLMONITOR monitor) const {
    std::vector<PHLWINDOW> out;
    if (!monitor)
        return out;
    out.reserve(pinnedFloatingWindows.size());
    for (const auto& windowRef : pinnedFloatingWindows) {
        const auto WINDOW = getOverviewWindowToShow(windowRef.lock());
        if (!shouldShowPinnedFloatingOverviewWindow(WINDOW) || WINDOW->m_monitor != monitor)
            continue;
        out.push_back(WINDOW);
    }
    return out;
}


CBox CScrollOverview::pinnedRailHostCard() const {
    const auto MONITOR = pMonitor.lock();
    if (!MONITOR || images.empty() || !scale)
        return {};

    size_t idx = viewportCurrentWorkspace;
    if (idx >= images.size())
        idx = activeWorkspaceIndex();
    if (idx >= images.size() || !images[idx])
        return {};

    const auto ACTIVEIDX = activeWorkspaceIndex();
    const auto PITCH     = getWorkspaceRenderedPitch(MONITOR, scale->value(), layout);
    const auto OFFSET    = workspaceOverviewOffset(idx, ACTIVEIDX, PITCH);
    return getOverviewWorkspaceBox(MONITOR, scale->value(), viewOffset->value(), OFFSET, layout);
}


CBox CScrollOverview::pinnedRailSlotBox(const PHLWINDOW& window, float* contentRenderScale) const {
    const auto MONITOR = pMonitor.lock();
    const auto WINDOW  = getOverviewWindowToShow(window);
    if (!MONITOR || !WINDOW) {
        if (contentRenderScale)
            *contentRenderScale = 1.F;
        return {};
    }

    const auto PINS = pinnedRailWindows(MONITOR);
    size_t     slot = PINS.size();
    for (size_t i = 0; i < PINS.size(); ++i) {
        if (PINS[i] == WINDOW) {
            slot = i;
            break;
        }
    }
    if (slot >= PINS.size()) {
        if (contentRenderScale)
            *contentRenderScale = 1.F;
        return {};
    }

    const auto CARD = pinnedRailHostCard();
    if (CARD.empty()) {
        if (contentRenderScale)
            *contentRenderScale = 1.F;
        return {};
    }

    const float S     = MONITOR->m_scale;
    const float PAD   = 8.F * S;
    const float GAP   = 8.F * S;
    const float COUNT = sc<float>(std::max<size_t>(PINS.size(), 1));
    // ~⅓ larger than the first icon-rail size (was ~0.9× icon_size, 28–40px)
    const float ICONCFG = sc<float>(ScrollOverview::Config::getValue<int>("plugin:gridoverview:window_icon_size", MONITOR)) * S;
    float       cell    = std::clamp(ICONCFG * 1.2F, 36.F * S, 52.F * S);

    const float AVAILH = std::max(1.F, sc<float>(CARD.height) - 2.F * PAD);
    float       totalH = COUNT * cell + (COUNT - 1.F) * GAP;
    if (totalH > AVAILH) {
        cell   = (AVAILH - (COUNT - 1.F) * GAP) / COUNT;
        cell   = std::max(cell, 28.F * S);
        totalH = COUNT * cell + (COUNT - 1.F) * GAP;
    }

    const float X = sc<float>(CARD.x) + PAD;
    const float Y = sc<float>(CARD.y) + (sc<float>(CARD.height) - totalH) * 0.5F + sc<float>(slot) * (cell + GAP);

    if (contentRenderScale)
        *contentRenderScale = 1.F;

    return CBox{X, Y, cell, cell}.round();
}


CBox CScrollOverview::pinnedRailWindowBox(const PHLWINDOW& window, float* contentRenderScale) const {
    const auto MONITOR = pMonitor.lock();
    const auto WINDOW  = getOverviewWindowToShow(window);
    const auto SLOT    = pinnedRailSlotBox(WINDOW);
    if (!MONITOR || !WINDOW || SLOT.empty()) {
        if (contentRenderScale)
            *contentRenderScale = 1.F;
        return {};
    }

    // morph the real floating window into the rail icon slot (and back when closing)
    const float PROGRESS = std::clamp(pinnedFloatingAnimationProgress(), 0.F, 1.F);
    const float S        = MONITOR->m_scale;
    const auto  NATIVE   = WINDOW->size(Desktop::View::IGeometric::GEOMETRIC_CURRENT) * S;
    const CBox  REAL     = {(WINDOW->position(Desktop::View::IGeometric::GEOMETRIC_CURRENT) - MONITOR->m_position) * S, NATIVE};

    CBox box = SLOT;
    if (!REAL.empty() && REAL.width > 1 && REAL.height > 1) {
        box.x      = REAL.x + (SLOT.x - REAL.x) * PROGRESS;
        box.y      = REAL.y + (SLOT.y - REAL.y) * PROGRESS;
        box.width  = REAL.width + (SLOT.width - REAL.width) * PROGRESS;
        box.height = REAL.height + (SLOT.height - REAL.height) * PROGRESS;
        box.round();
    }

    if (contentRenderScale) {
        const float SX = NATIVE.x > 1.F ? sc<float>(box.width) / sc<float>(NATIVE.x) : 1.F;
        const float SY = NATIVE.y > 1.F ? sc<float>(box.height) / sc<float>(NATIVE.y) : 1.F;
        *contentRenderScale = std::clamp(std::min(SX, SY), 0.05F, 1.F);
    }
    return box;
}


void CScrollOverview::endPinMorph() {
    if (!pinMorph)
        return;
    pinMorph.reset();
    if (pinMorphProgress)
        pinMorphProgress->setValueAndWarp(1.F);
    damage();
}


void CScrollOverview::beginPinMorph(const PHLWINDOW& window, const CBox& from, bool pinning) {
    const auto WINDOW = getOverviewWindowToShow(window);
    if (!WINDOW || from.empty() || !pinMorphProgress)
        return;

    // replace any in-flight morph without going through endPinMorph's warp-to-1 first
    pinMorph = SPinMorph{
        .window  = WINDOW,
        .from    = from,
        .to      = {},
        .pinning = pinning,
        .phase   = EPinMorphPhase::PendingTarget,
    };
    // hold at 0 until rebuild gives us the target slot/card
    pinMorphProgress->setValueAndWarp(0.F);
}


bool CScrollOverview::isPinMorphAnimating() const {
    return pinMorph && pinMorph->phase == EPinMorphPhase::Animating && pinMorphProgress &&
        (pinMorphProgress->isBeingAnimated() || pinMorphProgress->value() < 0.999F);
}


void CScrollOverview::snapshotPinMorphSources() {
    pinMorphSourceBoxes.clear();
    pinMorphSourcePinned.clear();
    if (closing || !scale)
        return;

    const auto MONITOR = pMonitor.lock();
    if (!MONITOR)
        return;

    for (const auto& img : images) {
        if (!img)
            continue;
        for (const auto& windowRef : img->windows) {
            const auto WINDOW = getOverviewWindowToShow(windowRef.lock());
            if (!shouldShowOverviewWindow(WINDOW))
                continue;
            const auto KEY = overviewWindowKey(WINDOW);
            const auto BOX = windowActionChromeHostBox(WINDOW);
            if (KEY.empty() || BOX.empty())
                continue;
            pinMorphSourceBoxes[KEY] = BOX;
        }
    }

    for (const auto& WINDOW : pinnedRailWindows(MONITOR)) {
        const auto KEY = overviewWindowKey(WINDOW);
        const auto BOX = pinnedRailSlotBox(WINDOW);
        if (KEY.empty() || BOX.empty())
            continue;
        pinMorphSourceBoxes[KEY] = BOX;
        pinMorphSourcePinned.insert(KEY);
    }
}


void CScrollOverview::maybeAutoBeginPinMorphFromSnapshot() {
    if (pinMorph || closing || !pinMorphProgress)
        return;

    const auto MONITOR = pMonitor.lock();
    if (!MONITOR || pinMorphSourceBoxes.empty())
        return;

    std::unordered_set<std::string> nowPinned;
    for (const auto& WINDOW : pinnedRailWindows(MONITOR)) {
        const auto KEY = overviewWindowKey(WINDOW);
        if (!KEY.empty())
            nowPinned.insert(KEY);
    }

    PHLWINDOW morphWindow;
    CBox      from{};
    bool      pinning = true;

    for (const auto& KEY : nowPinned) {
        if (pinMorphSourcePinned.contains(KEY))
            continue;
        const auto IT = pinMorphSourceBoxes.find(KEY);
        if (IT == pinMorphSourceBoxes.end())
            continue;
        // newly pinned — find the window
        for (const auto& WINDOW : pinnedRailWindows(MONITOR)) {
            if (overviewWindowKey(WINDOW) != KEY)
                continue;
            morphWindow = WINDOW;
            from        = IT->second;
            pinning     = true;
            break;
        }
        if (morphWindow)
            break;
    }

    if (!morphWindow) {
        for (const auto& KEY : pinMorphSourcePinned) {
            if (nowPinned.contains(KEY))
                continue;
            const auto IT = pinMorphSourceBoxes.find(KEY);
            if (IT == pinMorphSourceBoxes.end())
                continue;
            for (const auto& img : images) {
                if (!img)
                    continue;
                for (const auto& windowRef : img->windows) {
                    const auto WINDOW = getOverviewWindowToShow(windowRef.lock());
                    if (!shouldShowOverviewWindow(WINDOW) || overviewWindowKey(WINDOW) != KEY)
                        continue;
                    morphWindow = WINDOW;
                    from        = IT->second;
                    pinning     = false;
                    break;
                }
                if (morphWindow)
                    break;
            }
            if (morphWindow)
                break;
        }
    }

    if (morphWindow && !from.empty())
        beginPinMorph(morphWindow, from, pinning);
}


void CScrollOverview::retargetPinMorphIfNeeded() {
    if (!pinMorph || pinMorph->phase != EPinMorphPhase::PendingTarget || !pinMorphProgress)
        return;

    const auto WINDOW = getOverviewWindowToShow(pinMorph->window.lock());
    if (!WINDOW) {
        endPinMorph();
        return;
    }

    CBox to{};
    if (pinMorph->pinning)
        to = pinnedRailSlotBox(WINDOW);
    else {
        const auto MONITOR = pMonitor.lock();
        if (MONITOR && WINDOW->m_workspace) {
            for (size_t i = 0; i < images.size(); ++i) {
                if (!images[i] || images[i]->pWorkspace != WINDOW->m_workspace)
                    continue;
                const auto OFFSET = workspaceOverviewOffset(i, activeWorkspaceIndex(), getWorkspaceRenderedPitch(MONITOR, scale->value(), layout));
                to                = getOverviewWindowBox(WINDOW, MONITOR, scale->value(), viewOffset->value(), OFFSET, layout);
                break;
            }
        }
    }

    if (to.empty())
        return;

    pinMorph->to    = to;
    pinMorph->phase = EPinMorphPhase::Animating;
    *pinMorphProgress = 1.F;
}


void CScrollOverview::tickPinMorph() {
    retargetPinMorphIfNeeded();
    if (!pinMorph || pinMorph->phase != EPinMorphPhase::Animating || !pinMorphProgress)
        return;
    if (!pinMorphProgress->isBeingAnimated() && pinMorphProgress->value() >= 0.999F)
        endPinMorph();
}


bool CScrollOverview::isPinMorphWindow(const PHLWINDOW& window) const {
    if (!isPinMorphAnimating())
        return false;
    const auto WINDOW = getOverviewWindowToShow(window);
    const auto MORPH  = getOverviewWindowToShow(pinMorph->window.lock());
    return WINDOW && MORPH && WINDOW == MORPH;
}


CBox CScrollOverview::pinMorphBox(float* contentRenderScale) const {
    if (!isPinMorphAnimating()) {
        if (contentRenderScale)
            *contentRenderScale = 1.F;
        return {};
    }

    const float T    = std::clamp(pinMorphProgress->value(), 0.F, 1.F);
    const auto& FROM = pinMorph->from;
    const auto& TO   = pinMorph->to;
    CBox        box{FROM.x + (TO.x - FROM.x) * T, FROM.y + (TO.y - FROM.y) * T, FROM.width + (TO.width - FROM.width) * T, FROM.height + (TO.height - FROM.height) * T};
    box.round();

    if (contentRenderScale) {
        const auto WINDOW  = getOverviewWindowToShow(pinMorph->window.lock());
        const auto MONITOR = pMonitor.lock();
        if (WINDOW && MONITOR) {
            const auto  NATIVE = WINDOW->size(Desktop::View::IGeometric::GEOMETRIC_CURRENT) * MONITOR->m_scale;
            const float SX     = NATIVE.x > 1.F ? sc<float>(box.width) / sc<float>(NATIVE.x) : 1.F;
            const float SY     = NATIVE.y > 1.F ? sc<float>(box.height) / sc<float>(NATIVE.y) : 1.F;
            *contentRenderScale = std::clamp(std::min(SX, SY), 0.05F, 1.F);
        } else
            *contentRenderScale = 1.F;
    }
    return box;
}


float CScrollOverview::pinMorphLiveAlpha() const {
    if (!pinMorph || !pinMorphProgress)
        return 1.F;
    const float T = std::clamp(pinMorphProgress->value(), 0.F, 1.F);
    const float S = T * T * (3.F - 2.F * T);
    if (pinMorph->pinning)
        return 1.F - std::clamp((S - 0.45F) / 0.50F, 0.F, 1.F);
    return std::clamp((S - 0.10F) / 0.50F, 0.F, 1.F);
}


float CScrollOverview::pinMorphIconAlpha() const {
    if (!pinMorph || !pinMorphProgress)
        return 0.F;
    const float T = std::clamp(pinMorphProgress->value(), 0.F, 1.F);
    const float S = T * T * (3.F - 2.F * T);
    if (pinMorph->pinning)
        return std::clamp((S - 0.40F) / 0.45F, 0.F, 1.F);
    return 1.F - std::clamp((S - 0.15F) / 0.45F, 0.F, 1.F);
}


void CScrollOverview::renderPinMorph(PHLMONITOR monitor, const Time::steady_tp& now) {
    if (!monitor || !isPinMorphAnimating())
        return;

    const auto WINDOW = getOverviewWindowToShow(pinMorph->window.lock());
    if (!WINDOW)
        return;
    if (dragActiveWindow && WINDOW == getOverviewWindowToShow(dragActiveWindow.lock()))
        return; // drag owns the preview — avoid frozen morph duplicate

    float      renderScale = 1.F;
    const auto BOX         = pinMorphBox(&renderScale);
    if (BOX.empty() || !overviewBoxIntersectsMonitor(BOX, monitor))
        return;

    const float LIVEA = pinMorphLiveAlpha();
    if (LIVEA > 0.001F)
        renderWindowLive(monitor, WINDOW, BOX, renderScale, now, nullptr, false, LIVEA, true);

    const float BASE =
        std::clamp((1.F - scale->value()) / (1.F - ScrollOverview::Config::getScale(monitor)), 0.F, 1.F);
    const float ICONA = pinMorphIconAlpha() * BASE * BASE * (3.F - 2.F * BASE);
    if (ICONA <= 0.001F)
        return;

    const auto  THEME = ScrollOverview::Config::getValue<std::string>("plugin:gridoverview:icon_theme", monitor);
    const auto  CHROME = ScrollOverview::Config::getContextLabelColor(monitor);
    const auto  MID    = BOX.middle();
    const float SIDE   = sc<float>(std::min(BOX.width, BOX.height));
    const float ICON   = SIDE * 0.88F;

    if (const auto TEXTURE = GridIcons::iconForAppID(WINDOW->metadata().appID(), THEME)) {
        CTexPassElement::SRenderData data;
        data.tex = TEXTURE;
        data.box = CBox{MID.x - ICON / 2.F, MID.y - ICON / 2.F, ICON, ICON}.round();
        data.a   = ICONA;
        Render::IHyprRenderer::currentPass(g_pHyprRenderer->context()).add(makeUnique<CTexPassElement>(std::move(data)));
    }

    {
        const float BADGE = std::clamp(SIDE * 0.22F, 12.F * monitor->m_scale, 18.F * monitor->m_scale);
        const CBox  BADGEBOX{BOX.x + BOX.w - BADGE, BOX.y, BADGE, BADGE};
        auto        badgeColor = CHROME;
        badgeColor.a           = 1.F;
        GridIcons::drawUiIcon("pin", BADGEBOX, ICONA * 0.9F, badgeColor);
    }
}


void CScrollOverview::renderPinnedWindowOverlays(PHLMONITOR monitor, float renderScale) {
    if (!monitor || closing)
        return;

    const float PROGRESS = std::clamp((1.F - renderScale) / (1.F - ScrollOverview::Config::getScale(monitor)), 0.F, 1.F);
    const float ALPHA    = PROGRESS * PROGRESS * (3.F - 2.F * PROGRESS);
    if (ALPHA <= 0.001F)
        return;

    const float SETTLE = std::clamp(pinnedFloatingAnimationProgress(), 0.F, 1.F);
    const float SMOOTH = SETTLE * SETTLE * (3.F - 2.F * SETTLE);
    // crossfade with the live morph: icons take over as the window shrinks into the rail
    const float ICONA  = ALPHA * std::clamp((SMOOTH - 0.40F) / 0.45F, 0.F, 1.F);
    if (ICONA <= 0.001F)
        return;

    const auto DRAGGED = getOverviewWindowToShow(dragActiveWindow.lock());
    const auto THEME   = ScrollOverview::Config::getValue<std::string>("plugin:gridoverview:icon_theme", monitor);
    const auto CHROME  = ScrollOverview::Config::getContextLabelColor(monitor);

    for (const auto& window : pinnedRailWindows(monitor)) {
        if (window == DRAGGED || isPinMorphWindow(window))
            continue;

        // draw the icon on the morphing box so it feels like the window becomes the icon
        const auto BOX = pinnedRailWindowBox(window);
        if (BOX.empty() || !overviewBoxIntersectsMonitor(BOX, monitor))
            continue;

        const auto  MID  = BOX.middle();
        const float SIDE = sc<float>(std::min(BOX.width, BOX.height));
        const float ICON = SIDE * 0.88F;

        if (const auto TEXTURE = GridIcons::iconForAppID(window->metadata().appID(), THEME)) {
            CTexPassElement::SRenderData data;
            data.tex = TEXTURE;
            data.box = CBox{MID.x - ICON / 2.F, MID.y - ICON / 2.F, ICON, ICON}.round();
            data.a   = ICONA;
            Render::IHyprRenderer::currentPass(g_pHyprRenderer->context()).add(makeUnique<CTexPassElement>(std::move(data)));
        }

        {
            const float BADGE = std::clamp(SIDE * 0.22F, 12.F * monitor->m_scale, 18.F * monitor->m_scale);
            const CBox  BADGEBOX{BOX.x + BOX.w - BADGE, BOX.y, BADGE, BADGE};
            auto        badgeColor = CHROME;
            badgeColor.a           = 1.F;
            GridIcons::drawUiIcon("pin", BADGEBOX, ICONA * 0.9F, badgeColor);
        }

        // chrome only once the rail has settled (anchors to the slot)
        if (SMOOTH > 0.88F && windowShowsActionChrome(window))
            drawWindowActionChromeButtons(window, pinnedRailSlotBox(window), monitor, ALPHA, CHROME);
    }
}


void CScrollOverview::renderPinnedFloatingWindows(PHLMONITOR monitor, float overviewScale, const Time::steady_tp& now) {
    if (!monitor)
        return;

    const auto PINS = pinnedRailWindows(monitor);
    if (PINS.empty())
        return;

    const float PROGRESS = std::clamp(pinnedFloatingAnimationProgress(), 0.F, 1.F);
    const float SETTLE   = PROGRESS * PROGRESS * (3.F - 2.F * PROGRESS); // 0 = desktop, 1 = settled rail
    (void)overviewScale;

    // live window morphs into the rail; fades out as the icon takes over
    const float LIVEA = 1.F - std::clamp((SETTLE - 0.45F) / 0.50F, 0.F, 1.F);
    // rail plate only once icons dominate
    const float RAILA = std::clamp((SETTLE - 0.55F) / 0.40F, 0.F, 1.F);

    if (RAILA > 0.001F) {
        CBox unionBox{};
        bool any = false;
        for (const auto& window : PINS) {
            if (dragActiveWindow && window == getOverviewWindowToShow(dragActiveWindow.lock()))
                continue;
            const auto BOX = pinnedRailSlotBox(window);
            if (BOX.empty())
                continue;
            if (!any) {
                unionBox = BOX;
                any      = true;
            } else {
                const float x2 = std::max(unionBox.x + unionBox.w, BOX.x + BOX.w);
                const float y2 = std::max(unionBox.y + unionBox.h, BOX.y + BOX.h);
                unionBox.x     = std::min(unionBox.x, BOX.x);
                unionBox.y     = std::min(unionBox.y, BOX.y);
                unionBox.w     = x2 - unionBox.x;
                unionBox.h     = y2 - unionBox.y;
            }
        }
        if (any) {
            const float PAD = 4.F * monitor->m_scale;
            CRectPassElement::SRectData rail;
            rail.box   = unionBox.copy().expand(PAD).round();
            rail.color = CHyprColor{0.F, 0.F, 0.F, 0.35F * RAILA};
            rail.round = sc<int>(8.F * monitor->m_scale);
            Render::IHyprRenderer::currentPass(g_pHyprRenderer->context()).add(makeUnique<CRectPassElement>(rail));
        }
    }

    if (LIVEA <= 0.001F)
        return;

    for (const auto& window : PINS) {
        if (dragActiveWindow && window == getOverviewWindowToShow(dragActiveWindow.lock()))
            continue;
        if (isPinMorphWindow(window))
            continue; // drawn by renderPinMorph

        float      renderScale = 1.F;
        const auto windowBox   = pinnedRailWindowBox(window, &renderScale);
        if (windowBox.empty() || !overviewBoxIntersectsMonitor(windowBox, monitor))
            continue;

        renderWindowLive(monitor, window, windowBox, renderScale, now, nullptr, false, LIVEA, true);
    }
}


bool CScrollOverview::reconcilePinnedFloatingWindows() {
    // external pin toggles (e.g. Super+Shift+T) don't always emit a rebuild hook
    if (!pMonitor)
        return false;

    std::unordered_set<std::string> listed;
    listed.reserve(pinnedFloatingWindows.size());
    for (const auto& windowRef : pinnedFloatingWindows) {
        const auto WINDOW = getOverviewWindowToShow(windowRef.lock());
        if (!WINDOW)
            return true;
        listed.insert(overviewWindowKey(WINDOW));
        if (!shouldShowPinnedFloatingOverviewWindow(WINDOW) || WINDOW->m_monitor != pMonitor)
            return true;
    }
    for (const auto& window : Desktop::windowState()->windows()) {
        const auto WINDOW = getOverviewWindowToShow(window);
        if (!shouldShowPinnedFloatingOverviewWindow(WINDOW) || WINDOW->m_monitor != pMonitor)
            continue;
        if (!listed.contains(overviewWindowKey(WINDOW)))
            return true;
    }
    return false;
}

bool CScrollOverview::reconcileOverviewWindows() {
    // group dissolve/create (togglegroup / moveintogroup) posts Socket2 only — no EventBus rebuild
    if (!pMonitor)
        return false;

    std::unordered_set<std::string> listed;
    listed.reserve(Desktop::windowState()->windows().size());

    for (const auto& img : images) {
        if (!img || !img->pWorkspace)
            continue;

        for (const auto& windowRef : img->windows) {
            const auto STORED = windowRef.lock();
            if (!STORED)
                return true;

            const auto WINDOW = getOverviewWindowToShow(STORED);
            if (!WINDOW || WINDOW != STORED)
                return true; // member became non-current, or group current changed without rebuild

            listed.insert(overviewWindowKey(WINDOW));
            if (!shouldShowOverviewWindow(WINDOW) || WINDOW->m_workspace != img->pWorkspace)
                return true;
        }
    }

    for (const auto& window : Desktop::windowState()->windows()) {
        const auto WINDOW = getOverviewWindowToShow(window);
        if (!shouldShowOverviewWindow(WINDOW) || !WINDOW->m_workspace)
            continue;

        bool tracked = false;
        for (const auto& img : images) {
            if (img && img->pWorkspace == WINDOW->m_workspace) {
                tracked = true;
                break;
            }
        }
        if (!tracked)
            continue;

        if (!listed.contains(overviewWindowKey(WINDOW)))
            return true; // e.g. sibling after Super+W dissolve — layout slot exists, tile missing
    }

    return false;
}

