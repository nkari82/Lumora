#include "VulkanRenderer.h"

#include <stdexcept>
#include <fstream>
#include <iostream>
#include <cstring> // memcpy

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp> // 유니폼 (MVP) 예시
#include <glm/gtc/matrix_transform.hpp>

namespace Lumora
{

    // 정적 함수 구현
    std::unique_ptr<IRenderer> IRenderer::Create()
    {
        return std::make_unique<VulkanRenderer>();
    }

    // ----- 생성 / 파괴 -----
    VulkanRenderer::VulkanRenderer()
    {
        InitVulkan();
    }

    VulkanRenderer::~VulkanRenderer()
    {
        CleanupVulkan();
    }

    //
    // ----- Init / Cleanup ----
    //
    void VulkanRenderer::InitVulkan()
    {
        // 1. 윈도우 생성
        InitWindow();
        // 2. 인스턴스 생성
        {
            vk::ApplicationInfo app_info;
            app_info.pApplicationName = "Lumora App";
            app_info.applicationVersion = 1;
            app_info.pEngineName = "Lumora Engine";
            app_info.engineVersion = 1;
            app_info.apiVersion = VK_API_VERSION_1_2;

            vk::InstanceCreateInfo ici;
            ici.pApplicationInfo = &app_info;

            // 필요한 Extension(플랫폼별 Surface, GLFW 등)
            uint32_t glfwExtCount = 0;
            const char **glfwExts = glfwGetRequiredInstanceExtensions(&glfwExtCount);
            std::vector<const char *> exts(glfwExts, glfwExts + glfwExtCount);
            ici.enabledExtensionCount = static_cast<uint32_t>(exts.size());
            ici.ppEnabledExtensionNames = exts.data();

            // Validation Layers (디버그용)
            // const char* validation_layer = "VK_LAYER_KHRONOS_validation";
            // ...
            // ici.enabledLayerCount = 1;
            // ici.ppEnabledLayerNames = &validation_layer;

            instance_ = vk::createInstance(ici);
        }
        // 3. Surface 생성
        CreateSurface();
        // 4. 물리 디바이스 선택
        {
            auto pdevices = instance_.enumeratePhysicalDevices();
            if (pdevices.empty())
            {
                throw std::runtime_error("No Vulkan physical device found!");
            }
            physical_device_ = pdevices[0];
        }
        // 5. 그래픽스 큐 인덱스 찾기
        {
            auto qprops = physical_device_.getQueueFamilyProperties();
            for (uint32_t i = 0; i < qprops.size(); ++i)
            {
                if (qprops[i].queueFlags & vk::QueueFlagBits::eGraphics)
                {
                    // 서피스 프레젠테이션 지원 여부 확인
                    VkBool32 present_support = false;
                    present_support = physical_device_.getSurfaceSupportKHR(i, surface_);
                    if (present_support)
                    {
                        graphics_queue_index_ = i;
                        break;
                    }
                }
            }
            if (graphics_queue_index_ == 0)
            {
                throw std::runtime_error("Failed to find suitable queue family.");
            }
        }
        // 6. Logical Device
        {
            float qPriority = 1.0f;
            vk::DeviceQueueCreateInfo dqci;
            dqci.queueFamilyIndex = graphics_queue_index_;
            dqci.queueCount = 1;
            dqci.pQueuePriorities = &qPriority;

            vk::DeviceCreateInfo dci;
            dci.queueCreateInfoCount = 1;
            dci.pQueueCreateInfos = &dqci;
            device_ = physical_device_.createDevice(dci);

            graphics_queue_ = device_.getQueue(graphics_queue_index_, 0);
        }
        // 7. VMA Allocator
        InitVma();
        // 8. 스왑체인, 렌더패스, 프레임버퍼, 동기화
        CreateSwapchain();
        CreateRenderPass();
        CreateFramebuffers();
        CreateCommandPoolAndBuffers();
        CreateSyncObjects();
        // 9. Descriptor
        CreateDescriptorPool();
        CreateDescriptorSetLayout();
        // 10. 테스트 리소스(삼각형, UBO 등)
        CreateTestTriangleResources();
    }

