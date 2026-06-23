#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/CCDirector.hpp>
#include <Geode/modify/MenuLayer.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include <Geode/modify/EndLevelLayer.hpp>

#include "DualRender.hpp"
#include "LayoutConfig.hpp"
#include "SpoutWrapper.hpp"

using namespace geode::prelude;

// Layout mode color theme
static constexpr cocos2d::ccColor3B LAYOUT_BG    = { 40, 125, 255};
static constexpr cocos2d::ccColor3B LAYOUT_GROUND= {  0, 102, 255};
static constexpr cocos2d::ccColor3B LAYOUT_MG    = { 40, 125, 255};

static cocos2d::CCGLProgram* g_layoutShader = nullptr;

namespace DualRender {
    extern bool s_inPlayLayer;
}
bool DualRender::s_inPlayLayer = false;
static cocos2d::CCLayerColor* g_solidBg = nullptr;
static PauseLayer* g_pauseLayer = nullptr;
static EndLevelLayer* g_endLevelLayer = nullptr;

void initLayoutShader() {
    if (g_layoutShader) return;
    g_layoutShader = new cocos2d::CCGLProgram();
    
    const GLchar* fragSource = R"(
        #ifdef GL_ES
        precision mediump float;
        #endif
        varying vec4 v_fragmentColor;
        varying vec2 v_texCoord;
        uniform sampler2D CC_Texture0;
        void main()
        {
            vec4 texColor = texture2D(CC_Texture0, v_texCoord);
            // Cocos2d-x uses Premultiplied Alpha! We MUST multiply RGB by Alpha.
            // This fixes the "sharp color changes" (blown out edges when fading).
            gl_FragColor = vec4(texColor.rgb * v_fragmentColor.a, texColor.a * v_fragmentColor.a);
        }
    )";
    
    const GLchar* vertSource = R"(
        attribute vec4 a_position;
        attribute vec2 a_texCoord;
        attribute vec4 a_color;
        varying vec4 v_fragmentColor;
        varying vec2 v_texCoord;
        void main()
        {
            gl_Position = CC_MVPMatrix * a_position;
            v_fragmentColor = a_color;
            v_texCoord = a_texCoord;
        }
    )";
    
    g_layoutShader->initWithVertexShaderByteArray(vertSource, fragSource);
    g_layoutShader->addAttribute(kCCAttributeNamePosition, kCCVertexAttrib_Position);
    g_layoutShader->addAttribute(kCCAttributeNameColor, kCCVertexAttrib_Color);
    g_layoutShader->addAttribute(kCCAttributeNameTexCoord, kCCVertexAttrib_TexCoords);
    g_layoutShader->link();
    g_layoutShader->updateUniforms();
    g_layoutShader->retain();
}

// ============================================================
// LayoutFastSwapper (OPTIMIZED)
// ============================================================
namespace LayoutFastSwapper {
    static std::vector<GameObject*> g_decorations;
    
    struct BatchNodeData {
        cocos2d::CCSpriteBatchNode* node;
        cocos2d::CCGLProgram* origShader;
        bool isGlow;
    };
    static std::vector<BatchNodeData> g_batchNodes;
    static bool s_isCached = false;

    void clear() {
        g_decorations.clear();
        g_batchNodes.clear();
        s_isCached = false;
    }

    void tryAdd(GameObject* obj) {
        if (!obj) return;
        if (LayoutConfig::isDecoration(obj->m_objectID, obj->m_objectType)) {
            g_decorations.push_back(obj);
        }
    }

    void cacheBatchNodes(PlayLayer* pl) {
        if (s_isCached || !pl || !pl->m_objectLayer) return;
        auto children = pl->m_objectLayer->getChildren();
        if (children) {
            for (int i = 0; i < children->count(); i++) {
                auto node = static_cast<cocos2d::CCNode*>(children->objectAtIndex(i));
                if (auto batch = dynamic_cast<cocos2d::CCSpriteBatchNode*>(node)) {
                    bool isGlow = (batch->getBlendFunc().dst == GL_ONE);
                    g_batchNodes.push_back({batch, batch->getShaderProgram(), isGlow});
                }
            }
        }
        s_isCached = true;
    }

    void applyLayoutMode() {
        for (auto* obj : g_decorations) {
            obj->setVisible(false);
        }
        for (auto& b : g_batchNodes) {
            if (b.isGlow) {
                b.node->setVisible(false);
            } else {
                b.node->setShaderProgram(g_layoutShader);
            }
        }
    }

    void restoreOriginalMode() {
        for (auto* obj : g_decorations) {
            obj->setVisible(true);
        }
        for (auto& b : g_batchNodes) {
            if (b.isGlow) {
                b.node->setVisible(true);
            } else {
                b.node->setShaderProgram(b.origShader);
            }
        }
    }
}

