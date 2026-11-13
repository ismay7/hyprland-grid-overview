#pragma once

#define WLR_USE_UNSTABLE

#include "globals.hpp"
#include <hyprland/src/desktop/DesktopTypes.hpp>
#include <hyprland/src/helpers/AnimatedVariable.hpp>
#include <hyprland/src/helpers/signal/Signal.hpp>
#include <hyprland/src/event/EventBus.hpp>
#include <hyprland/src/helpers/time/Time.hpp>
#include <hyprland/src/layout/LayoutManager.hpp>
#include <hyprland/src/render/Framebuffer.hpp>
#include <hyprland/src/render/types.hpp>
#include <hyprland/src/managers/eventLoop/EventLoopTimer.hpp>
#include <chrono>
#include <string>
#include <unordered_map>
#include <vector>

#include "Config.hpp"
#include "DropIndicator.hpp"
#include "IOverview.hpp"

class CMonitor;
struct wl_event_source;

class CScrollOverview : public IOverview {
  public:
    CScrollOverview(PHLWORKSPACE startedOn_, bool swipe = false, PHLMONITOR monitor = {});
    ~CScrollOverview() override;

    void         render() override;
    void         damage() override;
    void         requestInputFrame() override;
    void         markBlurDirty();
    void         markBackdropBlurDirty();
    void         onDamageReported() override;
    bool         shouldHandleSurfaceDamage(SP<CWLSurfaceResource> surface) override;
    bool         shouldAllowSurfaceFrame(SP<CWLSurfaceResource> surface, const Time::steady_tp& now) override;
    bool         shouldAllowRealtimePreviewSchedule() override;
    bool         shouldSuppressRenderDamage() const override;
    void         onPreRender() override;
    std::string  specialTrayLayoutJson() const; // hl.plugin.gridoverview.tray_layout()

    void         setClosing(bool closing) override;

    void         resetSwipe() override;
    void         onSwipeUpdate(double delta) override;
    void         onSwipeEnd() override;

    // close without a selection
    void         close() override;
    void         dismissTransient() override;
    bool         isClosing() const override;
    void         reopen() override;
    void         selectHoveredWorkspace() override;
    // grid mode: "fit" (whole grid), "center" (selected workspace centred) or "togglefit"; also affects the next opening
    static void  setGridViewMode(const std::string& mode);
    // one wheel notch of the continuous zoom: +1 zooms out (towards the whole grid), -1 zooms in (towards the desktop)
    void         zoomStep(int direction);
    bool         moveSelection(const std::string& direction) override;
    bool         windowDispatcherAction(const std::string& action) override;

    void         fullRender() override;
    bool         adoptNativeWindowDrag(PHLWINDOW expectedWindow = {});

  private:
    enum class ECloseMode {
        COMMIT_SELECTION,
        PRESERVE_MONITOR_STATE,
    };

    struct SWindowDragSnapshot {
        PHLWINDOW    window;
        PHLWORKSPACE workspace;
        CBox         box;
        CBox         visualBox;
        Vector2D     floatSize;
        Vector2D     tapeTranslation;
        Vector2D     grabOffsetLocal;
        Vector2D     grabRatio = Vector2D{0.5, 0.5};
        bool         startedTiled = false;
    };

