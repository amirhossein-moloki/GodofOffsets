#pragma once
#include <vector>
#include <cstddef>
#include <cstdlib>
#include <algorithm>
#include <memory>

namespace Utils {

class ArenaAllocator {
public:
    ArenaAllocator(size_t blockSize = 1024 * 1024) : m_blockSize(blockSize), m_currentBlock(nullptr), m_currentOffset(0) {}

    ~ArenaAllocator() {
        for (void* block : m_blocks) std::free(block);
        for (void* block : m_largeBlocks) std::free(block);
    }

    void* Allocate(size_t size, size_t alignment = alignof(std::max_align_t)) {
        if (size > m_blockSize / 2) {
            // For large blocks, we rely on malloc's alignment (usually max_align_t)
            void* block = std::malloc(size);
            m_largeBlocks.push_back(block);
            return block;
        }

        if (m_currentBlock) {
            void* ptr = (char*)m_currentBlock + m_currentOffset;
            size_t space = m_blockSize - m_currentOffset;
            if (std::align(alignment, size, ptr, space)) {
                m_currentOffset = (char*)ptr - (char*)m_currentBlock + size;
                return ptr;
            }
        }

        // Need new block
        m_currentBlock = std::malloc(m_blockSize);
        m_blocks.push_back(m_currentBlock);

        void* ptr = m_currentBlock;
        size_t space = m_blockSize;
        if (std::align(alignment, size, ptr, space)) {
            m_currentOffset = (char*)ptr - (char*)m_currentBlock + size;
            return ptr;
        }

        return nullptr; // Should not happen with reasonable alignment/size
    }

    template<typename T, typename... Args>
    T* New(Args&&... args) {
        void* ptr = Allocate(sizeof(T), alignof(T));
        return new (ptr) T(std::forward<Args>(args)...);
    }

    void Reset() {
        for (void* block : m_blocks) std::free(block);
        for (void* block : m_largeBlocks) std::free(block);
        m_blocks.clear();
        m_largeBlocks.clear();
        m_currentBlock = nullptr;
        m_currentOffset = 0;
    }

private:
    size_t m_blockSize;
    std::vector<void*> m_blocks;
    std::vector<void*> m_largeBlocks;
    void* m_currentBlock;
    size_t m_currentOffset;
};

} // namespace Utils
