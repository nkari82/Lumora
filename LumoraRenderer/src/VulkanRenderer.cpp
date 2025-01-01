#include "VulkanRenderer.h"
#include <stdexcept>
#include <fstream>

namespace Lumora
{

    // 스태틱 함수
    std::unique_ptr<IRenderer> IRenderer::Create()
    {
        return std::make_unique<VulkanRenderer>();
    }

    VulkanRenderer::VulkanRenderer() {}

    VulkanRenderer::~VulkanRenderer()
    {
        CleanupVulkan();
    }

    void VulkanRenderer::InitVulkan()
    {
        if (created_)
            return;
        created_ = true;

        // 인스턴스
        vk::ApplicationInfo app_info;
        app_info.pApplicationName = "Lumora App";
        app_info.applicationVersion = 1;
        app_info.pEngineName = "Lumora Engine";
        app_info.engineVersion = 1;
        app_info.apiVersion = VK_API_VERSION_1_2;

        vk::InstanceCreateInfo ici;
        ici.pApplicationInfo = &app_info;

        // Win32 Surface 익스텐션 등
        // const char* ext[] = { "VK_KHR_surface", "VK_KHR_win32_surface" };
        // ici.enabledExtensionCount = 2;
        // ici.ppEnabledExtensionNames = ext;
        // Validation Layer 등 설정 가능

        instance_ = vk::createInstance(ici);

        // 물리 디바이스
        auto pdevices = instance_.enumeratePhysicalDevices();
        if (pdevices.empty())
        {
            throw std::runtime_error("No physical device found");
        }
        physical_device_ = pdevices[0];

        // 그래픽스 큐
        auto qprops = physical_device_.getQueueFamilyProperties();
        for (uint32_t i = 0; i < qprops.size(); ++i)
        {
            if (qprops[i].queueFlags & vk::QueueFlagBits::eGraphics)
            {
                graphics_queue_index_ = i;
                break;
            }
        }

        float qPriority = 1.f;
        vk::DeviceQueueCreateInfo dqci;
        dqci.queueFamilyIndex = graphics_queue_index_;
        dqci.queueCount = 1;
        dqci.pQueuePriorities = &qPriority;

        vk::DeviceCreateInfo dci;
        dci.queueCreateInfoCount = 1;
        dci.pQueueCreateInfos = &dqci;
        // Device extension: swapchain, etc.

        device_ = physical_device_.createDevice(dci);
        graphics_queue_ = device_.getQueue(graphics_queue_index_, 0);

        // TODO: command pool, sync objects
    }

    void VulkanRenderer::CleanupVulkan()
    {
        if (!created_)
            return;
        device_.waitIdle();

        // 스왑체인
        for (size_t i = 1; i < swapchains_.size(); ++i)
        {
            auto &sc = swapchains_[i];
            for (auto fb : sc.framebuffers)
            {
                device_.destroyFramebuffer(fb);
            }
            device_.destroyRenderPass(sc.render_pass);
            for (auto iv : sc.image_views)
            {
                device_.destroyImageView(iv);
            }
            if (sc.swapchain)
            {
                device_.destroySwapchainKHR(sc.swapchain);
            }
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
                device_.destroyBuffer(buffers_[i].buffer);
            }
        }

