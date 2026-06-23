#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/CCDirector.hpp>
#include <chrono>

#include "DualRender.hpp"
#include "LayoutConfig.hpp"
#include "SpoutWrapper.hpp"

using namespace geode::prelude;

// Layout mode color theme
static constexpr cocos2d::ccColor3B LAYOUT_BG    = { 40, 125, 255};
static constexpr cocos2d::ccColor3B LAYOUT_GROUND= {  0, 102, 255};
static constexpr cocos2d::ccColor3B LAYOUT_MG    = { 40, 125, 255};
static constexpr cocos2d::ccColor3B LAYOUT_LINE  = { 255, 255, 255};

// ============================================================
// LayoutStateSwapper — caches original state and swaps back/forth
// ============================================================
namespace LayoutStateSwapper {
    struct ObjState {
        GameObject* obj;
        bool isDeco;
        bool origVisible;
        bool origGlow;
        cocos2d::ccColor3B origColor;
        GLubyte origOpacity;
    };

    static std::vector<ObjState> g_objects;

    void clear() {
        g_objects.clear();
    }

    void tryAdd(GameObject* obj) {
        if (!obj) return;
        bool isDeco = LayoutConfig::isDecoration(obj->m_objectID, obj->m_objectType);
        
        g_objects.push_back({
            obj,
            isDeco,
            obj->isVisible(),
            obj->m_hasNoGlow,
            obj->getColor(),
            obj->getOpacity()
        });
    }

    void applyLayout() {
        for (auto& st : g_objects) {
            if (st.isDeco) {
                st.obj->setVisible(false);
            } else {
                st.obj->m_hasNoGlow = true;
                st.obj->setColor(LAYOUT_LINE);
                st.obj->setOpacity(255);
            }
        }
    }

    void restoreOriginal() {
        for (auto& st : g_objects) {
            if (st.isDeco) {
                st.obj->setVisible(st.origVisible);
            } else {
                st.obj->m_hasNoGlow = st.origGlow;
                st.obj->setColor(st.origColor);
                st.obj->setOpacity(st.origOpacity);
            }
        }
    }
    
    // Updates original state if game logic changed it while restored
    void snapshotOriginals() {
        for (auto& st : g_objects) {
            if (st.isDeco) {
                st.origVisible = st.obj->isVisible();
            } else {
                st.origGlow = st.obj->m_hasNoGlow;
                st.origColor = st.obj->getColor();
                st.origOpacity = st.obj->getOpacity();
            }
        }
    }
}

// ============================================================
// LayoutLook — temporarily recolor BG / Ground / MG nodes
// ============================================================
namespace LayoutLook {
    struct Saved { cocos2d::CCNodeRGBA* node; cocos2d::ccColor3B color; };
    static std::vector<Saved> g_saved;

    static void paint(cocos2d::CCNode* n, cocos2d::ccColor3B c) {
        if (!n) return;
        if (auto* rgba = dynamic_cast<cocos2d::CCNodeRGBA*>(n)) {
            g_saved.push_back({rgba, rgba->getColor()});
            rgba->setColor(c);
        }
    }

    static void paintChildren(cocos2d::CCNode* parent, cocos2d::ccColor3B c) {
        if (!parent || !parent->getChildren()) return;
        auto* ch = parent->getChildren();
        for (unsigned i = 0; i < ch->count(); ++i)
            paint(static_cast<cocos2d::CCNode*>(ch->objectAtIndex(i)), c);
    }

    static void apply(PlayLayer* pl) {
        g_saved.clear();
        if (!pl) return;
        paint(pl->m_background, LAYOUT_BG);
        paint(pl->m_groundLayer, LAYOUT_GROUND);
        paintChildren(pl->m_groundLayer, LAYOUT_GROUND);
        paint(pl->m_groundLayer2, LAYOUT_GROUND);
        paintChildren(pl->m_groundLayer2, LAYOUT_GROUND);
        paint(pl->m_middleground, LAYOUT_MG);
        paintChildren(pl->m_middleground, LAYOUT_MG);
    }

    static void restore() {
        for (auto& s : g_saved) if (s.node) s.node->setColor(s.color);
        g_saved.clear();
    }
}

// ============================================================
// Spout lifecycle
// ============================================================
namespace SpoutLife {
    static std::string name() {
        return Mod::get()->getSettingValue<std::string>("spout-name");
    }

    static bool start(int w, int h) {
        auto n = name();
        bool ok = SpoutWrapper::init(w, h, n);
        DualRender::s_width  = w;
        DualRender::s_height = h;
        DualRender::s_spoutInitialized = ok;
        if (ok) log::info("Spout '{}' ready ({}x{})", n, w, h);
        else    log::error("Spout '{}' FAILED", n);
        return ok;
    }

    static void send() {
        if (!DualRender::s_rt || !DualRender::s_spoutInitialized) return;
        SpoutWrapper::sendTexture(
            DualRender::s_rt->getSprite()->getTexture()->getName(),
            DualRender::s_width, DualRender::s_height, true);
    }

