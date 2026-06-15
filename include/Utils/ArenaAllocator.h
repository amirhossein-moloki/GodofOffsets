#pragma once
#include <vector>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <algorithm>

namespace Utils {

class ArenaAllocator {
public:
    ArenaAllocator(size_t blockSize = 1024 * 1024) : m_blockSize(blockSize), m_currentPos(0) {
        AddNewBlock();
    }

    ~ArenaAllocator() {
        for (void* block : m_blocks) std::free(block);
        for (void* block : m_largeBlocks) std::free(block);
    }

    // Prevent copying
    ArenaAllocator(const ArenaAllocator&) = delete;
    ArenaAllocator& operator=(const ArenaAllocator&) = delete;

    void* Allocate(size_t size, size_t alignment = sizeof(void*)) {
        size_t padding = (alignment - (m_currentPos % alignment)) % alignment;

        if (m_currentPos + padding + size > m_blockSize) {
            if (size > m_blockSize) {
                void* largeBlock = std::malloc(size);
                if (!largeBlock) return nullptr;
                m_largeBlocks.push_back(largeBlock);
                return largeBlock;
            }
            AddNewBlock();
            padding = 0;
        }

        m_currentPos += padding;
        void* ptr = static_cast<uint8_t*>(m_blocks.back()) + m_currentPos;
        m_currentPos += size;
        return ptr;
    }

    template<typename T, typename... Args>
    T* Create(Args&&... args) {
        void* ptr = Allocate(sizeof(T), alignof(T));
        if (!ptr) return nullptr;
        return new (ptr) T(std::forward<Args>(args)...);
    }

    void Reset() {
        // Free large blocks
        for (void* block : m_largeBlocks) std::free(block);
        m_largeBlocks.clear();

        // Keep only one standard block
        if (m_blocks.size() > 1) {
            for (size_t i = 1; i < m_blocks.size(); ++i) std::free(m_blocks[i]);
            m_blocks.resize(1);
        }
        m_currentPos = 0;
    }

private:
    void AddNewBlock() {
        void* block = std::malloc(m_blockSize);
        if (!block) throw std::bad_alloc();
        m_blocks.push_back(block);
        m_currentPos = 0;
    }

    std::vector<void*> m_blocks;
    std::vector<void*> m_largeBlocks;
    size_t m_blockSize;
    size_t m_currentPos;
};

} // namespace Utils