    void       close(ECloseMode mode);
    void   rebuildWorkspaceImages();
    void   seedRememberedSelections();
    void   redrawAll(bool forcelowres = false);
    void   onWorkspaceChange();
    void   renderGlobalWallpaper(Render::CRenderContext& ctx, PHLMONITOR monitor, const Time::steady_tp& now);
    void   renderWallpaperLayers(PHLMONITOR monitor, const CBox& workspaceBox, float renderScale, const Time::steady_tp& now, float alpha = 1.F);
    void   renderWidgetLayers(PHLMONITOR monitor, const CBox& workspaceBox, float renderScale, const Time::steady_tp& now, float alpha = 1.F);
    void   updateBackdropBlurCache(PHLMONITOR monitor, int wallpaperMode, const Time::steady_tp& now);
    void   renderBackdropBlurCache(PHLMONITOR monitor);
    void   renderWorkspaceBackground(PHLMONITOR monitor, size_t workspaceIdx, size_t activeIdx, float workspacePitch, float renderScale, int wallpaperMode, const Time::steady_tp& now);
    void   renderSelectedHighlight(PHLMONITOR monitor, size_t activeIdx, float workspacePitch, float renderScale, int wallpaperMode, const Time::steady_tp& now);
    void   renderSelectedEmptyMarker(PHLMONITOR monitor, size_t activeIdx, float workspacePitch, float renderScale);
    void   renderWindowIcons(PHLMONITOR monitor, size_t activeIdx, float workspacePitch, float renderScale);
    void   renderContextLabels(PHLMONITOR monitor, size_t activeIdx, float workspacePitch, float renderScale);
    std::unordered_map<std::string, SP<Render::ITexture>> contextLabelTextures;
    // where the labels were drawn last frame (pill box -> first workspace of the context), for clicks
    std::vector<std::pair<CBox, size_t>> contextLabelHitboxes;
    std::optional<size_t>            contextLabelPressed;
    // special_tray: scratchpads as cards at the right screen edge, in screen coordinates (not zoomed / panned)
    struct STrayCard {
        std::string  name;      // without the "special:" prefix
        PHLWORKSPACE workspace; // null for a configured name that does not exist yet
        CBox         box;       // render px on this monitor
    };
    std::vector<STrayCard> specialTrayCards() const;
    float                  specialTrayReserve() const; // logical px kept free right of the fitted grid
    float                  specialTrayShownWidth() const; // logical px; full width when shown or sliding in, else 0
    float                  specialTrayDragScale(const Vector2D& cursorGlobal, float gridScale) const;
    bool                   specialTrayAutohide() const;
    void                   updateSpecialTrayReveal(const Vector2D& pointLocal);
    PHLANIMVAR<float>      trayReveal; // autohide: 0 hidden .. 1 shown
    std::optional<size_t>  specialTrayCardAt(const Vector2D& pointLocal) const;
    bool                   specialTrayBandAt(const Vector2D& pointLocal) const;
    CBox                   specialTrayBandBox() const;
    bool                   specialTrayBandPressed = false;
    CBox                   specialTrayAddBox(const std::vector<STrayCard>& cards) const;
    bool                   specialTrayAddAt(const Vector2D& pointLocal) const;
    bool                   specialTrayAddPressed = false;
    std::string            newSpecialWorkspaceName(const PHLWINDOW& window) const;
    CBox                   specialTrayWindowBox(const STrayCard& card, const PHLWINDOW& window) const;
    float                  specialTrayRenderScale(const STrayCard& card) const;
    std::vector<PHLWINDOW> specialTrayWindows(const STrayCard& card) const;
    PHLWINDOW              specialTrayWindowAt(const Vector2D& pointLocal) const;
    PHLWORKSPACE           ensureSpecialWorkspace(const STrayCard& card) const;
    void                   hideActiveSpecialWorkspace();
    void                   renderSpecialTray(PHLMONITOR monitor, const Time::steady_tp& now);
    std::optional<size_t>  specialTrayPressed;
    // shown once this overview is gone; by name, an empty scratchpad created earlier would be cleaned up by then
    std::string            pendingSpecialName;
    void   renderWorkspaceLive(PHLMONITOR monitor, size_t workspaceIdx, size_t activeIdx, float workspacePitch, float renderScale, int wallpaperMode, const Time::steady_tp& now);
    bool   hasVisiblePrecomputedBlurWindow(PHLMONITOR monitor, size_t activeIdx, float workspacePitch, float renderScale) const;
    void   renderWindowLive(PHLMONITOR monitor, PHLWINDOW window, const CBox& windowBox, float renderScale, const Time::steady_tp& now, const CBox* workspaceBox = nullptr,
                             bool dragged = false, float alpha = 1.F);
    void   renderDraggedWindow(PHLMONITOR monitor, size_t activeIdx, float workspacePitch, float renderScale, const Time::steady_tp& now);
    void   renderPinnedFloatingWindows(PHLMONITOR monitor, float overviewScale, const Time::steady_tp& now);
    void   moveViewportWorkspace(bool up);
    void   trackpadSwipeLayout(const PHLWORKSPACE target, const double delta);
    void   trackpadSwipeWorkspace(const double delta);
    void   finishWorkspaceScrollFollow();
    double trackpadWorkspaceScrollOffset(PHLMONITOR monitor, float renderScale);
    bool   scrollStepAllowed(uint32_t timeMs);
    bool   selectOverviewWindow(PHLWINDOW window, size_t workspaceIdx, bool syncFocus = false);
    bool   selectWindowAtOverviewCursor(bool syncFocus = false);
    PHLWINDOW windowClosestToWorkspaceCenter(size_t workspaceIdx) const;
    void   rememberSelection(PHLWINDOW window);
    void   syncSelectionToViewport();
    void   syncFocusedSelection();
    size_t dragWorkspaceIndex(PHLWINDOW window) const;
    void   updateWorkspaceOverflow();
    CBox   workspaceOverviewVisibleBox(size_t workspaceIdx, const CBox& workspaceBox, float renderScale, PHLMONITOR monitor) const;
    Vector2D   workspaceOverviewOffset(size_t workspaceIdx, size_t activeIdx, float workspacePitch) const;
    Vector2D   workspaceOverviewLogicalOffset(size_t workspaceIdx, size_t activeIdx, float workspacePitch) const;
    Vector2D   workspaceGridCell(size_t workspaceIdx) const;
    std::optional<size_t> gridNeighbourWorkspace(size_t workspaceIdx, int dCol, int dRow) const;
    void       moveViewportToWorkspace(size_t workspaceIdx);
    void       ensureGridPlaceholders();
    void       moveGridViewport(int dCol, int dRow);
    void       beginGridPan();
    void       updateGridPan();
    void       endGridPan();
    void       updateDragEdgeScroll();
    // grid fit mode: zoom out and centre so the whole grid is visible ("Komplettansicht")
    float      targetOverviewScale() const;
    Vector2D   restingViewOffset() const;
    float      gridFitScale() const;
    Vector2D   gridFitViewOffset() const;
    // continuous zoom (SUPER + CTRL + wheel): 0 = desktop, 1 = centred view, 2 = whole grid
    void       applyZoomLevel();
    void       finishZoom();
    float                            zoomLevel = 1.F;
    SP<CEventLoopTimer>              zoomSnapTimer;
    void       applyGridFit();
    void       setGridFitMode(bool fit);
    CBox       gridModeButtonBox(int button) const;
    CBox       gridZoomSliderTrack() const;
    void       setZoomFromSlider(double xLocal);
    std::optional<int> gridModeButtonAt(const Vector2D& pointLocal) const;
    void       renderGridModeButtons(PHLMONITOR monitor);
    CBox       highlightBoxFor(size_t workspaceIdx, size_t activeIdx, float workspacePitch, float renderScale, PHLMONITOR monitor) const;
    float      workspaceOverviewAlpha(size_t workspaceIdx) const;
    PHLWINDOW windowAtOverviewPoint(const Vector2D& point, size_t* workspaceIdx = nullptr) const;
    PHLWINDOW windowAtOverviewCursor(size_t* workspaceIdx = nullptr);
    PHLWINDOW windowAtOverviewCursorOnWorkspace(size_t workspaceIdx, const PHLWINDOW& ignoredWindow = nullptr, CBox* windowBox = nullptr) const;
    CDropIndicator::SDropAnchor dropAnchorAtOverviewCursorOnWorkspace(size_t workspaceIdx, const PHLWINDOW& ignoredWindow = nullptr,
                                                                      CScrollOverview* dragContext = nullptr);
    PHLWORKSPACE workspaceAtOverviewPoint(const Vector2D& point, size_t* workspaceIdx = nullptr) const;
    PHLWORKSPACE workspaceAtOverviewDropPoint(const Vector2D& point, size_t* workspaceIdx = nullptr, const PHLWINDOW& draggedWindow = nullptr) const;
    PHLWORKSPACE workspaceAtOverviewCursor(size_t* workspaceIdx = nullptr) const;
    Vector2D  overviewPointToGlobal(size_t workspaceIdx, const Vector2D& pointLocal) const;
    CBox      draggedWindowBox(size_t workspaceIdx) const;
    CBox      draggedWindowBoxFor(PHLWINDOW window, size_t workspaceIdx, const Vector2D& pointLocal, const Vector2D& grabRatio) const;
    CBox      draggedWindowGlobalBox() const;
    void      refreshDragOriginalOverviewBoxes();
    void                        clearDragPending();
    void                        beginWindowDrag(PHLWINDOW window);
    std::optional<SWindowDragSnapshot> snapshotNativeWindowDrag(Layout::Supplementary::CDragStateController* dragController,
                                                                 PHLWINDOW expectedWindow) const;
    void initializeWindowDrag(const SWindowDragSnapshot& snapshot, bool adoptedFromNative, bool crossMonitorDrag);
    void                        ensureDragDestinationOverview();
    void                        updateWindowDrag();
    void                        endWindowDrag();
    void                        finishWindowDragSession(const SP<IOverview>& persistentDestination = {});
    void                        cancelWindowDrag();
    CBox      resizedWindowBox() const;
    void      beginWindowResize();
    void      updateWindowResize();
    void      endWindowResize();
    void      updateScrollingPan();
    void      beginScrollingPan(PHLWORKSPACE workspace);
    void      endScrollingPan();
    void      focusMostVisibleScrollingWindow(const PHLWORKSPACE& workspace);
    bool      moveScrollingColumnSelection(bool next);
    bool      moveScrollingStackSelection(bool next);
    void   forceSurfaceVisibility(SP<CWLSurfaceResource> surface);
    void   forceWindowSurfaceVisibility(PHLWINDOW window);
    void   forceWindowVisible(PHLWINDOW window);
    void   forceLayersAboveFullscreen();
    void   restoreForcedSurfaceVisibility();
    void   restoreForcedWindowVisibility();
    void   restoreForcedLayerVisibility();
    void   applyWorkspaceAnimationOverrides();
    void   restoreWorkspaceAnimationOverrides();
    void   forceWorkspaceAlphaVisible();
    void   forceWorkspaceWindowsDecoRecalc(const PHLWORKSPACE& workspace);
    void   emitFullscreenVisibilityState(PHLWINDOW window, bool hideFullscreen);
    void   applyInputConfigOverrides();
    void   restoreInputConfigOverrides();
    void   transferSharedStateOwnership();
    size_t activeWorkspaceIndex() const;
    bool   isSelectedWorkspace(const PHLWORKSPACE& workspace) const;
    void   sendOverviewFrameCallbacks(const Time::steady_tp& now);
    bool   isVisibleRealtimePreviewWindow(const PHLWINDOW& window) const;
    bool   hasRunningWorkspaceAnimation() const;
    bool   shouldAllowRealtimePreviewFrame() const;
    void   scheduleMinimumPreviewFrame();
    void   schedulePreviewFrameAfter(std::chrono::milliseconds delay);
    void   scheduleRealtimePreviewFrame();
    void   releaseInputListeners();
    void   scheduleCloseRemoval();
    void   activateSubmapIfConfigured();
    void   restoreSubmapIfActive();
    bool   dispatchSubmapMouseClick(uint32_t button);
    static int realtimePreviewTimerCallback(void* data);