    void VulkanRenderer::CleanupVulkan()
    {
        if (device_)
        {
            device_.waitIdle();
        }

        // 파이프라인
        for (size_t i = 1; i < pipelines_.size(); ++i)
        {
            if (pipelines_[i].pipeline)
            {
                device_.destroyPipeline(pipelines_[i].pipeline);
            }
            if (pipelines_[i].pipeline_layout)
            {
                device_.destroyPipelineLayout(pipelines_[i].pipeline_layout);
            }
        }

        // 버퍼
        for (size_t i = 1; i < buffers_.size(); ++i)
        {
            if (buffers_[i].buffer)
            {
                vmaDestroyBuffer(allocator_, (VkBuffer)buffers_[i].buffer, buffers_[i].allocation);
            }
        }
        // 텍스처
        for (size_t i = 1; i < textures_.size(); ++i)
        {
            if (textures_[i].image)
            {
                device_.destroyImageView(textures_[i].image_view);
                vmaDestroyImage(allocator_, (VkImage)textures_[i].image, textures_[i].allocation);
            }
        }
        // 샘플러
        for (size_t i = 1; i < samplers_.size(); ++i)
        {
            if (samplers_[i].sampler)
            {
                device_.destroySampler(samplers_[i].sampler);
            }
        }
        // 셰이더
        for (auto &kv : shaders_)
        {
            if (kv.second.shader_module)
            {
                device_.destroyShaderModule(kv.second.shader_module);
            }
        }
        shaders_.clear();

        // 프레임버퍼
        for (auto fb : framebuffers_)
        {
            device_.destroyFramebuffer(fb);
        }
        device_.destroyRenderPass(render_pass_);

        for (auto iv : swapchain_image_views_)
        {
            device_.destroyImageView(iv);
        }
        device_.destroySwapchainKHR(swapchain_);

        // 동기화
        for (int i = 0; i < kMaxFramesInFlight; ++i)
        {
            device_.destroySemaphore(image_available_semaphores_[i]);
            device_.destroySemaphore(render_finished_semaphores_[i]);
            device_.destroyFence(in_flight_fences_[i]);
        }
        device_.destroyCommandPool(command_pool_);

        device_.destroyDescriptorSetLayout(descriptor_set_layout_);
        device_.destroyDescriptorPool(descriptor_pool_);

        // VMA 해제
        if (allocator_)
        {
            vmaDestroyAllocator(allocator_);
            allocator_ = nullptr;
        }

        if (device_)
        {
            device_.destroy();
        }
        if (surface_)
        {
            instance_.destroySurfaceKHR(surface_);
        }

        if (instance_)
        {
            instance_.destroy();
        }

        // GLFW 윈도우 파괴
        if (window_)
        {
            glfwDestroyWindow(window_);
            glfwTerminate();
            window_ = nullptr;
        }
    }

    //
    // ----- GLFW Window ----
    //
    void VulkanRenderer::InitWindow()
    {
        if (!glfwInit())
        {
            throw std::runtime_error("Failed to init GLFW");
        }
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        window_ = glfwCreateWindow(1280, 720, "Lumora Window", nullptr, nullptr);
        if (!window_)
        {
            throw std::runtime_error("Failed to create GLFW window");
        }
    }

    void VulkanRenderer::CreateSurface()
    {
        // glfwCreateWindowSurface 와 vk::UniqueSurfaceKHR의 혼합 사용은 다소 번거롭지만,
        // 여기서는 간단히 C API인 glfwCreateWindowSurface를 호출
        VkSurfaceKHR c_surface;
        if (glfwCreateWindowSurface(instance_, window_, nullptr, &c_surface) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create window surface via GLFW");
        }
        surface_ = c_surface;
    }

    //
    // ----- VMA 초기화 ----
    //
    void VulkanRenderer::InitVma()
    {
        VmaAllocatorCreateInfo allocator_info = {};
        allocator_info.physicalDevice = physical_device_;
        allocator_info.device = device_;
        allocator_info.instance = instance_;
        vmaCreateAllocator(&allocator_info, &allocator_);
    }

    //
    // ----- Swapchain, RenderPass, Framebuffer, CommandBuffer ----
    //
    void VulkanRenderer::CreateSwapchain()
    {
        // (실제로는 SurfaceCapabilities, DesiredImageCount 등 고려)
        vk::SwapchainCreateInfoKHR sci;
        sci.surface = surface_;
        sci.minImageCount = 2;
        sci.imageFormat = swapchain_format_;
        sci.imageColorSpace = vk::ColorSpaceKHR::eSrgbNonlinear;
        sci.imageExtent = swapchain_extent_;
        sci.imageArrayLayers = 1;
        sci.imageUsage = vk::ImageUsageFlagBits::eColorAttachment;
        sci.imageSharingMode = vk::SharingMode::eExclusive;
        sci.preTransform = vk::SurfaceTransformFlagBitsKHR::eIdentity;
        sci.compositeAlpha = vk::CompositeAlphaFlagBitsKHR::eOpaque;
        sci.presentMode = vk::PresentModeKHR::eFifo;
        sci.clipped = true;

        swapchain_ = device_.createSwapchainKHR(sci);
        swapchain_images_ = device_.getSwapchainImagesKHR(swapchain_);

        swapchain_image_views_.resize(swapchain_images_.size());
        for (size_t i = 0; i < swapchain_images_.size(); ++i)
        {
            vk::ImageViewCreateInfo ivci;
            ivci.image = swapchain_images_[i];
            ivci.viewType = vk::ImageViewType::e2D;
            ivci.format = swapchain_format_;
            ivci.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
            ivci.subresourceRange.levelCount = 1;
            ivci.subresourceRange.layerCount = 1;
            swapchain_image_views_[i] = device_.createImageView(ivci);
        }
    }

