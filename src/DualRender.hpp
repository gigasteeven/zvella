#pragma once

#include <cocos2d.h>

// Global state for dual rendering
namespace DualRender {
    // true  = current pass is the layout mode render going to the screen
    // false = current pass is the clean render for OBS (Spout)
    inline bool s_isLayoutPass = false;

    // Whether dual render is currently active (mod enabled)
    inline bool s_active = false;

    // true while we are inside a PlayLayer (level is being played)
    inline bool s_inPlayLayer = false;

    // Spout state
    inline bool s_spoutInitialized = false;
    inline int  s_width  = 0;
    inline int  s_height = 0;

    // Offscreen target for the clean pass.
    // CCRenderTexture owns its FBO + depth/stencil + color texture internally,
    // so we no longer keep raw GLuint FBO/texture handles here.
    inline cocos2d::CCRenderTexture* s_rt = nullptr;
}
