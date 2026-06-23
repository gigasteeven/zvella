#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/GameObject.hpp>
#include <Geode/modify/CCDirector.hpp>
#include <Geode/modify/LevelTools.hpp>

#include "DualRender.hpp"
#include "SpoutWrapper.hpp"
#include "XDBotLayout.hpp"

using namespace geode::prelude;

// Layout mode color theme (blue). Applied ONLY to the screen (layout) pass.
// The OBS (clean) pass is rendered completely untouched.
static constexpr cocos2d::ccColor3B LAYOUT_BG    = { 40, 125, 255 };
static constexpr cocos2d::ccColor3B LAYOUT_GROUND = {  0, 102, 255 };

// Single global solid-color background for the layout pass. Lazily created,
// reused across levels, and toggled visible/hidden per frame. We do NOT touch
// the original PlayLayer background — we just draw our solid over it.
static cocos2d::CCLayerColor* g_solidBg = nullptr;

// ============================================================
// Solid background management (created once, reused)
// ============================================================
namespace SolidBackground {
    static void ensure(PlayLayer* pl) {
        if (g_solidBg) return;
        g_solidBg = cocos2d::CCLayerColor::create(
            cocos2d::ccc4(LAYOUT_BG.r, LAYOUT_BG.g, LAYOUT_BG.b, 255));
        g_solidBg->setZOrder(-100);
        g_solidBg->setVisible(false);
        g_solidBg->retain();
        if (pl) pl->addChild(g_solidBg);
    }

    static void attach(PlayLayer* pl) {
        if (!g_solidBg || !pl) return;
        if (!g_solidBg->getParent()) pl->addChild(g_solidBg);
    }

    static void show()   { if (g_solidBg) g_solidBg->setVisible(true);  }
    static void hide()   { if (g_solidBg) g_solidBg->setVisible(false); }

    static void destroy() {
        if (g_solidBg) {
            if (g_solidBg->getParent()) g_solidBg->removeFromParent();
            g_solidBg->release();
            g_solidBg = nullptr;
        }
    }
}

// ============================================================
// Hook: GameObject::visit — the heart of the dual render.
//
// Instead of mutating object state every frame (which was the old approach
// and caused severe FPS drops + stale-pointer crashes), we intercept the
// cocos2d traversal. Two paths based on the current render pass:
//
//   PASS 1 (s_isLayoutPass == false, OBS/clean):
//     Render everything normally — the viewer sees the real level.
//
//   PASS 2 (s_isLayoutPass == true, screen/layout):
//     - Decorations -> skip entirely (return without visiting)
//     - Remaining objects -> temporarily set white color + no glow, visit,
//       then restore. Non-destructive and O(1) per object.
//
// UI/HUD nodes are NOT GameObjects, so they are never touched here — they
// appear unchanged in both passes, exactly as required.
// ============================================================
class $modify(LayoutGameObject, GameObject) {
    void visit() {
        // Only interfere during the layout pass and only when we're actively
        // dual-rendering a PlayLayer.
        if (!DualRender::s_isLayoutPass || !DualRender::s_inPlayLayer) {
            GameObject::visit();
            return;
        }

        // Decorations + scaled-to-0 solids + ghost blocks + excluded triggers.
        if (XDBot::LayoutMode::isDecoration(this)) {
            return; // skip rendering entirely
        }

        // Save the bits of state we're about to temporarily change.
        const cocos2d::ccColor3B baseColor     = getColor();
        const bool hadGlow                     = !m_hasNoGlow;

        // Apply layout look: solid white main color, no glow.
        m_hasNoGlow = true;
        setColor({255, 255, 255});

        // If the object has a detail sprite, paint it white too.
        cocos2d::CCSprite* detail = m_detailColorSprite;
        const bool hasDetail = (detail != nullptr);
        const cocos2d::ccColor3B detailColor = hasDetail ? detail->getColor() : cocos2d::ccColor3B{};
        if (hasDetail) detail->setColor({255, 255, 255});

        // If the object has a glow sprite, hide it for this frame.
        cocos2d::CCSprite* glow = m_glowSprite;
        const bool hasGlowSprite = (glow != nullptr);
        const bool glowWasVisible = hasGlowSprite ? glow->isVisible() : false;
        if (hasGlowSprite) glow->setVisible(false);

        GameObject::visit(); // actually render the object

        // Restore everything — the clean OBS pass next frame must see the real state.
        setColor(baseColor);
        m_hasNoGlow = !hadGlow;
        if (hasDetail) detail->setColor(detailColor);
        if (hasGlowSprite) glow->setVisible(glowWasVisible);
    }
};

// ============================================================
// Hook: LevelTools — always consider level integrity valid when layout mode
// is enabled (matches XDBot; prevents the game from rejecting a level whose
// level string we modified for the important-groups parse).
// ============================================================
class $modify(LayoutLevelTools, LevelTools) {
    static bool verifyLevelIntegrity(gd::string v1, int v2) {
        if (DualRender::s_active) return true;
        return LevelTools::verifyLevelIntegrity(v1, v2);
    }
};

// ============================================================
// Hook: PlayLayer lifecycle
// ============================================================
class $modify(LayoutPlayLayer, PlayLayer) {
    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
        // Parse "important groups" before objects are created, so that the
        // decoration filter can preserve gameplay-critical deco objects.
        if (level && !level->m_levelString.empty()) {
            XDBot::LayoutMode::parseImportantGroups(level->m_levelString);
        }

        DualRender::s_inPlayLayer = true;
        DualRender::s_active      = true;