    size_t viewportCurrentWorkspace = 0;
    bool   rebuildPending           = false;
    bool   overviewBlurDirty        = true;
    bool   backdropBlurDirty        = true;
    bool   overviewBlurStateValid   = false;
    float  lastOverviewBlurScale    = 1.F;
    int    lastBackdropWallpaperMode = -1;
    std::vector<std::pair<const void*, uint32_t>> lastBackdropWallpaperStack;
    Vector2D lastOverviewBlurViewOffset = Vector2D{};
    SP<Render::IFramebuffer> backdropBlurFB;

    struct SWorkspaceImage {
        PHLWORKSPACE              pWorkspace;
        std::vector<PHLWINDOWREF> windows;
        float                     overflowLeft   = 0.F;
        float                     overflowRight  = 0.F;
        float                     overflowTop    = 0.F;
        float                     overflowBottom = 0.F;
    };

    struct SWorkspaceInsertTransition {
        bool                             active              = false;
        std::string                            transitionWorkspaceKey = "";
        bool                             transitionFadeIn   = true;
        std::unordered_map<std::string, float> oldRelativeOffsets;
        std::unordered_map<std::string, float> newRelativeOffsets;
        float                            transitionOldRelativeOffset = 0.F;
    };

    Vector2D                         lastMousePosLocal = Vector2D{}; // monitor-local pixel space

