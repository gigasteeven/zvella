#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/CCDirector.hpp>
#include <Geode/modify/MenuLayer.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include <Geode/modify/EndLevelLayer.hpp>

#include "DualRender.hpp"
#include "LayoutConfig.hpp"
#include "SpoutWrapper.hpp"
#include "XDBotLayout.hpp"

using namespace geode::prelude;

// Layout mode color theme
static constexpr cocos2d::ccColor3B LAYOUT_BG    = { 40, 125, 255};
static constexpr cocos2d::ccColor3B LAYOUT_GROUND= {  0, 102, 255};
static constexpr cocos2d::ccColor3B LAYOUT_MG    = { 40, 125, 255};

namespace DualRender {
    extern bool s_inPlayLayer;
}
bool DualRender::s_inPlayLayer = false;
static cocos2d::CCLayerColor* g_solidBg = nullptr;

// ============================================================
// Custom Layout Shader
// ============================================================
static cocos2d::CCGLProgram* g_layoutShader = nullptr;

void initLayoutShader() {
    if (g_layoutShader) return;

    g_layoutShader = new cocos2d::CCGLProgram();
    
    const GLchar* vertSource = R"(
        uniform mat4 CC_MVPMatrix;
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
            gl_FragColor = vec4(texColor.rgb * v_fragmentColor.a, texColor.a * v_fragmentColor.a);
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
// LayoutLayerManager - Perfect Toggling
// ============================================================
namespace LayoutLayerManager {
    struct BatchData {
        cocos2d::CCSpriteBatchNode* node;
        cocos2d::CCGLProgram* origShader;
        bool isGlow;
    };
    static std::vector<BatchData> g_batches;
    static std::vector<GameObject*> g_decoObjects;
    static bool s_isCached = false;

    void clear() {
        g_batches.clear();
        g_decoObjects.clear();
        s_isCached = false;
    }

    void initCache(PlayLayer* pl) {
        if (s_isCached || !pl || !pl->m_objectLayer) return;

        auto children = pl->m_objectLayer->getChildren();
        if (!children) return;

        for (int i = 0; i < children->count(); ++i) {
            if (auto batch = typeinfo_cast<cocos2d::CCSpriteBatchNode*>(children->objectAtIndex(i))) {
                bool isGlow = (batch->getBlendFunc().dst == GL_ONE);
                g_batches.push_back({batch, batch->getShaderProgram(), isGlow});

                auto batchChildren = batch->getChildren();
                if (batchChildren) {
                    for (int j = 0; j < batchChildren->count(); ++j) {
                        if (auto obj = typeinfo_cast<GameObject*>(batchChildren->objectAtIndex(j))) {
                            int id = obj->m_objectID;
                            bool isDeco = XDBot::decoObjectIDs.contains(id) || obj->m_objectType == GameObjectType::Decoration;
                            bool isImportantDeco = false;
                            
                            if (isDeco && obj->m_groups) {
                                for (int g = 0; g < obj->m_groupCount; ++g) {
                                    if (XDBot::LayoutMode::s_importantGroups.contains(obj->m_groups->at(g))) {
                                        isImportantDeco = true;
                                        break;
                                    }
                                }
                            }

                            bool isHiddenSolid = (XDBot::solidObjectIDs.contains(id) && obj->getScale() <= 0.f);

                            bool shouldHideInLayout = false;
                            if (isDeco && !isImportantDeco) {
                                shouldHideInLayout = true;
                            } else if (isHiddenSolid) {
                                shouldHideInLayout = true;
                            } else if (XDBot::excludedTriggerIDs.contains(id)) {
                                shouldHideInLayout = true;
                            }

                            if (shouldHideInLayout) {
                                g_decoObjects.push_back(obj);
                            } else {
                                if (id == 2065) {
                                    g_decoObjects.push_back(obj);
                                }
                            }
                        }
                    }
                }
            }
        }
        s_isCached = true;
    }

    void applyLayoutMode() {
        for (auto obj : g_decoObjects) {
            obj->setVisible(false);
        }
        for (auto& b : g_batches) {
            if (b.isGlow) {
                b.node->setVisible(false);
            } else {
                b.node->setShaderProgram(g_layoutShader);
            }
        }
    }

    void restoreOriginalMode() {
        for (auto obj : g_decoObjects) {
            obj->setVisible(true);
        }
        for (auto& b : g_batches) {
            if (b.isGlow) {
                b.node->setVisible(true);
            } else {
                b.node->setShaderProgram(b.origShader);
            }
        }
    }
}

// ============================================================
// LayoutLook
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
        auto tex = DualRender::s_rt->getSprite()->getTexture();
        SpoutWrapper::sendTexture(
            tex->getName(),
            tex->getPixelsWide(), tex->getPixelsHigh(), true);
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
            if (!DualRender::s_active) {
                auto winSize = cocos2d::CCDirector::sharedDirector()->getWinSize();
                int w = (int)winSize.width;
                int h = (int)winSize.height;

                DualRender::s_rt = cocos2d::CCRenderTexture::create(
                    w, h, cocos2d::kCCTexture2DPixelFormat_RGBA8888, GL_DEPTH24_STENCIL8);
                if (DualRender::s_rt) DualRender::s_rt->retain();

                SpoutLife::start(w, h);
                DualRender::s_active = true;
                initLayoutShader();
            }
        }
        return true;
    }
};

