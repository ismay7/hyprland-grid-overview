#pragma once

// Helpers of scrollOverview.cpp that SpecialTray.cpp needs as well

#include "scrollOverview.hpp"

PHLWINDOW getOverviewWindowToShow(const PHLWINDOW& window);
bool      shouldShowOverviewWindow(const PHLWINDOW& window);
Vector2D  getOverviewMousePosLocal(PHLMONITOR monitor);
bool      isOverviewPointerOnMonitor(PHLMONITOR monitor);
// logical px from the monitor's top edge to its work area (below a bar)
float gridWorkAreaTop(PHLMONITOR monitor);
void  renderEmptyWorkspaceMarker(PHLMONITOR monitor, const CBox& workspaceBox, float overviewScale, float alpha, bool selected);

// the overview a pointer grab (window drag) belongs to, if any
extern CScrollOverview* g_pointerGrabOverview;