    void VulkanRenderer::CreateRenderPass()
    {
        vk::AttachmentDescription color_attach;
        color_attach.format = swapchain_format_;
        color_attach.samples = vk::SampleCountFlagBits::e1;
        color_attach.loadOp = vk::AttachmentLoadOp::eClear;
        color_attach.storeOp = vk::AttachmentStoreOp::eStore;
        color_attach.initialLayout = vk::ImageLayout::eUndefined;
        color_attach.finalLayout = vk::ImageLayout::ePresentSrcKHR;

        vk::AttachmentReference color_ref;
        color_ref.attachment = 0;
        color_ref.layout = vk::ImageLayout::eColorAttachmentOptimal;

        vk::SubpassDescription subpass;
        subpass.pipelineBindPoint = vk::PipelineBindPoint::eGraphics;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &color_ref;

        vk::RenderPassCreateInfo rpci;
        rpci.attachmentCount = 1;
        rpci.pAttachments = &color_attach;
        rpci.subpassCount = 1;
        rpci.pSubpasses = &subpass;

        // 서브패스 의존성
        vk::SubpassDependency dep;
        dep.srcSubpass = VK_SUBPASS_EXTERNAL;
        dep.dstSubpass = 0;
        dep.srcStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput;
        dep.dstStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput;
        dep.srcAccessMask = {};
        dep.dstAccessMask = vk::AccessFlagBits::eColorAttachmentWrite;

        rpci.dependencyCount = 1;
        rpci.pDependencies = &dep;

        render_pass_ = device_.createRenderPass(rpci);
    }

    void VulkanRenderer::CreateFramebuffers()
    {
        framebuffers_.resize(swapchain_image_views_.size());
        for (size_t i = 0; i < swapchain_image_views_.size(); ++i)
        {
            vk::ImageView attachments[] = {swapchain_image_views_[i]};
            vk::FramebufferCreateInfo fci;
            fci.renderPass = render_pass_;
            fci.attachmentCount = 1;
            fci.pAttachments = attachments;
            fci.width = swapchain_extent_.width;
            fci.height = swapchain_extent_.height;
            fci.layers = 1;
            framebuffers_[i] = device_.createFramebuffer(fci);
        }
    }

    void VulkanRenderer::CreateCommandPoolAndBuffers()
    {
        vk::CommandPoolCreateInfo cpci;
        cpci.queueFamilyIndex = graphics_queue_index_;
        cpci.flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer;
        command_pool_ = device_.createCommandPool(cpci);

        vk::CommandBufferAllocateInfo cbai;
        cbai.commandPool = command_pool_;
        cbai.level = vk::CommandBufferLevel::ePrimary;
        cbai.commandBufferCount = static_cast<uint32_t>(swapchain_image_views_.size());

        command_buffers_ = device_.allocateCommandBuffers(cbai);
    }

    void VulkanRenderer::CreateSyncObjects()
    {
        image_available_semaphores_.resize(kMaxFramesInFlight);
        render_finished_semaphores_.resize(kMaxFramesInFlight);
        in_flight_fences_.resize(kMaxFramesInFlight);

        vk::SemaphoreCreateInfo sci;
        vk::FenceCreateInfo fci;
        fci.flags = vk::FenceCreateFlagBits::eSignaled;

        for (int i = 0; i < kMaxFramesInFlight; ++i)
        {
            image_available_semaphores_[i] = device_.createSemaphore(sci);
            render_finished_semaphores_[i] = device_.createSemaphore(sci);
            in_flight_fences_[i] = device_.createFence(fci);
        }
    }

    void VulkanRenderer::CreateDescriptorPool()
    {
        // UBO 100개, CombinedImageSampler 100개 정도를 가정
        std::vector<vk::DescriptorPoolSize> pool_sizes = {
            {vk::DescriptorType::eUniformBuffer, 100},
            {vk::DescriptorType::eCombinedImageSampler, 100},
        };
        vk::DescriptorPoolCreateInfo dpci;
        dpci.poolSizeCount = static_cast<uint32_t>(pool_sizes.size());
        dpci.pPoolSizes = pool_sizes.data();
        dpci.maxSets = 100;
        descriptor_pool_ = device_.createDescriptorPool(dpci);
    }

