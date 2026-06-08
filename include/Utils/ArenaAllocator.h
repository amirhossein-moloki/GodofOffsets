#pragma once
#include <vector>
#include <cstddef>
#include <memory>
#include <algorithm>

namespace Utils {

/**
 * @brief High-performance Arena Allocator for fast, bulk memory allocations.
 *
 * Provides O(1) allocation by utilizing pre-allocated memory blocks.
 * Memory is only freed when the Arena is destroyed or explicitly reset.
 */
class ArenaAllocator {
public:
    explicit ArenaAllocator(size_t blockSize = 1024 * 1024) : m_blockSize(blockSize) {
        AddNewBlock(m_blockSize);
    }

    // Prevent copying
    ArenaAllocator(const ArenaAllocator&) = delete;
    ArenaAllocator& operator=(const ArenaAllocator&) = delete;

    // Allow moving
    ArenaAllocator(ArenaAllocator&& other) noexcept
        : m_blocks(std::move(other.m_blocks)),
          m_blockSize(other.m_blockSize),
          m_currentBlockIdx(other.m_currentBlockIdx),
          m_offset(other.m_offset) {
        other.m_currentBlockIdx = 0;
        other.m_offset = 0;
    }

    ArenaAllocator& operator=(ArenaAllocator&& other) noexcept {
        if (this != &other) {
            m_blocks = std::move(other.m_blocks);
            m_blockSize = other.m_blockSize;
            m_currentBlockIdx = other.m_currentBlockIdx;
            m_offset = other.m_offset;
            other.m_currentBlockIdx = 0;
            other.m_offset = 0;
        }
        return *this;
    }

    void* Allocate(size_t size, size_t alignment = alignof(std::max_align_t)) {
        size_t currentAddr = reinterpret_cast<size_t>(m_blocks[m_currentBlockIdx].get() + m_offset);
        size_t padding = (alignment - (currentAddr % alignment)) % alignment;

        if (m_offset + padding + size > m_blockSize) {
            AddNewBlock((std::max)(m_blockSize, size + alignment));
            m_currentBlockIdx = m_blocks.size() - 1;
            m_offset = 0;
            currentAddr = reinterpret_cast<size_t>(m_blocks[m_currentBlockIdx].get());
            padding = (alignment - (currentAddr % alignment)) % alignment;
        }

        void* ptr = m_blocks[m_currentBlockIdx].get() + m_offset + padding;
        m_offset += padding + size;
        return ptr;
    }

    template<typename T, typename... Args>
    T* Create(Args&&... args) {
        void* ptr = Allocate(sizeof(T), alignof(T));
        return new (ptr) T(std::forward<Args>(args)...);
    }

    void Reset() {
        if (m_blocks.size() > 1) {
            auto firstBlock = std::move(m_blocks[0]);
            m_blocks.clear();
            m_blocks.push_back(std::move(firstBlock));
        }
        m_currentBlockIdx = 0;
        m_offset = 0;
    }

private:
    void AddNewBlock(size_t size) {
        m_blocks.push_back(std::make_unique<uint8_t[]>(size));
    }

    std::vector<std::unique_ptr<uint8_t[]>> m_blocks;
    size_t m_blockSize;
    size_t m_currentBlockIdx = 0;
    size_t m_offset = 0;
};

} // namespace Utils