        // 텍스처
        for (size_t i = 1; i < textures_.size(); ++i)
        {
            if (textures_[i].image_view)
            {
                device_.destroyImageView(textures_[i].image_view);
            }
            if (textures_[i].image)
            {
                device_.destroyImage(textures_[i].image);
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

        // device
        if (device_)
            device_.destroy();
        // instance
        if (instance_)
            instance_.destroy();

        created_ = false;
    }

    SwapChainHandle VulkanRenderer::CreateSwapChain(const SwapChainDesc &desc)
    {
        if (!created_)
        {
            InitVulkan();
        }
        return CreateSwapChainInternal(desc);
    }

    SwapChainHandle VulkanRenderer::CreateSwapChainInternal(const SwapChainDesc &desc)
    {
        VulkanSwapChain sc;
        sc.extent = vk::Extent2D{(uint32_t)desc.width, (uint32_t)desc.height};

        // Win32 Surface 만들기 (생략)
        // vk::Win32SurfaceCreateInfoKHR sci;
        // sci.hwnd = static_cast<HWND>(desc.window_handle);
        // sci.hinstance = GetModuleHandle(nullptr);
        // auto surface = instance_.createWin32SurfaceKHR(sci);

        // swapchain 생성 (surface 필요)
        vk::SwapchainCreateInfoKHR swapci;
        swapci.minImageCount = 2;
        swapci.imageFormat = vk::Format::eB8G8R8A8Unorm;
        swapci.imageColorSpace = vk::ColorSpaceKHR::eSrgbNonlinear;
        swapci.imageExtent = sc.extent;
        swapci.imageArrayLayers = 1;
        swapci.imageUsage = vk::ImageUsageFlagBits::eColorAttachment;
        swapci.imageSharingMode = vk::SharingMode::eExclusive;
        // swapci.surface = surface; // 실제로 필요
        swapci.presentMode = desc.vsync ? vk::PresentModeKHR::eFifo
                                        : vk::PresentModeKHR::eImmediate;

        sc.swapchain = device_.createSwapchainKHR(swapci);
        sc.images = device_.getSwapchainImagesKHR(sc.swapchain);

        // imageView
        sc.image_views.resize(sc.images.size());
        for (size_t i = 0; i < sc.images.size(); ++i)
        {
            vk::ImageViewCreateInfo ivci;
            ivci.image = sc.images[i];
            ivci.viewType = vk::ImageViewType::e2D;
            ivci.format = swapci.imageFormat;
            ivci.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
            ivci.subresourceRange.levelCount = 1;
            ivci.subresourceRange.layerCount = 1;
            sc.image_views[i] = device_.createImageView(ivci);
        }

        // renderPass
        vk::AttachmentDescription color_attach;
        color_attach.format = swapci.imageFormat;
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

        sc.render_pass = device_.createRenderPass(rpci);

        // framebuffer
        sc.framebuffers.resize(sc.image_views.size());
        for (size_t i = 0; i < sc.image_views.size(); ++i)
        {
            vk::ImageView attachments[] = {sc.image_views[i]};
            vk::FramebufferCreateInfo fci;
            fci.renderPass = sc.render_pass;
            fci.attachmentCount = 1;
            fci.pAttachments = attachments;
            fci.width = sc.extent.width;
            fci.height = sc.extent.height;
            fci.layers = 1;
            sc.framebuffers[i] = device_.createFramebuffer(fci);
        }

        // 할당
        SwapChainHandle handle = next_swapchain_handle_++;
        if (handle >= swapchains_.size())
        {
            swapchains_.resize(handle + 1);
        }
        swapchains_[handle] = sc;

        return handle;
    }

    // 버퍼
    BufferHandle VulkanRenderer::CreateBuffer(const BufferDesc &desc)
    {
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

        VulkanBuffer vb;
        vb.buffer = device_.createBuffer(bci);
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
        // 스테이징 버퍼 로직, direct mapping 등
    }

    void VulkanRenderer::BindBuffer(BufferHandle handle, uint32_t bind_point)
    {
    }

    // 텍스처
    TextureHandle VulkanRenderer::CreateTexture(const TextureDesc &desc,
                                                const void *initial_data)
    {
        vk::ImageCreateInfo ici;
        ici.imageType = vk::ImageType::e2D;
        ici.extent.width = desc.width;
        ici.extent.height = desc.height;
        ici.extent.depth = 1;
        ici.mipLevels = 1;
        ici.arrayLayers = 1;
        ici.format = vk::Format::eR8G8B8A8Unorm;
        ici.usage = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst;

        VulkanTexture vt;
        vt.image = device_.createImage(ici);
        vt.width = desc.width;
        vt.height = desc.height;

        // image_view
        vk::ImageViewCreateInfo ivci;
        ivci.image = vt.image;
        ivci.viewType = vk::ImageViewType::e2D;
        ivci.format = ici.format;
        ivci.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
        ivci.subresourceRange.levelCount = 1;
        ivci.subresourceRange.layerCount = 1;
        vt.image_view = device_.createImageView(ivci);

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
    }

    // 샘플러
    SamplerHandle VulkanRenderer::CreateSampler(const SamplerDesc &desc)
    {
        vk::SamplerCreateInfo sci;
        sci.magFilter = vk::Filter::eLinear;
        sci.minFilter = vk::Filter::eLinear;

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
    }

    // 셰이더
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
            device_.destroyShaderModule(it->second.shader_module);
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

    // 파이프라인
    PipelineHandle VulkanRenderer::CreatePipeline(const PipelineDesc &desc)
    {
        // (생략) 실제 그래픽스 파이프라인 생성
        VulkanPipeline vpip;
        vpip.pipeline_layout = vk::PipelineLayout();
        vpip.pipeline = vk::Pipeline();

        PipelineHandle handle = next_pipeline_handle_++;
        if (handle >= pipelines_.size())
        {
            pipelines_.resize(handle + 1);
        }
        pipelines_[handle] = vpip;
        return handle;
    }
    void VulkanRenderer::BindPipeline(PipelineHandle handle)
    {
    }

    // 리소스 해제
    void VulkanRenderer::ReleaseResource(uint64_t handle)
    {
        // 스왑체인
        if (handle < swapchains_.size() && swapchains_[handle].swapchain)
        {
            auto &sc = swapchains_[handle];
            for (auto fb : sc.framebuffers)
            {
                device_.destroyFramebuffer(fb);
            }
            device_.destroyRenderPass(sc.render_pass);
            for (auto iv : sc.image_views)
            {
                device_.destroyImageView(iv);
            }
            device_.destroySwapchainKHR(sc.swapchain);
            sc.swapchain = nullptr;
            return;
        }
        // 버퍼
        if (handle < buffers_.size() && buffers_[handle].buffer)
        {
            device_.destroyBuffer(buffers_[handle].buffer);
            buffers_[handle].buffer = nullptr;
            return;
        }
        // 텍스처
        if (handle < textures_.size() && textures_[handle].image)
        {
            device_.destroyImageView(textures_[handle].image_view);
            device_.destroyImage(textures_[handle].image);
            textures_[handle].image = nullptr;
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

    // 프레임
    void VulkanRenderer::BeginFrame()
    {
        // Acquire swapchain image, record command buffer, etc.
    }
    void VulkanRenderer::EndFrame()
    {
        // Present
    }

} // namespace Lumora
