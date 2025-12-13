// ============================================================================
// hud/pointer_widget.h
// Pointer widget - customizable mouse pointer rendered with quads
// ============================================================================
#pragma once

#include "base_hud.h"
#include "../core/plugin_constants.h"

class PointerWidget : public BaseHud {
    friend class SettingsManager;

public:
    PointerWidget();
    virtual ~PointerWidget() = default;

    void update() override;
    bool handlesDataType(DataChangeType dataType) const override;
    void resetToDefaults();

    // Override setScale to clamp to reasonable range for pointer
    void setScale(float scale) override;

protected:
    void rebuildLayout() override;

private:
    void rebuildRenderData() override;

    // Create sprite-based pointer (single quad with TGA texture)
    void createPointerSprite(float x, float y);

    // Create quad-based pointer (4 quads forming arrow shape)
    void createPointerQuads(float x, float y);

    // Helper to create a triangle quad (degenerate quad with two vertices at same point)
    static void createTriangleQuad(SPluginQuad_t& quad,
                                   float x0, float y0,
                                   float x1, float y1,
                                   float x2, float y2,
                                   unsigned long color);

    // Display constants (base size at scale 1.0)
    static constexpr float BASE_SIZE = 0.04f;  // ~43 pixels at 1920x1080
};
