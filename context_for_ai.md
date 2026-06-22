# Context: Geometry Dash Layout Mode OBS Bypass Mod

## 📌 Project Overview
We are building a **Geode mod** (SDK v5.7.1) for **Geometry Dash 2.2081** (Windows). 
The goal of the mod is to provide a **Dual Render** system:
1. **Screen (Player View):** Shows "Layout Mode" (decorations hidden, simplified solid colors for BG/Ground, no glow).
2. **OBS (Stream/Record View):** Receives a "Clean" normal gameplay render via **Spout2** (zero-latency GPU texture sharing).
3. **HUD/UI:** Must remain visible and untouched in both views (CPS counters, progress bars, etc.).

We are using CMake's `FetchContent` to pull the latest [Spout2 SDK](https://github.com/leadedge/Spout2) directly during the build process.

## 🏗️ Current Architecture
The mod relies on hooking the Cocos2d-x render pipeline:

- **`DualRender.hpp`**: Contains a global boolean `s_isLayoutPass` to track which pass is currently rendering.
- **`GameObject::visit` Hook**: If `s_isLayoutPass == true`, we check if the object is a decoration (via `LayoutConfig::isDecoration`). If it is, we `return;` (skip rendering). We also temporarily set `m_hasNoGlow = true` during this pass.
- **`CCDirector::drawScene` Hook**: The core of the dual render. 
  - **Pass 1 (Clean):** Sets `s_isLayoutPass = false`, binds a custom OpenGL FBO, calls `scene->visit()`, sends the FBO texture to Spout2, and unbinds.
  - **Pass 2 (Layout):** Sets `s_isLayoutPass = true`, and calls the original `CCDirector::drawScene()` to render to the screen.

## 🐛 Bugs to Fix (Your Task)

The current implementation in `src/main.cpp` has several critical flaws that need to be rewritten:

### 1. Color Modification Logic is Broken
**Bug:** We hooked `GJBaseGameLayer::updateColor` and checked `if (s_isLayoutPass)`. However, `updateColor` is triggered by game logic (color triggers, level start), **not** during the render loop. So checking the render pass flag here does nothing.
**Solution Required:** You need to find a way to dynamically change the colors of the Background, Ground, and Middleground *only* during the Layout Pass render, or intercept the drawing of these specific layers (e.g., modifying `CCSprite` or `CCLayerColor` right before they are drawn, or applying a shader). 

### 2. Broken FBO (Missing Depth/Stencil)
**Bug:** We manually created an FBO (`glGenFramebuffers`) with only a color attachment. Geometry Dash heavily relies on depth and stencil buffers (for masks, portals, Z-layering). Rendering the scene to our raw FBO causes severe graphical glitches and z-fighting.
**Solution Required:** Rip out the raw OpenGL FBO code. Replace it with Cocos2d's **`CCRenderTexture`**. `CCRenderTexture` automatically handles depth/stencil buffers and projection matrices correctly.
*Note: Render to `CCRenderTexture`, then pass `renderTexture->getSprite()->getTexture()->getName()` (the GLuint texture ID) to Spout2.*

### 3. Outdated Spout2 API
**Bug:** The code attempts to instantiate `new spoutSender()`. In the latest versions of the Spout2 repository (which we fetch via CMake), the class is simply called `Spout` (or uses the `SpoutSender` class differently depending on the exact header, but `GetSpout()` or the modern API should be checked). 
**Solution Required:** Update the Spout2 wrapper to use the correct modern Spout2 API syntax.

### 4. Incomplete Scene Rendering in Pass 1
**Bug:** In `drawScene`, calling `scene->visit()` into a raw FBO skips important Director setup steps (like clearing the screen with the correct color).
**Solution Required:** By switching to `CCRenderTexture`, you should use `renderTexture->beginWithClear(r, g, b, a, depth, stencil)` and then `scene->visit()`, which handles clearing properly.

## 📂 Current File Structure to Reference
- `CMakeLists.txt` (Fetches Spout2 from github.com/leadedge/Spout2)
- `src/main.cpp` (Needs the rewrite)
- `src/LayoutConfig.hpp` (Contains `isDecoration()` logic - works fine)
- `src/DualRender.hpp` (Global flags)