    static void stop() {
        SpoutWrapper::release();
        if (DualRender::s_rt) { DualRender::s_rt->release(); DualRender::s_rt = nullptr; }
        DualRender::s_spoutInitialized = false;
    }
}

// ============================================================
// PlayLayer — lifecycle + cache decorations in addObject
// ============================================================
class $modify(DualPlayLayer, PlayLayer) {
    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
        LayoutStateSwapper::clear();

        if (!PlayLayer::init(level, useReplay, dontCreateObjects)) return false;

        if (Mod::get()->getSettingValue<bool>("enabled")) {
            auto ws = cocos2d::CCDirector::sharedDirector()->getWinSizeInPixels();
            int w = static_cast<int>(ws.width);
            int h = static_cast<int>(ws.height);

            DualRender::s_rt = cocos2d::CCRenderTexture::create(
                w, h, cocos2d::kCCTexture2DPixelFormat_RGBA8888);
            if (DualRender::s_rt) DualRender::s_rt->retain();

            SpoutLife::start(w, h);
            DualRender::s_active = true;

            log::info("Dual render ON — {} objects cached", LayoutStateSwapper::g_objects.size());
            
            // Set objects to Layout Mode by default!
            LayoutStateSwapper::applyLayout();
        }
        return true;
    }

    void addObject(GameObject* obj) {
        PlayLayer::addObject(obj);
        LayoutStateSwapper::tryAdd(obj);
    }

    void onQuit() {
        if (DualRender::s_active) {
            LayoutStateSwapper::restoreOriginal();
        }
        DualRender::s_active = false;
        LayoutStateSwapper::clear();
        SpoutLife::stop();
        PlayLayer::onQuit();
    }

    void resetLevel() {
        if (DualRender::s_active) {
            LayoutStateSwapper::restoreOriginal();
        }
        PlayLayer::resetLevel();
        if (DualRender::s_active) {
            // Snapshot any reset states then reapply layout
            LayoutStateSwapper::snapshotOriginals();
            LayoutStateSwapper::applyLayout();
        }
    }
};

// ============================================================
// CCDirector::drawScene — asymmetric dual render core
// ============================================================
class $modify(DualDirector, cocos2d::CCDirector) {
    void drawScene() {
        if (!DualRender::s_active) {
            cocos2d::CCDirector::drawScene();
            return;
        }

        auto* scene = getRunningScene();
        auto* pl    = PlayLayer::get();
        auto* rt    = DualRender::s_rt;

        if (!scene || !rt || !DualRender::s_spoutInitialized) {
            LayoutLook::apply(pl);
            cocos2d::CCDirector::drawScene();
            LayoutLook::restore();
            return;
        }

        // Handle window resize
        auto ws = getWinSizeInPixels();
        int w = static_cast<int>(ws.width);
        int h = static_cast<int>(ws.height);
        if (w != DualRender::s_width || h != DualRender::s_height) {
            SpoutLife::stop();
            SpoutLife::start(w, h);
            DualRender::s_rt = cocos2d::CCRenderTexture::create(w, h, cocos2d::kCCTexture2DPixelFormat_RGBA8888);
            if (DualRender::s_rt) DualRender::s_rt->retain();
            rt = DualRender::s_rt;
            if (!rt) { cocos2d::CCDirector::drawScene(); return; }
        }

        // --- FPS Limiter for OBS ---
        static auto s_lastObsFrameTime = std::chrono::high_resolution_clock::now();
        int targetFps = Mod::get()->getSettingValue<int>("obs-fps");
        bool shouldRenderObs = true;
        
        if (targetFps > 0) {
            auto now = std::chrono::high_resolution_clock::now();
            float elapsed = std::chrono::duration<float>(now - s_lastObsFrameTime).count();
            float frameTime = 1.0f / targetFps;
            if (elapsed < frameTime) {
                shouldRenderObs = false; // Skip this frame for OBS
            } else {
                s_lastObsFrameTime = now;
            }
        }

        // --- THE ASYMMETRIC SWAP ---
        // Game is natively in Layout Mode state right now!
        
        if (shouldRenderObs) {
            // ─── OBS PASS: Restore, Render, Re-apply ───
            DualRender::s_isLayoutPass = false;
            
            LayoutStateSwapper::restoreOriginal(); // Put normal properties back
            
            rt->beginWithClear(0.f, 0.f, 0.f, 1.f, 1.f, 0);
            scene->visit();
            rt->end();
            SpoutLife::send();
            
            LayoutStateSwapper::applyLayout(); // Put layout properties back
        }

        // ─── SCREEN PASS: Layout Mode ───
        // Properties are ALREADY in Layout Mode state!
        DualRender::s_isLayoutPass = true;
        
        LayoutLook::apply(pl);
        cocos2d::CCDirector::drawScene();
        LayoutLook::restore();
        
        DualRender::s_isLayoutPass = false;
    }
};

$on_mod(Loaded) {
    log::info("Layout Mode OBS Bypass Ultimate loaded!");
}
