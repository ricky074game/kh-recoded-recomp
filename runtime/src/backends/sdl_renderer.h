#pragma once
#include <SDL2/SDL.h>
#include <vector>
#include <array>
#include <mutex>
#include "hw_gxengine.h"
#include "hw_2d_engine.h"

// SDL2Renderer implements both 3D and 2D abstract interfaces.
// It receives frame data from the Geometry Engine and 2D Engines
// and converts them into an SDL_Texture for final display.
class SDL2Renderer : public GXRenderer, public Renderer2D {
public:
    SDL2Renderer(SDL_Renderer* sdl_renderer);
    ~SDL2Renderer();

    // From GXRenderer (3D)
    void SubmitFrame(const std::vector<GXVertex>& vertices, 
                     const std::vector<GXPolygon>& polygons) override;

    // From Renderer2D (2D)
    void SubmitFrame2D(const std::vector<Sprite2D>& sprites,
                       const std::array<BGLayer2D, 4>& bg_layers,
                       const BlendControl& blend,
                       const WindowControl& windows) override;

    // Draw the final combined framebuffer to the SDL screen
    void Present(SDL_Renderer* renderer, int x, int y, int w, int h);

    // Snapshot both DS framebuffers for multi-window presentation.
    void CopyFramebuffers(std::array<uint32_t, 256 * 192>& top,
                          std::array<uint32_t, 256 * 192>& bottom);

private:
    SDL_Texture* m_framebuffer_top;
    SDL_Texture* m_framebuffer_bottom;
    std::mutex m_render_mutex;

    // Internal buffers for pixel conversion
    uint32_t m_pixels_top[256 * 192];
    uint32_t m_pixels_bottom[256 * 192];

    void Render3D(const std::vector<GXVertex>& vertices, const std::vector<GXPolygon>& polygons);
    void Render2D(const std::vector<Sprite2D>& sprites, const std::array<BGLayer2D, 4>& bg_layers);
};
