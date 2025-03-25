#include "../include/url_processing.h"
#include "../include/bang.h"

#ifdef __x86_64__
#include <immintrin.h>
#endif

size_t urlDecode(const std::string_view str, char *buffer) {
    char *outputBuffer = buffer;
    std::unique_ptr<AlignedBuffer> tempBuffer;
    if (!outputBuffer) {
        const auto &buf = BufferPool::getDecodeBuffer();
        outputBuffer = buf.buffer;
    }

    size_t dest = 0;
    const size_t len = str.length();
    const char *src = str.data();
    const char *end = src + len;

    const auto &hexTables = getHexTables();

#ifdef __x86_64__
    if (len >= 16) {
        const __m128i plus_mask = _mm_set1_epi8('+');
        const __m128i percent_mask = _mm_set1_epi8('%');

        while (src + 16 <= end) {
            const __m128i chunk = (reinterpret_cast<uintptr_t>(src) & 0xF) == 0
                                      ? _mm_load_si128(reinterpret_cast<const __m128i *>(src))
                                      : _mm_loadu_si128(reinterpret_cast<const __m128i *>(src));

            const __m128i is_plus = _mm_cmpeq_epi8(chunk, plus_mask);
            const __m128i is_percent = _mm_cmpeq_epi8(chunk, percent_mask);
            const __m128i special_chars = _mm_or_si128(is_plus, is_percent);

            if (_mm_testz_si128(special_chars, special_chars)) {
                if ((reinterpret_cast<uintptr_t>(outputBuffer + dest) & 0xF) == 0) {
                    _mm_store_si128(reinterpret_cast<__m128i *>(outputBuffer + dest), chunk);
                } else {
                    _mm_storeu_si128(reinterpret_cast<__m128i *>(outputBuffer + dest), chunk);
                }
                src += 16;
                dest += 16;
                continue;
            }

            // Fall back to scalar path for this chunk
            break;
        }
    }
#endif

    while (src < end) {
        if (*src == '%' && src + 2 < end) {
            const auto high = static_cast<unsigned char>(*(src + 1));
            const auto low = static_cast<unsigned char>(*(src + 2));

            // character & 0x1F gives unique indices for hex chars
            const unsigned char highVal = hexTables.perfectHexMap[high & 0x1F];
            const unsigned char lowVal = hexTables.perfectHexMap[low & 0x1F];

            // Validate if the hex characters are valid (255 is our invalid marker)
            if (highVal != 255 && lowVal != 255) {
                outputBuffer[dest++] = static_cast<char>((highVal << 4) | lowVal);
                src += 3;
            } else {
                // Fallback if not valid hex chars
                outputBuffer[dest++] = *src++;
            }
        } else if (*src == '+') {
            outputBuffer[dest++] = ' ';
            src++;
        } else {
            outputBuffer[dest++] = *src++;
        }
    }

    outputBuffer[dest] = '\0';
    return dest;
}

