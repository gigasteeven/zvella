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

static cocos2d::CCGLProgram* g_layoutShader = nullptr;

void initLayoutShader() {
    if (g_layoutShader) return;
    g_layoutShader = new cocos2d::CCGLProgram();
    
    const GLchar* fragSource = R"(
        varying vec4 v_fragmentColor;
        varying vec2 v_texCoord;
        uniform sampler2D CC_Texture0;
        void main()
        {
            vec4 texColor = texture2D(CC_Texture0, v_texCoord);
            if (texColor.a > 0.0) {
                // Force white color, keep original alpha
                gl_FragColor = vec4(1.0, 1.0, 1.0, texColor.a);
            } else {
                gl_FragColor = vec4(0.0);
            }
        }
    )";
    
    // Standard Cocos2d-x vertex shader
    const GLchar* vertSource = R"(
        attribute vec4 a_position;
        attribute vec2 a_texCoord;
        attribute vec4 a_color;
        varying vec4 v_fragmentColor;
        varying vec2 v_texCoord;
        void main()
        {
            gl_Position = CC_PMatrix * a_position;
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
// LayoutFastSwapper — ZERO CPU OVERHEAD
// Uses Shaders and Visibility toggles. No VBO rebuilds!
// ============================================================
namespace LayoutFastSwapper {
    static std::vector<GameObject*> g_decorations;
    
    struct SavedShader { cocos2d::CCNode* node; cocos2d::CCGLProgram* shader; };
    static std::vector<SavedShader> g_savedShaders;
    
    struct SavedVisibility { cocos2d::CCNode* node; bool visible; };
    static std::vector<SavedVisibility> g_savedGlowNodes;

    void clear() {
        g_decorations.clear();
        g_savedShaders.clear();
        g_savedGlowNodes.clear();
    }

    void tryAdd(GameObject* obj) {
        if (!obj) return;
        if (LayoutConfig::isDecoration(obj->m_objectID, obj->m_objectType)) {
            g_decorations.push_back(obj);
        }
    }

    void applyLayoutMode(PlayLayer* pl) {
        if (!pl || !pl->m_objectLayer) return;

        // 1. Hide decorations (Fast)
        for (auto* obj : g_decorations) {
            obj->setVisible(false);
        }

        // 2. Hide Glow & Apply White Shader to blocks
        auto children = pl->m_objectLayer->getChildren();
        if (children) {
            for (int i = 0; i < children->count(); i++) {
                auto node = static_cast<cocos2d::CCNode*>(children->objectAtIndex(i));
                if (auto batch = dynamic_cast<cocos2d::CCSpriteBatchNode*>(node)) {
                    // Check if this is a glow batch node (Additive blending)
                    cocos2d::ccBlendFunc blend = batch->getBlendFunc();
                    if (blend.dst == GL_ONE) {
                        g_savedGlowNodes.push_back({batch, batch->isVisible()});
                        batch->setVisible(false);
                    } else {
                        // Regular blocks: apply white shader!
                        g_savedShaders.push_back({batch, batch->getShaderProgram()});
                        batch->setShaderProgram(g_layoutShader);
                    }
                }
            }
        }
    }

    void restoreOriginalMode() {
        // 1. Restore decorations
        for (auto* obj : g_decorations) {
            obj->setVisible(true);
        }

        // 2. Restore Shaders
        for (auto& st : g_savedShaders) {
            if (st.node) st.node->setShaderProgram(st.shader);
        }
        g_savedShaders.clear();

        // 3. Restore Glow
        for (auto& st : g_savedGlowNodes) {
            if (st.node) st.node->setVisible(st.visible);
        }
        g_savedGlowNodes.clear();
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
        LayoutFastSwapper::clear();
        initLayoutShader();

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
        }
        return true;
    }

    void addObject(GameObject* obj) {
        PlayLayer::addObject(obj);
        LayoutFastSwapper::tryAdd(obj);
    }

    void onQuit() {
        DualRender::s_active = false;
        LayoutFastSwapper::clear();
        SpoutLife::stop();
        PlayLayer::onQuit();
    }
    
    void resetLevel() {
        // Ensure visible before reset
        LayoutFastSwapper::restoreOriginalMode();
        PlayLayer::resetLevel();
    }
};

// ============================================================
// CCDirector::drawScene — Fast Dual Render
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
            LayoutFastSwapper::applyLayoutMode(pl);
            LayoutLook::apply(pl);
            cocos2d::CCDirector::drawScene();
            LayoutLook::restore();
            LayoutFastSwapper::restoreOriginalMode();
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
                shouldRenderObs = false;
            } else {
                s_lastObsFrameTime = now;
            }
        }

        if (shouldRenderObs) {
            // ─── PASS 1: Clean render → texture → Spout (for OBS) ───
            // Everything is in its NORMAL state right now!
            DualRender::s_isLayoutPass = false;
            rt->beginWithClear(0.f, 0.f, 0.f, 1.f, 1.f, 0);
            scene->visit();
            rt->end();
            SpoutLife::send();
        }

        // ─── PASS 2: Layout mode → screen (for player) ───
        DualRender::s_isLayoutPass = true;
        
        // Fast Layout Apply (Shaders + Visibility toggles = 0 CPU overhead)
        LayoutFastSwapper::applyLayoutMode(pl);
        LayoutLook::apply(pl);

        cocos2d::CCDirector::drawScene();

        // Restore fast
        LayoutLook::restore();
        LayoutFastSwapper::restoreOriginalMode();
        
        DualRender::s_isLayoutPass = false;
    }
};

$on_mod(Loaded) {
    log::info("Layout Mode OBS Bypass ShaderEdition loaded!");
}