    PHLWINDOWREF                     closeOnWindow;
    PHLWINDOWREF                     dragActiveWindow;
    PHLWORKSPACEREF                  dragOriginalWorkspace;
    std::vector<WP<IOverview>>                    dragTransientOverviews;
    PHLWORKSPACEREF                  scrollingPanWorkspace;
    PHLWINDOWREF                     scrollingPanInitialWindow;
    PHLWINDOWREF                     resizePendingWindow;
    PHLWINDOWREF                     resizeActiveWindow;

    Vector2D                         dragStartMouseLocal   = Vector2D{};
    Vector2D                         dragGrabOffsetLocal   = Vector2D{};
    Vector2D                         dragGrabRatio         = Vector2D{0.5, 0.5};
    Vector2D                         dragOriginalFloatSize = Vector2D{};
    Vector2D                         dragOriginalTapeTranslation = Vector2D{};
    Vector2D                         resizeStartMouseLocal = Vector2D{};
    Vector2D                         resizeLastMouseLocal  = Vector2D{};
    Vector2D                         scrollingPanLastMouseLocal = Vector2D{};
    CBox                             dragOriginalBox            = CBox{};
    CBox                             dragOriginalVisualBox      = CBox{};
    CBox                             dragOriginalOverviewBox    = CBox{};
    CBox                             dragOriginalOverviewHitbox = CBox{};
    CBox                             resizeOriginalBox          = CBox{};
    std::string                      focusSyncedFromWorkspaceKey       = "";
    size_t                           resizeWorkspaceIdx     = 0;
    Layout::eRectCorner              resizeCorner           = Layout::CORNER_NONE;
    bool                             dragPendingPrimary    = false;
    bool                                          dragCancelledAwaitingRelease      = false;
    bool                             resizePointerDown     = false;
    bool                             scrollingPanPointerDown = false;
    bool                             submapMouseClickPending = false;
    bool                             dragStartedTiled      = false;
    bool                             dragAdoptedFromNative = false;
    bool                             dragCrossMonitor      = false;
    bool                             emittingFullscreenVisibilityState = false;
    bool                             inputConfigOverridden = false;
    bool                             realtimePreviewTimerArmed = false;
    bool                             realtimePreviewFrameQueued = false;
    bool                             selectedWorkspaceFramePending = false;
    bool                             inputFramePending = false;
    bool                             sendingOverviewFrameCallbacks = false;
    bool                             usesSubmapKeybinds = false;
    bool                             submapActive = false;
    uint32_t                         submapMouseClickButton = 0;
    std::string                      previousSubmapName;
    int                              previousNoWarps = 0;
    int                              previousWarpOnChangeWorkspace = 0;
    int                              previousWarpOnToggleSpecial = 0;
    int                              previousWarpBackAfterNonMouseInput = 0;
    int                              previousFollowMouse = 0;