size_t urlEncode(const std::string_view str, char *buffer) {
    char *outputBuffer = buffer;
    std::unique_ptr<AlignedBuffer> tempBuffer;
    if (!outputBuffer) {
        const auto &buf = BufferPool::getEncodeBuffer();
        outputBuffer = buf.buffer;
    }

    const auto &hexTables = getHexTables();
    const auto &safeChars = getSafeChars();

    size_t dest = 0;
    const size_t len = str.length();
    auto src = reinterpret_cast<const unsigned char *>(str.data());
    const unsigned char *end = src + len;

#ifdef __x86_64__
    if (len >= 16) {
        while (src + 16 <= end) {
            const __m128i chunk = (reinterpret_cast<uintptr_t>(src) & 0xF) == 0
                                      ? _mm_load_si128(reinterpret_cast<const __m128i *>(src))
                                      : _mm_loadu_si128(reinterpret_cast<const __m128i *>(src));

            const __m128i below_A = _mm_cmplt_epi8(chunk, _mm_set1_epi8('A'));
            const __m128i above_z = _mm_cmpgt_epi8(chunk, _mm_set1_epi8('z'));
            const __m128i outside_AZ = _mm_or_si128(below_A, above_z);

            bool all_safe = true;

            if (const uint16_t mask = _mm_movemask_epi8(outside_AZ); mask != 0) {
                for (int i = 0; i < 16; i++) {
                    if (mask & (1 << i)) {
                        if (const unsigned char c = src[i]; c != ' ') {
                            // Only check safeMap for ASCII range
                            if (c < 128) {
                                // Bits: 0=a-z, 1=A-Z, 2=0-9, 3=special chars
                                if (const unsigned char catMask = safeChars.safeMap[c & 0x7F]; catMask == 0) {
                                    // Not in any safe category
                                    all_safe = false;
                                    break;
                                }
                            } else {
                                // Outside ASCII range, definitely not safe
                                all_safe = false;
                                break;
                            }
                        }
                    }
                }
            }

            if (all_safe) {
                if ((reinterpret_cast<uintptr_t>(outputBuffer + dest) & 0xF) == 0 &&
                    !_mm_movemask_epi8(_mm_cmpeq_epi8(chunk, _mm_set1_epi8(' ')))) {
                    _mm_store_si128(reinterpret_cast<__m128i *>(outputBuffer + dest), chunk);
                    dest += 16;
                } else {
                    // Handle spaces by writing byte by byte
                    for (int i = 0; i < 16; i++) {
                        if (src[i] == ' ') {
                            outputBuffer[dest++] = '+';
                        } else {
                            outputBuffer[dest++] = static_cast<char>(src[i]);
                        }
                    }
                }
                src += 16;
                continue;
            }

            // Fall back to scalar path
            break;
        }
    }
#endif

    while (src < end) {
        if (const unsigned char c = *src++; c < 128) {
            const unsigned char catMask = safeChars.safeMap[c & 0x7F];

            if (c == ' ') {
                outputBuffer[dest++] = '+';
            } else if (catMask != 0) {
                outputBuffer[dest++] = static_cast<char>(c);
            } else {
                // Need to encode
                outputBuffer[dest++] = '%';
                outputBuffer[dest++] = hexTables.hexChars[c >> 4];
                outputBuffer[dest++] = hexTables.hexChars[c & 15];
            }
        } else {
            // Non-ASCII characters must be encoded
            outputBuffer[dest++] = '%';
            outputBuffer[dest++] = hexTables.hexChars[c >> 4];
            outputBuffer[dest++] = hexTables.hexChars[c & 15];
        }
    }

    outputBuffer[dest] = '\0';
    return dest;
}

size_t findBangPositions(const char *buffer, const size_t length, size_t *positions, const size_t maxPositions) {
    size_t count = 0;

    const char *ptr = buffer;
    const char *end = buffer + length;

#ifdef __x86_64__
    if (length >= 16) {
        const __m128i bang_mask = _mm_set1_epi8('!');

        while (ptr + 16 <= end && count < maxPositions) {
            const __m128i chunk = (reinterpret_cast<uintptr_t>(ptr) & 0xF) == 0
                                      ? _mm_load_si128(reinterpret_cast<const __m128i *>(ptr))
                                      : _mm_loadu_si128(reinterpret_cast<const __m128i *>(ptr));

            const __m128i matches = _mm_cmpeq_epi8(chunk, bang_mask);

            if (const uint16_t mask = _mm_movemask_epi8(matches); mask != 0) {
                for (int i = 0; i < 16 && count < maxPositions; ++i) {
                    if (mask & (1 << i)) {
                        positions[count++] = ptr - buffer + i;
                    }
                }
            }

            ptr += 16;
        }
    }
#endif

    while (ptr < end && count < maxPositions) {
        if (*ptr == '!') {
            positions[count++] = ptr - buffer;
        }
        ++ptr;
    }

    return count;
}

struct BangMatch {
    std::string_view bangCmd;
    size_t position;
    size_t length;

    BangMatch() : position(0), length(0) {
    }

    BangMatch(const std::string_view cmd, const size_t pos, const size_t len)
        : bangCmd(cmd), position(pos), length(len) {
    }

    bool operator<(const BangMatch &other) const {
        return length > other.length;
    }
};

