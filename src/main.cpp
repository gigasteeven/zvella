#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/GameObject.hpp>
#include <Geode/modify/CCDirector.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>

#include "DualRender.hpp"
#include "LayoutConfig.hpp"

// Spout2 SDK (compiled into a separate static lib, see CMakeLists.txt).
// Only this .cpp includes Spout headers — avoids GLEW conflicts with Geode's bundled GLEW.
#include "Spout.h"

using namespace geode::prelude;

// ============================================================
// Layout color theme (hardcoded blue, matches EclipseMenu defaults)
// ============================================================
static constexpr cocos2d::ccColor3B LAYOUT_BG    = { 40, 125, 255 };
static constexpr cocos2d::ccColor3B LAYOUT_GROUND= {  0, 102, 255 };
static constexpr cocos2d::ccColor3B LAYOUT_LINE  = {255, 255, 255 };
static constexpr cocos2d::ccColor3B LAYOUT_MG    = { 40, 125, 255 };

// ============================================================
// Spout2 wrapper — modern `Spout` class API
// ============================================================
namespace SpoutWrapper {
    static Spout* s_sender = nullptr;

    bool init(int w, int h) {
        if (s_sender) return true;

        s_sender = new Spout();

        std::string name = Mod::get()->getSettingValue<std::string>("spout-name");
        bool ok = s_sender->CreateSender(name.c_str(), w, h);

        DualRender::s_width  = w;
        DualRender::s_height = h;
        DualRender::s_spoutInitialized = ok;

        if (ok) {
            log::info("Spout sender '{}' created ({}x{})", name, w, h);
        } else {
            log::error("Failed to create Spout sender '{}'", name);
        }
        return ok;
    }

    void sendTexture(GLuint texID) {
        if (!s_sender || !DualRender::s_spoutInitialized) return;
        // SendTexture(texID, target, width, height, bInvert, HostFBO)
        // bInvert=true : Spout internally flips vertically so OBS displays upright.
        s_sender->SendTexture(texID, GL_TEXTURE_2D,
                              DualRender::s_width, DualRender::s_height, true, 0);
    }

    void release() {
        if (s_sender) {
            s_sender->ReleaseSender();
            delete s_sender;
            s_sender = nullptr;
        }
        if (DualRender::s_rt) {
            DualRender::s_rt->release();
            DualRender::s_rt = nullptr;
        }
        DualRender::s_spoutInitialized = false;
        log::info("Spout sender released");
    }

    void resize(int w, int h) {
        if (w == DualRender::s_width && h == DualRender::s_height) return;
        release();
        init(w, h);
    }
}

// ============================================================
// Render-time color override (fixes Bug #1)
// `updateColor` is called by game logic (triggers / level start), NOT by
// the render loop. Checking s_isLayoutPass there had no effect. Instead we
// recolor the BG / Ground sprites right before the layout pass is drawn,
// and restore them immediately after, so the clean OBS pass is unaffected.
// ============================================================
namespace LayoutLook {
    struct Saved {
        cocos2d::CCNodeRGBA* node;
        cocos2d::ccColor3B color;
    };
    static std::vector<Saved> g_saved;

    static void recolorOne(cocos2d::CCNode* node, cocos2d::ccColor3B c) {
        if (!node) return;
        // Only CCNodeRGBA subclasses (CCSprite, CCLayerRGBA, etc.) have setColor.
        auto* rgba = dynamic_cast<cocos2d::CCNodeRGBA*>(node);
        if (!rgba) return;
        g_saved.push_back({ rgba, rgba->getColor() });
        rgba->setColor(c);
    }

    static void apply(PlayLayer* pl) {
        g_saved.clear();
        if (!pl) return;
        recolorOne(pl->m_background,  LAYOUT_BG);
        recolorOne(pl->m_groundLayer, LAYOUT_GROUND);
        if (pl->m_groundLayer && pl->m_groundLayer->getChildren()) {
            for (size_t i = 0; i < pl->m_groundLayer->getChildrenCount(); ++i) {
                recolorOne(static_cast<cocos2d::CCNode*>(
                    pl->m_groundLayer->getChildren()->objectAtIndex(i)), LAYOUT_GROUND);
            }
        }
    }

    static void restore() {
        for (auto& s : g_saved) {
            if (s.node) s.node->setColor(s.color);
        }
        g_saved.clear();
    }
}

