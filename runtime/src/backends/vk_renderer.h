#pragma once

#include <cstdint>
#include <vector>
#include <vulkan/vulkan.h>

#include "hw_gxengine.h"

namespace Backend::Vulkan {
    void InitContext();
    void ShutdownContext();

    void SetRenderTarget(VkFramebuffer framebuffer, uint32_t width, uint32_t height);
    void UploadFrameGeometry(const std::vector<GXVertex>& vertices,
                             const std::vector<GXPolygon>& polygons);
    void UploadMatrixStack(const Mat4& projection, const Mat4& model_view);

    void InitializeShaders();
    void ManageCommandBuffers();
    void TranslateMatrixStack(); // Handle software matrix stack translation
    void SubmitFrame();
    bool IsReady();
}
