#pragma once
#include <vector>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <immintrin.h>

namespace Utils {

class ArenaAllocator {
public:
    explicit ArenaAllocator(size_t blockSize = 1024 * 1024) : m_blockSize(blockSize), m_currentOffset(0) {
        m_blocks.push_back(std::make_unique<uint8_t[]>(m_blockSize));
    }

    void* Allocate(size_t size, size_t alignment = alignof(std::max_align_t)) {
        if (size > m_blockSize) {
            // For blocks larger than m_blockSize, allocate a dedicated block
            // Ensure alignment for the dedicated block
            void* ptr = _mm_malloc(size, alignment);
            if (!ptr) throw std::bad_alloc();
            m_customBlocks.push_back(ptr);
            return ptr;
        }

        size_t padding = (alignment - (m_currentOffset % alignment)) % alignment;
        size_t totalSize = size + padding;

        if (m_currentOffset + totalSize > m_blockSize) {
            m_blocks.push_back(std::make_unique<uint8_t[]>(m_blockSize));
            m_currentOffset = 0;
            padding = (alignment - (m_currentOffset % alignment)) % alignment;
            totalSize = size + padding;
        }

        void* ptr = m_blocks.back().get() + m_currentOffset + padding;
        m_currentOffset += totalSize;
        return ptr;
    }

    template<typename T, typename... Args>
    T* Create(Args&&... args) {
        void* ptr = Allocate(sizeof(T), alignof(T));
        return new (ptr) T(std::forward<Args>(args)...);
    }

    ~ArenaAllocator() {
        for (void* ptr : m_customBlocks) _mm_free(ptr);
    }

    void Reset() {
        m_blocks.clear();
        for (void* ptr : m_customBlocks) _mm_free(ptr);
        m_customBlocks.clear();
        m_blocks.push_back(std::make_unique<uint8_t[]>(m_blockSize));
        m_currentOffset = 0;
    }

private:
    size_t m_blockSize;
    size_t m_currentOffset;
    std::vector<std::unique_ptr<uint8_t[]>> m_blocks;
    std::vector<void*> m_customBlocks;
};

} // namespace Utils