// ============================================================
// Hook: GameObject::visit — filter decorations + blue-tint the rest
// (Non-destructive: object state is restored after the call.)
// ============================================================
class $modify(LayoutGameObject, GameObject) {
    void visit() {
        if (!(DualRender::s_active && DualRender::s_isLayoutPass)) {
            GameObject::visit();
            return;
        }

        // Skip decorations entirely
        if (LayoutConfig::isDecoration(m_objectID, m_objectType)) {
            return;
        }

        const bool hadGlow = !m_hasNoGlow;
        m_hasNoGlow = true;

        const cocos2d::ccColor3B prevColor = getColor();
        setColor(LAYOUT_MG);
        GameObject::visit();
        setColor(prevColor);

        m_hasNoGlow = !hadGlow;
    }
};

// ============================================================
// Hook: PlayLayer — manage dual render lifecycle
// ============================================================
class $modify(DualRenderPlayLayer, PlayLayer) {
    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
        if (!PlayLayer::init(level, useReplay, dontCreateObjects)) return false;

        if (Mod::get()->getSettingValue<bool>("enabled")) {
            const auto win = CCDirector::sharedDirector()->getWinSizeInPixels();
            const int w = static_cast<int>(win.width);
            const int h = static_cast<int>(win.height);

            // Offscreen target for the clean OBS pass — CCRenderTexture handles
            // depth/stencil + projection correctly (fixes Bugs #2 and #4).
            DualRender::s_rt = CCRenderTexture::create(w, h, kCCTexture2DPixelFormat_RGBA8888);
            DualRender::s_rt->retain();

            SpoutWrapper::init(w, h);
            DualRender::s_active = true;
            log::info("Dual render activated for level: {}", level->m_levelName);
        }
        return true;
    }

    void onQuit() {
        DualRender::s_active = false;
        SpoutWrapper::release();
        PlayLayer::onQuit();
    }
};

// ============================================================
// Hook: CCDirector::drawScene — the dual render core
//   Pass 1 (clean  → OBS) : render scene into CCRenderTexture, send via Spout
//   Pass 2 (layout → screen) : normal cocos drawScene with layout overrides
// HUD/labels are CCNodes, not GameObjects, so they appear in both passes.
// ============================================================
class $modify(DualRenderDirector, CCDirector) {
    void drawScene() {
        if (!DualRender::s_active) {
            CCDirector::drawScene();
            return;
        }

        auto* scene = getRunningScene();
        // Get PlayLayer via static getter — it's the currently active gameplay layer
        auto* pl = PlayLayer::get();

        // Spout not ready / no offscreen target — still do layout on screen.
        if (!DualRender::s_spoutInitialized || !DualRender::s_rt || !scene) {
            DualRender::s_isLayoutPass = true;
            LayoutLook::apply(pl);
            CCDirector::drawScene();
            LayoutLook::restore();
            DualRender::s_isLayoutPass = false;
            return;
        }

        // Handle window resize
        const auto win = getWinSizeInPixels();
        const int w = static_cast<int>(win.width);
        const int h = static_cast<int>(win.height);
        if (w != DualRender::s_width || h != DualRender::s_height) {
            SpoutWrapper::resize(w, h);
            if (!DualRender::s_rt) {
                DualRender::s_rt = CCRenderTexture::create(w, h, kCCTexture2DPixelFormat_RGBA8888);
                DualRender::s_rt->retain();
            }
        }

        // --- PASS 1: CLEAN → offscreen texture → Spout (OBS) ---
        DualRender::s_isLayoutPass = false;
        // beginWithClear(r,g,b,a, depth, stencil) correctly clears depth/stencil
        // and sets up the projection for the offscreen FBO (fixes Bugs #2 & #4).
        DualRender::s_rt->beginWithClear(0.f, 0.f, 0.f, 1.f, 1.f, 0);
        scene->visit();
        DualRender::s_rt->end();

        SpoutWrapper::sendTexture(
            DualRender::s_rt->getSprite()->getTexture()->getName());

        // --- PASS 2: LAYOUT → default framebuffer (screen) ---
        DualRender::s_isLayoutPass = true;
        LayoutLook::apply(pl);
        CCDirector::drawScene();        // real cocos pipeline → screen
        LayoutLook::restore();
        DualRender::s_isLayoutPass = false;
    }
};

// ============================================================
// Geode mod entry
// ============================================================
$on_mod(Loaded) {
    log::info("Layout Mode OBS Bypass loaded!");
    log::info("Use Spout2 Capture in OBS with the sender name from settings.");
}