    void VulkanRenderer::CreateDescriptorSetLayout()
    {
        // binding=0 -> UBO, binding=1 -> sampler
        vk::DescriptorSetLayoutBinding ubo_bind;
        ubo_bind.binding = 0;
        ubo_bind.descriptorType = vk::DescriptorType::eUniformBuffer;
        ubo_bind.descriptorCount = 1;
        ubo_bind.stageFlags = vk::ShaderStageFlagBits::eVertex;

        vk::DescriptorSetLayoutBinding sampler_bind;
        sampler_bind.binding = 1;
        sampler_bind.descriptorType = vk::DescriptorType::eCombinedImageSampler;
        sampler_bind.descriptorCount = 1;
        sampler_bind.stageFlags = vk::ShaderStageFlagBits::eFragment;

        std::vector<vk::DescriptorSetLayoutBinding> bindings = {
            ubo_bind, sampler_bind};

        vk::DescriptorSetLayoutCreateInfo dsci;
        dsci.bindingCount = (uint32_t)bindings.size();
        dsci.pBindings = bindings.data();

        descriptor_set_layout_ = device_.createDescriptorSetLayout(dsci);
    }

    //
    // ----- 간단 예시: 정점/인덱스/UBO 리소스 생성 (삼각형) -----
    //
    struct Vertex
    {
        glm::vec2 pos;
        glm::vec2 uv;
    };

    void VulkanRenderer::CreateTestTriangleResources()
    {
        // 정점 데이터
        std::vector<Vertex> vertices = {
            {{-0.5f, -0.5f}, {0.0f, 1.0f}},
            {{0.0f, 0.5f}, {0.5f, 0.0f}},
            {{0.5f, -0.5f}, {1.0f, 1.0f}},
        };
        // 인덱스
        std::vector<uint16_t> indices = {0, 1, 2};
        index_count_ = (uint32_t)indices.size();

        // 1) 정점 버퍼 생성
        {
            BufferDesc bd;
            bd.size_in_bytes = sizeof(Vertex) * vertices.size();
            bd.usage_transfer_dst = true;
            bd.usage_vertex_buffer = true;
            vbo_handle_ = CreateBuffer(bd);
            // 데이터 업로드
            UpdateBuffer(vbo_handle_, vertices.data(), bd.size_in_bytes);
        }

        // 2) 인덱스 버퍼 생성
        {
            BufferDesc bd;
            bd.size_in_bytes = sizeof(uint16_t) * indices.size();
            bd.usage_transfer_dst = true;
            bd.usage_index_buffer = true;
            ibo_handle_ = CreateBuffer(bd);
            UpdateBuffer(ibo_handle_, indices.data(), bd.size_in_bytes);
        }

        // 3) 유니폼 버퍼 (MVP 행렬 등)
        {
            BufferDesc bd;
            bd.size_in_bytes = sizeof(glm::mat4);
            bd.usage_uniform_buffer = true;
            bd.usage_transfer_dst = true;
            ubo_handle_ = CreateBuffer(bd);

            // 일단 단위 행렬
            glm::mat4 model = glm::mat4(1.0f);
            glm::mat4 view = glm::lookAt(glm::vec3(0.f, 0.f, 2.f),
                                         glm::vec3(0.f, 0.f, 0.f),
                                         glm::vec3(0.f, 1.f, 0.f));
            glm::mat4 proj = glm::perspective(glm::radians(45.f), 1280.f / 720.f, 0.1f, 10.f);
            proj[1][1] *= -1; // Vulkan 좌표계 보정
            glm::mat4 mvp = proj * view * model;

            UpdateBuffer(ubo_handle_, &mvp, sizeof(mvp));
        }

        // DescriptorSet 할당 + UBO+Sampler 바인딩(예: 디폴트 흰 텍스처)
        {
            vk::DescriptorSetAllocateInfo dsai;
            dsai.descriptorPool = descriptor_pool_;
            dsai.descriptorSetCount = 1;
            dsai.pSetLayouts = &descriptor_set_layout_;
            auto sets = device_.allocateDescriptorSets(dsai);
            descriptor_sets_ = sets;

            // UBO 바인딩
            vk::DescriptorBufferInfo dbi;
            dbi.buffer = buffers_[ubo_handle_].buffer;
            dbi.offset = 0;
            dbi.range = sizeof(glm::mat4);

            vk::WriteDescriptorSet wds_ubo;
            wds_ubo.dstSet = descriptor_sets_[0];
            wds_ubo.dstBinding = 0; // ubo binding=0
            wds_ubo.descriptorCount = 1;
            wds_ubo.descriptorType = vk::DescriptorType::eUniformBuffer;
            wds_ubo.pBufferInfo = &dbi;

            // 샘플러 (디폴트 흰 텍스처 없이 일단 비워둘 수도 있음)
            // 여기서는 그냥 dummy
            vk::DescriptorImageInfo dii;
            dii.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
            dii.imageView = VK_NULL_HANDLE; // 실제 텍스처가 없으니
            dii.sampler = VK_NULL_HANDLE;

            vk::WriteDescriptorSet wds_sampler;
            wds_sampler.dstSet = descriptor_sets_[0];
            wds_sampler.dstBinding = 1; // sampler binding=1
            wds_sampler.descriptorCount = 1;
            wds_sampler.descriptorType = vk::DescriptorType::eCombinedImageSampler;
            wds_sampler.pImageInfo = &dii;

            std::vector<vk::WriteDescriptorSet> writes = {wds_ubo, wds_sampler};
            device_.updateDescriptorSets(writes, {});
        }
    }

