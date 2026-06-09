#pragma once
#include <vector>
#include <cstddef>
#include <memory>
#include <algorithm>

namespace Utils {

/**
 * @brief A high-performance Arena Allocator for fast, bulk memory allocations.
 * @details This allocator is ideal for scenarios where many small objects are allocated
 * and then all freed at once, such as during a memory scan.
 *
 * تخصیص‌دهنده حافظه Arena با کارایی بالا برای تخصیص‌های حجیم و سریع.
 * این ابزار برای سناریوهایی که اشیاء کوچک زیادی تخصیص داده شده و سپس همه با هم آزاد می‌شوند، مناسب است.
 */
class ArenaAllocator {
public:
    explicit ArenaAllocator(size_t blockSize = 1024 * 1024) : m_blockSize(blockSize) {
        m_currentBlock = nullptr;
        m_currentPos = 0;
        m_remaining = 0;
    }

    ~ArenaAllocator() {
        Reset();
    }

    // Prevent copying
    ArenaAllocator(const ArenaAllocator&) = delete;
    ArenaAllocator& operator=(const ArenaAllocator&) = delete;

    /**
     * @brief Allocates a block of memory of the specified size.
     * @param size The number of bytes to allocate.
     * @return A pointer to the allocated memory.
     */
    void* Allocate(size_t size) {
        // Align to 8 bytes
        size = (size + 7) & ~7;

        if (size > m_remaining) {
            AllocateNewBlock(size);
        }

        void* ptr = m_currentBlock + m_currentPos;
        m_currentPos += size;
        m_remaining -= size;
        return ptr;
    }

    /**
     * @brief Frees all memory managed by the arena.
     */
    void Reset() {
        for (auto block : m_blocks) {
            delete[] block;
        }
        m_blocks.clear();
        m_currentBlock = nullptr;
        m_currentPos = 0;
        m_remaining = 0;
    }

private:
    void AllocateNewBlock(size_t minSize) {
        size_t sizeToAllocate = (std::max)(m_blockSize, minSize);
        uint8_t* newBlock = new uint8_t[sizeToAllocate];
        m_blocks.push_back(newBlock);
        m_currentBlock = newBlock;
        m_currentPos = 0;
        m_remaining = sizeToAllocate;
    }

    size_t m_blockSize;
    std::vector<uint8_t*> m_blocks;
    uint8_t* m_currentBlock;
    size_t m_currentPos;
    size_t m_remaining;
};

} // namespace Utils
