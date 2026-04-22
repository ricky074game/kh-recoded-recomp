#include "sdl_renderer.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <cmath>

SDL2Renderer::SDL2Renderer(SDL_Renderer* sdl_renderer) {
    m_framebuffer_top = nullptr;
    m_framebuffer_bottom = nullptr;
    if (sdl_renderer) {
        m_framebuffer_top = SDL_CreateTexture(sdl_renderer, SDL_PIXELFORMAT_RGBA8888,
                                              SDL_TEXTUREACCESS_STREAMING, 256, 192);
        m_framebuffer_bottom = SDL_CreateTexture(sdl_renderer, SDL_PIXELFORMAT_RGBA8888,
                                                 SDL_TEXTUREACCESS_STREAMING, 256, 192);
    }
    std::memset(m_pixels_top, 0, sizeof(m_pixels_top));
    std::memset(m_pixels_bottom, 0, sizeof(m_pixels_bottom));
}

SDL2Renderer::~SDL2Renderer() {
    if (m_framebuffer_top) {
        SDL_DestroyTexture(m_framebuffer_top);
    }
    if (m_framebuffer_bottom) {
        SDL_DestroyTexture(m_framebuffer_bottom);
    }
}

// Simple edge function for rasterization
static inline int EdgeFunction(const std::pair<int, int>& a, const std::pair<int, int>& b, const std::pair<int, int>& c) {
    return (c.first - a.first) * (b.second - a.second) - (c.second - a.second) * (b.first - a.first);
}

static inline uint32_t PackRGBA(uint32_t r, uint32_t g, uint32_t b, uint32_t a) {
    return ((r & 0xFF) << 24) | ((g & 0xFF) << 16) | ((b & 0xFF) << 8) | (a & 0xFF);
}

static inline void UnpackRGBA(uint32_t color, uint32_t& r, uint32_t& g, uint32_t& b, uint32_t& a) {
    r = (color >> 24) & 0xFF;
    g = (color >> 16) & 0xFF;
    b = (color >> 8) & 0xFF;
    a = color & 0xFF;
}

static bool IsInWindowRect(uint8_t x1, uint8_t x2, uint8_t y1, uint8_t y2, int x, int y) {
    const bool x_in = (x1 <= x2) ? (x >= x1 && x < x2) : (x >= x1 || x < x2);
    const bool y_in = (y1 <= y2) ? (y >= y1 && y < y2) : (y >= y1 || y < y2);
    return x_in && y_in;
}

static uint8_t ResolveWindowMask(const WindowControl& windows, int x, int y) {
    if (IsInWindowRect(windows.win0_x1, windows.win0_x2, windows.win0_y1, windows.win0_y2, x, y)) {
        return windows.winin_win0;
    }
    if (IsInWindowRect(windows.win1_x1, windows.win1_x2, windows.win1_y1, windows.win1_y2, x, y)) {
        return windows.winin_win1;
    }
    return windows.winout_outside;
}

static uint32_t BlendPixel(uint32_t dst, uint32_t src, const BlendControl& blend, bool effects_enabled) {
    if (!effects_enabled || blend.mode == 0) {
        return src;
    }

    uint32_t sr, sg, sb, sa;
    uint32_t dr, dg, db, da;
    UnpackRGBA(src, sr, sg, sb, sa);
    UnpackRGBA(dst, dr, dg, db, da);

    if (blend.mode == 1) {
        const uint32_t eva = std::min<uint32_t>(blend.eva, 16);
        const uint32_t evb = std::min<uint32_t>(blend.evb, 16);
        const uint32_t r = std::min<uint32_t>(255, (sr * eva + dr * evb) / 16);
        const uint32_t g = std::min<uint32_t>(255, (sg * eva + dg * evb) / 16);
        const uint32_t b = std::min<uint32_t>(255, (sb * eva + db * evb) / 16);
        return PackRGBA(r, g, b, 0xFF);
    }

    if (blend.mode == 2) {
        const uint32_t evy = std::min<uint32_t>(blend.evy, 16);
        const uint32_t r = std::min<uint32_t>(255, sr + ((255 - sr) * evy) / 16);
        const uint32_t g = std::min<uint32_t>(255, sg + ((255 - sg) * evy) / 16);
        const uint32_t b = std::min<uint32_t>(255, sb + ((255 - sb) * evy) / 16);
        return PackRGBA(r, g, b, 0xFF);
    }

    const uint32_t evy = std::min<uint32_t>(blend.evy, 16);
    const uint32_t r = (sr * (16 - evy)) / 16;
    const uint32_t g = (sg * (16 - evy)) / 16;
    const uint32_t b = (sb * (16 - evy)) / 16;
    return PackRGBA(r, g, b, 0xFF);
}