std::pair<std::string_view, std::string_view> processQuery(
    const std::string_view url, char *decode_buffer, char *encode_buffer) {
    char *decodeOutputBuffer = decode_buffer;
    char *encodeOutputBuffer = encode_buffer;

    if (!decodeOutputBuffer) {
        const auto &buf = BufferPool::getDecodeBuffer();
        decodeOutputBuffer = buf.buffer;
    }

    if (!encodeOutputBuffer) {
        const auto &buf = BufferPool::getEncodeBuffer();
        encodeOutputBuffer = buf.buffer;
    }

    const char *url_data = url.data();
    const size_t url_size = url.size();
    constexpr size_t query_param_size = QUERY_PARAM.size();

    const auto q_pos = static_cast<const char *>(memchr(url_data, '?', url_size));
    if (!q_pos || q_pos + query_param_size > url_data + url_size) {
        return {DEFAULT_SEARCH_URL, std::string_view()};
    }

    if (q_pos[1] != 'q' || q_pos[2] != '=') {
        return {DEFAULT_SEARCH_URL, std::string_view()};
    }

    // Calculate position directly from pointers
    const size_t pos = q_pos - url_data;

    const auto queryStart = pos + query_param_size;

    const auto query_end_ptr = static_cast<const char *>(memchr(url_data + queryStart, ' ', url_size - queryStart));
    const size_t encodedQueryLen = query_end_ptr ? query_end_ptr - (url_data + queryStart) : url_size - queryStart;

    const std::string_view encodedQuery(url_data + queryStart, encodedQueryLen);
    const size_t rawQueryLen = urlDecode(encodedQuery, decodeOutputBuffer);

    if (rawQueryLen == 0) {
        const size_t encodedLen = urlEncode(std::string_view(decodeOutputBuffer, rawQueryLen), encodeOutputBuffer);
        return {DEFAULT_SEARCH_URL, std::string_view(encodeOutputBuffer, encodedLen)};
    }

    if (decodeOutputBuffer[0] == '!') {
        const auto space_pos = static_cast<const char *>(memchr(decodeOutputBuffer, ' ', rawQueryLen));

        if (const size_t bangEnd = space_pos ? space_pos - decodeOutputBuffer : rawQueryLen; bangEnd >= 2) {
            const std::string_view bangCmd(decodeOutputBuffer, bangEnd);
            if (const auto it = BANG_URLS.find(bangCmd); it != BANG_URLS.end()) {
                std::string_view searchUrl = it->second;

                if (space_pos && bangEnd < rawQueryLen) {
                    const std::string_view cleanQuery(decodeOutputBuffer + bangEnd + 1, rawQueryLen - bangEnd - 1);
                    const size_t encodedLen = urlEncode(cleanQuery, encodeOutputBuffer);
                    return {searchUrl, std::string_view(encodeOutputBuffer, encodedLen)};
                }

                // No text after bang
                return {searchUrl, std::string_view()};
            }
        }
    }

    if (memchr(decodeOutputBuffer + 1, '!', rawQueryLen - 1) == nullptr) {
        // No '!' found except possibly at position 0, which we already checked
        const size_t encodedLen = urlEncode(std::string_view(decodeOutputBuffer, rawQueryLen), encodeOutputBuffer);
        return {DEFAULT_SEARCH_URL, std::string_view(encodeOutputBuffer, encodedLen)};
    }

    // Look for the first valid bang command anywhere in the query
    BangMatch bestMatch;
    const char *ptr = decodeOutputBuffer + 1; // Start after position 0 (which we already checked)
    const char *end = decodeOutputBuffer + rawQueryLen;

    while (ptr < end) {
        ptr = static_cast<const char *>(memchr(ptr, '!', end - ptr));
        if (!ptr) break;

        if (ptr + 1 >= end) break;

        const auto space_pos = static_cast<const char *>(memchr(ptr, ' ', end - ptr));
        const size_t bangEndPos = space_pos ? space_pos - ptr : end - ptr;

        if (bangEndPos < 2) {
            ptr++;
            continue;
        }

        const std::string_view bangCmd(ptr, bangEndPos);
        if (const auto it = BANG_URLS.find(bangCmd); it != BANG_URLS.end()) {
            bestMatch = BangMatch(bangCmd, ptr - decodeOutputBuffer, bangEndPos);
            break;
        }

        ptr++;
    }

    // If no valid bangs found, use default search
    if (bestMatch.length == 0) {
        const size_t encodedLen = urlEncode(std::string_view(decodeOutputBuffer, rawQueryLen), encodeOutputBuffer);
        return {DEFAULT_SEARCH_URL, std::string_view(encodeOutputBuffer, encodedLen)};
    }
    std::string_view searchUrl = BANG_URLS.find(bestMatch.bangCmd)->second;

    const auto &tempBuf = BufferPool::getTempBuffer();
    char *queryBuffer = tempBuf.buffer;
    size_t stitchedQueryLen = 0;

    if (bestMatch.position > 0) {
        const size_t prefixLen = bestMatch.position;
        memcpy(queryBuffer, decodeOutputBuffer, prefixLen);
        stitchedQueryLen += prefixLen;

        if (stitchedQueryLen > 0 && bestMatch.position + bestMatch.length < rawQueryLen) {
            queryBuffer[stitchedQueryLen++] = ' ';
        }
    }

    if (bestMatch.position + bestMatch.length < rawQueryLen) {
        const size_t suffixStart = bestMatch.position + bestMatch.length;
        const size_t suffixLen = rawQueryLen - suffixStart;

        size_t actualStart = suffixStart;
        size_t actualLen = suffixLen;
        if (decodeOutputBuffer[suffixStart] == ' ') {
            actualStart++;
            actualLen--;
        }

        if (actualLen > 0) {
            memcpy(queryBuffer + stitchedQueryLen, decodeOutputBuffer + actualStart, actualLen);
            stitchedQueryLen += actualLen;
        }
    }

    if (stitchedQueryLen == 0) {
        return {searchUrl, std::string_view()};
    }

    const size_t encodedLen = urlEncode(std::string_view(queryBuffer, stitchedQueryLen), encodeOutputBuffer);
    return {searchUrl, std::string_view(encodeOutputBuffer, encodedLen)};
}

