# Context: Layout Mode OBS Bypass — Ultimate Version

## The Conflict
To achieve **Exact Eclipse Layout Mode**, we must structurally modify `GameObject` properties (colors, glow, opacity, particles).
However, modifying these permanently ruins the "Clean" OBS pass.
But doing it dynamically (swapping back and forth twice a frame) causes massive VBO rebuilds in Cocos2d-x, destroying FPS.

## The Solution: Asymmetric Dual Rendering
1. **The Game State is Layout Mode by Default:** We apply the full Layout Mode structural changes to all objects. Most of the time (e.g., 240 FPS), the game just renders this Layout Mode to the screen. Zero overhead.
2. **The OBS Pass is Rate-Limited:** We limit the OBS render to 60 FPS (or whatever is set in mod.json). 
3. **The Swap (Only during OBS frames):** When an OBS frame is due (e.g., 60 times a second):
   - We **Restore** all original colors/glow/decorations to the objects.
   - We render the scene to the `CCRenderTexture` for OBS.
   - We immediately **Re-apply** the Layout Mode properties.
   
This means the expensive VBO rebuild only happens 60 times a second, NOT 480 times a second. The player gets a buttery smooth 240+ FPS on their screen with perfect Layout Mode, while OBS gets a perfectly clean 60 FPS capture.

## Implementation details to fix in main.cpp
- Keep the `PlayLayer::addObject` hook to cache objects.
- Create a `struct ObjectOriginalState` to store `m_hasNoGlow`, `baseColor`, `detailColor`, `opacity`, `isVisible()`.
- Create `LayoutStateSwapper::applyLayout(obj)` and `LayoutStateSwapper::restoreOriginal(obj)`.
- In `drawScene`, check the `obs-fps` timer. If it's time for an OBS frame:
  1. `LayoutStateSwapper::restoreAll()`
  2. Render to FBO -> Spout
  3. `LayoutStateSwapper::applyAll()`
- Render to screen (which is always in Layout state).
