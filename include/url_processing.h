#pragma once

#include <string_view>
#include <utility>
#include <thread>

constexpr std::string_view QUERY_PARAM = "?q=";
constexpr std::string_view DEFAULT_SEARCH_URL = "https://www.google.com/search?q=";

// 16-byte aligned buffer allocation for SIMD operations
inline void *alignedAlloc(size_t size, size_t alignment = 16) {
    void *ptr = nullptr;
#ifdef _MSC_VER
    ptr = _aligned_malloc(size, alignment);
#else
    if (posix_memalign(&ptr, alignment, size) != 0) {
        ptr = nullptr;
    }
#endif
    return ptr;
}

inline void alignedFree(void *ptr) {
#ifdef _MSC_VER
    _aligned_free(ptr);
#else
    free(ptr);
#endif
}

struct alignas(64) AlignedBuffer {
    char *buffer;
    size_t size;

    explicit AlignedBuffer(const size_t bufSize = 4096) : size(bufSize) {
        buffer = static_cast<char *>(alignedAlloc(bufSize, 64));
    }

    ~AlignedBuffer() {
        if (buffer) {
            alignedFree(buffer);
            buffer = nullptr;
        }
    }

    AlignedBuffer(const AlignedBuffer &) = delete;

    AlignedBuffer &operator=(const AlignedBuffer &) = delete;

    AlignedBuffer(AlignedBuffer &&other) noexcept : buffer(other.buffer), size(other.size) {
        other.buffer = nullptr;
        other.size = 0;
    }

    AlignedBuffer &operator=(AlignedBuffer &&other) noexcept {
        if (this != &other) {
            if (buffer) alignedFree(buffer);
            buffer = other.buffer;
            size = other.size;
            other.buffer = nullptr;
            other.size = 0;
        }
        return *this;
    }
};

class BufferPool {
public:
    static AlignedBuffer &getDecodeBuffer() {
        static thread_local AlignedBuffer decodeBuffer(4096);
        return decodeBuffer;
    }

    static AlignedBuffer &getEncodeBuffer() {
        static thread_local AlignedBuffer encodeBuffer(4096);
        return encodeBuffer;
    }

    static AlignedBuffer &getTempBuffer() {
        static thread_local AlignedBuffer tempBuffer(4096);
        return tempBuffer;
    }

    static AlignedBuffer &getResponseBuffer() {
        static thread_local AlignedBuffer responseBuffer(8192);
        return responseBuffer;
    }
};

struct alignas(64) HexTables {
    constexpr HexTables() : hexChars{}, perfectHexMap{} {
        for (int i = 0; i < 16; i++) {
            constexpr auto hex = "0123456789ABCDEF";
            hexChars[i] = hex[i];
        }

        // Perfect hash for hex characters - uses ASCII value & 0x1F for 0-9, A-F, a-f
        // '0'-'9' & 0x1F = 0-9
        // 'A'-'F' & 0x1F = 1-6
        // 'a'-'f' & 0x1F = 1-6
        for (unsigned char &i: perfectHexMap) {
            i = 255; // Invalid marker
        }

        // Map digits 0-9
        for (int i = '0'; i <= '9'; i++) {
            perfectHexMap[i & 0x1F] = i - '0';
        }

        // Map A-F (will overwrite 1-6 from digits, which is fine for our purposes)
        for (int i = 'A'; i <= 'F'; i++) {
            perfectHexMap[i & 0x1F] = i - 'A' + 10;
        }

        // Map a-f (identical index pattern to A-F, which is fine since we want same value)
        for (int i = 'a'; i <= 'f'; i++) {
            perfectHexMap[i & 0x1F] = i - 'a' + 10;
        }
    }

    alignas(64) char hexChars[16]; // Hex encoding characters
    alignas(64) unsigned char perfectHexMap[32]; // Perfect hash map for hex decoding
};

struct alignas(64) SafeChars {
    constexpr SafeChars() : safe{}, safeMap{} {
        // Fill traditional safe character array
        for (int i = 'a'; i <= 'z'; i++) safe[i] = true;
        for (int i = 'A'; i <= 'Z'; i++) safe[i] = true;
        for (int i = '0'; i <= '9'; i++) safe[i] = true;
        safe[static_cast<int>('-')] = true;
        safe[static_cast<int>('_')] = true;
        safe[static_cast<int>('.')] = true;
        safe[static_cast<int>('~')] = true;
        safe[static_cast<int>('!')] = true;

        for (unsigned char &i: safeMap) {
            i = 0; // Default to unsafe
        }

        // Letters a-z: bits 0-25
        for (int i = 'a'; i <= 'z'; i++) {
            safeMap[i & 0x7F] |= 1;
        }

        // Letters A-Z: bit 1
        for (int i = 'A'; i <= 'Z'; i++) {
            safeMap[i & 0x7F] |= 2;
        }

        // Digits 0-9: bit 2
        for (int i = '0'; i <= '9'; i++) {
            safeMap[i & 0x7F] |= 4;
        }

        // Special chars: bit 3
        for (constexpr char special[] = "-_.~!"; const char c: special) {
            safeMap[static_cast<unsigned char>(c) & 0x7F] |= 8;
        }
    }

    alignas(64) bool safe[256];
    alignas(64) unsigned char safeMap[128];
};

inline const HexTables &getHexTables() {
    static thread_local constexpr HexTables hexTables{};
    return hexTables;
}

inline const SafeChars &getSafeChars() {
    static thread_local constexpr SafeChars safeChars{};
    return safeChars;
}

size_t urlDecode(std::string_view str, char *buffer);

size_t urlEncode(std::string_view str, char *buffer);

std::pair<std::string_view, std::string_view> processQuery(std::string_view url, char *decode_buffer = nullptr,
                                                           char *encode_buffer = nullptr);