        const bool ok = PlayLayer::init(level, useReplay, dontCreateObjects);
        if (ok) SolidBackground::ensure(this);
        return ok;
    }

    void onQuit() {
        DualRender::s_inPlayLayer = false;
        PlayLayer::onQuit();
    }

    void resetLevel() {
        // Important groups don't change on reset; just let the base handle it.
        PlayLayer::resetLevel();
    }
};

// ============================================================
// Spout lifecycle helpers
// ============================================================
namespace SpoutLife {
    static std::string name() {
        return Mod::get()->getSettingValue<std::string>("spout-name");
    }

    static bool start(int w, int h) {
        const auto n = name();
        const bool ok = SpoutWrapper::init(w, h, n);
        DualRender::s_width  = w;
        DualRender::s_height = h;
        DualRender::s_spoutInitialized = ok;
        if (ok) log::info("Spout '{}' ready ({}x{})", n, w, h);
        else    log::error("Spout '{}' FAILED", n);
        return ok;
    }

    static void send() {
        if (!DualRender::s_rt || !DualRender::s_spoutInitialized) return;
        cocos2d::CCTexture2D* tex = DualRender::s_rt->getSprite()->getTexture();
        SpoutWrapper::sendTexture(tex->getName(),
            tex->getPixelsWide(), tex->getPixelsHigh(), true);
    }

    static void stop() {
        SpoutWrapper::release();
        if (DualRender::s_rt) {
            DualRender::s_rt->release();
            DualRender::s_rt = nullptr;
        }
        DualRender::s_spoutInitialized = false;
    }
}

// ============================================================
// CCDirector::drawScene — the dual render core.
//
// Global Spout capture: the menu/editor/playlayer all get captured so OBS
// always has something to show. In PlayLayer specifically we do the two-pass
// layout bypass. The UI/HUD is never filtered — only GameObjects are, via the
// visit() hook above. So CPS counters, progress bars, labels etc. render
// identically in both the OBS feed and on the player's screen.
// ============================================================
class $modify(DualDirector, cocos2d::CCDirector) {
    void drawScene() {
        const bool enabled = Mod::get()->getSettingValue<bool>("enabled");
        if (!enabled) {
            if (DualRender::s_active) {
                SpoutLife::stop();
                SolidBackground::destroy();
                DualRender::s_active = false;
            }
            cocos2d::CCDirector::drawScene();
            return;
        }

        const auto winSize = cocos2d::CCDirector::sharedDirector()->getWinSize();
        const int w = static_cast<int>(winSize.width);
        const int h = static_cast<int>(winSize.height);

        // Lazily (re)create the render texture + Spout sender.
        if (!DualRender::s_active || !DualRender::s_rt) {
            DualRender::s_rt = cocos2d::CCRenderTexture::create(
                w, h, cocos2d::kCCTexture2DPixelFormat_RGBA8888, GL_DEPTH24_STENCIL8);
            if (DualRender::s_rt) DualRender::s_rt->retain();
            SpoutLife::start(w, h);
            DualRender::s_active = true;
        }

        // Handle window resize.
        if (w != DualRender::s_width || h != DualRender::s_height) {
            SpoutLife::stop();
            DualRender::s_rt = cocos2d::CCRenderTexture::create(
                w, h, cocos2d::kCCTexture2DPixelFormat_RGBA8888, GL_DEPTH24_STENCIL8);
            if (DualRender::s_rt) DualRender::s_rt->retain();
            SpoutLife::start(w, h);
        }

        auto* scene = getRunningScene();
        auto* rt    = DualRender::s_rt;
        const bool inPlay = DualRender::s_inPlayLayer;

        if (!scene || !rt || !DualRender::s_spoutInitialized) {
            // No offscreen target — just render normally (no capture this frame).
            cocos2d::CCDirector::drawScene();
            return;
        }

        auto* pl = inPlay ? PlayLayer::get() : nullptr;

        // ==========================================
        // NON-PLAYLAYER (Menu/Editor): plain capture
        // ==========================================
        if (!pl) {
            cocos2d::CCDirector::drawScene();
            // Copy the default framebuffer into the render texture, then send.
            GLint vp[4];
            glGetIntegerv(GL_VIEWPORT, vp);
            cocos2d::CCTexture2D* tex = rt->getSprite()->getTexture();
            ccGLBindTexture2D(tex->getName());
            glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, vp[0], vp[1], vp[2], vp[3]);
            SpoutLife::send();
            return;
        }

        // ==========================================
        // PLAYLAYER: two-pass dual render
        // ==========================================
        SolidBackground::attach(pl);

        // --- PASS 1: CLEAN → render texture → Spout (OBS) ---
        DualRender::s_isLayoutPass = false;
        rt->beginWithClear(0.f, 0.f, 0.f, 1.f, 1.f, 0);
        scene->visit();   // the visit() hook renders everything normally here
        rt->end();
        SpoutLife::send();

        // --- PASS 2: LAYOUT → default framebuffer (screen) ---
        DualRender::s_isLayoutPass = true;
        SolidBackground::show();
        cocos2d::CCDirector::drawScene(); // real pipeline → screen, hook filters
        SolidBackground::hide();
        DualRender::s_isLayoutPass = false;
    }
};

// ============================================================
// Geode mod entry
// OBS setup: Add Source → Spout2 Capture → choose the sender name from settings
// (default "GD_Clean"). The player's screen shows Layout Mode; OBS gets clean.
// ============================================================
$on_mod(Loaded) {
    log::info("Layout Mode OBS Bypass loaded!");
}
