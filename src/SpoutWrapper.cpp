// ============================================================
// SpoutWrapper.cpp
//
// This TU is compiled WITHOUT the Geode PCH (see CMakeLists:
// SKIP_PRECOMPILE_HEADERS ON) and does NOT include any Geode/Cocos headers.
// That isolation is required: Geode bundles GLEW which #defines OpenGL entry
// points as macros, conflicting with Spout2's own GL extension declarations
// in SpoutGLextensions.h. Keeping Spout.h out of any Geode-aware TU avoids
// the glShaderSource redeclaration error.
// ============================================================

#include "SpoutWrapper.hpp"

// Spout2 SDK — self-contained (defines its own GL types/constants).
#include "Spout.h"

namespace SpoutWrapper {
    static Spout* s_sender = nullptr;
    static bool s_ok = false;

    bool init(int w, int h, const std::string& name) {
        if (s_sender) return s_ok;

        s_sender = new Spout();
        s_ok = s_sender->CreateSender(name.c_str(), w, h);

        if (!s_ok) {
            delete s_sender;
            s_sender = nullptr;
        }
        return s_ok;
    }

    void sendTexture(unsigned int texID, int w, int h, bool invert) {
        if (!s_sender || !s_ok) return;
        // SendTexture(texID, target, width, height, bInvert, HostFBO=0)
        s_sender->SendTexture(
            texID,
            GL_TEXTURE_2D,
            static_cast<unsigned int>(w),
            static_cast<unsigned int>(h),
            invert,
            0);
    }

    void release() {
        if (s_sender) {
            s_sender->ReleaseSender();
            delete s_sender;
            s_sender = nullptr;
        }
        s_ok = false;
    }

    void resize(int w, int h, const std::string& name) {
        release();
        init(w, h, name);
    }
}
