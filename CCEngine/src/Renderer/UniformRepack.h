#pragma once
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <vector>

namespace CCEngine
{
    struct UniformCopyRange
    {
        uint32_t Source = 0, Destination = 0, Size = 0;
    };

    struct UniformRepackLayout
    {
        uint32_t Binding = 0, SourceSize = 0, DestinationSize = 0;
        std::vector<UniformCopyRange> Copies;
    };

    inline void ValidateUniformRepack(const std::vector<UniformRepackLayout>& layouts)
    {
        bool bindings[8]{};
        for (const auto& layout : layouts)
        {
            if (layout.Binding >= 8 || bindings[layout.Binding] || !layout.SourceSize ||
                !layout.DestinationSize || layout.SourceSize > 65536 || layout.DestinationSize > 65536 ||
                layout.Copies.empty() || layout.Copies.size() > 16384)
                throw std::invalid_argument("Invalid uniform repack layout");
            bindings[layout.Binding] = true;
            std::vector<bool> occupied((layout.DestinationSize + 3) / 4, false);
            for (const auto& copy : layout.Copies)
            {
                // 덧셈 후 비교하면 정수 overflow를 놓칠 수 있으므로 남은 크기와 비교한다.
                if (!copy.Size || (copy.Source | copy.Destination | copy.Size) % 4 ||
                    copy.Source > layout.SourceSize || copy.Size > layout.SourceSize - copy.Source ||
                    copy.Destination > layout.DestinationSize || copy.Size > layout.DestinationSize - copy.Destination)
                    throw std::invalid_argument("Uniform repack copy exceeds storage or alignment");
                for (uint32_t index = copy.Destination / 4; index < (copy.Destination + copy.Size) / 4; ++index)
                {
                    if (occupied[index]) throw std::invalid_argument("Uniform repack destination overlap");
                    occupied[index] = true;
                }
            }
        }
    }

    inline std::vector<uint8_t> RepackUniformBytes(const UniformRepackLayout& layout,
        const std::vector<uint8_t>& source)
    {
        ValidateUniformRepack({layout});
        if (source.size() < layout.SourceSize)
            throw std::invalid_argument("Uniform repack source buffer is too small");
        // 원본 패딩을 읽지 않으며 목적 패딩은 항상 0이다. 원본을 덮어쓰지 않는다.
        std::vector<uint8_t> result(layout.DestinationSize, 0);
        for (const auto& copy : layout.Copies)
            std::memcpy(result.data() + copy.Destination, source.data() + copy.Source, copy.Size);
        return result;
    }
}
