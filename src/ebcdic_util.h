#ifndef EBCDIC_UTIL_H
#define EBCDIC_UTIL_H

#include <vector>
#include <string>
#include <cstdint>

// UTF-8 sequence length definitions
#define UTF8_SIZE1     1
#define UTF8_SIZE2     2
#define UTF8_SIZE3     3
#define UTF8_SIZE4     4
#define UTF8_CONTINUE  5
#define UTF8_ILLEGAL   6

// Transition table state definitions
#define TR_CP037_START_STATE (0)
#define TR_CP037_ACCEPTING_STATES_START (3)

// EBCDIC substitute character
#define EBCDIC_SUB (63)

class EbcdicUtil {
public:
    /**
     * Convert UTF-8 encoded data to EBCDIC (CP037)
     * @param input Vector of UTF-8 encoded bytes
     * @return Vector of EBCDIC encoded bytes
     */
    static std::vector<uint8_t> utf8ToEbcdic(const std::vector<uint8_t>& input);

    /**
     * Convert UTF-8 string to EBCDIC (CP037)
     * @param input UTF-8 encoded string
     * @return Vector of EBCDIC encoded bytes
     */
    static std::vector<uint8_t> utf8ToEbcdic(const std::string& input);

    /**
     * Convert EBCDIC (CP037) data to UTF-8
     * @param ebcdicStr Pointer to EBCDIC encoded bytes
     * @param length Length of EBCDIC data
     * @return Vector of UTF-8 encoded bytes
     */
    static std::vector<uint8_t> ebcdicToUtf8(const unsigned char* ebcdicStr, size_t length);

    /**
     * Convert EBCDIC (CP037) data to UTF-8 string with optional trimming
     * @param ebcdicStr Pointer to EBCDIC encoded bytes
     * @param length Length of EBCDIC data
     * @param trim If true, remove trailing spaces
     * @return UTF-8 encoded string
     */
    static std::string ebcdicToUtf8String(const unsigned char* ebcdicStr, size_t length, bool trim = false);

    /**
     * Convert EBCDIC (CP037) vector to UTF-8 string with optional trimming
     * @param ebcdicData Vector of EBCDIC encoded bytes
     * @param trim If true, remove trailing spaces
     * @return UTF-8 encoded string
     */
    static std::string ebcdicToUtf8String(const std::vector<uint8_t>& ebcdicData, bool trim = false);

    /**
     * Convert single EBCDIC character to ASCII
     * Only handles specific characters used in tape labels (F, V, U, B, S, R, space)
     * @param eb EBCDIC character
     * @return ASCII character or '?' if not recognized
     */
    static char ebcdicToAscii(unsigned char eb);

private:
    // Conversion tables
    static const unsigned char utf8_FirstByte[256];
    static const unsigned char tr_cp037_itt[256];
    static const unsigned short tr_cp037_sot[3];
    static const unsigned short tr_cp037_sbt[274];
    static const uint8_t cp037_to_utf8_lengths[256];
    static const uint8_t cp037_to_utf8_bytes[256][4];
};

#endif // EBCDIC_UTIL_H