void SDL2Renderer::SubmitFrame(const std::vector<GXVertex>& vertices, 
                               const std::vector<GXPolygon>& polygons) {
    std::lock_guard<std::mutex> lock(m_render_mutex);
    
    // Clear top screen to black
    for (int i = 0; i < 256 * 192; ++i) {
        m_pixels_top[i] = 0x000000FF; // RGBA
    }

    // Software Rasterizer for DS 3D Geometry
    for (const auto& poly : polygons) {
        if (poly.vertex_count < 3) continue;

        // Extract and project vertices for the polyhedron
        std::vector<std::pair<int, int>> screen_pts;
        for (int i = 0; i < poly.vertex_count; ++i) {
            uint32_t idx = poly.vertex_indices[i];
            if (idx >= vertices.size()) continue;
            
            const auto& v = vertices[idx];
            // Perspective divide
            float ndc_x = v.position.x;
            float ndc_y = v.position.y;
            if (v.position.w != 0.0f) {
                ndc_x /= v.position.w;
                ndc_y /= v.position.w;
            }
            
            // Viewport transform: 
            // map (-1.0 to 1.0) -> (0 to 256) and (-1.0 to 1.0) -> (0 to 192)
            int sx = static_cast<int>((ndc_x + 1.0f) * 128.0f);
            int sy = static_cast<int>((1.0f - ndc_y) * 96.0f); // DS Y goes down
            screen_pts.push_back({sx, sy});
        }

        // Fan triangulate
        for (size_t i = 1; i + 1 < screen_pts.size(); ++i) {
            auto v0 = screen_pts[0];
            auto v1 = screen_pts[i];
            auto v2 = screen_pts[i + 1];

            // Bounding box for the triangle
            int minX = std::max(0, std::min({v0.first, v1.first, v2.first}));
            int minY = std::max(0, std::min({v0.second, v1.second, v2.second}));
            int maxX = std::min(255, std::max({v0.first, v1.first, v2.first}));
            int maxY = std::min(191, std::max({v0.second, v1.second, v2.second}));

            // Calculate triangle area for barycentric coordinates
            int area = EdgeFunction(v0, v1, v2);
            if (area == 0) continue;
            // Backface culling
            if (area < 0) {
                std::swap(v1, v2); 
                area = -area;
            }

            // Rasterize
            for (int y = minY; y <= maxY; ++y) {
                for (int x = minX; x <= maxX; ++x) {
                    std::pair<int, int> p{x, y};
                    int w0 = EdgeFunction(v1, v2, p);
                    int w1 = EdgeFunction(v2, v0, p);
                    int w2 = EdgeFunction(v0, v1, p);

                    if (w0 >= 0 && w1 >= 0 && w2 >= 0) {
                        // Inside triangle - map color from first vertex
                        uint32_t r = 255, g = 255, b = 255, a = 255;
                        if (poly.vertex_count > 0) {
                            r = static_cast<uint32_t>(std::clamp(vertices[poly.vertex_indices[0]].r * 255.0f, 0.0f, 255.0f));
                            g = static_cast<uint32_t>(std::clamp(vertices[poly.vertex_indices[0]].g * 255.0f, 0.0f, 255.0f));
                            b = static_cast<uint32_t>(std::clamp(vertices[poly.vertex_indices[0]].b * 255.0f, 0.0f, 255.0f));
                        }
                        
                        m_pixels_top[y * 256 + x] = (r << 24) | (g << 16) | (b << 8) | a;
                    }
                }
            }
        }
    }
}

