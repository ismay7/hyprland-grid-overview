#pragma once

// Helpers of scrollOverview.cpp that SpecialTray.cpp needs as well

#include "scrollOverview.hpp"

PHLWINDOW getOverviewWindowToShow(const PHLWINDOW& window);
bool      shouldShowOverviewWindow(const PHLWINDOW& window);
bool      shouldShowPinnedFloatingOverviewWindow(const PHLWINDOW& window);
std::string overviewWindowKey(const PHLWINDOW& window);
CBox      getOverviewWindowBox(const PHLWINDOW& window, PHLMONITOR monitor, float scale, const Vector2D& viewOffset, const Vector2D& offset, ScrollOverview::Config::ELayout layout,
                              bool round = true, bool includeDecorations = true);
bool      overviewBoxIntersectsMonitor(const CBox& box, PHLMONITOR monitor);
float     getWorkspaceRenderedPitch(PHLMONITOR monitor, float scale, ScrollOverview::Config::ELayout layout);
CBox      getOverviewWorkspaceBox(PHLMONITOR monitor, float scale, const Vector2D& viewOffset, const Vector2D& offset, ScrollOverview::Config::ELayout layout);
Vector2D  getOverviewMousePosLocal(PHLMONITOR monitor);
bool      isOverviewPointerOnMonitor(PHLMONITOR monitor);
// logical px from the monitor's top edge to its work area (below a bar)
float gridWorkAreaTop(PHLMONITOR monitor);
void  renderEmptyWorkspaceMarker(PHLMONITOR monitor, const CBox& workspaceBox, float overviewScale, float alpha, bool selected);

// the overview a pointer grab (window drag) belongs to, if any
extern CScrollOverview* g_pointerGrabOverview;
