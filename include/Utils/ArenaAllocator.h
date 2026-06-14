#pragma once
#include <vector>
#include <cstddef>
#include <cstdlib>

namespace Utils {

class ArenaAllocator {
public:
    ArenaAllocator(size_t blockSize = 1024 * 1024) : m_blockSize(blockSize) {
        void* firstBlock = std::malloc(m_blockSize);
        if (firstBlock) m_blocks.push_back(firstBlock);
        m_currentPos = 0;
    }

    ~ArenaAllocator() {
        for (void* block : m_blocks) {
            std::free(block);
        }
    }

    void* Allocate(size_t size, size_t alignment = alignof(std::max_align_t)) {
        if (m_blocks.empty()) return nullptr;

        size_t padding = (alignment - (m_currentPos % alignment)) % alignment;

        if (m_currentPos + padding + size > m_blockSize) {
            size_t nextBlockSize = m_blockSize > size ? m_blockSize : size;
            void* newBlock = std::malloc(nextBlockSize);
            if (!newBlock) return nullptr;

            m_blocks.push_back(newBlock);
            m_currentPos = 0;
            padding = 0;
        }

        void* ptr = static_cast<char*>(m_blocks.back()) + m_currentPos + padding;
        m_currentPos += padding + size;
        return ptr;
    }

    template<typename T, typename... Args>
    T* New(Args&&... args) {
        void* ptr = Allocate(sizeof(T), alignof(T));
        if (!ptr) return nullptr;
        return new (ptr) T(std::forward<Args>(args)...);
    }

    void Reset() {
        if (m_blocks.size() > 1) {
            for (size_t i = 1; i < m_blocks.size(); ++i) {
                std::free(m_blocks[i]);
            }
            m_blocks.resize(1);
        }
        m_currentPos = 0;
    }

private:
    std::vector<void*> m_blocks;
    size_t m_blockSize;
    size_t m_currentPos;
};

} // namespace Utils