void SDL2Renderer::SubmitFrame2D(const std::vector<Sprite2D>& sprites,
                                 const std::array<BGLayer2D, 4>& bg_layers,
                                 const BlendControl& blend,
                                 const WindowControl& windows) {
    std::lock_guard<std::mutex> lock(m_render_mutex);
    
    // Clear bottom screen to transparent/black
    for (int i = 0; i < 256 * 192; ++i) m_pixels_bottom[i] = 0x000000FF;

    // Render BG and OBJ by priority (3 = lowest, 0 = highest).
    for (int p = 3; p >= 0; --p) {
        for (int i = 0; i < 4; ++i) {
            const auto& bg = bg_layers[i];
            if (!bg.enabled || bg.priority != p) continue;

            int32_t current_ref_x = bg.affine.ref_x;
            int32_t current_ref_y = bg.affine.ref_y;

            for (int y = 0; y < 192; ++y) {
                int32_t map_x = current_ref_x;
                int32_t map_y = current_ref_y;

                for (int x = 0; x < 256; ++x) {
                    const uint8_t windowMask = ResolveWindowMask(windows, x, y);
                    const bool bg_enabled_in_window = ((windowMask >> i) & 0x1) != 0;
                    const bool effects_enabled = ((windowMask >> 5) & 0x1) != 0;
                    if (!bg_enabled_in_window) continue;

                    int px_x = 0;
                    int px_y = 0;

                    if (bg.mode == 0) {
                        px_x = (x + bg.scroll_x) & 0x1FF;
                        px_y = (y + bg.scroll_y) & 0x1FF;
                    } else {
                        px_x = map_x >> 8;
                        px_y = map_y >> 8;
                        map_x += bg.affine.pa;
                        map_y += bg.affine.pc;
                    }

                    if (px_x < 0 || px_x >= 512 || px_y < 0 || px_y >= 512) continue;

                    const bool tile_on = ((px_x / 8) + (px_y / 8)) % 2 == 0;
                    if (!tile_on) continue;

                    const uint32_t src = PackRGBA((i * 60) + 80, px_x & 0xFF, px_y & 0xFF, 0xFF);
                    uint32_t& dst = m_pixels_bottom[y * 256 + x];
                    dst = BlendPixel(dst, src, blend, effects_enabled);
                }

                current_ref_x += bg.affine.pb;
                current_ref_y += bg.affine.pd;
            }
        }

        for (const auto& sprite : sprites) {
            if (sprite.priority != p) continue;

            const int sx = static_cast<int>(sprite.x);
            const int sy = static_cast<int>(sprite.y);
            const int sw = static_cast<int>(sprite.width);
            const int sh = static_cast<int>(sprite.height);
            const int cx = sw / 2;
            const int cy = sh / 2;

            for (int py = 0; py < sh; ++py) {
                for (int px = 0; px < sw; ++px) {
                    int src_x = px;
                    int src_y = py;

                    if (sprite.has_affine) {
                        const int dx = (px - cx) << 8;
                        const int dy = (py - cy) << 8;
                        const int tx = (sprite.affine_pa * dx + sprite.affine_pb * dy) >> 16;
                        const int ty = (sprite.affine_pc * dx + sprite.affine_pd * dy) >> 16;
                        src_x = tx + cx;
                        src_y = ty + cy;
                    }

                    if (src_x < 0 || src_x >= sw || src_y < 0 || src_y >= sh) continue;

                    const int tx = sx + px;
                    const int ty = sy + py;
                    if (tx < 0 || tx >= 256 || ty < 0 || ty >= 192) continue;

                    const uint8_t windowMask = ResolveWindowMask(windows, tx, ty);
                    const bool obj_enabled_in_window = ((windowMask >> 4) & 0x1) != 0;
                    const bool effects_enabled = ((windowMask >> 5) & 0x1) != 0;
                    if (!obj_enabled_in_window) continue;

                    const bool opaque_pixel = ((src_x + src_y + sprite.tile_index) & 0x3) != 0;
                    if (!opaque_pixel) continue;

                    const uint32_t src = PackRGBA(
                        180 + (sprite.tile_index % 60),
                        ((sprite.tile_index * 13) + src_x * 5) & 0xFF,
                        ((sprite.tile_index * 7) + src_y * 9) & 0xFF,
                        0xFF);
                    uint32_t& dst = m_pixels_bottom[ty * 256 + tx];
                    dst = BlendPixel(dst, src, blend, effects_enabled);
                }
            }
        }
    }
}

void SDL2Renderer::Present(SDL_Renderer* renderer, int x, int y, int w, int h) {
    if (renderer == nullptr || m_framebuffer_top == nullptr || m_framebuffer_bottom == nullptr) {
        return;
    }

    std::lock_guard<std::mutex> lock(m_render_mutex);
    
    SDL_UpdateTexture(m_framebuffer_top, nullptr, m_pixels_top, 256 * 4);
    SDL_UpdateTexture(m_framebuffer_bottom, nullptr, m_pixels_bottom, 256 * 4);

    SDL_Rect top_rect = {x, y, w, h / 2};
    SDL_Rect bottom_rect = {x, y + h / 2, w, h / 2};
    
    SDL_RenderCopy(renderer, m_framebuffer_top, nullptr, &top_rect);
    SDL_RenderCopy(renderer, m_framebuffer_bottom, nullptr, &bottom_rect);
}

void SDL2Renderer::CopyFramebuffers(std::array<uint32_t, 256 * 192>& top,
                                    std::array<uint32_t, 256 * 192>& bottom) {
    std::lock_guard<std::mutex> lock(m_render_mutex);
    std::copy(std::begin(m_pixels_top), std::end(m_pixels_top), top.begin());
    std::copy(std::begin(m_pixels_bottom), std::end(m_pixels_bottom), bottom.begin());
}