// ============================================================
// LayoutLook (OPTIMIZED - Solid Background Replacement)
// ============================================================
namespace LayoutLook {
    struct CachedColorNode {
        cocos2d::CCNodeRGBA* node;
        cocos2d::ccColor3B origColor;
        cocos2d::ccColor3B targetColor;
    };
    static std::vector<CachedColorNode> g_cachedNodes;

    static void cacheNode(cocos2d::CCNode* n, cocos2d::ccColor3B target) {
        if (!n) return;
        if (auto* rgba = dynamic_cast<cocos2d::CCNodeRGBA*>(n)) {
            g_cachedNodes.push_back({rgba, rgba->getColor(), target});
        }
    }

    static void cacheChildren(cocos2d::CCNode* parent, cocos2d::ccColor3B target) {
        if (!parent || !parent->getChildren()) return;
        auto* ch = parent->getChildren();
        for (unsigned i = 0; i < ch->count(); ++i) {
            cacheNode(static_cast<cocos2d::CCNode*>(ch->objectAtIndex(i)), target);
        }
    }

    static void initCache(PlayLayer* pl) {
        g_cachedNodes.clear();
        if (!pl) return;
        
        // Hide original background completely and overlay a perfect solid blue layer
        if (pl->m_background) {
            pl->m_background->setVisible(false);
        }
        if (!g_solidBg) {
            g_solidBg = cocos2d::CCLayerColor::create(cocos2d::ccc4(LAYOUT_BG.r, LAYOUT_BG.g, LAYOUT_BG.b, 255));
            g_solidBg->setZOrder(-100);
            pl->addChild(g_solidBg);
            g_solidBg->retain();
        }

        cacheNode(pl->m_groundLayer, LAYOUT_GROUND);
        cacheChildren(pl->m_groundLayer, LAYOUT_GROUND);
        cacheNode(pl->m_groundLayer2, LAYOUT_GROUND);
        cacheChildren(pl->m_groundLayer2, LAYOUT_GROUND);
        cacheNode(pl->m_middleground, LAYOUT_MG);
        cacheChildren(pl->m_middleground, LAYOUT_MG);
    }

    static void apply() {
        if (g_solidBg) g_solidBg->setVisible(true);
        for (auto& c : g_cachedNodes) {
            c.origColor = c.node->getColor();
            c.node->setColor(c.targetColor);
        }
    }

    static void restore() {
        if (g_solidBg) g_solidBg->setVisible(false);
        for (auto& c : g_cachedNodes) {
            c.node->setColor(c.origColor);
        }
    }

    static void clear() {
        g_cachedNodes.clear();
        if (g_solidBg) {
            if (g_solidBg->getParent()) g_solidBg->removeFromParent();
            g_solidBg->release();
            g_solidBg = nullptr;
        }
    }
}

// ============================================================
// Spout lifecycle (Global)
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
// MenuLayer - Start Global Spout
// ============================================================
class $modify(DualMenuLayer, MenuLayer) {
    bool init() {
        if (!MenuLayer::init()) return false;
        
        if (Mod::get()->getSettingValue<bool>("enabled")) {
            initLayoutShader();
            
            if (!DualRender::s_active) {
                auto ws = cocos2d::CCDirector::sharedDirector()->getWinSizeInPixels();
                int w = static_cast<int>(ws.width);
                int h = static_cast<int>(ws.height);

                DualRender::s_rt = cocos2d::CCRenderTexture::create(
                    w, h, cocos2d::kCCTexture2DPixelFormat_RGBA8888, GL_DEPTH24_STENCIL8);
                if (DualRender::s_rt) DualRender::s_rt->retain();

                SpoutLife::start(w, h);
                DualRender::s_active = true;
                
                if (Loader::get()->isModLoaded("eclipse.layoutmode") || Loader::get()->isModLoaded("eclipse")) {
                    log::warn("ECLIPSE LAYOUT MODE DETECTED! Disable it for OBS bypass to work!");
                }
            }
        }
        return true;
    }
};

// ============================================================
// UI Hooks (O(1) Pause Menu Tracking)
// ============================================================
class $modify(DualPauseLayer, PauseLayer) {
    void customSetup() {
        PauseLayer::customSetup();
        g_pauseLayer = this;
    }
    void onExit() {
        g_pauseLayer = nullptr;
        PauseLayer::onExit();
    }
};

class $modify(DualEndLevelLayer, EndLevelLayer) {
    void showLayer(bool p0) {
        EndLevelLayer::showLayer(p0);
        g_endLevelLayer = this;
    }
    void onExit() {
        g_endLevelLayer = nullptr;
        EndLevelLayer::onExit();
    }
};

// ============================================================
// PlayLayer
// ============================================================
class $modify(DualPlayLayer, PlayLayer) {
    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
        LayoutFastSwapper::clear();
        DualRender::s_inPlayLayer = true;
        
