#include <stdexcept>
#include <cstring>
#include "ebcdic_converter.h"
#include "cp037_tables.h"
#include "cp273_tables.h"
#include "cp277_tables.h"
#include "cp285_tables.h"

// UTF-8 sequence length definitions
#define UTF8_SIZE1     1
#define UTF8_SIZE2     2
#define UTF8_SIZE3     3
#define UTF8_SIZE4     4
#define UTF8_CONTINUE  5
#define UTF8_ILLEGAL   6

// EBCDIC substitute character
#define EBCDIC_SUB (63)

// This will help decode UTF-8 sequences.
//
// 0xxxxxxx ==> 00000000-01111111 ==> 00-7F 1 byte sequence.
// 10xxxxxx ==> 10000000-10111111 ==> 80-BF continue
// 110xxxxx ==> 11000000-11011111 ==> C0-DF 2 byte sequence.
// 1110xxxx ==> 11100000-11101111 ==> E0-EF 3 byte sequence.
// 11110xxx ==> 11110000-11110111 ==> F0-F7 4 byte sequence.
//              11111000-11111111 illegal
//
// Also, RFC 3629 specifies that 0xC0, 0xC1, and 0xF5-0xFF never
// appear in a valid sequence.
//
// The first byte gives the length of a sequence (UTF8_SIZE1 - UTF8_SIZE4).
// Bytes in the middle of a sequence map to UTF8_CONTINUE.  Bytes which should
// not appear map to UTF8_ILLEGAL.
//
const unsigned char utf8_FirstByte[256] = {
//  0   1   2   3   4   5   6   7   8   9   A   B   C   D   E   F
//
    1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  // 0
    1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  // 1
    1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  // 2
    1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  // 3
    1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  // 4
    1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  // 5
    1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  // 6
    1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  // 7

    5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  // 8
    5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  // 9
    5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  // A
    5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  // B
    6,  6,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  // C
    2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  // D
    3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  // E
    4,  4,  4,  4,  4,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6   // F
};

// Registry initialization
EbcdicTableRegistry::EbcdicTableRegistry() {
    // Initialize tables for CP037
    ConversionTables cp037tables;
    cp037tables.utf8_FirstByte = utf8_FirstByte;
    cp037tables.tr_itt = tr_cp037_itt;
    cp037tables.tr_sot = tr_cp037_sot;
    cp037tables.tr_sbt = tr_cp037_sbt;
    cp037tables.toUtf8Lengths = cp037_to_utf8_lengths;
    cp037tables.toUtf8Bytes = cp037_to_utf8_bytes;
    tables_[EbcdicCodePage::CP037] = cp037tables;

    // Initialize tables for CP273
    ConversionTables cp273tables;
    cp273tables.utf8_FirstByte = utf8_FirstByte;
    cp273tables.tr_itt = tr_cp273_itt;
    cp273tables.tr_sot = tr_cp273_sot;
    cp273tables.tr_sbt = tr_cp273_sbt;
    cp273tables.toUtf8Lengths = cp273_to_utf8_lengths;
    cp273tables.toUtf8Bytes = cp273_to_utf8_bytes;
    tables_[EbcdicCodePage::CP273] = cp273tables;

    // Initialize tables for CP277
    ConversionTables cp277tables;
    cp277tables.utf8_FirstByte = utf8_FirstByte;
    cp277tables.tr_itt = tr_cp277_itt;
    cp277tables.tr_sot = tr_cp277_sot;
    cp277tables.tr_sbt = tr_cp277_sbt;
    cp277tables.toUtf8Lengths = cp277_to_utf8_lengths;
    cp277tables.toUtf8Bytes = cp277_to_utf8_bytes;
    tables_[EbcdicCodePage::CP277] = cp277tables;

    // Initialize tables for CP285
    ConversionTables cp285tables;
    cp285tables.utf8_FirstByte = utf8_FirstByte;
    cp285tables.tr_itt = tr_cp285_itt;
    cp285tables.tr_sot = tr_cp285_sot;
    cp285tables.tr_sbt = tr_cp285_sbt;
    cp285tables.toUtf8Lengths = cp285_to_utf8_lengths;
    cp285tables.toUtf8Bytes = cp285_to_utf8_bytes;
    tables_[EbcdicCodePage::CP285] = cp285tables;
}