    //
    // ----- 데이터 업로드 (스테이징 버퍼) ----
    //
    void VulkanRenderer::UploadDataToBuffer(const void *src_data, size_t src_size,
                                            vk::Buffer dst_buffer)
    {
        // 1) 스테이징 버퍼 생성
        vk::BufferCreateInfo staging_info;
        staging_info.size = src_size;
        staging_info.usage = vk::BufferUsageFlagBits::eTransferSrc;

        VmaAllocationCreateInfo alloc_ci = {};
        alloc_ci.usage = VMA_MEMORY_USAGE_CPU_ONLY;

        VkBuffer staging_buf;
        VmaAllocation staging_alloc;
        vmaCreateBuffer(allocator_, (VkBufferCreateInfo *)&staging_info,
                        &alloc_ci, &staging_buf, &staging_alloc, nullptr);

        // 2) 맵핑 & memcpy
        void *mapped = nullptr;
        vmaMapMemory(allocator_, staging_alloc, &mapped);
        std::memcpy(mapped, src_data, src_size);
        vmaUnmapMemory(allocator_, staging_alloc);

        // 3) 커맨드 버퍼 하나 만들고 copy
        vk::CommandBufferAllocateInfo cbai;
        cbai.commandPool = command_pool_;
        cbai.level = vk::CommandBufferLevel::ePrimary;
        cbai.commandBufferCount = 1;
        auto cbs = device_.allocateCommandBuffers(cbai);
        auto cmd = cbs[0];

        vk::CommandBufferBeginInfo begin_info;
        begin_info.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;
        cmd.begin(begin_info);

        vk::BufferCopy copy_region(0, 0, src_size);
        cmd.copyBuffer(staging_buf, dst_buffer, copy_region);

        cmd.end();

        // 4) 큐 제출 & 대기
        vk::SubmitInfo si;
        si.commandBufferCount = 1;
        si.pCommandBuffers = &cmd;
        graphics_queue_.submit(si);
        graphics_queue_.waitIdle();

        // 5) 스테이징 버퍼 해제
        device_.freeCommandBuffers(command_pool_, cbs);
        vmaDestroyBuffer(allocator_, staging_buf, staging_alloc);
    }

    //
    // ----- IRenderer 구현부 -----
    //
    BufferHandle VulkanRenderer::CreateBuffer(const BufferDesc &desc)
    {
        // VulkanBuffer 생성
        vk::BufferCreateInfo bci;
        bci.size = desc.size_in_bytes;

        vk::BufferUsageFlags usage;
        if (desc.usage_uniform_buffer)
            usage |= vk::BufferUsageFlagBits::eUniformBuffer;
        if (desc.usage_vertex_buffer)
            usage |= vk::BufferUsageFlagBits::eVertexBuffer;
        if (desc.usage_index_buffer)
            usage |= vk::BufferUsageFlagBits::eIndexBuffer;
        if (desc.usage_transfer_src)
            usage |= vk::BufferUsageFlagBits::eTransferSrc;
        if (desc.usage_transfer_dst)
            usage |= vk::BufferUsageFlagBits::eTransferDst;
        bci.usage = usage;

        VmaAllocationCreateInfo alloc_ci = {};
        // GPU에 올릴 데이터이지만, Update를 자주 할지 여부에 따라 달라집니다.
        // 여기서는 “**동적 업데이트 가능**” UBO처럼 쓴다면 CPU_TO_GPU.
        // 정적이라면 GPU_ONLY. 예시로 GPU_ONLY로 가정
        alloc_ci.usage = VMA_MEMORY_USAGE_GPU_ONLY;

        VulkanBuffer vb;
        VkBuffer raw_buf;
        VmaAllocation alloc;
        auto result = vmaCreateBuffer(allocator_, (VkBufferCreateInfo *)&bci,
                                      &alloc_ci, &raw_buf, &alloc, nullptr);
        if (result != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create buffer with VMA");
        }
        vb.buffer = raw_buf;
        vb.allocation = alloc;
        vb.size_in_bytes = desc.size_in_bytes;

        BufferHandle handle = next_buffer_handle_++;
        if (handle >= buffers_.size())
        {
            buffers_.resize(handle + 1);
        }
        buffers_[handle] = vb;
        return handle;
    }

    void VulkanRenderer::UpdateBuffer(BufferHandle handle, const void *data, size_t size)
    {
        if (handle == 0 || handle >= buffers_.size())
        {
            throw std::runtime_error("Invalid buffer handle in UpdateBuffer");
        }
        VulkanBuffer &vb = buffers_[handle];
        // 스테이징 버퍼를 통해 GPU_ONLY 버퍼에 복사
        UploadDataToBuffer(data, size, vb.buffer);
    }