        // Ensure Spout is alive if started from somewhere else directly
        if (!DualRender::s_active && Mod::get()->getSettingValue<bool>("enabled")) {
            initLayoutShader();
            auto ws = cocos2d::CCDirector::sharedDirector()->getWinSizeInPixels();
            int w = static_cast<int>(ws.width);
            int h = static_cast<int>(ws.height);
            DualRender::s_rt = cocos2d::CCRenderTexture::create(w, h, cocos2d::kCCTexture2DPixelFormat_RGBA8888, GL_DEPTH24_STENCIL8);
            if (DualRender::s_rt) DualRender::s_rt->retain();
            SpoutLife::start(w, h);
            DualRender::s_active = true;
        }

        if (!PlayLayer::init(level, useReplay, dontCreateObjects)) return false;

        if (DualRender::s_active) {
            LayoutLook::initCache(this);
        }
        return true;
    }

    void addObject(GameObject* obj) {
        PlayLayer::addObject(obj);
        LayoutFastSwapper::tryAdd(obj);
    }

    void onQuit() {
        DualRender::s_inPlayLayer = false;
        LayoutFastSwapper::clear();
        LayoutLook::clear();
        PlayLayer::onQuit();
    }
    
    void resetLevel() {
        LayoutFastSwapper::restoreOriginalMode();
        PlayLayer::resetLevel();
        LayoutLook::initCache(this);
    }
};

// ============================================================
// CCDirector::drawScene — Global Spout Architecture
// ============================================================
class $modify(DualDirector, cocos2d::CCDirector) {
    void drawScene() {
        if (!DualRender::s_active) {
            cocos2d::CCDirector::drawScene();
            return;
        }

        auto* scene = getRunningScene();
        auto* pl    = DualRender::s_inPlayLayer ? PlayLayer::get() : nullptr;
        auto* rt    = DualRender::s_rt;

        if (!scene || !rt || !DualRender::s_spoutInitialized) {
            if (pl) {
                LayoutFastSwapper::cacheBatchNodes(pl);
                LayoutFastSwapper::applyLayoutMode();
                LayoutLook::apply();
            }
            
            cocos2d::CCDirector::drawScene();
            
            if (pl) {
                LayoutLook::restore();
                LayoutFastSwapper::restoreOriginalMode();
            }
            return;
        }

        auto ws = getWinSizeInPixels();
        int w = static_cast<int>(ws.width);
        int h = static_cast<int>(ws.height);
        if (w != DualRender::s_width || h != DualRender::s_height) {
            SpoutLife::stop();
            SpoutLife::start(w, h);
            DualRender::s_rt = cocos2d::CCRenderTexture::create(w, h, cocos2d::kCCTexture2DPixelFormat_RGBA8888, GL_DEPTH24_STENCIL8);
            if (DualRender::s_rt) DualRender::s_rt->retain();
            rt = DualRender::s_rt;
            if (!rt) { cocos2d::CCDirector::drawScene(); return; }
        }

        // ==========================================
        // GLOBAL CAPTURE (Menus, Editor, etc.)
        // ==========================================
        if (!pl) {
            // Draw normal scene to Spout
            DualRender::s_isLayoutPass = false;
            rt->beginWithClear(0.f, 0.f, 0.f, 1.f, 1.f, 0);
            scene->visit();
            rt->end();
            SpoutLife::send();

            // Draw normal scene to Screen
            cocos2d::CCDirector::drawScene();
            return;
        }

        // ==========================================
        // PLAYLAYER CAPTURE (Layout Mode Bypass)
        // ==========================================
        LayoutFastSwapper::cacheBatchNodes(pl);

        // ─── PASS 1: Clean render → texture → Spout (for OBS) ───
        DualRender::s_isLayoutPass = false;
        rt->beginWithClear(0.f, 0.f, 0.f, 1.f, 1.f, 0);
        
        // Optimize FPS: Only visit PlayLayer and necessary standard UI layers.
        // We AVOID scene->visit() to skip rendering 70+ mod overlays twice.
        if (pl) {
            if (pl->m_background) pl->m_background->setVisible(true);
            pl->visit();
            if (pl->m_background) pl->m_background->setVisible(false);
        }

        if (g_pauseLayer) g_pauseLayer->visit();
        if (g_endLevelLayer) g_endLevelLayer->visit();
        
        rt->end();
        SpoutLife::send();

        // ─── PASS 2: Layout mode → screen (for player) ───
        DualRender::s_isLayoutPass = true;
        
        LayoutFastSwapper::applyLayoutMode();
        LayoutLook::apply();

        cocos2d::CCDirector::drawScene();

        LayoutLook::restore();
        LayoutFastSwapper::restoreOriginalMode();
        
        DualRender::s_isLayoutPass = false;
    }
};

$on_mod(Loaded) {
    log::info("Layout Mode OBS Bypass Global Edition loaded!");
}
