/*
* Vulkan Example - Order Independent Transparency rendering using linked lists
*
* Copyright by Sascha Willems - www.saschawillems.de
* Copyright by Daemyung Jang  - dm86.jang@gmail.com
*
* This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
*/
#include "vulkanexamplebase.h"
#include "VulkanglTFModel.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
//#include "stb_image.h"
#include "stb_image_write.h"

// #include "tiny_obj_loader.h"

#define NODE_COUNT 20

struct UISettings {
    int oitAlgorithm = 0;
    int optimization = 0;
    bool animateLight = false;
    bool onlyOpt32 = false;
    bool unrollBubble = false;
    float lightSpeed = 0.25f;
    float lightTimer = 0.0f;
    int OIT_LAYERS = 16;
    int MAX_FRAGMENT_COUNT = 128;
    bool exportRequest = false;
} uiSettings;

class VulkanExample : public VulkanExampleBase {
public:
    std::vector<std::string> methods{"base", "BMA", "RBS_BMA", "iRBS_BMA", "RBS_only", "iRBS_only"};
    std::vector<std::string> oit_alg{"linked list", "atomic loop"};
    struct {
        vkglTF::Model sphere;
        vkglTF::Model cube;
    } models;

    struct Node {
        glm::vec4 color;
        float depth{0.0f};
        uint32_t next{0};
    };

    struct {
        uint32_t count{0};
        uint32_t maxNodeCount{0};
    } geometrySBO;

    struct GeometryPass {
        VkRenderPass renderPass{VK_NULL_HANDLE};
        VkFramebuffer framebuffer{VK_NULL_HANDLE};
        vks::Buffer geometry;
        vks::Texture headIndex;
        vks::Buffer abuffer;
    } geometryPass;

    struct RenderPassUniformData {
        glm::mat4 projection;
        glm::mat4 view;
        glm::vec4 lightPos;
        glm::ivec3 viewport; // (width, height, width * height)
    } renderPassUniformData;
    vks::Buffer renderPassUniformBuffer;

    struct ObjectData {
        glm::mat4 model;
        glm::vec4 color;
    };

    struct {
        VkDescriptorSetLayout geometry{VK_NULL_HANDLE};
        VkDescriptorSetLayout color{VK_NULL_HANDLE};
    } descriptorSetLayouts;

    struct {
        VkPipelineLayout geometry{VK_NULL_HANDLE};
        VkPipelineLayout color{VK_NULL_HANDLE};
    } pipelineLayouts;

    struct {
        VkPipeline geometry{VK_NULL_HANDLE};
        VkPipeline loop64{VK_NULL_HANDLE};
        VkPipeline kbuf_blend{VK_NULL_HANDLE};
        VkPipeline color{VK_NULL_HANDLE};
        VkPipeline rbs_color_128{VK_NULL_HANDLE};
        VkPipeline bma_color_128{VK_NULL_HANDLE};
        VkPipeline color_32{VK_NULL_HANDLE};
        VkPipeline color_16{VK_NULL_HANDLE};
        VkPipeline color_8{VK_NULL_HANDLE};
        VkPipeline color_4{VK_NULL_HANDLE};
        VkPipeline base_color_32{VK_NULL_HANDLE};
        VkPipeline bitonic_color{VK_NULL_HANDLE};
        VkPipeline rbs_only{VK_NULL_HANDLE};
        VkPipeline irbs_only{VK_NULL_HANDLE};
    } pipelines;

    struct {
        VkDescriptorSet geometry{VK_NULL_HANDLE};
        VkDescriptorSet color{VK_NULL_HANDLE};
    } descriptorSets;

    VkDeviceSize objectUniformBufferSize{0};

    VulkanExample() : VulkanExampleBase() {
        title = "Order independent transparency rendering";
        camera.type = Camera::CameraType::lookat;
        //camera.setPosition(glm::vec3(0.0f, 0.0f, -6.0f));
        //camera.setRotation(glm::vec3(0.0f, 0.0f, 0.0f));
        camera.setPosition(glm::vec3(1.685f, 0.46f, -6.0f));
        camera.setRotation(glm::vec3(-38.f, -38.f, 0.f));
        camera.setPerspective(60.0f, (float) width / (float) height, 0.1f, 256.0f);
        requiresStencil = true;

        // compile shader begin
        std::string shaderPath = getShadersPath();

        // compile for vary
        std::vector<std::string> shaders_vary{"color.frag"};
        for (const auto &shader: shaders_vary) {
            auto command =
                    "glslc -DMAX_FRAGMENT_COUNT=128 " + shaderPath + "oit/" + shader + " -o " + shaderPath + "oit/" +
                    shader + ".spv -g";
            std::system(command.c_str());
        }

        // compile for OIT_LAYERS
        std::vector<std::string> shaders_atomic_loop{"loop64.vert", "loop64.frag",
                                                     "kbuf_blend.frag",
                                                     "kbuf_blend.vert"};
        for (const auto &shader: shaders_atomic_loop) {
            auto command =
                    "glslc -DOIT_LAYERS=" + std::to_string(uiSettings.OIT_LAYERS) + " " + shaderPath +
                    "oit/" + shader + " -o " + shaderPath + "oit/" + shader +
                    ".spv -g";
            std::system(command.c_str());
        }

        // compile for constant
        std::vector<std::string> shaders_constant{"color.vert", "geometry.frag", "geometry.vert"};
        for (const auto &shader: shaders_constant) {
            auto command =
                    "glslc -DMAX_FRAGMENT_COUNT=16 " + shaderPath + "oit/" + shader + " -o " + shaderPath + "oit/" +
                    shader + ".spv -g";
            std::system(command.c_str());
        }

        // compile for 128
        std::vector<std::string> shaders_128{"rbs_color_128.frag", "bma_color_128.frag", "base_color_32.frag",
                                             "bitonic.frag", "irbs_only.frag", "rbs_only.frag"};
        for (const auto &shader: shaders_128) {
            auto command =
                    "glslc -DMAX_FRAGMENT_COUNT=128 " + shaderPath + "oit/" + shader + " -o " + shaderPath + "oit/" +
                    shader + ".spv -g";
            std::system(command.c_str());
        }

        // compile for 32
        std::vector<std::string> shaders_32{"base_color_32.frag"};
        for (const auto &shader: shaders_32) {
            auto command =
                    "glslc -DMAX_FRAGMENT_COUNT=32 " + shaderPath + "oit/" + shader + " -o " + shaderPath + "oit/" +
                    shader + ".spv -g";
            std::system(command.c_str());
        }


        // compile for color_x
/*        for (int i = 4; i <= 32; i = i * 2) {
            auto command =
                    "glslc -DMAX_FRAGMENT_COUNT=" + std::to_string(i) + " " + shaderPath + "oit/" + "color_" + std::to_string(i) + ".frag" +
                    " -o " + shaderPath + "oit/color_" + std::to_string(i) + ".frag.spv -g";
            std::system(command.c_str());
        }*/
        // compile shader end

        std::vector<std::string> shader_colors{
        "color_4.frag", "color_8.frag", "color_16.frag",
        "color_32.frag"};
        for (const auto &shader: shader_colors) {
            auto command =
                    "glslc " + shaderPath + "oit/" + shader + " -o " + shaderPath + "oit/" +
                    shader + ".spv -g";
            std::system(command.c_str());
        }

    }

