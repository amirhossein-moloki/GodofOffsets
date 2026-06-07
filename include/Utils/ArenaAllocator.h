#pragma once
#include <vector>
#include <cstddef>
#include <memory>

namespace Utils {

/**
 * @brief A high-performance Arena Allocator for large-scale memory management.
 *
 * Provides O(1) allocation and minimizes fragmentation by allocating memory in large blocks.
 * Memory is only freed when the arena is destroyed or explicitly reset.
 */
class ArenaAllocator {
public:
    explicit ArenaAllocator(size_t blockSize = 1024 * 1024) : m_blockSize(blockSize) {
        AllocateBlock();
    }

    ~ArenaAllocator() {
        for (auto block : m_blocks) {
            delete[] block;
        }
    }

    // Prevent copying
    ArenaAllocator(const ArenaAllocator&) = delete;
    ArenaAllocator& operator=(const ArenaAllocator&) = delete;

    /**
     * @brief Allocates a block of memory of the specified size.
     */
    void* Allocate(size_t size) {
        // Align size to 8 bytes
        size = (size + 7) & ~7;

        if (m_currentOffset + size > m_blockSize) {
            if (size > m_blockSize) {
                // Large allocation - give it its own block
                uint8_t* block = new uint8_t[size];
                m_blocks.push_back(block);
                return block;
            }
            AllocateBlock();
        }

        void* ptr = m_blocks.back() + m_currentOffset;
        m_currentOffset += size;
        return ptr;
    }

    /**
     * @brief Resets the allocator, invalidating all previously allocated memory.
     */
    void Reset() {
        for (size_t i = 1; i < m_blocks.size(); ++i) {
            delete[] m_blocks[i];
        }
        if (m_blocks.size() > 1) {
            m_blocks.erase(m_blocks.begin() + 1, m_blocks.end());
        }
        m_currentOffset = 0;
    }

private:
    void AllocateBlock() {
        m_blocks.push_back(new uint8_t[m_blockSize]);
        m_currentOffset = 0;
    }

    size_t m_blockSize;
    size_t m_currentOffset = 0;
    std::vector<uint8_t*> m_blocks;
};

} // namespace Utils
