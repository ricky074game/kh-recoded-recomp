#pragma once
#include <cstddef>
#include <vector>
#include <array>
#include <mutex>
#include <cstdint>
#include "hw_gxengine.h"
#include "hw_2d_engine.h"

// SoftwareRenderer implements both 3D and 2D abstract interfaces.
// It receives frame data from the Geometry Engine and 2D Engines
// and converts them into raw pixel buffers for final display.
class SoftwareRenderer : public GXRenderer, public Renderer2D {
public:
    SoftwareRenderer();
    ~SoftwareRenderer();

    // From GXRenderer (3D)
    void SubmitFrame(const std::vector<GXVertex>& vertices, 
                     const std::vector<GXPolygon>& polygons) override;

    // From Renderer2D (2D)
    void SubmitFrame2D(const std::vector<Sprite2D>& sprites,
                       const std::array<BGLayer2D, 4>& bg_layers,
                       const BlendControl& blend,
                       const WindowControl& windows,
                       bool render_to_top,
                       const uint8_t* vram_data = nullptr,
                       size_t vram_size = 0,
                       const uint8_t* palette_data = nullptr,
                       size_t palette_size = 0) override;

    // Snapshot both DS framebuffers for multi-window presentation.
    void CopyFramebuffers(std::array<uint32_t, 256 * 192>& top,
                          std::array<uint32_t, 256 * 192>& bottom);

    // Directly set both framebuffers from external pixel arrays (used by
    // the title screen loader to bypass the normal rendering pipeline).
    void SetFramebuffers(const uint32_t* top, const uint32_t* bottom);

private:
    std::mutex m_render_mutex;

    // Internal buffers for pixel conversion
    uint32_t m_pixels_top[256 * 192];
    uint32_t m_pixels_bottom[256 * 192];

    void Render3D(const std::vector<GXVertex>& vertices, const std::vector<GXPolygon>& polygons);
    void Render2D(const std::vector<Sprite2D>& sprites, const std::array<BGLayer2D, 4>& bg_layers);
};
