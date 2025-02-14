#include <algorithm>
#include "ebcdic_util.h"

std::vector<uint8_t> EbcdicUtil::utf8ToEbcdic(const std::vector<uint8_t>& input) {
    std::vector<uint8_t> ebcdic;
    ebcdic.reserve(input.size());

    const uint8_t* pString = input.data();
    const uint8_t* pEnd = pString + input.size();

    while (pString < pEnd) {
        const uint8_t* p = pString;
        uint8_t t = utf8_FirstByte[*p];
        if (UTF8_CONTINUE <= t) {
            ebcdic.push_back(static_cast<uint8_t>(EBCDIC_SUB));
            ++pString;
            continue;
        }

        int iState = TR_CP031_START_STATE;

        do {
            unsigned char ch = *p++;
            unsigned char iColumn = tr_cp031_itt[ch];
            unsigned short iOffset = tr_cp031_sot[iState];

            for (;;) {
                int y = tr_cp031_sbt[iOffset];
                if (y < 128) {
                    if (iColumn < y) {
                        iState = tr_cp031_sbt[iOffset+1];
                        break;
                    } else {
                        iColumn = static_cast<unsigned char>(iColumn - y);
                        iOffset += 2;
                    }
                } else {
                    y = 256-y;
                    if (iColumn < y) {
                        iState = tr_cp031_sbt[iOffset+iColumn+1];
                        break;
                    } else {
                        iColumn = static_cast<unsigned char>(iColumn - y);
                        iOffset = static_cast<unsigned short>(iOffset + y + 1);
                    }
                }
            }
        } while (iState < TR_CP031_ACCEPTING_STATES_START);

        ebcdic.push_back(static_cast<uint8_t>(iState - TR_CP031_ACCEPTING_STATES_START));
        pString = pString + t;
    }

    return ebcdic;
}

std::vector<uint8_t> EbcdicUtil::utf8ToEbcdic(const std::string& input) {
    return utf8ToEbcdic(std::vector<uint8_t>(input.begin(), input.end()));
}

std::vector<uint8_t> EbcdicUtil::ebcdicToUtf8(const unsigned char* ebcdicStr, size_t length) {
    // Pre-calculate maximum possible size to avoid reallocations
    std::vector<uint8_t> result;
    size_t maxSize = 0;
    for (size_t i = 0; i < length; i++) {
        maxSize += ebcdicToUtf8Lengths[ebcdicStr[i]];
    }
    result.reserve(maxSize);

    // Do the conversion using our pre-calculated tables
    for (size_t i = 0; i < length; i++) {
        unsigned char eb = ebcdicStr[i];
        uint8_t utf8_len = ebcdicToUtf8Lengths[eb];
        if (utf8_len > 0) {
            result.insert(result.end(),
                        ebcdicToUtf8Bytes[eb],
                        ebcdicToUtf8Bytes[eb] + utf8_len);
        }
    }
    return result;
}

std::string EbcdicUtil::ebcdicToUtf8String(const unsigned char* ebcdicStr, size_t length, bool trim) {
    auto utf8Bytes = ebcdicToUtf8(ebcdicStr, length);
    if (!trim) {
        return std::string(utf8Bytes.begin(), utf8Bytes.end());
    }

    // Find last non-space character
    auto it = utf8Bytes.rbegin();
    while (it != utf8Bytes.rend()) {
        // Check for UTF-8 space (0x20)
        if (*it != 0x20) {
            break;
        }
        ++it;
    }

    return std::string(utf8Bytes.begin(), utf8Bytes.end() - (it - utf8Bytes.rbegin()));
}

std::string EbcdicUtil::ebcdicToUtf8String(const std::vector<uint8_t>& ebcdicData, bool trim) {
    return ebcdicToUtf8String(ebcdicData.data(), ebcdicData.size(), trim);
}