    void VulkanRenderer::BindBuffer(BufferHandle handle, uint32_t bind_point)
    {
        // 실제 vkCmdBindVertexBuffers, vkCmdBindIndexBuffer, DescriptorSet 업데이트 등
        // 여기서는 RecordCommandBuffer에서 실제로 bind
    }

    TextureHandle VulkanRenderer::CreateTexture(const TextureDesc &desc,
                                                const void *initial_data)
    {
        // 이미지를 GPU 전용으로 생성
        vk::ImageCreateInfo ici;
        ici.imageType = vk::ImageType::e2D;
        ici.extent.width = desc.width;
        ici.extent.height = desc.height;
        ici.extent.depth = 1;
        ici.mipLevels = 1;
        ici.arrayLayers = 1;
        ici.format = vk::Format::eR8G8B8A8Unorm;
        ici.usage = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst;

        VmaAllocationCreateInfo alloc_ci = {};
        alloc_ci.usage = VMA_MEMORY_USAGE_GPU_ONLY;

        VulkanTexture vt;
        VkImage raw_img;
        VmaAllocation alloc;
        auto result = vmaCreateImage(allocator_, (VkImageCreateInfo *)&ici,
                                     &alloc_ci, &raw_img, &alloc, nullptr);
        if (result != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create image with VMA");
        }
        vt.image = raw_img;
        vt.allocation = alloc;
        vt.width = desc.width;
        vt.height = desc.height;

        // ImageView
        vk::ImageViewCreateInfo ivci;
        ivci.image = vt.image;
        ivci.viewType = vk::ImageViewType::e2D;
        ivci.format = ici.format;
        ivci.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
        ivci.subresourceRange.levelCount = 1;
        ivci.subresourceRange.layerCount = 1;
        vt.image_view = device_.createImageView(ivci);

        // initial_data -> 스테이징 버퍼 복사 (생략 가능)
        // 실제로는 layout 전환(vkCmdPipelineBarrier) 등 필요.

        TextureHandle handle = next_texture_handle_++;
        if (handle >= textures_.size())
        {
            textures_.resize(handle + 1);
        }
        textures_[handle] = vt;
        return handle;
    }

    void VulkanRenderer::BindTexture(TextureHandle handle, uint32_t bind_point)
    {
        // DescriptorSet 업데이트 등
    }

    SamplerHandle VulkanRenderer::CreateSampler(const SamplerDesc &desc)
    {
        vk::SamplerCreateInfo sci;
        sci.magFilter = vk::Filter::eLinear;
        sci.minFilter = vk::Filter::eLinear;
        sci.addressModeU = vk::SamplerAddressMode::eRepeat;
        sci.addressModeV = vk::SamplerAddressMode::eRepeat;
        sci.addressModeW = vk::SamplerAddressMode::eRepeat;

        VulkanSampler vs;
        vs.sampler = device_.createSampler(sci);

        SamplerHandle handle = next_sampler_handle_++;
        if (handle >= samplers_.size())
        {
            samplers_.resize(handle + 1);
        }
        samplers_[handle] = vs;
        return handle;
    }

    void VulkanRenderer::BindSampler(SamplerHandle handle, uint32_t bind_point)
    {
        // DescriptorSet 업데이트 등
    }

    //
    // ----- 셰이더 생성/해제 ----
    //
    ShaderHandle VulkanRenderer::CreateShader(const ShaderDesc &desc)
    {
        auto code = ReadFile(desc.file_path);
        vk::ShaderModule sm = CreateShaderModule(code);

        VulkanShader vs;
        vs.shader_module = sm;

        ShaderHandle handle = next_shader_handle_++;
        shaders_[handle] = vs;
        return handle;
    }

    void VulkanRenderer::ReleaseShader(ShaderHandle handle)
    {
        auto it = shaders_.find(handle);
        if (it != shaders_.end())
        {
            if (it->second.shader_module)
            {
                device_.destroyShaderModule(it->second.shader_module);
            }
            shaders_.erase(it);
        }
    }

    vk::ShaderModule VulkanRenderer::CreateShaderModule(const std::vector<char> &code)
    {
        vk::ShaderModuleCreateInfo smci;
        smci.codeSize = code.size();
        smci.pCode = reinterpret_cast<const uint32_t *>(code.data());
        return device_.createShaderModule(smci);
    }

    std::vector<char> VulkanRenderer::ReadFile(const std::string &filename)
    {
        std::ifstream file(filename, std::ios::ate | std::ios::binary);
        if (!file.is_open())
        {
            throw std::runtime_error("Failed to open file: " + filename);
        }
        size_t file_size = (size_t)file.tellg();
        std::vector<char> buffer(file_size);
        file.seekg(0);
        file.read(buffer.data(), file_size);
        file.close();
        return buffer;
    }