const EbcdicTableRegistry::ConversionTables& 
EbcdicTableRegistry::getTables(EbcdicCodePage codepage) {
    auto it = tables_.find(codepage);
    if (it == tables_.end()) {
        throw std::runtime_error("Unsupported EBCDIC codepage");
    }
    return it->second;
}

// Base class implementations
std::vector<uint8_t> IEbcdicConverter::utf8ToEbcdic(const std::string& input) {
    return utf8ToEbcdic(std::vector<uint8_t>(input.begin(), input.end()));
}

std::string IEbcdicConverter::ebcdicToUtf8String(
    const unsigned char* ebcdicStr, 
    size_t length, 
    bool trim) {
    auto utf8Bytes = ebcdicToUtf8(ebcdicStr, length);
    if (!trim) {
        return std::string(utf8Bytes.begin(), utf8Bytes.end());
    }

    // Find last non-space
    auto it = utf8Bytes.rbegin();
    while (it != utf8Bytes.rend() && *it == 0x20) {
        ++it;
    }
    return std::string(utf8Bytes.begin(), utf8Bytes.end() - (it - utf8Bytes.rbegin()));
}

std::string IEbcdicConverter::ebcdicToUtf8String(
    const std::vector<uint8_t>& ebcdicData, 
    bool trim) {
    return ebcdicToUtf8String(ebcdicData.data(), ebcdicData.size(), trim);
}

// Memory-based converter implementation
EbcdicConverter::EbcdicConverter(EbcdicCodePage codepage) {
    auto& tables = EbcdicTableRegistry::getInstance().getTables(codepage);
    utf8_FirstByte_ = tables.utf8_FirstByte;
    tr_itt_ = tables.tr_itt;
    tr_sot_ = tables.tr_sot;
    tr_sbt_ = tables.tr_sbt;
    toUtf8Lengths_ = tables.toUtf8Lengths;
    toUtf8Bytes_ = tables.toUtf8Bytes;
}

std::vector<uint8_t> EbcdicConverter::utf8ToEbcdic(const std::vector<uint8_t>& input) {
    std::vector<uint8_t> ebcdic;
    ebcdic.reserve(input.size());

    const uint8_t* pString = input.data();
    const uint8_t* pEnd = pString + input.size();

    while (pString < pEnd) {
        const uint8_t* p = pString;
        uint8_t t = utf8_FirstByte_[*p];
        
        if (t >= UTF8_CONTINUE) {
            ebcdic.push_back(static_cast<uint8_t>(EBCDIC_SUB));
            ++pString;
            continue;
        }

        int iState = 0;  // Start state

        do {
            unsigned char ch = *p++;
            unsigned char iColumn = tr_itt_[ch];
            unsigned short iOffset = tr_sot_[iState];

            for (;;) {
                int y = tr_sbt_[iOffset];
                if (y < 128) {
                    if (iColumn < y) {
                        iState = tr_sbt_[iOffset+1];
                        break;
                    }
                    iColumn = static_cast<unsigned char>(iColumn - y);
                    iOffset += 2;
                } else {
                    y = 256-y;
                    if (iColumn < y) {
                        iState = tr_sbt_[iOffset+iColumn+1];
                        break;
                    }
                    iColumn = static_cast<unsigned char>(iColumn - y);
                    iOffset = static_cast<unsigned short>(iOffset + y + 1);
                }
            }
        } while (iState < 3);  // Until accepting state

        ebcdic.push_back(static_cast<uint8_t>(iState - 3));
        pString = pString + t;
    }

    return ebcdic;
}

std::vector<uint8_t> EbcdicConverter::ebcdicToUtf8(
    const unsigned char* ebcdicStr, 
    size_t length) {
    std::vector<uint8_t> result;
    result.reserve(length * 2);  // Reasonable initial capacity

    for (size_t i = 0; i < length; i++) {
        unsigned char eb = ebcdicStr[i];
        uint8_t utf8_len = toUtf8Lengths_[eb];
        
        // We're using fixed 4-byte tables now
        for (size_t j = 0; j < utf8_len; j++) {
            result.push_back(toUtf8Bytes_[eb][j]);
        }
    }
    
    return result;
}

char EbcdicConverter::ebcdicToAscii(unsigned char eb) {
    if (toUtf8Lengths_[eb] != 1) {
        return '?';  // Not an ASCII character
    }
    return static_cast<char>(toUtf8Bytes_[eb][0]);
}

