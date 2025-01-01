#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <memory>

namespace Lumora
{
    // 리소스 핸들들
    using BufferHandle = uint64_t;
    using TextureHandle = uint64_t;
    using SamplerHandle = uint64_t;
    using PipelineHandle = uint64_t;

    // 플랫폼 독립적인 버퍼 생성 파라미터
    struct BufferDesc
    {
        size_t sizeInBytes = 0; // 버퍼 크기
        bool usageUniformBuffer = false;
        bool usageVertexBuffer = false;
        bool usageIndexBuffer = false;
        bool usageTransferSrc = false;
        bool usageTransferDst = false;
    };

    // 플랫폼 독립적인 텍스처 생성 파라미터
    struct TextureDesc
    {
        uint32_t width = 0;
        uint32_t height = 0;
        // 원하는 포맷, 샘플링/필터링 옵션 등 더 필요한 것이 있다면
        // 여기에 계속 확장 가능합니다.
    };

    // 플랫폼 독립적인 샘플러 생성 파라미터
    struct SamplerDesc
    {
        // 필터링, 어드레싱 모드 등등
        // Vulkan의 VkSamplerCreateInfo와 1:1로 매핑 가능하도록
        // 계속 확장 가능
    };

    // 파이프라인(셰이더, 렌더 스테이트 등등) 생성 파라미터
    struct PipelineDesc
    {
        // 셰이더 코드, 렌더 상태, 블렌딩, 깊이버퍼 상태 등
        // Vulkan의 각종 파이프라인 상태를 매핑할 수 있도록 확장
    };

    class IRenderer
    {
    public:
        virtual ~IRenderer() = default;

        // 버퍼
        virtual BufferHandle CreateBuffer(const BufferDesc &desc) = 0;
        virtual void UpdateBuffer(BufferHandle handle, const void *data, size_t size) = 0;
        virtual void BindBuffer(BufferHandle handle, uint32_t bindPoint) = 0;

        // 텍스처
        virtual TextureHandle CreateTexture(const TextureDesc &desc, const void *initialData = nullptr) = 0;
        virtual void BindTexture(TextureHandle handle, uint32_t bindPoint) = 0;

        // 샘플러
        virtual SamplerHandle CreateSampler(const SamplerDesc &desc) = 0;
        virtual void BindSampler(SamplerHandle handle, uint32_t bindPoint) = 0;

        // 파이프라인
        virtual PipelineHandle CreatePipeline(const PipelineDesc &desc) = 0;
        virtual void BindPipeline(PipelineHandle handle) = 0;

        // 리소스 해제: 모든 리소스를 하나의 인터페이스로만 해제
        // 어떤 종류의 리소스인지 내부에서 구분하여 해제
        virtual void ReleaseResource(uint64_t handle) = 0;

        // 스테틱 함수로 VulkanRenderer 인스턴스화
        static std::unique_ptr<IRenderer> Create();
    };
}