    std::vector<SP<SWorkspaceImage>> images;
    std::vector<PHLWINDOWREF>        pinnedFloatingWindows;
    // Selectors distinguish numbered, named, and special workspace identities.
    std::unordered_map<std::string, PHLWINDOWREF> rememberedSelection;
    SWorkspaceInsertTransition       workspaceInsertTransition;
    PHLWORKSPACEREF                  pendingRemovedWorkspace;
    ScrollOverview::Config::ELayout  layout = ScrollOverview::Config::ELayout::VERTICAL;
    bool                             grid   = false;
    bool                             gridContextsAsColumns = false; // grid_orientation = columns
    // empty workspaces created for grid mode; persistent while the overview is open
    std::vector<PHLWORKSPACE>        gridPlaceholders;

    struct SForcedSurfaceVisibility {
        WP<CWLSurfaceResource> surface;
        CRegion               visibleRegion;
    };
    std::vector<SForcedSurfaceVisibility> forcedSurfaceVisibility;

    struct SForcedWindowVisibility {
        PHLWINDOWREF window;
        bool         hidden = false;
    };
    std::vector<SForcedWindowVisibility> forcedWindowVisibility;

    struct SForcedLayerVisibility {
        PHLLSREF layer;
        bool     aboveFullscreen = true;
        float    alpha           = 1.F;
    };
    std::vector<SForcedLayerVisibility> forcedLayerVisibility;

