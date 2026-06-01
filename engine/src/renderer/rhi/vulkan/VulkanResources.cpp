#include "renderer/rhi/vulkan/VulkanResources.h"

#if defined(NARAGAME_HAVE_VULKAN)

#include "NaraGame/core/Log.h"

#include <cstring>
#include <vector>

namespace NaraGame {
namespace {

VkDeviceSize AlignUp(VkDeviceSize value, VkDeviceSize align) {
    return (value + align - 1) & ~(align - 1);
}

VkFormat ToVkFormat(VertexFormat format) {
    switch (format) {
        case VertexFormat::Float:      return VK_FORMAT_R32_SFLOAT;
        case VertexFormat::Float2:     return VK_FORMAT_R32G32_SFLOAT;
        case VertexFormat::Float3:     return VK_FORMAT_R32G32B32_SFLOAT;
        case VertexFormat::Float4:     return VK_FORMAT_R32G32B32A32_SFLOAT;
        case VertexFormat::UByte4Norm: return VK_FORMAT_R8G8B8A8_UNORM;
    }
    return VK_FORMAT_UNDEFINED;
}

} // namespace

uint32_t FindMemoryType(VkPhysicalDevice physicalDevice, uint32_t typeBits,
                        VkMemoryPropertyFlags properties) {
    VkPhysicalDeviceMemoryProperties props{};
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &props);
    for (uint32_t i = 0; i < props.memoryTypeCount; ++i) {
        if ((typeBits & (1u << i)) &&
            (props.memoryTypes[i].propertyFlags & properties) == properties)
            return i;
    }
    NARAGAME_ERROR("Vulkan: no suitable memory type");
    return 0;
}