// Streaming converter implementation
StreamingEbcdicConverter::StreamingEbcdicConverter(
    EbcdicCodePage codepage, 
    size_t bufferSize)
    : bufferSize_(bufferSize)
    , buffer_(bufferSize) {
    
    auto& tables = EbcdicTableRegistry::getInstance().getTables(codepage);
    utf8_FirstByte_ = tables.utf8_FirstByte;
    tr_itt_ = tables.tr_itt;
    tr_sot_ = tables.tr_sot;
    tr_sbt_ = tables.tr_sbt;
    toUtf8Lengths_ = tables.toUtf8Lengths;
    toUtf8Bytes_ = tables.toUtf8Bytes;
}

std::vector<uint8_t> StreamingEbcdicConverter::utf8ToEbcdic(
    const std::vector<uint8_t>& input) {
    std::vector<uint8_t> result;
    result.reserve(input.size());

    size_t pos = 0;
    while (pos < input.size()) {
        size_t chunk_size = std::min(bufferSize_, input.size() - pos);
        std::memcpy(buffer_.data(), input.data() + pos, chunk_size);
        
        bool isLastChunk = (pos + chunk_size) == input.size();
        processBuffer(buffer_.data(), chunk_size, result, isLastChunk);
        
        pos += chunk_size;
    }

    return result;
}

std::vector<uint8_t> StreamingEbcdicConverter::ebcdicToUtf8(
    const unsigned char* ebcdicStr, 
    size_t length) {
    std::vector<uint8_t> result;
    result.reserve(length * 2);

    size_t pos = 0;
    while (pos < length) {
        size_t chunk_size = std::min(bufferSize_, length - pos);
        
        for (size_t i = 0; i < chunk_size; i++) {
            unsigned char eb = ebcdicStr[pos + i];
            uint8_t utf8_len = toUtf8Lengths_[eb];
            
            for (size_t j = 0; j < utf8_len; j++) {
                result.push_back(toUtf8Bytes_[eb][j]);
            }
        }
        
        pos += chunk_size;
    }

    return result;
}

char StreamingEbcdicConverter::ebcdicToAscii(unsigned char eb) {
    if (toUtf8Lengths_[eb] != 1) {
        return '?';
    }
    return static_cast<char>(toUtf8Bytes_[eb][0]);
}

void StreamingEbcdicConverter::processBuffer(
    const unsigned char* input,
    size_t length,
    std::vector<uint8_t>& output,
    bool isLastChunk) {
    
    size_t pos = 0;
    size_t safe_end = isLastChunk ? length : findSafeBufferEnd(input, length);

    while (pos < safe_end) {
        const uint8_t* p = input + pos;
        uint8_t t = utf8_FirstByte_[*p];
        
        if (t >= UTF8_CONTINUE) {
            output.push_back(static_cast<uint8_t>(EBCDIC_SUB));
            pos++;
            continue;
        }

        int iState = 0;

        do {
            unsigned char ch = *p++;
            unsigned char iColumn = tr_itt_[ch];
            unsigned short iOffset = tr_sot_[iState];

            for (;;) {
                int y = tr_sbt_[iOffset];
                if (y < 128) {
                    if (iColumn < y) {
                        iState = tr_sbt_[iOffset+1];
                        break;
                    }
                    iColumn = static_cast<unsigned char>(iColumn - y);
                    iOffset += 2;
                } else {
                    y = 256-y;
                    if (iColumn < y) {
                        iState = tr_sbt_[iOffset+iColumn+1];
                        break;
                    }
                    iColumn = static_cast<unsigned char>(iColumn - y);
                    iOffset = static_cast<unsigned short>(iOffset + y + 1);
                }
            }
        } while (iState < 3);

        output.push_back(static_cast<uint8_t>(iState - 3));
        pos += t;
    }

    // If there's remaining data and this isn't the last chunk,
    // save it for the next buffer
    if (!isLastChunk && pos < length) {
        size_t remaining = length - pos;
        std::memmove(buffer_.data(), input + pos, remaining);
    }
}

size_t StreamingEbcdicConverter::findSafeBufferEnd(
    const unsigned char* buffer,
    size_t length) {
    // Work backwards to find a safe UTF-8 sequence boundary
    size_t pos = length;
    while (pos > 0) {
        if (utf8_FirstByte_[buffer[pos - 1]] < UTF8_CONTINUE) {
            break;
        }
        pos--;
    }
    return pos;
}
