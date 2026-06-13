#pragma once
#include <vector>
#include <cstdint>
#include <cstdlib>

namespace Utils {

/**
 * @brief A high-performance Arena Allocator for bulk data allocations.
 * Provides O(1) allocation and linear memory layout.
 */
class ArenaAllocator {
public:
    ArenaAllocator(size_t blockSize = 1024 * 1024 * 16) // Default 16MB blocks
        : m_blockSize(blockSize), m_currentBlock(nullptr), m_currentOffset(0) {}

    ~ArenaAllocator() {
        for (void* block : m_blocks) {
            std::free(block);
        }
    }

    // Prevent copying
    ArenaAllocator(const ArenaAllocator&) = delete;
    ArenaAllocator& operator=(const ArenaAllocator&) = delete;

    /**
     * @brief Allocates memory of size T * count.
     */
    template<typename T>
    T* Allocate(size_t count = 1) {
        size_t size = sizeof(T) * count;

        // Ensure alignment (simplified to 8 bytes for common data types)
        size = (size + 7) & ~7;

        if (!m_currentBlock || m_currentOffset + size > m_blockSize) {
            AllocateNewBlock((std::max)(size, m_blockSize));
        }

        T* ptr = reinterpret_cast<T*>(reinterpret_cast<uint8_t*>(m_currentBlock) + m_currentOffset);
        m_currentOffset += size;
        return ptr;
    }

    void Reset() {
        if (m_blocks.empty()) return;

        // Keep the first block but free all others to prevent accumulation
        for (size_t i = 1; i < m_blocks.size(); ++i) {
            std::free(m_blocks[i]);
        }
        void* firstBlock = m_blocks[0];
        m_blocks.clear();
        m_blocks.push_back(firstBlock);

        m_currentBlock = firstBlock;
        m_currentOffset = 0;
    }

    size_t GetTotalAllocated() const {
        return m_blocks.size() * m_blockSize;
    }

private:
    void AllocateNewBlock(size_t size) {
        void* block = std::malloc(size);
        if (!block) throw std::bad_alloc();

        m_blocks.push_back(block);
        m_currentBlock = block;
        m_currentOffset = 0;
        // Update m_blockSize if we allocated a larger block than default
        if (size > m_blockSize) m_blockSize = size;
    }

    size_t m_blockSize;
    std::vector<void*> m_blocks;
    void* m_currentBlock;
    size_t m_currentOffset;
};

} // namespace Utils
