// Exhaustive check of every EBCDIC code page against its mapping file.
//
// For each code page: every EBCDIC byte must decode to the UTF-8 encoding
// of the code point the mapping file assigns it, every mapped code point
// must encode back to that byte, and characters outside the code page must
// encode to the EBCDIC substitute byte (0x3F).  Both the in-memory and the
// streaming converter are exercised.
//
// Usage: codepage_check <dir containing tr_utf8_cpNNN.txt>

#include <cstdio>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "ebcdic_converter.h"

namespace {

struct Page { const char* name; EbcdicCodePage cp; };
const Page PAGES[] = {
    {"037",  EbcdicCodePage::CP037},
    {"273",  EbcdicCodePage::CP273},
    {"277",  EbcdicCodePage::CP277},
    {"285",  EbcdicCodePage::CP285},
    {"500",  EbcdicCodePage::CP500},
    {"1047", EbcdicCodePage::CP1047},
};

std::vector<uint8_t> utf8(uint32_t cp) {
    std::vector<uint8_t> v;
    if (cp < 0x80) v.push_back(cp);
    else if (cp < 0x800) { v.push_back(0xC0 | (cp >> 6)); v.push_back(0x80 | (cp & 0x3F)); }
    else if (cp < 0x10000) { v.push_back(0xE0 | (cp >> 12)); v.push_back(0x80 | ((cp >> 6) & 0x3F)); v.push_back(0x80 | (cp & 0x3F)); }
    else { v.push_back(0xF0 | (cp >> 18)); v.push_back(0x80 | ((cp >> 12) & 0x3F)); v.push_back(0x80 | ((cp >> 6) & 0x3F)); v.push_back(0x80 | (cp & 0x3F)); }
    return v;
}

int failures = 0;
void fail(const std::string& page, const std::string& what) {
    std::printf("FAIL cp%s: %s\n", page.c_str(), what.c_str());
    ++failures;
}

void checkPage(const std::string& dir, const Page& page) {
    std::ifstream in(dir + "/tr_utf8_cp" + page.name + ".txt");
    if (!in) { fail(page.name, "mapping file not found"); return; }

    std::map<uint32_t, uint8_t> toEbcdic;
    std::map<uint8_t, uint32_t> toUnicode;
    std::string line;
    while (std::getline(in, line)) {
        std::istringstream ss(line);
        std::string u, b;
        std::getline(ss, u, ';'); std::getline(ss, b, ';');
        uint32_t cp = std::stoul(u, nullptr, 16);
        uint8_t eb = static_cast<uint8_t>(std::stoi(b));
        toEbcdic[cp] = eb; toUnicode[eb] = cp;
    }
    if (toEbcdic.size() != 256 || toUnicode.size() != 256) {
        fail(page.name, "mapping file is not a 256-entry bijection"); return;
    }

    for (bool streaming : {false, true}) {
        auto conv = IEbcdicConverter::create(page.cp, streaming);
        const char* mode = streaming ? " (streaming)" : "";

        for (int b = 0; b < 256; ++b) {
            unsigned char eb = static_cast<unsigned char>(b);
            auto got = conv->ebcdicToUtf8(&eb, 1);
            if (got != utf8(toUnicode[eb])) {
                char msg[96];
                std::snprintf(msg, sizeof msg, "EBCDIC 0x%02X should decode to U+%04X%s", b, toUnicode[eb], mode);
                fail(page.name, msg);
            }
        }
        for (const auto& kv : toEbcdic) {
            auto got = conv->utf8ToEbcdic(utf8(kv.first));
            if (got.size() != 1 || got[0] != kv.second) {
                char msg[96];
                std::snprintf(msg, sizeof msg, "U+%04X should encode to EBCDIC 0x%02X%s", kv.first, kv.second, mode);
                fail(page.name, msg);
            }
        }
        for (uint32_t cp : {0x0100u, 0x4E2Du, 0x1F600u}) {
            if (toEbcdic.count(cp)) continue;
            auto got = conv->utf8ToEbcdic(utf8(cp));
            if (got.size() != 1 || got[0] != 0x3F) {
                char msg[96];
                std::snprintf(msg, sizeof msg, "unmapped U+%04X should encode to 0x3F%s", cp, mode);
                fail(page.name, msg);
            }
        }
        // A whole string, both directions, through the same converter
        std::string text = "Hello, World! [brackets] ^caret ~tilde |bar";
        auto eb = conv->utf8ToEbcdic(text);
        auto back = conv->ebcdicToUtf8String(eb);
        if (back != text) fail(page.name, "round trip of ASCII sample string" + std::string(mode));
    }
    std::printf("  OK: cp%s\n", page.name);
}

}  // namespace

int main(int argc, char* argv[]) {
    if (argc != 2) { std::fprintf(stderr, "usage: %s <mapping dir>\n", argv[0]); return 2; }
    for (const auto& page : PAGES) checkPage(argv[1], page);
    if (failures) { std::printf("%d failure(s)\n", failures); return 1; }
    std::printf("All code pages verified.\n");
    return 0;
}
