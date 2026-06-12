#pragma once
#include <vector>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <algorithm>

namespace Utils {

/**
 * @brief A high-performance Arena Allocator for fast, bulk memory allocations.
 *
 * This allocator is ideal for scenarios where many small objects are allocated
 * and can be freed all at once.
 */
class ArenaAllocator {
public:
    explicit ArenaAllocator(size_t blockSize = 1024 * 1024) : m_blockSize(blockSize) {
        AllocateBlock();
    }

    ~ArenaAllocator() {
        for (void* block : m_blocks) {
            std::free(block);
        }
    }

    // No copying
    ArenaAllocator(const ArenaAllocator&) = delete;
    ArenaAllocator& operator=(const ArenaAllocator&) = delete;

    /**
     * @brief Allocates raw memory of the given size and alignment.
     */
    void* Allocate(size_t size, size_t alignment = sizeof(void*)) {
        size_t padding = 0;
        uintptr_t currentAddr = reinterpret_cast<uintptr_t>(m_currentPos);
        if (currentAddr % alignment != 0) {
            padding = alignment - (currentAddr % alignment);
        }

        if (m_currentPos + padding + size > m_currentEnd) {
            if (size > m_blockSize) {
                // Large allocation that exceeds block size
                void* largeBlock = std::malloc(size + alignment);
                m_blocks.push_back(largeBlock);
                uintptr_t addr = reinterpret_cast<uintptr_t>(largeBlock);
                size_t largePadding = 0;
                if (addr % alignment != 0) {
                    largePadding = alignment - (addr % alignment);
                }
                return reinterpret_cast<void*>(addr + largePadding);
            }
            AllocateBlock();
            currentAddr = reinterpret_cast<uintptr_t>(m_currentPos);
            if (currentAddr % alignment != 0) {
                padding = alignment - (currentAddr % alignment);
            } else {
                padding = 0;
            }
        }

        void* result = m_currentPos + padding;
        m_currentPos += padding + size;
        return result;
    }

    /**
     * @brief Allocates and constructs an object of type T.
     */
    template<typename T, typename... Args>
    T* Alloc(Args&&... args) {
        void* mem = Allocate(sizeof(T), alignof(T));
        return new (mem) T(std::forward<Args>(args)...);
    }

    /**
     * @brief Resets the arena, invalidating all previously allocated memory.
     */
    void Reset() {
        if (m_blocks.empty()) return;

        void* firstBlock = m_blocks[0];
        for (size_t i = 1; i < m_blocks.size(); ++i) {
            std::free(m_blocks[i]);
        }

        m_blocks.clear();
        m_blocks.push_back(firstBlock);
        m_currentPos = static_cast<uint8_t*>(firstBlock);
        m_currentEnd = m_currentPos + m_blockSize;
    }

    /**
     * @brief Returns the total memory allocated by the arena in bytes.
     */
    size_t TotalAllocated() const {
        return m_blocks.size() * m_blockSize;
    }

private:
    void AllocateBlock() {
        void* block = std::malloc(m_blockSize);
        m_blocks.push_back(block);
        m_currentPos = static_cast<uint8_t*>(block);
        m_currentEnd = m_currentPos + m_blockSize;
    }

    size_t m_blockSize;
    std::vector<void*> m_blocks;
    uint8_t* m_currentPos = nullptr;
    uint8_t* m_currentEnd = nullptr;
};

} // namespace Utils