// ============================================================
// PlayLayer Hooks
// ============================================================
class $modify(PlayLayer) {
    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
        if (level && !level->m_levelString.empty()) {
            XDBot::LayoutMode::parseImportantGroups(level->m_levelString);
        }
        DualRender::s_inPlayLayer = true;
        DualRender::s_active = true;
        LayoutLayerManager::clear();
        bool ret = PlayLayer::init(level, useReplay, dontCreateObjects);
        if (ret) {
            LayoutLayerManager::initCache(this);
            LayoutLook::initCache(this);
        }
        return ret;
    }

    void onQuit() {
        DualRender::s_inPlayLayer = false;
        LayoutLayerManager::clear();
        LayoutLook::clear();
        PlayLayer::onQuit();
    }
    
    void resetLevel() {
        LayoutLayerManager::restoreOriginalMode();
        PlayLayer::resetLevel();
        LayoutLook::initCache(this);
    }
};

// ============================================================
// CCDirector::drawScene — Global Spout Architecture
// ============================================================
class $modify(DualDirector, cocos2d::CCDirector) {
    void drawScene() {
        bool enabled = Mod::get()->getSettingValue<bool>("enabled");
        if (!enabled) {
            if (DualRender::s_active) {
                SpoutLife::stop();
                DualRender::s_active = false;
            }
            cocos2d::CCDirector::drawScene();
            return;
        }

        auto winSize = cocos2d::CCDirector::sharedDirector()->getWinSize();
        int w = (int)winSize.width;
        int h = (int)winSize.height;

        if (!DualRender::s_active) {
            DualRender::s_rt = cocos2d::CCRenderTexture::create(
                w, h, cocos2d::kCCTexture2DPixelFormat_RGBA8888, GL_DEPTH24_STENCIL8);
            if (DualRender::s_rt) DualRender::s_rt->retain();

            SpoutLife::start(w, h);
            DualRender::s_active = true;
            initLayoutShader();
        }

        auto* scene = getRunningScene();
        auto* pl    = DualRender::s_inPlayLayer ? PlayLayer::get() : nullptr;
        auto* rt    = DualRender::s_rt;

        if (w != DualRender::s_width || h != DualRender::s_height) {
            SpoutLife::stop();
            SpoutLife::start(w, h);
            DualRender::s_rt = cocos2d::CCRenderTexture::create(w, h, cocos2d::kCCTexture2DPixelFormat_RGBA8888, GL_DEPTH24_STENCIL8);
            if (DualRender::s_rt) DualRender::s_rt->retain();
            rt = DualRender::s_rt;
            if (!rt) { cocos2d::CCDirector::drawScene(); return; }
        }

        if (!scene || !rt || !DualRender::s_spoutInitialized) {
            if (pl) {
                LayoutLayerManager::applyLayoutMode();
                LayoutLook::apply();
            }
            cocos2d::CCDirector::drawScene();
            if (pl) {
                LayoutLook::restore();
                LayoutLayerManager::restoreOriginalMode();
            }
            return;
        }

        // ==========================================
        // GLOBAL CAPTURE (Menus, Editor, etc.)
        // ==========================================
        if (!pl) {
            cocos2d::CCDirector::drawScene();

            if (rt && DualRender::s_spoutInitialized) {
                GLint viewport[4];
                glGetIntegerv(GL_VIEWPORT, viewport);
                auto tex = rt->getSprite()->getTexture();
                ccGLBindTexture2D(tex->getName());
                glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, viewport[0], viewport[1], viewport[2], viewport[3]);
                SpoutLife::send();
            }
            return;
        }

        // ==========================================
        // PLAYLAYER CAPTURE (Layout Mode Bypass)
        // ==========================================

        // PASS 1: Clean render -> Spout (OBS)
        DualRender::s_isLayoutPass = false;
        rt->beginWithClear(0.f, 0.f, 0.f, 1.f, 1.f, 0);
        
        // We visit the entire scene so OBS gets the UI layer!
        scene->visit();
        
        rt->end();
        SpoutLife::send();

        // PASS 2: Layout mode render -> Screen (Player)
        DualRender::s_isLayoutPass = true;
        LayoutLayerManager::applyLayoutMode();
        LayoutLook::apply();

        cocos2d::CCDirector::drawScene();

        LayoutLook::restore();
        LayoutLayerManager::restoreOriginalMode();
        DualRender::s_isLayoutPass = false;
    }
};

$on_mod(Loaded) {
    log::info("Layout Mode OBS Bypass Global Edition loaded!");
}