char EbcdicUtil::ebcdicToAscii(unsigned char eb) {
    /**
     * Convert single EBCDIC character to ASCII by leveraging existing UTF-8 conversion tables
     *
     * This function uses the ebcdicToUtf8Lengths and ebcdicToUtf8Bytes tables to determine:
     * 1. If the EBCDIC character maps to a multi-byte UTF-8 sequence (return '?')
     * 2. If it's a single-byte sequence, return that byte as ASCII
     *
     * This approach:
     * - Eliminates the need for separate conversion tables
     * - Handles all valid single-byte conversions automatically
     * - Consistently returns '?' for any character that would require multiple bytes
     *
     * @param eb EBCDIC character to convert
     * @return ASCII character or '?' if the EBCDIC character would require multiple UTF-8 bytes
     */

    // First check if this EBCDIC character maps to a multi-byte UTF-8 sequence
    if (ebcdicToUtf8Lengths[eb] != 1) {
        return '?';  // Character requires multiple UTF-8 bytes, can't represent in ASCII
    }

    // For single-byte sequences, return the UTF-8 byte (which is ASCII compatible)
    return static_cast<char>(ebcdicToUtf8Bytes[eb][0]);
}

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
const unsigned char EbcdicUtil::utf8_FirstByte[256] = {
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

// utf/tr_utf8_cp037.txt
//
// 255 code points.
// 3 states, 193 columns, 804 bytes
//
const unsigned char EbcdicUtil::tr_cp031_itt[256] =
{
       0,   1,   2,   3,   4,   5,   6,   7,    8,   9,  10,  11,  12,  13,  14,  15,
      16,  17,  18,  19,  20,  21,  22,  23,   24,  25,  13,  26,  27,  28,  29,  30,
      31,  32,  33,  34,  35,  36,  37,  38,   39,  40,  41,  42,  43,  44,  45,  46,
      47,  48,  49,  50,  51,  52,  53,  54,   55,  56,  57,  58,  59,  60,  61,  62,
      63,  64,  65,  66,  67,  68,  69,  70,   71,  72,  73,  74,  75,  76,  77,  78,
      79,  80,  81,  82,  83,  84,  85,  86,   87,  88,  89,  90,  91,  92,  93,  94,
      95,  96,  97,  98,  99, 100, 101, 102,  103, 104, 105, 106, 107, 108, 109, 110,
     111, 112, 113, 114, 115, 116, 117, 118,  119, 120, 121, 122, 123, 124, 125, 126,

     127, 128, 129, 130, 131, 132, 133, 134,  135, 136, 137, 138, 139, 140, 141, 142,
     143, 144, 145, 146, 147, 148, 149, 150,  151, 152, 153, 154, 155, 156, 157, 158,
     159, 160, 161, 162, 163, 164, 165, 166,  167, 168, 169, 170, 171, 172, 173, 174,
     175, 176, 177, 178, 179, 180, 181, 182,  183, 184, 185, 186, 187, 188, 189, 190,
      13,  13, 191, 192,  13,  13,  13,  13,   13,  13,  13,  13,  13,  13,  13,  13,
      13,  13,  13,  13,  13,  13,  13,  13,   13,  13,  13,  13,  13,  13,  13,  13,
      13,  13,  13,  13,  13,  13,  13,  13,   13,  13,  13,  13,  13,  13,  13,  13,
      13,  13,  13,  13,  13,  13,  13,  13,   13,  13,  13,  13,  13,  13,  13,  13

};

const unsigned short EbcdicUtil::tr_cp031_sot[3] =
{
        0,  133,  202
};

const unsigned short EbcdicUtil::tr_cp031_sbt[271] =
{
     129,   3,   4,   5,   6,  58,  48,  49,   50,  25,   8,  16,  14,  15,  66,  17,
      18,  19,  20,  21,  22,  63,  64,  53,   41,  27,  28,  42,  31,  32,  33,  34,
      67,  93, 130, 126,  94, 111,  83, 128,   80,  96,  95,  81, 110,  99,  78, 100,
     243, 244, 245, 246, 247, 248, 249, 250,  251, 252, 125,  97,  79, 129, 113, 114,
     127, 196, 197, 198, 199, 200, 201, 202,  203, 204, 212, 213, 214, 215, 216, 217,
     218, 219, 220, 229, 230, 231, 232, 233,  234, 235, 236, 189, 227, 190, 179, 112,
     124, 132, 133, 134, 135, 136, 137, 138,  139, 140, 148, 149, 150, 151, 152, 153,
     154, 155, 156, 165, 166, 167, 168, 169,  170, 171, 172, 195,  82, 211, 164,  10,
      64,  66, 254,   1,   2, 127,  66, 192,   35,  36,  37,  38,  39,  24,   9,  26,
      43,  44,  45,  46,  47,  12,  13,  30,   51,  52,  29,  54,  55,  56,  57,  11,
      59,  60,  61,  62,   7,  23,  65, 258,   68, 173,  77, 180, 162, 181, 109, 184,
     192, 183, 157, 141,  98, 205, 178, 191,  147, 146, 237, 253, 193, 163, 185, 182,
     160, 221, 158, 142, 186, 187, 188, 174,    2,  66, 127,  66, 192, 103, 104, 101,
     105, 102, 106, 161, 107, 119, 116, 117,  118, 123, 120, 121, 122, 175, 108, 240,
     241, 238, 242, 239, 194, 131, 256, 257,  254, 255, 176, 177,  92,  71,  72,  69,
      73,  70,  74, 159,  75,  87,  84,  85,   86,  91,  88,  89,  90, 143,  76, 208,
     209, 206, 210, 207, 228, 115, 224, 225,  222, 223, 144, 145, 226,   2,  66
};

const uint8_t EbcdicUtil::ebcdicToUtf8Lengths[256] = {
    1, 1, 1, 1, 2, 1, 2, 1,
    2, 2, 2, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 2, 2, 1, 2,
    1, 1, 2, 2, 1, 1, 1, 1,
    2, 2, 2, 2, 2, 1, 1, 1,
    2, 2, 2, 2, 2, 1, 1, 1,
    2, 2, 1, 2, 2, 2, 2, 1,
    2, 2, 2, 2, 1, 1, 2, 1,
    1, 2, 2, 2, 2, 2, 2, 2,
    2, 2, 2, 1, 1, 1, 1, 1,
    1, 2, 2, 2, 2, 2, 2, 2,
    2, 2, 1, 1, 1, 1, 1, 2,
    1, 1, 2, 2, 2, 2, 2, 2,
    2, 2, 2, 1, 1, 1, 1, 1,
    2, 2, 2, 2, 2, 2, 2, 2,
    2, 1, 1, 1, 1, 1, 1, 1,
    2, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 2, 2, 2, 2, 2, 2,
    2, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 2, 2, 2, 2, 2, 2,
    2, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 2, 2, 2, 2, 2, 2,
    1, 2, 2, 2, 2, 2, 2, 2,
    2, 2, 1, 1, 2, 2, 2, 2,
    1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 2, 2, 2, 2, 2, 2,
    1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 2, 2, 2, 2, 2, 2,
    1, 2, 1, 1, 1, 1, 1, 1,
    1, 1, 2, 2, 2, 2, 2, 2,
    1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 2, 2, 2, 2, 2, 2,
};

const uint8_t EbcdicUtil::ebcdicToUtf8Bytes[256][2] = {
    {0x00, 0},  // 0x00
    {0x01, 0},  // 0x01
    {0x02, 0},  // 0x02
    {0x03, 0},  // 0x03
    {0xC2, 0x9C},  // 0x04
    {0x09, 0},  // 0x05
    {0xC2, 0x86},  // 0x06
    {0x7F, 0},  // 0x07
    {0xC2, 0x97},  // 0x08
    {0xC2, 0x8D},  // 0x09
    {0xC2, 0x8E},  // 0x0A
    {0x0B, 0},  // 0x0B
    {0x0C, 0},  // 0x0C
    {0x0D, 0},  // 0x0D
    {0x0E, 0},  // 0x0E
    {0x0F, 0},  // 0x0F
    {0x10, 0},  // 0x10
    {0x11, 0},  // 0x11
    {0x12, 0},  // 0x12
    {0x13, 0},  // 0x13
    {0xC2, 0x9D},  // 0x14
    {0xC2, 0x85},  // 0x15
    {0x08, 0},  // 0x16
    {0xC2, 0x87},  // 0x17
    {0x18, 0},  // 0x18
    {0x19, 0},  // 0x19
    {0xC2, 0x92},  // 0x1A
    {0xC2, 0x8F},  // 0x1B
    {0x1C, 0},  // 0x1C
    {0x1D, 0},  // 0x1D
    {0x1E, 0},  // 0x1E
    {0x1F, 0},  // 0x1F
    {0xC2, 0x80},  // 0x20
    {0xC2, 0x81},  // 0x21
    {0xC2, 0x82},  // 0x22
    {0xC2, 0x83},  // 0x23
    {0xC2, 0x84},  // 0x24
    {0x0A, 0},  // 0x25
    {0x17, 0},  // 0x26
    {0x1B, 0},  // 0x27
    {0xC2, 0x88},  // 0x28
    {0xC2, 0x89},  // 0x29
    {0xC2, 0x8A},  // 0x2A
    {0xC2, 0x8B},  // 0x2B
    {0xC2, 0x8C},  // 0x2C
    {0x05, 0},  // 0x2D
    {0x06, 0},  // 0x2E
    {0x07, 0},  // 0x2F
    {0xC2, 0x90},  // 0x30
    {0xC2, 0x91},  // 0x31
    {0x16, 0},  // 0x32
    {0xC2, 0x93},  // 0x33
    {0xC2, 0x94},  // 0x34
    {0xC2, 0x95},  // 0x35
    {0xC2, 0x96},  // 0x36
    {0x04, 0},  // 0x37
    {0xC2, 0x98},  // 0x38
    {0xC2, 0x99},  // 0x39
    {0xC2, 0x9A},  // 0x3A
    {0xC2, 0x9B},  // 0x3B
    {0x14, 0},  // 0x3C
    {0x15, 0},  // 0x3D
    {0xC2, 0x9E},  // 0x3E
    {0x1A, 0},  // 0x3F
    {0x20, 0},  // 0x40
    {0xC2, 0xA0},  // 0x41
    {0xC3, 0xA2},  // 0x42
    {0xC3, 0xA4},  // 0x43
    {0xC3, 0xA0},  // 0x44
    {0xC3, 0xA1},  // 0x45
    {0xC3, 0xA3},  // 0x46
    {0xC3, 0xA5},  // 0x47
    {0xC3, 0xA7},  // 0x48
    {0xC3, 0xB1},  // 0x49
    {0xC2, 0xA2},  // 0x4A
    {0x2E, 0},  // 0x4B
    {0x3C, 0},  // 0x4C
    {0x28, 0},  // 0x4D
    {0x2B, 0},  // 0x4E
    {0x7C, 0},  // 0x4F
    {0x26, 0},  // 0x50
    {0xC3, 0xA9},  // 0x51
    {0xC3, 0xAA},  // 0x52
    {0xC3, 0xAB},  // 0x53
    {0xC3, 0xA8},  // 0x54
    {0xC3, 0xAD},  // 0x55
    {0xC3, 0xAE},  // 0x56
    {0xC3, 0xAF},  // 0x57
    {0xC3, 0xAC},  // 0x58
    {0xC3, 0x9F},  // 0x59
    {0x21, 0},  // 0x5A
    {0x24, 0},  // 0x5B
    {0x2A, 0},  // 0x5C
    {0x29, 0},  // 0x5D
    {0x3B, 0},  // 0x5E
    {0xC2, 0xAC},  // 0x5F
    {0x2D, 0},  // 0x60
    {0x2F, 0},  // 0x61
    {0xC3, 0x82},  // 0x62
    {0xC3, 0x84},  // 0x63
    {0xC3, 0x80},  // 0x64
    {0xC3, 0x81},  // 0x65
    {0xC3, 0x83},  // 0x66
    {0xC3, 0x85},  // 0x67
    {0xC3, 0x87},  // 0x68
    {0xC3, 0x91},  // 0x69
    {0xC2, 0xA6},  // 0x6A
    {0x2C, 0},  // 0x6B
    {0x25, 0},  // 0x6C
    {0x5F, 0},  // 0x6D
    {0x3E, 0},  // 0x6E
    {0x3F, 0},  // 0x6F
    {0xC3, 0xB8},  // 0x70
    {0xC3, 0x89},  // 0x71
    {0xC3, 0x8A},  // 0x72
    {0xC3, 0x8B},  // 0x73
    {0xC3, 0x88},  // 0x74
    {0xC3, 0x8D},  // 0x75
    {0xC3, 0x8E},  // 0x76
    {0xC3, 0x8F},  // 0x77
    {0xC3, 0x8C},  // 0x78
    {0x60, 0},  // 0x79
    {0x3A, 0},  // 0x7A
    {0x23, 0},  // 0x7B
    {0x40, 0},  // 0x7C
    {0x27, 0},  // 0x7D
    {0x3D, 0},  // 0x7E
    {0x22, 0},  // 0x7F
    {0xC3, 0x98},  // 0x80
    {0x61, 0},  // 0x81
    {0x62, 0},  // 0x82
    {0x63, 0},  // 0x83
    {0x64, 0},  // 0x84
    {0x65, 0},  // 0x85
    {0x66, 0},  // 0x86
    {0x67, 0},  // 0x87
    {0x68, 0},  // 0x88
    {0x69, 0},  // 0x89
    {0xC2, 0xAB},  // 0x8A
    {0xC2, 0xBB},  // 0x8B
    {0xC3, 0xB0},  // 0x8C
    {0xC3, 0xBD},  // 0x8D
    {0xC3, 0xBE},  // 0x8E
    {0xC2, 0xB1},  // 0x8F
    {0xC2, 0xB0},  // 0x90
    {0x6A, 0},  // 0x91
    {0x6B, 0},  // 0x92
    {0x6C, 0},  // 0x93
    {0x6D, 0},  // 0x94
    {0x6E, 0},  // 0x95
    {0x6F, 0},  // 0x96
    {0x70, 0},  // 0x97
    {0x71, 0},  // 0x98
    {0x72, 0},  // 0x99
    {0xC2, 0xAA},  // 0x9A
    {0xC2, 0xBA},  // 0x9B
    {0xC3, 0xA6},  // 0x9C
    {0xC2, 0xB8},  // 0x9D
    {0xC3, 0x86},  // 0x9E
    {0xC2, 0xA4},  // 0x9F
    {0xC2, 0xB5},  // 0xA0
    {0x7E, 0},  // 0xA1
    {0x73, 0},  // 0xA2
    {0x74, 0},  // 0xA3
    {0x75, 0},  // 0xA4
    {0x76, 0},  // 0xA5
    {0x77, 0},  // 0xA6
    {0x78, 0},  // 0xA7
    {0x79, 0},  // 0xA8
    {0x7A, 0},  // 0xA9
    {0xC2, 0xA1},  // 0xAA
    {0xC2, 0xBF},  // 0xAB
    {0xC3, 0x90},  // 0xAC
    {0xC3, 0x9D},  // 0xAD
    {0xC3, 0x9E},  // 0xAE
    {0xC2, 0xAE},  // 0xAF
    {0x5E, 0},  // 0xB0
    {0xC2, 0xA3},  // 0xB1
    {0xC2, 0xA5},  // 0xB2
    {0xC2, 0xB7},  // 0xB3
    {0xC2, 0xA9},  // 0xB4
    {0xC2, 0xA7},  // 0xB5
    {0xC2, 0xB6},  // 0xB6
    {0xC2, 0xBC},  // 0xB7
    {0xC2, 0xBD},  // 0xB8
    {0xC2, 0xBE},  // 0xB9
    {0x5B, 0},  // 0xBA
    {0x5D, 0},  // 0xBB
    {0xC2, 0xAF},  // 0xBC
    {0xC2, 0xA8},  // 0xBD
    {0xC2, 0xB4},  // 0xBE
    {0xC3, 0x97},  // 0xBF
    {0x7B, 0},  // 0xC0
    {0x41, 0},  // 0xC1
    {0x42, 0},  // 0xC2
    {0x43, 0},  // 0xC3
    {0x44, 0},  // 0xC4
    {0x45, 0},  // 0xC5
    {0x46, 0},  // 0xC6
    {0x47, 0},  // 0xC7
    {0x48, 0},  // 0xC8
    {0x49, 0},  // 0xC9
    {0xC2, 0xAD},  // 0xCA
    {0xC3, 0xB4},  // 0xCB
    {0xC3, 0xB6},  // 0xCC
    {0xC3, 0xB2},  // 0xCD
    {0xC3, 0xB3},  // 0xCE
    {0xC3, 0xB5},  // 0xCF
    {0x7D, 0},  // 0xD0
    {0x4A, 0},  // 0xD1
    {0x4B, 0},  // 0xD2
    {0x4C, 0},  // 0xD3
    {0x4D, 0},  // 0xD4
    {0x4E, 0},  // 0xD5
    {0x4F, 0},  // 0xD6
    {0x50, 0},  // 0xD7
    {0x51, 0},  // 0xD8
    {0x52, 0},  // 0xD9
    {0xC2, 0xB9},  // 0xDA
    {0xC3, 0xBB},  // 0xDB
    {0xC3, 0xBC},  // 0xDC
    {0xC3, 0xB9},  // 0xDD
    {0xC3, 0xBA},  // 0xDE
    {0xC3, 0xBF},  // 0xDF
    {0x5C, 0},  // 0xE0
    {0xC3, 0xB7},  // 0xE1
    {0x53, 0},  // 0xE2
    {0x54, 0},  // 0xE3
    {0x55, 0},  // 0xE4
    {0x56, 0},  // 0xE5
    {0x57, 0},  // 0xE6
    {0x58, 0},  // 0xE7
    {0x59, 0},  // 0xE8
    {0x5A, 0},  // 0xE9
    {0xC2, 0xB2},  // 0xEA
    {0xC3, 0x94},  // 0xEB
    {0xC3, 0x96},  // 0xEC
    {0xC3, 0x92},  // 0xED
    {0xC3, 0x93},  // 0xEE
    {0xC3, 0x95},  // 0xEF
    {0x30, 0},  // 0xF0
    {0x31, 0},  // 0xF1
    {0x32, 0},  // 0xF2
    {0x33, 0},  // 0xF3
    {0x34, 0},  // 0xF4
    {0x35, 0},  // 0xF5
    {0x36, 0},  // 0xF6
    {0x37, 0},  // 0xF7
    {0x38, 0},  // 0xF8
    {0x39, 0},  // 0xF9
    {0xC2, 0xB3},  // 0xFA
    {0xC3, 0x9B},  // 0xFB
    {0xC3, 0x9C},  // 0xFC
    {0xC3, 0x99},  // 0xFD
    {0xC3, 0x9A},  // 0xFE
    {0xC2, 0x9F},  // 0xFF
};
