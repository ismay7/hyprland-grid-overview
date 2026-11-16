#pragma once

#include "globals.hpp"

#include <string>
#include <string_view>

namespace Render {
    class ITexture;
}

namespace GridIcons {
    // App icon for a window's app id (class): desktop entry (StartupWMClass or file name) -> Icon= -> icon theme
    // (iconTheme if set, then hicolor) -> pixmaps. Loaded once per app id and theme; nullptr if none was found.
    // Needs the renderer's GL context, i.e. call it while rendering.
    SP<Render::ITexture> iconForAppID(const std::string& appID, const std::string& iconTheme);

    // First FreeDesktop main category from the app's .desktop Categories= (lowercase slug), or empty if none.
    std::string categoryForAppID(const std::string& appID);

    // Built-in UI icon (SVG under assets/icons/, embedded in the plugin). `name` is the file stem: grip, close,
    // plus, pencil, view-center, view-grid, float, maximize, fullscreen, pin. `color` tints fills/strokes that were
    // #FFFFFF in the SVG; alpha is applied when drawing. Needs the renderer's GL context.
    SP<Render::ITexture> uiIcon(std::string_view name, const CHyprColor& color = CHyprColor{1.F, 1.F, 1.F, 1.F}, int rasterPx = 128);

    // draw a UI icon centred in `box` (keeps aspect); no-op if the icon is missing
    void drawUiIcon(std::string_view name, const CBox& box, float alpha, const CHyprColor& color = CHyprColor{1.F, 1.F, 1.F, 1.F});
}
