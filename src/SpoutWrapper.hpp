#pragma once
#include <string>

// ============================================================
// Spout2 sender wrapper.
//
// IMPORTANT ARCHITECTURE NOTE:
// The implementation lives in SpoutWrapper.cpp, which is a SEPARATE translation
// unit that is compiled WITHOUT Geode/GLEW headers and WITHOUT the Geode PCH.
// This isolation is MANDATORY because Spout2's SpoutGLextensions.h declares
// OpenGL function pointers (e.g. `extern glShaderSourcePROC glShaderSource;`)
// that conflict with Geode's bundled GLEW, which #defines `glShaderSource` as a
// macro (`GLEW_GET_FUN(__glewShaderSource)`). Including both in the same TU
// produces a hard redeclaration error.
//
// Therefore:
//   - This header must NOT include Spout.h, Geode, or anything GL-related.
//   - Only plain C++ types (unsigned int, std::string) cross the boundary.
//   - Callers (e.g. main.cpp) pass in values they read from Geode themselves
//     (sender name, texture id, dimensions) and do their own logging.
// ============================================================

namespace SpoutWrapper {
    // Create a Spout sender with the given name and dimensions.
    // Returns true on success.
    bool init(int w, int h, const std::string& name);

    // Send a GL 2D texture to OBS via the shared Spout texture.
    // invert=true flips vertically (OBS Spout2 Capture expects upright output).
    void sendTexture(unsigned int texID, int w, int h, bool invert = true);

    // Release the sender and free resources.
    void release();

    // Recreate the sender at a new size.
    void resize(int w, int h, const std::string& name);
}