// ===========================================================================
// VulkanBuffer
// ===========================================================================
VulkanBuffer::VulkanBuffer(const VulkanContext& ctx, const BufferDesc& desc) : m_ctx(&ctx) {
    m_dynamic = desc.usage == BufferUsage::Dynamic;

    VkBufferUsageFlags usage = 0;
    switch (desc.type) {
        case BufferType::Vertex:  usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT; break;
        case BufferType::Index:   usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT; break;
        case BufferType::Uniform: usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT; break;
    }

    m_align = (desc.type == BufferType::Uniform) ? ctx.minUniformAlignment : 4;
    m_capacity = desc.size;
    if (desc.type == BufferType::Uniform)
        m_capacity = AlignUp(desc.size, m_align) * 64; // ring of 64 slots per frame

    VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bi.size = m_capacity;
    bi.usage = usage;
    bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    vkCreateBuffer(ctx.device, &bi, nullptr, &m_buffer);

    VkMemoryRequirements req{};
    vkGetBufferMemoryRequirements(ctx.device, m_buffer, &req);
    VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    ai.allocationSize = req.size;
    ai.memoryTypeIndex = FindMemoryType(ctx.physicalDevice, req.memoryTypeBits,
                                        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                            VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    vkAllocateMemory(ctx.device, &ai, nullptr, &m_memory);
    vkBindBufferMemory(ctx.device, m_buffer, m_memory, 0);
    vkMapMemory(ctx.device, m_memory, 0, m_capacity, 0, &m_mapped);

    if (desc.initialData != nullptr && desc.size > 0)
        std::memcpy(m_mapped, desc.initialData, desc.size);
}

VulkanBuffer::~VulkanBuffer() {
    if (m_mapped)
        vkUnmapMemory(m_ctx->device, m_memory);
    if (m_buffer)
        vkDestroyBuffer(m_ctx->device, m_buffer, nullptr);
    if (m_memory)
        vkFreeMemory(m_ctx->device, m_memory, nullptr);
}

void VulkanBuffer::Update(const void* data, size_t size, size_t offset) {
    if (m_mapped == nullptr || data == nullptr)
        return;

    if (!m_dynamic) {
        std::memcpy(static_cast<char*>(m_mapped) + offset, data, size);
        m_currentOffset = offset;
        return;
    }

    const uint64_t frame = m_ctx->frame ? *m_ctx->frame : 0;
    if (frame != m_lastFrame) { // first update of a new frame: reset the ring
        m_cursor = 0;
        m_lastFrame = frame;
    }
    VkDeviceSize off = AlignUp(m_cursor, m_align);
    if (off + size > m_capacity)
        off = 0; // wrap (not expected within a single frame)
    std::memcpy(static_cast<char*>(m_mapped) + off, data, size);
    m_currentOffset = off;
    m_cursor = off + size;
}

// ===========================================================================
// VulkanTexture
// ===========================================================================
VulkanTexture::VulkanTexture(const VulkanContext& ctx, const TextureDesc& desc)
    : m_ctx(&ctx), m_width(desc.width), m_height(desc.height) {
    const bool isRT = desc.renderTarget;
    const VkDeviceSize imageBytes = static_cast<VkDeviceSize>(desc.width) * desc.height * 4;

    // Render-target textures skip the staging buffer; they're written into by
    // the GPU via SetRenderTarget, not seeded from CPU pixels.
    VkBuffer staging = VK_NULL_HANDLE;
    VkDeviceMemory stagingMem = VK_NULL_HANDLE;
    if (!isRT) {
        VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        bi.size = imageBytes;
        bi.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        vkCreateBuffer(ctx.device, &bi, nullptr, &staging);
        VkMemoryRequirements req{};
        vkGetBufferMemoryRequirements(ctx.device, staging, &req);
        VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        ai.allocationSize = req.size;
        ai.memoryTypeIndex = FindMemoryType(ctx.physicalDevice, req.memoryTypeBits,
                                            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                                VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        vkAllocateMemory(ctx.device, &ai, nullptr, &stagingMem);
        vkBindBufferMemory(ctx.device, staging, stagingMem, 0);
        void* mapped = nullptr;
        vkMapMemory(ctx.device, stagingMem, 0, imageBytes, 0, &mapped);
        if (desc.pixels)
            std::memcpy(mapped, desc.pixels, static_cast<size_t>(imageBytes));
        vkUnmapMemory(ctx.device, stagingMem);
    }

    // Device-local image.
    VkImageCreateInfo ic{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    ic.imageType = VK_IMAGE_TYPE_2D;
    ic.format = VK_FORMAT_R8G8B8A8_UNORM;
    ic.extent = {desc.width, desc.height, 1};
    ic.mipLevels = 1;
    ic.arrayLayers = 1;
    ic.samples = VK_SAMPLE_COUNT_1_BIT;
    ic.tiling = VK_IMAGE_TILING_OPTIMAL;
    ic.usage = isRT
        ? (VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT)
        : (VK_IMAGE_USAGE_TRANSFER_DST_BIT     | VK_IMAGE_USAGE_SAMPLED_BIT);
    ic.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    ic.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    vkCreateImage(ctx.device, &ic, nullptr, &m_image);

    VkMemoryRequirements req{};
    vkGetImageMemoryRequirements(ctx.device, m_image, &req);
    VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    ai.allocationSize = req.size;
    ai.memoryTypeIndex =
        FindMemoryType(ctx.physicalDevice, req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    vkAllocateMemory(ctx.device, &ai, nullptr, &m_memory);
    vkBindImageMemory(ctx.device, m_image, m_memory, 0);

    // One-time setup: upload pixels (CPU-sourced texture) or just transition
    // the image into SHADER_READ_ONLY_OPTIMAL so the first sample before any
    // RT draw lands on a valid layout (the offscreen pass's initialLayout is
    // UNDEFINED, which discards whatever's there at the start of a draw).
    VkCommandBufferAllocateInfo cbai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    cbai.commandPool = ctx.uploadPool;
    cbai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cbai.commandBufferCount = 1;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    vkAllocateCommandBuffers(ctx.device, &cbai, &cmd);
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &begin);

    VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    barrier.image = m_image;
    barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

    if (isRT) {
        // UNDEFINED -> SHADER_READ_ONLY_OPTIMAL so descriptor writes can
        // reference this view straight away. The offscreen render pass will
        // discard the layout on its next BeginRenderPass anyway.
        barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                             VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                             0, 0, nullptr, 0, nullptr, 1, &barrier);
    } else {
        barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                             VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
                             0, nullptr, 0, nullptr, 1, &barrier);

        VkBufferImageCopy copy{};
        copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        copy.imageExtent = {desc.width, desc.height, 1};
        vkCmdCopyBufferToImage(cmd, staging, m_image,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);

        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
                             0, nullptr, 0, nullptr, 1, &barrier);
    }

    vkEndCommandBuffer(cmd);
    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &cmd;
    vkQueueSubmit(ctx.queue, 1, &submit, VK_NULL_HANDLE);
    vkQueueWaitIdle(ctx.queue);
    vkFreeCommandBuffers(ctx.device, ctx.uploadPool, 1, &cmd);
    if (staging)    vkDestroyBuffer(ctx.device, staging, nullptr);
    if (stagingMem) vkFreeMemory(ctx.device, stagingMem, nullptr);

    VkImageViewCreateInfo iv{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    iv.image = m_image;
    iv.viewType = VK_IMAGE_VIEW_TYPE_2D;
    iv.format = VK_FORMAT_R8G8B8A8_UNORM;
    iv.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCreateImageView(ctx.device, &iv, nullptr, &m_view);

    if (isRT) {
        VkFramebufferCreateInfo fb{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
        fb.renderPass      = ctx.offscreenRenderPass;
        fb.attachmentCount = 1;
        fb.pAttachments    = &m_view;
        fb.width           = desc.width;
        fb.height          = desc.height;
        fb.layers          = 1;
        vkCreateFramebuffer(ctx.device, &fb, nullptr, &m_framebuffer);
    }
}

bool VulkanTexture::UpdatePixels(const void* pixels) {
    if (!pixels || m_image == VK_NULL_HANDLE || m_framebuffer != VK_NULL_HANDLE) {
        // RT textures don't take CPU pixel updates; the GPU draws into them.
        return false;
    }
    const VkDeviceSize imageBytes =
        static_cast<VkDeviceSize>(m_width) * m_height * 4;

    // Staging buffer with the new pixels.
    VkBuffer staging = VK_NULL_HANDLE;
    VkDeviceMemory stagingMem = VK_NULL_HANDLE;
    VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bi.size = imageBytes;
    bi.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    vkCreateBuffer(m_ctx->device, &bi, nullptr, &staging);
    VkMemoryRequirements req{};
    vkGetBufferMemoryRequirements(m_ctx->device, staging, &req);
    VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    ai.allocationSize = req.size;
    ai.memoryTypeIndex = FindMemoryType(m_ctx->physicalDevice, req.memoryTypeBits,
                                        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                            VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    vkAllocateMemory(m_ctx->device, &ai, nullptr, &stagingMem);
    vkBindBufferMemory(m_ctx->device, staging, stagingMem, 0);
    void* mapped = nullptr;
    vkMapMemory(m_ctx->device, stagingMem, 0, imageBytes, 0, &mapped);
    std::memcpy(mapped, pixels, static_cast<size_t>(imageBytes));
    vkUnmapMemory(m_ctx->device, stagingMem);

    // One-time command: transition SHADER_READ -> TRANSFER_DST, copy,
    // transition back to SHADER_READ.
    VkCommandBufferAllocateInfo cbai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    cbai.commandPool = m_ctx->uploadPool;
    cbai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cbai.commandBufferCount = 1;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    vkAllocateCommandBuffers(m_ctx->device, &cbai, &cmd);
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &begin);

    VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    b.image = m_image;
    b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    b.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    b.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    b.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    b.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
                         0, nullptr, 0, nullptr, 1, &b);

    VkBufferImageCopy copy{};
    copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    copy.imageExtent = {m_width, m_height, 1};
    vkCmdCopyBufferToImage(cmd, staging, m_image,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);

    b.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    b.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    b.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    b.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
                         0, nullptr, 0, nullptr, 1, &b);

    vkEndCommandBuffer(cmd);
    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &cmd;
    vkQueueSubmit(m_ctx->queue, 1, &submit, VK_NULL_HANDLE);
    vkQueueWaitIdle(m_ctx->queue);
    vkFreeCommandBuffers(m_ctx->device, m_ctx->uploadPool, 1, &cmd);
    vkDestroyBuffer(m_ctx->device, staging, nullptr);
    vkFreeMemory(m_ctx->device, stagingMem, nullptr);
    return true;
}