std::string_view createRedirectResponse(const std::string_view searchUrl,
                                        const std::string_view encodedQuery,
                                        char *buffer) {
    char *responseBuffer = buffer;
    if (!responseBuffer) {
        const auto &buf = BufferPool::getResponseBuffer();
        responseBuffer = buf.buffer;
    }

    char *ptr = responseBuffer;

    constexpr std::string_view header = "HTTP/1.1 302 Found\r\nLocation: ";
    constexpr std::string_view footer = "\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
    constexpr std::string_view placeholder = "{{{s}}}";

    constexpr size_t headerSize = header.size();

#ifdef __x86_64__
    if ((reinterpret_cast<uintptr_t>(ptr) & 0xF) == 0) {
        size_t copied = 0;
        while (copied + 16 <= headerSize) {
            _mm_store_si128(
                reinterpret_cast<__m128i *>(ptr + copied),
                _mm_loadu_si128(reinterpret_cast<const __m128i *>(header.data() + copied))
            );
            copied += 16;
        }

        // Copy remaining bytes
        if (copied < headerSize) {
            memcpy(ptr + copied, header.data() + copied, headerSize - copied);
        }
    } else {
        memcpy(ptr, header.data(), headerSize);
    }
#else
    memcpy(ptr, header.data(), headerSize);
#endif
    ptr += headerSize;

    const auto placeholderPos = static_cast<const char *>(memmem(searchUrl.data(), searchUrl.size(),
                                                                 placeholder.data(), placeholder.size()));

    if (placeholderPos) {
        const size_t beforeLen = placeholderPos - searchUrl.data();
        memcpy(ptr, searchUrl.data(), beforeLen);
        ptr += beforeLen;
        memcpy(ptr, encodedQuery.data(), encodedQuery.size());
        ptr += encodedQuery.size();
        const size_t afterLen = searchUrl.size() - beforeLen - placeholder.size();
        memcpy(ptr, placeholderPos + placeholder.size(), afterLen);
        ptr += afterLen;
    } else {
        memcpy(ptr, searchUrl.data(), searchUrl.size());
        ptr += searchUrl.size();

        memcpy(ptr, encodedQuery.data(), encodedQuery.size());
        ptr += encodedQuery.size();
    }

    memcpy(ptr, footer.data(), footer.size());
    ptr += footer.size();

    return {responseBuffer, static_cast<std::string_view::size_type>(ptr - responseBuffer)};
}
