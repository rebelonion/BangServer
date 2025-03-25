#pragma once

#include <cstddef>
#include <vector>
#include <mutex>

class alignas(64) MemoryPool {
public:
    explicit MemoryPool(const size_t bufferSize, const size_t initialCapacity = 64)
        : m_bufferSize(bufferSize), m_capacity(initialCapacity) {
        m_blocks.reserve(initialCapacity);
        for (size_t i = 0; i < initialCapacity; ++i) {
            m_blocks.push_back(new char[bufferSize]);
            m_freeList.push_back(i);
        }
    }

    ~MemoryPool() {
        for (const auto *block: m_blocks) {
            delete[] block;
        }
    }

    char *acquire() {
        std::lock_guard lock(m_mutex);
        if (m_freeList.empty()) {
            // Grow pool by 50% when out of buffers
            const size_t newBlocks = m_capacity / 2 + 1;
            const size_t oldSize = m_blocks.size();
            m_blocks.reserve(oldSize + newBlocks);
            m_freeList.reserve(newBlocks);

            for (size_t i = 0; i < newBlocks; ++i) {
                m_blocks.push_back(new char[m_bufferSize]);
                m_freeList.push_back(oldSize + i);
            }

            m_capacity += newBlocks;
        }

        const size_t index = m_freeList.back();
        m_freeList.pop_back();
        return m_blocks[index];
    }

    void release(const char *buffer) {
        if (!buffer) return;

        std::lock_guard lock(m_mutex);
        for (size_t i = 0; i < m_blocks.size(); ++i) {
            if (m_blocks[i] == buffer) {
                m_freeList.push_back(i);
                return;
            }
        }
    }

    [[nodiscard]] size_t bufferSize() const { return m_bufferSize; }

private:
    std::mutex m_mutex;
    std::vector<char *> m_blocks;
    std::vector<size_t> m_freeList;
    size_t m_bufferSize;
    size_t m_capacity;
};

// RAII wrapper
class PoolBuffer {
public:
    explicit PoolBuffer(MemoryPool &pool)
        : m_pool(pool), m_buffer(pool.acquire()) {
    }

    ~PoolBuffer() {
        m_pool.release(m_buffer);
    }

    // No copying
    PoolBuffer(const PoolBuffer &) = delete;

    PoolBuffer &operator=(const PoolBuffer &) = delete;

    // Moving is allowed
    PoolBuffer(PoolBuffer &&other) noexcept
        : m_pool(other.m_pool), m_buffer(other.m_buffer) {
        other.m_buffer = nullptr;
    }

    PoolBuffer &operator=(PoolBuffer &&other) noexcept {
        if (this != &other) {
            m_pool.release(m_buffer);
            m_buffer = other.m_buffer;
            other.m_buffer = nullptr;
        }
        return *this;
    }

    // Get the underlying buffer
    char *get() { return m_buffer; }
    [[nodiscard]] const char *get() const { return m_buffer; }

    // Conversion operator
    explicit operator char *() const { return m_buffer; }
    explicit operator const char *() const { return m_buffer; }

private:
    MemoryPool &m_pool;
    char *m_buffer;
};

inline MemoryPool &getRequestPool() {
    static MemoryPool requestPool(4096); // For request buffers
    return requestPool;
}

inline MemoryPool &getEncodePool() {
    static MemoryPool encodePool(12288); // For URL encoding (3x request size)
    return encodePool;
}

inline MemoryPool &getRedirectPool() {
    static MemoryPool redirectPool(4096); // For response buffers
    return redirectPool;
}