    //
    // ----- 파이프라인 ----
    //
    PipelineHandle VulkanRenderer::CreatePipeline(const PipelineDesc &desc)
    {
        // 셰이더 모듈
        auto vsh_it = shaders_.find(desc.vertex_shader);
        auto fsh_it = shaders_.find(desc.fragment_shader);
        if (vsh_it == shaders_.end() || fsh_it == shaders_.end())
        {
            throw std::runtime_error("Invalid shader handle in pipeline desc");
        }
        vk::ShaderModule vert_mod = vsh_it->second.shader_module;
        vk::ShaderModule frag_mod = fsh_it->second.shader_module;

        vk::PipelineShaderStageCreateInfo vert_stage;
        vert_stage.stage = vk::ShaderStageFlagBits::eVertex;
        vert_stage.module = vert_mod;
        vert_stage.pName = "main";

        vk::PipelineShaderStageCreateInfo frag_stage;
        frag_stage.stage = vk::ShaderStageFlagBits::eFragment;
        frag_stage.module = frag_mod;
        frag_stage.pName = "main";

        vk::PipelineShaderStageCreateInfo stages[] = {vert_stage, frag_stage};

        // 간단 정점입력
        vk::PipelineVertexInputStateCreateInfo vi;
        // (생략: 바인딩/속성)

        vk::PipelineInputAssemblyStateCreateInfo ia;
        ia.topology = vk::PrimitiveTopology::eTriangleList;

        vk::Viewport viewport(0, 0,
                              float(swapchain_extent_.width),
                              float(swapchain_extent_.height),
                              0.f, 1.f);
        vk::Rect2D scissor({0, 0}, swapchain_extent_);
        vk::PipelineViewportStateCreateInfo vp;
        vp.viewportCount = 1;
        vp.pViewports = &viewport;
        vp.scissorCount = 1;
        vp.pScissors = &scissor;

        vk::PipelineRasterizationStateCreateInfo rs;
        rs.polygonMode = vk::PolygonMode::eFill;
        rs.cullMode = vk::CullModeFlagBits::eBack;
        rs.frontFace = vk::FrontFace::eCounterClockwise;
        rs.lineWidth = 1.0f;

        vk::PipelineMultisampleStateCreateInfo ms;
        ms.rasterizationSamples = vk::SampleCountFlagBits::e1;

        vk::PipelineColorBlendAttachmentState cbAttach;
        cbAttach.colorWriteMask =
            vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
            vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;
        cbAttach.blendEnable = false;

        vk::PipelineColorBlendStateCreateInfo cb;
        cb.attachmentCount = 1;
        cb.pAttachments = &cbAttach;

        vk::PipelineLayoutCreateInfo plci;
        plci.setLayoutCount = 1;
        plci.pSetLayouts = &descriptor_set_layout_;

        VulkanPipeline vpip;
        vpip.pipeline_layout = device_.createPipelineLayout(plci);

        vk::GraphicsPipelineCreateInfo gpci;
        gpci.stageCount = 2;
        gpci.pStages = stages;
        gpci.pVertexInputState = &vi;
        gpci.pInputAssemblyState = &ia;
        gpci.pViewportState = &vp;
        gpci.pRasterizationState = &rs;
        gpci.pMultisampleState = &ms;
        gpci.pColorBlendState = &cb;
        gpci.layout = vpip.pipeline_layout;
        gpci.renderPass = render_pass_;
        gpci.subpass = 0;

        auto res = device_.createGraphicsPipeline(nullptr, gpci);
        if (res.result != vk::Result::eSuccess)
        {
            throw std::runtime_error("Failed to create pipeline");
        }
        vpip.pipeline = res.value;

        PipelineHandle ph = next_pipeline_handle_++;
        if (ph >= pipelines_.size())
        {
            pipelines_.resize(ph + 1);
        }
        pipelines_[ph] = vpip;
        return ph;
    }

    void VulkanRenderer::BindPipeline(PipelineHandle handle)
    {
        // 실제 vkCmdBindPipeline은 RecordCommandBuffer에서 사용
    }

    //
    // ----- 프레임 ----
    //
    void VulkanRenderer::BeginFrame()
    {
        device_.waitForFences(in_flight_fences_[current_frame_], VK_TRUE, UINT64_MAX);
        device_.resetFences(in_flight_fences_[current_frame_]);

        auto [acquire_result, image_index] = device_.acquireNextImageKHR(
            swapchain_, UINT64_MAX, image_available_semaphores_[current_frame_], {});
        if (acquire_result != vk::Result::eSuccess &&
            acquire_result != vk::Result::eSuboptimalKHR)
        {
            throw std::runtime_error("Failed to acquire swapchain image");
        }

        // 커맨드버퍼 기록
        command_buffers_[image_index].reset();
        RecordCommandBuffer(command_buffers_[image_index], image_index);
    }

