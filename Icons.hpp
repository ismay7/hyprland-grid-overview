#pragma once

#include "globals.hpp"

#include <string>

namespace Render {
    class ITexture;
}

namespace GridIcons {
    // App icon for a window's app id (class): desktop entry (StartupWMClass or file name) -> Icon= -> icon theme
    // (iconTheme if set, then hicolor) -> pixmaps. Loaded once per app id and theme; nullptr if none was found.
    // Needs the renderer's GL context, i.e. call it while rendering.
    SP<Render::ITexture> iconForAppID(const std::string& appID, const std::string& iconTheme);
}