VulkanTexture::~VulkanTexture() {
    if (m_framebuffer)
        vkDestroyFramebuffer(m_ctx->device, m_framebuffer, nullptr);
    if (m_view)
        vkDestroyImageView(m_ctx->device, m_view, nullptr);
    if (m_image)
        vkDestroyImage(m_ctx->device, m_image, nullptr);
    if (m_memory)
        vkFreeMemory(m_ctx->device, m_memory, nullptr);
}

// ===========================================================================
// VulkanShader
// ===========================================================================
VulkanShader::VulkanShader(const VulkanContext& ctx, const ShaderDesc& desc) : m_ctx(&ctx) {
    if (desc.vertexSpirv == nullptr || desc.pixelSpirv == nullptr) {
        NARAGAME_ERROR("VulkanShader: ShaderDesc has no SPIR-V");
        return;
    }
    VkShaderModuleCreateInfo v{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    v.codeSize = desc.vertexSpirvSize;
    v.pCode = desc.vertexSpirv;
    vkCreateShaderModule(ctx.device, &v, nullptr, &m_vert);

    VkShaderModuleCreateInfo f{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    f.codeSize = desc.pixelSpirvSize;
    f.pCode = desc.pixelSpirv;
    vkCreateShaderModule(ctx.device, &f, nullptr, &m_frag);
}

VulkanShader::~VulkanShader() {
    if (m_vert)
        vkDestroyShaderModule(m_ctx->device, m_vert, nullptr);
    if (m_frag)
        vkDestroyShaderModule(m_ctx->device, m_frag, nullptr);
}

// ===========================================================================
// VulkanPipeline
// ===========================================================================
VulkanPipeline::VulkanPipeline(const VulkanContext& ctx, const PipelineDesc& desc) : m_ctx(&ctx) {
    auto* shader = static_cast<VulkanShader*>(desc.shader);
    if (shader == nullptr || !shader->Valid()) {
        NARAGAME_ERROR("VulkanPipeline: invalid shader");
        return;
    }

    VkPipelineLayoutCreateInfo plci{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    plci.setLayoutCount = 1;
    plci.pSetLayouts = &ctx.descriptorLayout;
    vkCreatePipelineLayout(ctx.device, &plci, nullptr, &m_layout);

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = shader->Vertex();
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = shader->Fragment();
    stages[1].pName = "main";

    VkVertexInputBindingDescription binding{};
    binding.binding = 0;
    binding.stride = desc.vertexLayout.stride;
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    std::vector<VkVertexInputAttributeDescription> attrs;
    for (uint32_t i = 0; i < desc.vertexLayout.attributes.size(); ++i) {
        const VertexAttribute& a = desc.vertexLayout.attributes[i];
        VkVertexInputAttributeDescription vad{};
        vad.location = i;
        vad.binding = 0;
        vad.format = ToVkFormat(a.format);
        vad.offset = a.offset;
        attrs.push_back(vad);
    }

    VkPipelineVertexInputStateCreateInfo vi{
        VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    vi.vertexBindingDescriptionCount = 1;
    vi.pVertexBindingDescriptions = &binding;
    vi.vertexAttributeDescriptionCount = static_cast<uint32_t>(attrs.size());
    vi.pVertexAttributeDescriptions = attrs.data();

    VkPipelineInputAssemblyStateCreateInfo ia{
        VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo vp{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    vp.viewportCount = 1;
    vp.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rs{
        VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode = VK_CULL_MODE_NONE;
    rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rs.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo ms{
        VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState blend{};
    blend.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                           VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    // Mirrors the D3D11 mapping. Alpha channel stays src-over so a chained
    // pipeline that reads this buffer still gets meaningful alpha back.
    switch (desc.blend) {
        case BlendMode::Alpha:
            blend.blendEnable         = VK_TRUE;
            blend.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
            blend.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            blend.colorBlendOp        = VK_BLEND_OP_ADD;
            blend.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            blend.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            blend.alphaBlendOp        = VK_BLEND_OP_ADD;
            break;
        case BlendMode::Additive:
            blend.blendEnable         = VK_TRUE;
            blend.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
            blend.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
            blend.colorBlendOp        = VK_BLEND_OP_ADD;
            blend.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            blend.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            blend.alphaBlendOp        = VK_BLEND_OP_ADD;
            break;
        case BlendMode::Multiply:
            blend.blendEnable         = VK_TRUE;
            blend.srcColorBlendFactor = VK_BLEND_FACTOR_DST_COLOR;
            blend.dstColorBlendFactor = VK_BLEND_FACTOR_ZERO;
            blend.colorBlendOp        = VK_BLEND_OP_ADD;
            blend.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            blend.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            blend.alphaBlendOp        = VK_BLEND_OP_ADD;
            break;
        case BlendMode::None:
        default:
            blend.blendEnable = VK_FALSE;
            break;
    }
    VkPipelineColorBlendStateCreateInfo cb{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    cb.attachmentCount = 1;
    cb.pAttachments = &blend;

    const VkDynamicState dynamics[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dyn{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dyn.dynamicStateCount = 2;
    dyn.pDynamicStates = dynamics;

    VkGraphicsPipelineCreateInfo pci{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    pci.stageCount = 2;
    pci.pStages = stages;
    pci.pVertexInputState = &vi;
    pci.pInputAssemblyState = &ia;
    pci.pViewportState = &vp;
    pci.pRasterizationState = &rs;
    pci.pMultisampleState = &ms;
    pci.pColorBlendState = &cb;
    pci.pDynamicState = &dyn;
    pci.layout = m_layout;
    pci.renderPass = ctx.renderPass;
    pci.subpass = 0;
    if (vkCreateGraphicsPipelines(ctx.device, VK_NULL_HANDLE, 1, &pci, nullptr, &m_pipeline) !=
        VK_SUCCESS)
        NARAGAME_ERROR("vkCreateGraphicsPipelines failed");
}

VulkanPipeline::~VulkanPipeline() {
    if (m_pipeline)
        vkDestroyPipeline(m_ctx->device, m_pipeline, nullptr);
    if (m_layout)
        vkDestroyPipelineLayout(m_ctx->device, m_layout, nullptr);
}

} // namespace NaraGame

#endif // NARAGAME_HAVE_VULKAN