    struct SWorkspaceAnimationConfig {
        std::string                                      name;
        Hyprutils::Animation::SAnimationPropertyConfig   config;
    };
    std::vector<SWorkspaceAnimationConfig> savedWorkspaceAnimationConfigs;
    bool                                   workspaceAnimationsOverridden = false;
    bool                                   sharedStateOwner = false;

    PHLWORKSPACE                     startedOn;

    PHLANIMVAR<float>                scale;
    PHLANIMVAR<Vector2D>             viewOffset;
    PHLANIMVAR<float>                workspaceInsertProgress;
    PHLANIMVAR<float>                workspaceInsertFadeProgress;
    // highlight_selected: wallpaper + shadow slide from the previously selected card to the selected one
    PHLANIMVAR<float>                highlightProgress;
    // transition_bezier: own curve for viewOffset / highlight, instead of the (possibly springy) windowsMove
    SP<Hyprutils::Animation::SAnimationPropertyConfig> workspaceTransitionConfig;
    // zoom (open/close) and move (inside the overview) configs, and view_switch_*: centred <-> whole-grid view
    SP<Hyprutils::Animation::SAnimationPropertyConfig> zoomAnimationConfig, moveAnimationConfig, viewSwitchConfig;
    void                             useViewSwitchAnimation(bool viewSwitch);
    PHLWORKSPACEREF                  highlightFrom, highlightTo;
    SP<Hyprutils::Animation::SAnimationPropertyConfig> workspaceInsertFadeConfig;
    SP<Hyprutils::Animation::SAnimationPropertyConfig> workspaceRemoveFadeConfig;
    Time::steady_tp                  lastRealtimePreviewFrame = {};
    Time::steady_tp                  realtimePreviewTimerDue = {};
    wl_event_source*                 realtimePreviewTimer = nullptr;
    wl_event_source*                 closeRemovalIdle = nullptr;

    bool                             closing = false;
    bool                             closeApplied = false; // close() has run its teardown; guards against double-invocation
    bool                                               closeRemovalPending = false;

    CHyprSignalListener             mouseMoveHook;
    CHyprSignalListener             mouseButtonHook;
    CHyprSignalListener             touchMoveHook;
    CHyprSignalListener             touchDownHook;
    CHyprSignalListener             mouseAxisHook;
    CHyprSignalListener             windowOpenHook;
    CHyprSignalListener             windowCloseHook;
    CHyprSignalListener             windowMoveHook;
    CHyprSignalListener             windowActiveHook;
    CHyprSignalListener             windowFullscreenHook;
    CHyprSignalListener             keyboardKeyHook;
    CHyprSignalListener                                dragKeyboardKeyHook;
    CHyprSignalListener             workspaceCreatedHook;
    CHyprSignalListener             workspaceRemovedHook;

    bool                             swipe = false;

    uint32_t                         lastScrollStepTimeMs = 0;      // event time of the last discrete scroll step, for scroll_event_delay throttling

    double                           trackpadScrollAccum          = 0.0;
    bool                             trackpadWorkspaceFollowing   = false;
    bool                             trackpadTapeFollowing        = false;
    bool                             trackpadGestureSettlePending = false;
    double                           trackpadGestureSettleOffset  = 0.0;
    // grid mode: free 2D panning with the middle mouse button, snapping to the nearest workspace on release
    bool                             gridPanActive        = false;
    Vector2D                         gridPanStartMouse;
    Vector2D                         gridPanStartOffset;
    bool                             gridPanSettlePending = false;
    Vector2D                         gridPanSettleOffset;
    // grid mode: holding a dragged window at a screen edge steps the viewport to the neighbouring workspace
    SP<CEventLoopTimer>              dragEdgeTimer;
    int                              dragEdgeCol = 0, dragEdgeRow = 0;
    bool                             gridModeButtonPressed = false;
    bool                             gridSliderDragging    = false;
    PHLWORKSPACEREF                  hoveredWorkspace;

    friend class CScrollOverviewPassElement;
    friend void removeOverview(IOverview* overview);
};