    void VulkanRenderer::EndFrame()
    {
        // 여기서는 image_index=0으로 단순 처리
        // 실제로는 BeginFrame()에서 얻은 image_index를 저장해둬야 합니다.
        uint32_t image_index = 0;

        vk::SubmitInfo si;
        vk::Semaphore wait_semaphores[] = {image_available_semaphores_[current_frame_]};
        vk::PipelineStageFlags wait_stages[] = {vk::PipelineStageFlagBits::eColorAttachmentOutput};
        si.waitSemaphoreCount = 1;
        si.pWaitSemaphores = wait_semaphores;
        si.pWaitDstStageMask = wait_stages;
        si.commandBufferCount = 1;
        si.pCommandBuffers = &command_buffers_[image_index];
        vk::Semaphore signal_semaphores[] = {render_finished_semaphores_[current_frame_]};
        si.signalSemaphoreCount = 1;
        si.pSignalSemaphores = signal_semaphores;

        graphics_queue_.submit(si, in_flight_fences_[current_frame_]);

        vk::PresentInfoKHR pi;
        pi.waitSemaphoreCount = 1;
        pi.pWaitSemaphores = signal_semaphores;
        vk::SwapchainKHR swapchains[] = {swapchain_};
        pi.swapchainCount = 1;
        pi.pSwapchains = swapchains;
        pi.pImageIndices = &image_index;

        graphics_queue_.presentKHR(pi);

        current_frame_ = (current_frame_ + 1) % kMaxFramesInFlight;
    }

    void VulkanRenderer::RecordCommandBuffer(vk::CommandBuffer cmd, uint32_t image_index)
    {
        vk::CommandBufferBeginInfo begin_info;
        cmd.begin(begin_info);

        vk::RenderPassBeginInfo rpbi;
        rpbi.renderPass = render_pass_;
        rpbi.framebuffer = framebuffers_[image_index];
        rpbi.renderArea.offset = vk::Offset2D{0, 0};
        rpbi.renderArea.extent = swapchain_extent_;

        vk::ClearValue clear_color = vk::ClearColorValue(std::array<float, 4>{0.1f, 0.2f, 0.3f, 1.0f});
        rpbi.clearValueCount = 1;
        rpbi.pClearValues = &clear_color;

        cmd.beginRenderPass(rpbi, vk::SubpassContents::eInline);

        // 파이프라인 바인딩(예시로 handle=1)
        if (pipelines_.size() > 1 && pipelines_[1].pipeline)
        {
            cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, pipelines_[1].pipeline);
        }

        // DescriptorSet 바인딩 (UBO+Sampler)
        if (!descriptor_sets_.empty())
        {
            cmd.bindDescriptorSets(
                vk::PipelineBindPoint::eGraphics,
                pipelines_[1].pipeline_layout, // 예: 파이프라인 레이아웃
                0,                             // firstSet
                descriptor_sets_[0],
                {});
        }

        // 정점/인덱스 버퍼 바인딩
        if (vbo_handle_ < buffers_.size())
        {
            vk::Buffer vb = buffers_[vbo_handle_].buffer;
            vk::DeviceSize offset = 0;
            cmd.bindVertexBuffers(0, vb, offset);
        }
        if (ibo_handle_ < buffers_.size())
        {
            cmd.bindIndexBuffer(buffers_[ibo_handle_].buffer, 0,
                                vk::IndexType::eUint16);
        }

        // Draw
        cmd.drawIndexed(index_count_, 1, 0, 0, 0);

        cmd.endRenderPass();
        cmd.end();
    }

    //
    // ----- ReleaseResource -----
    //
    void VulkanRenderer::ReleaseResource(uint64_t handle)
    {
        // 버퍼
        if (handle < buffers_.size() && buffers_[handle].buffer)
        {
            vmaDestroyBuffer(allocator_, (VkBuffer)buffers_[handle].buffer,
                             buffers_[handle].allocation);
            buffers_[handle].buffer = nullptr;
            buffers_[handle].allocation = nullptr;
            return;
        }
        // 텍스처
        if (handle < textures_.size() && textures_[handle].image)
        {
            device_.destroyImageView(textures_[handle].image_view);
            vmaDestroyImage(allocator_, (VkImage)textures_[handle].image,
                            textures_[handle].allocation);
            textures_[handle].image = nullptr;
            textures_[handle].allocation = nullptr;
            return;
        }
        // 샘플러
        if (handle < samplers_.size() && samplers_[handle].sampler)
        {
            device_.destroySampler(samplers_[handle].sampler);
            samplers_[handle].sampler = nullptr;
            return;
        }
        // 파이프라인
        if (handle < pipelines_.size() && pipelines_[handle].pipeline)
        {
            device_.destroyPipeline(pipelines_[handle].pipeline);
            device_.destroyPipelineLayout(pipelines_[handle].pipeline_layout);
            pipelines_[handle].pipeline = nullptr;
            return;
        }
        // 셰이더
        auto it = shaders_.find(handle);
        if (it != shaders_.end())
        {
            device_.destroyShaderModule(it->second.shader_module);
            shaders_.erase(it);
            return;
        }
    }

} // namespace Lumora