    ~VulkanExample() {
        if (device) {
            vkDestroyPipeline(device, pipelines.geometry, nullptr);
            vkDestroyPipeline(device, pipelines.loop64, nullptr);
            vkDestroyPipeline(device, pipelines.kbuf_blend, nullptr);
            vkDestroyPipeline(device, pipelines.color, nullptr);
            vkDestroyPipeline(device, pipelines.bitonic_color, nullptr);
            vkDestroyPipeline(device, pipelines.color_4, nullptr);
            vkDestroyPipeline(device, pipelines.color_8, nullptr);
            vkDestroyPipeline(device, pipelines.color_16, nullptr);
            vkDestroyPipeline(device, pipelines.color_32, nullptr);
            vkDestroyPipeline(device, pipelines.rbs_color_128, nullptr);
            vkDestroyPipeline(device, pipelines.bma_color_128, nullptr);
            vkDestroyPipeline(device, pipelines.base_color_32, nullptr);
            vkDestroyPipelineLayout(device, pipelineLayouts.geometry, nullptr);
            vkDestroyPipelineLayout(device, pipelineLayouts.color, nullptr);
            vkDestroyDescriptorSetLayout(device, descriptorSetLayouts.geometry, nullptr);
            vkDestroyDescriptorSetLayout(device, descriptorSetLayouts.color, nullptr);
            destroyGeometryPass();
            renderPassUniformBuffer.destroy();
        }
    }

    void getEnabledFeatures() override {
        // The linked lists are built in a fragment shader using atomic stores, so the sample won't work without that feature available
        if (deviceFeatures.fragmentStoresAndAtomics) {
            enabledFeatures.fragmentStoresAndAtomics = VK_TRUE;
            enabledFeatures.shaderInt64 = VK_TRUE;
        } else {
            vks::tools::exitFatal("Selected GPU does not support stores and atomic operations in the fragment stage",
                                  VK_ERROR_FEATURE_NOT_PRESENT);
        }
    };

    void loadAssets() {
        const uint32_t glTFLoadingFlags =
                vkglTF::FileLoadingFlags::PreTransformVertices | vkglTF::FileLoadingFlags::FlipY;
        models.cube.loadFromFile(getAssetPath() + "models/car.gltf", vulkanDevice, queue, glTFLoadingFlags);
    }

    void prepareUniformBuffers() {
        // Create an uniform buffer for a render pass.
        VK_CHECK_RESULT(vulkanDevice->createBuffer(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                                                   VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                                   VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &renderPassUniformBuffer,
                                                   sizeof(RenderPassUniformData), &renderPassUniformBuffer));
        updateUniformBuffers();
    }

    void prepareGeometryPass() {
        VkAttachmentDescription attachments[1];
        attachments[0].flags = VK_ATTACHMENT_DESCRIPTION_MAY_ALIAS_BIT;
        attachments[0].format = depthFormat;
        attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
        attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        attachments[0].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;


        VkAttachmentReference depthStencilAttachmentRef = {};
        depthStencilAttachmentRef.attachment = 0;
        depthStencilAttachmentRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        VkSubpassDescription subpassDescription = {};
        subpassDescription.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpassDescription.pDepthStencilAttachment = &depthStencilAttachmentRef;
        VkRenderPassCreateInfo renderPassInfo = vks::initializers::renderPassCreateInfo();
        renderPassInfo.attachmentCount = 1;
        renderPassInfo.pAttachments = attachments;
        renderPassInfo.subpassCount = 1;
        renderPassInfo.pSubpasses = &subpassDescription;

        VK_CHECK_RESULT(vkCreateRenderPass(device, &renderPassInfo, nullptr, &geometryPass.renderPass));


        VkImageView v_attachments[1];
        v_attachments[0] = depthStencil.view;
        VkFramebufferCreateInfo fbufCreateInfo = vks::initializers::framebufferCreateInfo();
        fbufCreateInfo.renderPass = geometryPass.renderPass;
        fbufCreateInfo.attachmentCount = 1;
        fbufCreateInfo.pAttachments = v_attachments;
        fbufCreateInfo.width = width;
        fbufCreateInfo.height = height;
        fbufCreateInfo.layers = 1;

        VK_CHECK_RESULT(vkCreateFramebuffer(device, &fbufCreateInfo, nullptr, &geometryPass.framebuffer));

        // Create a buffer for GeometrySBO
        vks::Buffer stagingBuffer;

        VK_CHECK_RESULT(vulkanDevice->createBuffer(
                VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                &stagingBuffer,
                sizeof(geometrySBO)));
        VK_CHECK_RESULT(stagingBuffer.map());

        VK_CHECK_RESULT(vulkanDevice->createBuffer(
                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                &geometryPass.geometry,
                sizeof(geometrySBO)));

        // Set up GeometrySBO data.
        geometrySBO.count = 0;
        geometrySBO.maxNodeCount = NODE_COUNT * width * height;
        memcpy(stagingBuffer.mapped, &geometrySBO, sizeof(geometrySBO));

        // Copy data to device
        VkCommandBuffer copyCmd = vulkanDevice->createCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);
        VkBufferCopy copyRegion = {};
        copyRegion.size = sizeof(geometrySBO);
        vkCmdCopyBuffer(copyCmd, stagingBuffer.buffer, geometryPass.geometry.buffer, 1, &copyRegion);
        vulkanDevice->flushCommandBuffer(copyCmd, queue, true);

        stagingBuffer.destroy();

        // Create a texture for HeadIndex.
        // This image will track the head index of each fragment.
        geometryPass.headIndex.device = vulkanDevice;

