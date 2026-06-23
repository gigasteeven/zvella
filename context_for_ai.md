# Context: Layout Mode OBS Bypass — Updated

## What Changed (latest rewrite)

### Root Cause of Previous Bug
`GameObject::visit()` hook did NOT work for hiding decorations because GD uses **CCSpriteBatchNode** for batch rendering. In batch mode, individual sprite `visit()` calls don't control drawing — the batch node renders all children together. Our `return` in `visit()` had zero effect.

### New Approach: setVisible() + addObject cache
1. **`PlayLayer::addObject` hook** — every object added to the level is checked against the decoration list (`LayoutConfig::isDecoration`). Decorations are cached in `DecoCache::g_objects`.
2. **Before layout pass** (screen): `DecoCache::hide()` sets `setVisible(false)` on ALL cached decorations. `setVisible(false)` IS respected by CCSpriteBatchNode.
3. **After layout pass**: `DecoCache::show()` restores visibility.
4. **BG/Ground colors**: `LayoutLook::apply()` temporarily recolors background/ground nodes, `LayoutLook::restore()` puts them back.

### Remaining Potential Issues
1. **Glow removal**: Not implemented yet. Non-decoration objects still show glow in layout pass. Could be added by iterating objects and temporarily setting `m_hasNoGlow = true`.
2. **`m_background`, `m_groundLayer`, `m_groundLayer2`, `m_middleground`**: These member names are from Geode bindings. If they don't exist or are named differently in GD 2.2081 bindings, compilation will fail. Check the bindings.
3. **Spout2 sender**: Must verify `CreateSender` actually succeeds. Check Geode console logs for "Spout 'GD_Clean' ready" vs "Spout 'GD_Clean' FAILED".
4. **User MUST disable Eclipse's Layout Mode** — Eclipse modifies objects destructively at creation time, which makes our clean pass also show layout mode.
