#pragma once

#include "globals.hpp"
#include <hyprland/src/helpers/time/Time.hpp>

namespace OverviewWindow {

void initDecorationRenderer();
void cleanupDecorationRenderer();

bool shouldBlurBackground(const PHLWINDOW& window);
bool shouldUsePrecomputedBlur(const PHLWINDOW& window, PHLMONITOR monitor = nullptr, const CBox* workspaceBox = nullptr, const CBox* windowBox = nullptr, bool dragged = false);
bool shouldUseBlurFramebuffer(const PHLWINDOW& window, PHLMONITOR monitor = nullptr, const CBox* workspaceBox = nullptr, const CBox* windowBox = nullptr);
void forceDecoRecalc(const PHLWINDOW& window);

struct SRenderParams {
    PHLMONITOR            monitor;
    PHLWINDOW             window;
    CBox                  windowBox;
    float                 renderScale          = 1.F;
    Time::steady_tp       now                  = {};
    const CBox*           workspaceBox         = nullptr;
    bool                  selected             = false;
    bool                  dragged              = false;
    PHLWINDOW             pseudoFocusWindow;
    // multiplies borders, shadows and decorations; the contents follow the workspace alpha
    float                 alpha                = 1.F;
    bool                  shadow               = true;
};

void renderOverviewWindow(const SRenderParams& params);

// Group tab strip above an overview window (title + indicator). Returns the member under point, else null.
// expandCeilingY (>=0): grow hit upward to that Y (workspace card top), clamped by blockers below
// that are above this window and overlap the tab column in X.
PHLWINDOW groupTabAt(PHLMONITOR monitor, const PHLWINDOW& window, const CBox& windowBox, float renderScale, const Vector2D& point,
                     float expandCeilingY = -1.F, const std::vector<CBox>* blockers = nullptr);

}