        VkImageCreateInfo imageInfo = vks::initializers::imageCreateInfo();
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.format = VK_FORMAT_R32_UINT;
        imageInfo.extent.width = width;
        imageInfo.extent.height = height;
        imageInfo.extent.depth = 1;
        imageInfo.mipLevels = 1;
        imageInfo.arrayLayers = 1;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
#if (defined(VK_USE_PLATFORM_IOS_MVK) || defined(VK_USE_PLATFORM_MACOS_MVK))
        // SRS - On macOS/iOS use linear tiling for atomic image access, see https://github.com/KhronosGroup/MoltenVK/issues/1027
        imageInfo.tiling = VK_IMAGE_TILING_LINEAR;
#else
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
#endif
        imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_STORAGE_BIT;

        VK_CHECK_RESULT(vkCreateImage(device, &imageInfo, nullptr, &geometryPass.headIndex.image));

        geometryPass.headIndex.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

        VkMemoryRequirements memReqs;
        vkGetImageMemoryRequirements(device, geometryPass.headIndex.image, &memReqs);

        VkMemoryAllocateInfo memAlloc = vks::initializers::memoryAllocateInfo();
        memAlloc.allocationSize = memReqs.size;
        memAlloc.memoryTypeIndex = vulkanDevice->getMemoryType(memReqs.memoryTypeBits,
                                                               VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

        VK_CHECK_RESULT(vkAllocateMemory(device, &memAlloc, nullptr, &geometryPass.headIndex.deviceMemory));
        VK_CHECK_RESULT(
                vkBindImageMemory(device, geometryPass.headIndex.image, geometryPass.headIndex.deviceMemory, 0));

        VkImageViewCreateInfo imageViewInfo = vks::initializers::imageViewCreateInfo();
        imageViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        imageViewInfo.format = VK_FORMAT_R32_UINT;
        imageViewInfo.flags = 0;
        imageViewInfo.image = geometryPass.headIndex.image;
        imageViewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        imageViewInfo.subresourceRange.baseMipLevel = 0;
        imageViewInfo.subresourceRange.levelCount = 1;
        imageViewInfo.subresourceRange.baseArrayLayer = 0;
        imageViewInfo.subresourceRange.layerCount = 1;

        VK_CHECK_RESULT(vkCreateImageView(device, &imageViewInfo, nullptr, &geometryPass.headIndex.view));

        geometryPass.headIndex.width = width;
        geometryPass.headIndex.height = height;
        geometryPass.headIndex.mipLevels = 1;
        geometryPass.headIndex.layerCount = 1;
        geometryPass.headIndex.descriptor.imageView = geometryPass.headIndex.view;
        geometryPass.headIndex.descriptor.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        geometryPass.headIndex.sampler = VK_NULL_HANDLE;

        // Create a buffer for LinkedListSBO
        VK_CHECK_RESULT(vulkanDevice->createBuffer(
                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                &geometryPass.abuffer,
                sizeof(Node) * geometrySBO.maxNodeCount));

        // Change HeadIndex image's layout from UNDEFINED to GENERAL
        VkCommandBufferAllocateInfo cmdBufAllocInfo = vks::initializers::commandBufferAllocateInfo(cmdPool,
                                                                                                   VK_COMMAND_BUFFER_LEVEL_PRIMARY,
                                                                                                   1);

        VkCommandBuffer cmdBuf;
        VK_CHECK_RESULT(vkAllocateCommandBuffers(device, &cmdBufAllocInfo, &cmdBuf));

        VkCommandBufferBeginInfo cmdBufInfo = vks::initializers::commandBufferBeginInfo();
        VK_CHECK_RESULT(vkBeginCommandBuffer(cmdBuf, &cmdBufInfo));

        VkImageMemoryBarrier barrier = vks::initializers::imageMemoryBarrier();
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
        barrier.image = geometryPass.headIndex.image;
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.layerCount = 1;

        vkCmdPipelineBarrier(cmdBuf, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr,
                             0, nullptr, 1, &barrier);

        VK_CHECK_RESULT(vkEndCommandBuffer(cmdBuf));

        VkSubmitInfo submitInfo = vks::initializers::submitInfo();
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &cmdBuf;

        VK_CHECK_RESULT(vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE));
        VK_CHECK_RESULT(vkQueueWaitIdle(queue));
    }

    void setupDescriptors() {
        // Pool
        std::vector<VkDescriptorPoolSize> poolSizes = {
                vks::initializers::descriptorPoolSize(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 2),
                vks::initializers::descriptorPoolSize(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1),
                vks::initializers::descriptorPoolSize(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 3),
                vks::initializers::descriptorPoolSize(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 2),
        };
        VkDescriptorPoolCreateInfo descriptorPoolInfo = vks::initializers::descriptorPoolCreateInfo(poolSizes, 2);
        VK_CHECK_RESULT(vkCreateDescriptorPool(device, &descriptorPoolInfo, nullptr, &descriptorPool));

        // Layouts

        // Create a geometry descriptor set layout
        std::vector<VkDescriptorSetLayoutBinding> setLayoutBindings = {
                // renderPassUniformData
                vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                                                              VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                                                              0),
                // AtomicSBO
                vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                                                              VK_SHADER_STAGE_FRAGMENT_BIT, 1),
                // headIndexImage
                vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                                                              VK_SHADER_STAGE_FRAGMENT_BIT, 2),
                // LinkedListSBO
                vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                                                              VK_SHADER_STAGE_FRAGMENT_BIT, 3),
        };
        VkDescriptorSetLayoutCreateInfo descriptorLayoutCI = vks::initializers::descriptorSetLayoutCreateInfo(
                setLayoutBindings);
        VK_CHECK_RESULT(
                vkCreateDescriptorSetLayout(device, &descriptorLayoutCI, nullptr, &descriptorSetLayouts.geometry));

        // Create a color descriptor set layout
        setLayoutBindings = {
                // headIndexImage
                vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                                                              VK_SHADER_STAGE_FRAGMENT_BIT, 0),
                // LinkedListSBO
                vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                                                              VK_SHADER_STAGE_FRAGMENT_BIT, 1),
                // renderPassUniformData
                vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                                                              VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                                                              2),
        };
        descriptorLayoutCI = vks::initializers::descriptorSetLayoutCreateInfo(setLayoutBindings);
        VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &descriptorLayoutCI, nullptr, &descriptorSetLayouts.color));

        // Sets
        VkDescriptorSetAllocateInfo allocInfo = vks::initializers::descriptorSetAllocateInfo(descriptorPool,
                                                                                             &descriptorSetLayouts.geometry,
                                                                                             1);

        // Update a geometry descriptor set

        VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &allocInfo, &descriptorSets.geometry));

        std::vector<VkWriteDescriptorSet> writeDescriptorSets = {
                // Binding 0: renderPassUniformData
                vks::initializers::writeDescriptorSet(descriptorSets.geometry, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 0,
                                                      &renderPassUniformBuffer.descriptor),
                // Binding 2: GeometrySBO
                vks::initializers::writeDescriptorSet(descriptorSets.geometry, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1,
                                                      &geometryPass.geometry.descriptor),
                // Binding 3: headIndexImage
                vks::initializers::writeDescriptorSet(descriptorSets.geometry, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 2,
                                                      &geometryPass.headIndex.descriptor),
                // Binding 4: LinkedListSBO
                vks::initializers::writeDescriptorSet(descriptorSets.geometry, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 3,
                                                      &geometryPass.abuffer.descriptor)
        };
        vkUpdateDescriptorSets(device, static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0,
                               nullptr);

        // Update a color descriptor set
        allocInfo = vks::initializers::descriptorSetAllocateInfo(descriptorPool, &descriptorSetLayouts.color, 1);
        VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &allocInfo, &descriptorSets.color));

        writeDescriptorSets = {
                // Binding 0: headIndexImage
                vks::initializers::writeDescriptorSet(descriptorSets.color, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 0,
                                                      &geometryPass.headIndex.descriptor),
                // Binding 1: LinkedListSBO
                vks::initializers::writeDescriptorSet(descriptorSets.color, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1,
                                                      &geometryPass.abuffer.descriptor),
                // Binding 2: renderPassUniformData
                vks::initializers::writeDescriptorSet(descriptorSets.color, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 2,
                                                      &renderPassUniformBuffer.descriptor),
        };
        vkUpdateDescriptorSets(device, static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0,
                               nullptr);
    }

    void preparePipelines() {
        // Layouts

        // Create a geometry pipeline layout
        VkPipelineLayoutCreateInfo pipelineLayoutCI = vks::initializers::pipelineLayoutCreateInfo(
                &descriptorSetLayouts.geometry, 1);
        // Static object data passed using push constants
        VkPushConstantRange pushConstantRange = vks::initializers::pushConstantRange(
                VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, sizeof(ObjectData), 0);
        pipelineLayoutCI.pushConstantRangeCount = 1;
        pipelineLayoutCI.pPushConstantRanges = &pushConstantRange;
        VK_CHECK_RESULT(vkCreatePipelineLayout(device, &pipelineLayoutCI, nullptr, &pipelineLayouts.geometry));

        // Create color pipeline layout

        // stencil > 32
        pipelineLayoutCI = vks::initializers::pipelineLayoutCreateInfo(&descriptorSetLayouts.color, 1);
        VK_CHECK_RESULT(vkCreatePipelineLayout(device, &pipelineLayoutCI, nullptr, &pipelineLayouts.color));
        // stencil > 16
        pipelineLayoutCI = vks::initializers::pipelineLayoutCreateInfo(&descriptorSetLayouts.color, 1);
        VK_CHECK_RESULT(vkCreatePipelineLayout(device, &pipelineLayoutCI, nullptr, &pipelineLayouts.color));
        // stencil > 8
        pipelineLayoutCI = vks::initializers::pipelineLayoutCreateInfo(&descriptorSetLayouts.color, 1);
        VK_CHECK_RESULT(vkCreatePipelineLayout(device, &pipelineLayoutCI, nullptr, &pipelineLayouts.color));
        // stencil > 4
        pipelineLayoutCI = vks::initializers::pipelineLayoutCreateInfo(&descriptorSetLayouts.color, 1);
        VK_CHECK_RESULT(vkCreatePipelineLayout(device, &pipelineLayoutCI, nullptr, &pipelineLayouts.color));
        // stencil > 0
        pipelineLayoutCI = vks::initializers::pipelineLayoutCreateInfo(&descriptorSetLayouts.color, 1);
        VK_CHECK_RESULT(vkCreatePipelineLayout(device, &pipelineLayoutCI, nullptr, &pipelineLayouts.color));


        // Pipelines
        VkPipelineInputAssemblyStateCreateInfo inputAssemblyState = vks::initializers::pipelineInputAssemblyStateCreateInfo(
                VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, 0, VK_FALSE);
        VkPipelineRasterizationStateCreateInfo rasterizationState = vks::initializers::pipelineRasterizationStateCreateInfo(
                VK_POLYGON_MODE_FILL, VK_CULL_MODE_BACK_BIT, VK_FRONT_FACE_COUNTER_CLOCKWISE, 0);
        VkPipelineColorBlendStateCreateInfo colorBlendState = vks::initializers::pipelineColorBlendStateCreateInfo(0,
                                                                                                                   nullptr);
        VkPipelineDepthStencilStateCreateInfo depthStencilState = vks::initializers::pipelineDepthStencilStateCreateInfo(
                VK_FALSE, VK_FALSE, VK_COMPARE_OP_LESS_OR_EQUAL);
        VkPipelineViewportStateCreateInfo viewportState = vks::initializers::pipelineViewportStateCreateInfo(1, 1, 0);
        VkPipelineMultisampleStateCreateInfo multisampleState = vks::initializers::pipelineMultisampleStateCreateInfo(
                VK_SAMPLE_COUNT_1_BIT, 0);
        std::vector<VkDynamicState> dynamicStateEnables = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
        VkPipelineDynamicStateCreateInfo dynamicState = vks::initializers::pipelineDynamicStateCreateInfo(
                dynamicStateEnables);
        std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages;

        VkGraphicsPipelineCreateInfo pipelineCI = vks::initializers::pipelineCreateInfo(pipelineLayouts.geometry,
                                                                                        geometryPass.renderPass);
        pipelineCI.pInputAssemblyState = &inputAssemblyState;
        pipelineCI.pRasterizationState = &rasterizationState;
        pipelineCI.pColorBlendState = &colorBlendState;
        pipelineCI.pMultisampleState = &multisampleState;
        pipelineCI.pViewportState = &viewportState;
        pipelineCI.pDepthStencilState = &depthStencilState;
        pipelineCI.pDynamicState = &dynamicState;
        pipelineCI.stageCount = static_cast<uint32_t>(shaderStages.size());
        pipelineCI.pStages = shaderStages.data();
        pipelineCI.pVertexInputState = vkglTF::Vertex::getPipelineVertexInputState(
                {vkglTF::VertexComponent::Position, vkglTF::VertexComponent::Normal, vkglTF::VertexComponent::Color});

        // Create a geometry pipeline
        shaderStages[0] = loadShader(getShadersPath() + "oit/geometry.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
        shaderStages[1] = loadShader(getShadersPath() + "oit/geometry.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);

        //

        depthStencilState.stencilTestEnable = VK_TRUE;
        depthStencilState.back.compareOp = VK_COMPARE_OP_NOT_EQUAL;
        depthStencilState.back.failOp = VK_STENCIL_OP_INCREMENT_AND_CLAMP;
        depthStencilState.back.depthFailOp = VK_STENCIL_OP_INCREMENT_AND_CLAMP;
        depthStencilState.back.passOp = VK_STENCIL_OP_INCREMENT_AND_CLAMP;
        depthStencilState.back.compareMask = 0xff;
        depthStencilState.back.writeMask = 0xff;
        depthStencilState.back.reference = 0xff;
        depthStencilState.front = depthStencilState.back;

        VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCI, nullptr, &pipelines.geometry));

        //  loop64
        shaderStages[0] = loadShader(getShadersPath() + "oit/loop64.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
        shaderStages[1] = loadShader(getShadersPath() + "oit/loop64.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
        VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCI, nullptr, &pipelines.loop64));

        // Create color pipeline
        // stencil > 32
        VkPipelineColorBlendAttachmentState blendAttachmentState = vks::initializers::pipelineColorBlendAttachmentState(
                0xf, VK_FALSE);
        colorBlendState = vks::initializers::pipelineColorBlendStateCreateInfo(1, &blendAttachmentState);

        VkPipelineVertexInputStateCreateInfo vertexInputInfo = {};
        vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

        pipelineCI = vks::initializers::pipelineCreateInfo(pipelineLayouts.color, renderPass);
        pipelineCI.pInputAssemblyState = &inputAssemblyState;
        pipelineCI.pRasterizationState = &rasterizationState;
        pipelineCI.pColorBlendState = &colorBlendState;
        pipelineCI.pMultisampleState = &multisampleState;
        pipelineCI.pViewportState = &viewportState;
        pipelineCI.pDepthStencilState = &depthStencilState;
        pipelineCI.pDynamicState = &dynamicState;
        pipelineCI.stageCount = static_cast<uint32_t>(shaderStages.size());
        pipelineCI.pStages = shaderStages.data();
        pipelineCI.pVertexInputState = &vertexInputInfo;

        depthStencilState.back.compareOp = VK_COMPARE_OP_LESS;
        depthStencilState.back.failOp = VK_STENCIL_OP_KEEP;
        depthStencilState.back.depthFailOp = VK_STENCIL_OP_KEEP;
        depthStencilState.back.passOp = VK_STENCIL_OP_ZERO;
        depthStencilState.back.reference = 32;
        depthStencilState.front = depthStencilState.back;


        shaderStages[0] = loadShader(getShadersPath() + "oit/color.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
        shaderStages[1] = loadShader(getShadersPath() + "oit/rbs_color_128.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
        rasterizationState.cullMode = VK_CULL_MODE_FRONT_BIT;
        rasterizationState.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;

        VK_CHECK_RESULT(
                vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCI, nullptr, &pipelines.rbs_color_128));
        shaderStages[1] = loadShader(getShadersPath() + "oit/bma_color_128.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
        VK_CHECK_RESULT(
                vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCI, nullptr, &pipelines.bma_color_128));

        // bitonic
        shaderStages[1] = loadShader(getShadersPath() + "oit/bitonic.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
        VK_CHECK_RESULT(
                vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCI, nullptr, &pipelines.bitonic_color));

        // stencil > 16

        depthStencilState.back.compareOp = VK_COMPARE_OP_LESS;
        depthStencilState.back.failOp = VK_STENCIL_OP_KEEP;
        depthStencilState.back.depthFailOp = VK_STENCIL_OP_KEEP;
        depthStencilState.back.passOp = VK_STENCIL_OP_ZERO;
        depthStencilState.back.reference = 16;
        depthStencilState.front = depthStencilState.back;

        shaderStages[1] = loadShader(getShadersPath() + "oit/color_32.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
        rasterizationState.cullMode = VK_CULL_MODE_FRONT_BIT;
        rasterizationState.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;

        VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCI, nullptr, &pipelines.color_32));

        // stencil > 8

        depthStencilState.back.compareOp = VK_COMPARE_OP_LESS;
        depthStencilState.back.failOp = VK_STENCIL_OP_KEEP;
        depthStencilState.back.depthFailOp = VK_STENCIL_OP_KEEP;
        depthStencilState.back.passOp = VK_STENCIL_OP_ZERO;
        depthStencilState.back.reference = 8;
        depthStencilState.front = depthStencilState.back;

        shaderStages[1] = loadShader(getShadersPath() + "oit/color_16.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
        rasterizationState.cullMode = VK_CULL_MODE_FRONT_BIT;
        rasterizationState.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;

        VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCI, nullptr, &pipelines.color_16));

        // stencil > 4

        depthStencilState.back.compareOp = VK_COMPARE_OP_LESS;
        depthStencilState.back.failOp = VK_STENCIL_OP_KEEP;
        depthStencilState.back.depthFailOp = VK_STENCIL_OP_KEEP;
        depthStencilState.back.passOp = VK_STENCIL_OP_ZERO;
        depthStencilState.back.reference = 4;
        depthStencilState.front = depthStencilState.back;

        shaderStages[1] = loadShader(getShadersPath() + "oit/color_8.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
        rasterizationState.cullMode = VK_CULL_MODE_FRONT_BIT;
        rasterizationState.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;

        VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCI, nullptr, &pipelines.color_8));

        // stencil > 0

        depthStencilState.back.compareOp = VK_COMPARE_OP_LESS;
        depthStencilState.back.failOp = VK_STENCIL_OP_KEEP;
        depthStencilState.back.depthFailOp = VK_STENCIL_OP_KEEP;
        depthStencilState.back.passOp = VK_STENCIL_OP_ZERO;
        depthStencilState.back.reference = 0;
        depthStencilState.front = depthStencilState.back;

        shaderStages[1] = loadShader(getShadersPath() + "oit/color_4.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
        rasterizationState.cullMode = VK_CULL_MODE_FRONT_BIT;
        rasterizationState.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;

        VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCI, nullptr, &pipelines.color_4));

        // base 32
        shaderStages[1] = loadShader(getShadersPath() + "oit/base_color_32.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
        VK_CHECK_RESULT(
                vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCI, nullptr, &pipelines.base_color_32));

        // baseline

        depthStencilState.back.compareOp = VK_COMPARE_OP_ALWAYS;
        depthStencilState.back.failOp = VK_STENCIL_OP_KEEP;
        depthStencilState.back.depthFailOp = VK_STENCIL_OP_KEEP;
        depthStencilState.back.passOp = VK_STENCIL_OP_ZERO;
        depthStencilState.back.reference = 0;
        depthStencilState.front = depthStencilState.back;

        shaderStages[1] = loadShader(getShadersPath() + "oit/color.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
        rasterizationState.cullMode = VK_CULL_MODE_FRONT_BIT;
        rasterizationState.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;

        VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCI, nullptr, &pipelines.color));

        shaderStages[1] = loadShader(getShadersPath() + "oit/irbs_only.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
        VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCI, nullptr, &pipelines.irbs_only));
        shaderStages[1] = loadShader(getShadersPath() + "oit/rbs_only.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
        VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCI, nullptr, &pipelines.rbs_only));


        // kbuf
        shaderStages[0] = loadShader(getShadersPath() + "oit/kbuf_blend.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
        shaderStages[1] = loadShader(getShadersPath() + "oit/kbuf_blend.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
        rasterizationState.cullMode = VK_CULL_MODE_FRONT_BIT;
        rasterizationState.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;

        VK_CHECK_RESULT(
                vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCI, nullptr, &pipelines.kbuf_blend));


    }

    void buildCommandBuffers() override {
        if (resized)
            return;

        VkCommandBufferBeginInfo cmdBufInfo = vks::initializers::commandBufferBeginInfo();

        VkClearValue geometry_clearValues[1];
        geometry_clearValues[0].depthStencil = {1.f, 0};

        VkClearValue clearValues[2];
        clearValues[0].color = defaultClearColor;
        clearValues[1].depthStencil = {1.0f, 0};

        VkRenderPassBeginInfo renderPassBeginInfo = vks::initializers::renderPassBeginInfo();
        renderPassBeginInfo.renderArea.offset.x = 0;
        renderPassBeginInfo.renderArea.offset.y = 0;
        renderPassBeginInfo.renderArea.extent.width = width;
        renderPassBeginInfo.renderArea.extent.height = height;

        VkViewport viewport = vks::initializers::viewport((float) width, (float) height, 0.0f, 1.0f);
        VkRect2D scissor = vks::initializers::rect2D(width, height, 0, 0);

        for (int32_t i = 0; i < drawCmdBuffers.size(); ++i) {
            VK_CHECK_RESULT(vkBeginCommandBuffer(drawCmdBuffers[i], &cmdBufInfo));

            // Update dynamic viewport state
            vkCmdSetViewport(drawCmdBuffers[i], 0, 1, &viewport);

            // Update dynamic scissor state
            vkCmdSetScissor(drawCmdBuffers[i], 0, 1, &scissor);

            VkClearColorValue clearColor;
            clearColor.uint32[0] = 0xffffffff;

            VkImageSubresourceRange subresRange = {};

            subresRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            subresRange.levelCount = 1;
            subresRange.layerCount = 1;

            vkCmdClearColorImage(drawCmdBuffers[i], geometryPass.headIndex.image, VK_IMAGE_LAYOUT_GENERAL, &clearColor,
                                 1, &subresRange);

            // Clear previous geometry pass data
            vkCmdFillBuffer(drawCmdBuffers[i], geometryPass.geometry.buffer, 0, sizeof(uint32_t), 0);

            // We need a barrier to make sure all writes are finished before starting to write again
            VkMemoryBarrier memoryBarrier = vks::initializers::memoryBarrier();
            memoryBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            memoryBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
            vkCmdPipelineBarrier(drawCmdBuffers[i], VK_PIPELINE_STAGE_TRANSFER_BIT,
                                 VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 1, &memoryBarrier, 0, nullptr, 0, nullptr);

            // Begin the geometry render pass
            renderPassBeginInfo.renderPass = geometryPass.renderPass;
            renderPassBeginInfo.framebuffer = geometryPass.framebuffer;
            renderPassBeginInfo.clearValueCount = 1;
            renderPassBeginInfo.pClearValues = geometry_clearValues;

            vkCmdBeginRenderPass(drawCmdBuffers[i], &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);
            if (uiSettings.oitAlgorithm == 0) {
                vkCmdBindPipeline(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.geometry);
            } else if (uiSettings.oitAlgorithm == 1) {
                uint32_t data = 0xFFFFFFFF;  // 32-bit value with all bits set to 1
                vkCmdFillBuffer(drawCmdBuffers[i], geometryPass.abuffer.buffer, 0, geometryPass.abuffer.size, data);
                vkCmdBindPipeline(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.loop64);
            }

            // Render the scene
            ObjectData objectData;

            vkCmdBindDescriptorSets(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayouts.geometry, 0, 1,
                                    &descriptorSets.geometry, 0, nullptr);

            models.cube.bindBuffers(drawCmdBuffers[i]);
            objectData.color = glm::vec4(0.8f, 0.8f, 0.8f, 0.5f);

            glm::mat4 T = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, 0.0f));
            glm::mat4 S = glm::scale(glm::mat4(1.0f), glm::vec3(2.0f));
            objectData.model = T * S;
            vkCmdPushConstants(drawCmdBuffers[i], pipelineLayouts.geometry,
                               VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(ObjectData),
                               &objectData);
            models.cube.draw(drawCmdBuffers[i]);


            vkCmdEndRenderPass(drawCmdBuffers[i]);

            // Make a pipeline barrier to guarantee the geometry pass is done
            vkCmdPipelineBarrier(drawCmdBuffers[i], VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                                 VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 0, nullptr);

            // We need a barrier to make sure all writes are finished before starting to write again
            memoryBarrier = vks::initializers::memoryBarrier();
            memoryBarrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
            memoryBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
            vkCmdPipelineBarrier(drawCmdBuffers[i], VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                                 VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 1, &memoryBarrier, 0, nullptr, 0, nullptr);

            // Begin the color render pass
            renderPassBeginInfo.renderPass = renderPass;
            renderPassBeginInfo.framebuffer = frameBuffers[i];
            renderPassBeginInfo.clearValueCount = 2;
            renderPassBeginInfo.pClearValues = clearValues;

            vkCmdBeginRenderPass(drawCmdBuffers[i], &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);
            vkCmdBindDescriptorSets(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayouts.color, 0, 1,
                                    &descriptorSets.color, 0, nullptr);
            if (oit_alg[uiSettings.oitAlgorithm] == "linked list") {
                if (methods[uiSettings.optimization] == "base") {
                    vkCmdBindPipeline(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.color);
                    vkCmdDraw(drawCmdBuffers[i], 3, 1, 0, 0);
                } else if (methods[uiSettings.optimization] == "RBS_only") {
                    vkCmdBindPipeline(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.rbs_only);
                    vkCmdDraw(drawCmdBuffers[i], 3, 1, 0, 0);
                } else if (methods[uiSettings.optimization] == "iRBS_only") {
                    vkCmdBindPipeline(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.irbs_only);
                    vkCmdDraw(drawCmdBuffers[i], 3, 1, 0, 0);
                }
                else {
                    if (methods[uiSettings.optimization] == "iRBS_BMA") {
                        vkCmdBindPipeline(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.bitonic_color);
                        vkCmdDraw(drawCmdBuffers[i], 3, 1, 0, 0);
                    } else if (methods[uiSettings.optimization] == "RBS_BMA") {
                        vkCmdBindPipeline(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.rbs_color_128);
                        vkCmdDraw(drawCmdBuffers[i], 3, 1, 0, 0);
                    } else if (methods[uiSettings.optimization] == "BMA") {
                        vkCmdBindPipeline(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.bma_color_128);
                        vkCmdDraw(drawCmdBuffers[i], 3, 1, 0, 0);
                    }
                    if (uiSettings.onlyOpt32) {
                        vkCmdBindPipeline(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.base_color_32);
                        vkCmdDraw(drawCmdBuffers[i], 3, 1, 0, 0);
                    } else {
                        vkCmdBindPipeline(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.color_32);
                        vkCmdDraw(drawCmdBuffers[i], 3, 1, 0, 0);
                        vkCmdBindPipeline(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.color_16);
                        vkCmdDraw(drawCmdBuffers[i], 3, 1, 0, 0);
                        vkCmdBindPipeline(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.color_8);
                        vkCmdDraw(drawCmdBuffers[i], 3, 1, 0, 0);
                        vkCmdBindPipeline(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.color_4);
                        vkCmdDraw(drawCmdBuffers[i], 3, 1, 0, 0);
                    }
                }
            } else if (oit_alg[uiSettings.oitAlgorithm] == "atomic loop") {
                vkCmdBindPipeline(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.kbuf_blend);
                vkCmdDraw(drawCmdBuffers[i], 3, 1, 0, 0);
            }

            if (!uiSettings.exportRequest) {
                drawUI(drawCmdBuffers[i]);
            }

            vkCmdEndRenderPass(drawCmdBuffers[i]);

            VK_CHECK_RESULT(vkEndCommandBuffer(drawCmdBuffers[i]));
        }
    }

    void updateUniformBuffers() {
        renderPassUniformData.projection = camera.matrices.perspective;
        renderPassUniformData.view = camera.matrices.view;
        renderPassUniformData.viewport = glm::ivec3(width, height, width * height);

        // light source
        if (uiSettings.animateLight) {
            uiSettings.lightTimer += frameTimer * uiSettings.lightSpeed;
            renderPassUniformData.lightPos.x = sin(glm::radians(uiSettings.lightTimer * 360.f)) * 15.0f;
            renderPassUniformData.lightPos.y = cos(glm::radians(uiSettings.lightTimer * 360.f)) * 15.0f;
        }

        VK_CHECK_RESULT(renderPassUniformBuffer.map());
        memcpy(renderPassUniformBuffer.mapped, &renderPassUniformData, sizeof(RenderPassUniformData));
        renderPassUniformBuffer.unmap();

    }

    void prepare() override {
        VulkanExampleBase::prepare();
        loadAssets();
        prepareUniformBuffers();
        prepareGeometryPass();
        setupDescriptors();
        preparePipelines();
        buildCommandBuffers();
        //updateUniformBuffers();
        prepared = true;
    }

    void draw() {
        VulkanExampleBase::prepareFrame();
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &drawCmdBuffers[currentBuffer];
        VK_CHECK_RESULT(vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE));

        if (uiSettings.exportRequest) {
            exportImage();
            uiSettings.exportRequest = false;
            UIOverlay.updated = true;
        }
        VulkanExampleBase::submitFrame();
    }

    void render() override {
        if (!prepared)
            return;
        updateUniformBuffers();
        draw();
    }

    void windowResized() override {
        destroyGeometryPass();
        prepareGeometryPass();
        vkResetDescriptorPool(device, descriptorPool, 0);
        setupDescriptors();
        resized = false;
        buildCommandBuffers();
    }

    void destroyGeometryPass() {
        vkDestroyRenderPass(device, geometryPass.renderPass, nullptr);
        vkDestroyFramebuffer(device, geometryPass.framebuffer, nullptr);
        geometryPass.geometry.destroy();
        geometryPass.headIndex.destroy();
        geometryPass.abuffer.destroy();
    }

    void rePreparePipeline() {
        vkDestroyPipeline(device, pipelines.geometry, nullptr);
        vkDestroyPipeline(device, pipelines.loop64, nullptr);
        vkDestroyPipeline(device, pipelines.kbuf_blend, nullptr);
        vkDestroyPipeline(device, pipelines.color, nullptr);
        vkDestroyPipeline(device, pipelines.bitonic_color, nullptr);
        vkDestroyPipeline(device, pipelines.color_4, nullptr);
        vkDestroyPipeline(device, pipelines.color_8, nullptr);
        vkDestroyPipeline(device, pipelines.color_16, nullptr);
        vkDestroyPipeline(device, pipelines.color_32, nullptr);
        vkDestroyPipeline(device, pipelines.rbs_color_128, nullptr);
        vkDestroyPipeline(device, pipelines.bma_color_128, nullptr);
        vkDestroyPipeline(device, pipelines.base_color_32, nullptr);
        vkDestroyPipeline(device, pipelines.irbs_only, nullptr);
        vkDestroyPipeline(device, pipelines.rbs_only, nullptr);
        preparePipelines();
    }

    void exportImage() {
        // 获取当前帧缓冲的图像
        VkImage srcImage = swapChain.buffers[currentBuffer].image;

        // 创建临时的 VkImage 和 VkDeviceMemory
        VkImageCreateInfo imageInfo = {};
        imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.format = swapChain.colorFormat;
        imageInfo.extent.width = width;
        imageInfo.extent.height = height;
        imageInfo.extent.depth = 1;
        imageInfo.mipLevels = 1;
        imageInfo.arrayLayers = 1;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.tiling = VK_IMAGE_TILING_LINEAR;
        imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

        VkImage dstImage;
        vkCreateImage(device, &imageInfo, nullptr, &dstImage);

        VkMemoryRequirements memRequirements;
        vkGetImageMemoryRequirements(device, dstImage, &memRequirements);

        VkMemoryAllocateInfo allocInfo = {};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = memRequirements.size;
        allocInfo.memoryTypeIndex = vulkanDevice->getMemoryType(memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

        VkDeviceMemory dstImageMemory;
        vkAllocateMemory(device, &allocInfo, nullptr, &dstImageMemory);
        vkBindImageMemory(device, dstImage, dstImageMemory, 0);

        // 将当前帧缓冲的图像复制到临时图像中
        VkCommandBuffer commandBuffer = vulkanDevice->createCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);

        VkImageMemoryBarrier barrier = {};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = dstImage;
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.baseMipLevel = 0;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = 1;
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

        vkCmdPipelineBarrier(
                commandBuffer,
                VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                0,
                0, nullptr,
                0, nullptr,
                1, &barrier
        );

        VkImageCopy imageCopyRegion = {};
        imageCopyRegion.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        imageCopyRegion.srcSubresource.layerCount = 1;
        imageCopyRegion.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        imageCopyRegion.dstSubresource.layerCount = 1;
        imageCopyRegion.extent.width = width;
        imageCopyRegion.extent.height = height;
        imageCopyRegion.extent.depth = 1;

        vkCmdCopyImage(
                commandBuffer,
                srcImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                dstImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                1, &imageCopyRegion
        );

        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;

        vkCmdPipelineBarrier(
                commandBuffer,
                VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT,
                0,
                0, nullptr,
                0, nullptr,
                1, &barrier
        );

        vulkanDevice->flushCommandBuffer(commandBuffer, queue, true);

        // 将图像数据映射到主机内存并保存到文件
        void* data;
        vkMapMemory(device, dstImageMemory, 0, VK_WHOLE_SIZE, 0, &data);
        stbi_write_png("exported_image.png", width, height, 4, data, 0);
        vkUnmapMemory(device, dstImageMemory);

        // 清理资源
        vkDestroyImage(device, dstImage, nullptr);
        vkFreeMemory(device, dstImageMemory, nullptr);
    }

    virtual void OnUpdateUIOverlay(vks::UIOverlay *overlay) {

        if (overlay->header("Algorithm Settings")) {
            if (overlay->comboBox("Oit algorithm", &uiSettings.oitAlgorithm, oit_alg)) {
                buildCommandBuffers();
            }

            if (oit_alg[uiSettings.oitAlgorithm] == "linked list") {
                if (overlay->comboBox("optimization", &uiSettings.optimization, methods)) {
                    buildCommandBuffers();
                }
                if (overlay->sliderInt("MAX_FRAGMENT_COUNT", &uiSettings.MAX_FRAGMENT_COUNT, 1, 128) ||
                    ImGui::InputInt("MAX_FRAGMENT_COUNT", &uiSettings.MAX_FRAGMENT_COUNT)) {
                    // compile
                    std::string shaderPath = getShadersPath();
                    std::vector<std::string> shaders_vary{"color.frag"};
                    for (const auto &shader: shaders_vary) {
                        auto command =
                                "glslc -DMAX_FRAGMENT_COUNT=" + std::to_string(uiSettings.MAX_FRAGMENT_COUNT) + " " +
                                shaderPath + "oit/" + shader + " -o " + shaderPath + "oit/" +
                                shader + ".spv -g";
                        std::system(command.c_str());
                    }

                    rePreparePipeline();

                }

                if (overlay->checkBox("Only optimize 32+", &uiSettings.onlyOpt32)) {
                    buildCommandBuffers();
                }
                if (overlay->checkBox("Unroll Bubble Sort", &uiSettings.unrollBubble)) {
                    std::string shaderPath = getShadersPath();
                    std::vector<std::string> shaders_bubble{"rbs_only.frag", "rbs_color_128.frag"};

                    for (const auto &shader: shaders_bubble) {
                        auto command =
                                "glslc -DMAX_FRAGMENT_COUNT=" + std::to_string(uiSettings.MAX_FRAGMENT_COUNT) + " -Dunroll=" + std::to_string(uiSettings.unrollBubble) + " " +
                                shaderPath + "oit/" + shader + " -o " + shaderPath + "oit/" +
                                shader + ".spv -g";
                        std::system(command.c_str());
                    }


                    rePreparePipeline();
                }

            } else if (oit_alg[uiSettings.oitAlgorithm] == "atomic loop") {
                if (overlay->sliderInt("OIT_LAYERS", &uiSettings.OIT_LAYERS, 1, 32) ||
                    ImGui::InputInt("OIT_LAYERS", &uiSettings.OIT_LAYERS)) {
                    // compile for OIT_LAYERS
                    std::string shaderPath = getShadersPath();
                    std::vector<std::string> shaders_atomic_loop{"loop64.vert", "loop64.frag",
                                                                 "kbuf_blend.frag",
                                                                 "kbuf_blend.vert"};
                    for (const auto &shader: shaders_atomic_loop) {
                        auto command =
                                "glslc -DOIT_LAYERS=" + std::to_string(uiSettings.OIT_LAYERS) + " " + shaderPath +
                                "oit/" + shader + " -o " + shaderPath + "oit/" + shader +
                                ".spv -g";
                        std::system(command.c_str());
                    }

                    rePreparePipeline();

                }
            }

        }
        if (overlay->header("Render Pass Uniform Settings")) {
            overlay->checkBox("Animate light", &uiSettings.animateLight);
            overlay->inputFloat("Light speed", &uiSettings.lightSpeed, 0.1f, 2);  // 假设步长为0.1，精度为2位小数
            if (overlay->button("print camera")) {
                //const Camera::Matrices& m = camera.matrices;
                std::cout << "position\n";
                std::cout << camera.position.x << " " << camera.position.y << " " << camera.position.z << "\n";
                std::cout << "rotation\n";
                std::cout << camera.rotation.x << " " << camera.rotation.y << " " << camera.rotation.z << "\n";
                //std::cout << camera.fov << " " << camera.znear << " " << camera.zfar;
            }

            if (overlay->button("export picture")) {
                //exportImage();
                uiSettings.exportRequest = true;
            }
        }
    }

    void keyPressed(uint32_t keycode) override {
        if (keycode == KEY_SPACE) {
            UIOverlay.visible = !UIOverlay.visible;
            UIOverlay.updated = true;
        }
        VulkanExampleBase::keyPressed(keycode);
    }
};

VULKAN_EXAMPLE_MAIN()
