#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/CCDirector.hpp>

#include "DualRender.hpp"
#include "LayoutConfig.hpp"
#include "SpoutWrapper.hpp"

using namespace geode::prelude;

// Layout mode color theme
static constexpr cocos2d::ccColor3B LAYOUT_BG    = { 40, 125, 255};
static constexpr cocos2d::ccColor3B LAYOUT_GROUND= {  0, 102, 255};
static constexpr cocos2d::ccColor3B LAYOUT_MG    = { 40, 125, 255};

// ============================================================
// DecoCache — cache decoration objects at addObject time,
// toggle visibility per frame. Works with batch rendering
// because setVisible(false) is respected by CCSpriteBatchNode.
// ============================================================
namespace DecoCache {
    static std::vector<GameObject*> g_objects;

    void clear() { g_objects.clear(); }

    void tryAdd(GameObject* obj) {
        if (LayoutConfig::isDecoration(obj->m_objectID, obj->m_objectType)) {
            g_objects.push_back(obj);
        }
    }

    void hide() {
        for (auto* o : g_objects) o->setVisible(false);
    }

    void show() {
        for (auto* o : g_objects) o->setVisible(true);
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
        DecoCache::clear();

        // PlayLayer::init() calls addObject() for every object in the level.
        // Our hooked addObject below caches decorations during this process.
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

            log::info("Dual render ON — {} decorations cached", DecoCache::g_objects.size());
        }
        return true;
    }

    // Called for EVERY object added to the level (during init and gameplay).
    // We always cache — the cache is only USED when s_active is true.
    void addObject(GameObject* obj) {
        PlayLayer::addObject(obj);
        DecoCache::tryAdd(obj);
    }

    void onQuit() {
        DualRender::s_active = false;
        DecoCache::clear();
        SpoutLife::stop();
        PlayLayer::onQuit();
    }

    // Also handle resetLevel (practice mode respawn etc.)
    void resetLevel() {
        // Ensure all objects are visible before reset
        if (DualRender::s_active) DecoCache::show();
        PlayLayer::resetLevel();
    }
};

// ============================================================
// CCDirector::drawScene — dual render core
//
// Pass 1 (Clean → OBS):  All objects VISIBLE, normal colors
//                         → render to CCRenderTexture → send via Spout
// Pass 2 (Layout → Screen): Decorations HIDDEN, BG/Ground recolored
//                         → render to default framebuffer (screen)
//
// HUD elements (CPS, labels etc.) are CCNodes not GameObjects,
// so they're untouched and visible in both passes.
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

        // No RT or Spout failed → still show layout on screen
        if (!scene || !rt || !DualRender::s_spoutInitialized) {
            DecoCache::hide();
            LayoutLook::apply(pl);
            cocos2d::CCDirector::drawScene();
            LayoutLook::restore();
            DecoCache::show();
            return;
        }

        // Handle window resize
        auto ws = getWinSizeInPixels();
        int w = static_cast<int>(ws.width);
        int h = static_cast<int>(ws.height);
        if (w != DualRender::s_width || h != DualRender::s_height) {
            SpoutLife::stop();
            SpoutLife::start(w, h);
            DualRender::s_rt = cocos2d::CCRenderTexture::create(
                w, h, cocos2d::kCCTexture2DPixelFormat_RGBA8888);
            if (DualRender::s_rt) DualRender::s_rt->retain();
            rt = DualRender::s_rt;
            if (!rt) { cocos2d::CCDirector::drawScene(); return; }
        }

        // ─── PASS 1: Clean render → texture → Spout (for OBS) ───
        // All decorations visible, original colors — normal gameplay.
        DualRender::s_isLayoutPass = false;
        rt->beginWithClear(0.f, 0.f, 0.f, 1.f, 1.f, 0);
        scene->visit();
        rt->end();
        SpoutLife::send();

        // ─── PASS 2: Layout mode → screen (for player) ───
        // Hide decorations, recolor BG/Ground, then render.
        DualRender::s_isLayoutPass = true;
        DecoCache::hide();
        LayoutLook::apply(pl);

        cocos2d::CCDirector::drawScene();

        LayoutLook::restore();
        DecoCache::show();
        DualRender::s_isLayoutPass = false;
    }
};

// ============================================================
$on_mod(Loaded) {
    log::info("Layout Mode OBS Bypass v1.0 loaded!");
    log::info("IMPORTANT: Disable Eclipse's Layout Mode! This mod handles it.");
    log::info("In OBS: Add 'Spout2 Capture' → select sender from mod settings.");
}
