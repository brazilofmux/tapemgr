#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <cstring>
#include <cstdint>
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <chrono>

struct FileConfig {
    std::string inputFile;
    uint16_t lrecl;
    uint16_t blksize;
    char recfm;
    bool binary;
};

struct AwsTapeBlockHeader {
    uint16_t curblkl;
    uint16_t prvblkl;
    uint8_t flags1;
    uint8_t flags2;
};

class AwsTapeMaker {
public:
    AwsTapeMaker(const std::string& volser, const std::string& outputFile)
        : m_volser(volser), m_outputFile(outputFile), m_prevBlockSize(0) {
        std::cout << "Initializing tape maker for volume " << volser << std::endl;
        m_outFile.open(outputFile, std::ios::binary);
        if (!m_outFile) {
            throw std::runtime_error("Unable to open output file: " + outputFile);
        }
    }

    void addFile(const FileConfig& config) {
        std::cout << "Adding file: " << config.inputFile << std::endl;
        // Verify input file
        std::ifstream testFile(config.inputFile, std::ios::binary);
        if (!testFile) {
            throw std::runtime_error("Unable to open input file: " + config.inputFile);
        }
        testFile.close();

        // Verify LRECL and BLKSIZE
        if (config.lrecl == 0 || config.blksize == 0 || config.blksize % config.lrecl != 0) {
            throw std::runtime_error("Invalid LRECL or BLKSIZE for file: " + config.inputFile);
        }

        // Verify RECFM
        if (config.recfm != 'F' && config.recfm != 'V' && config.recfm != 'U') {
            throw std::runtime_error("Invalid RECFM for file: " + config.inputFile);
        }

        m_files.push_back(config);
    }

    void writeTape() {
        std::cout << "Starting tape writing process..." << std::endl;
        writeVolumeLabel();
        for (size_t i = 0; i < m_files.size(); ++i) {
            writeFile(m_files[i], i + 1);
        }
        writeEndOfTape();
        verifyTape();
    }

private:
    std::string m_volser;
    std::string m_outputFile;
    std::vector<FileConfig> m_files;
    std::ofstream m_outFile;
    uint16_t m_prevBlockSize;

    void writeVolumeLabel() {
        std::cout << "Writing VOL1 label" << std::endl;
        std::string label = "VOL1" + m_volser + std::string(69, ' ');
        writeBlock(asciiToEbcdic(label), 0xA0, true);
    }

    void writeFile(const FileConfig& config, int fileNumber) {
        std::cout << "Writing file " << fileNumber << ": " << config.inputFile << std::endl;
        writeHeaderLabels(config, fileNumber);
        writeDataBlocks(config);
        writeEOFLabels(config, fileNumber);
    }

    void writeHeaderLabels(const FileConfig& config, int fileNumber) {
        std::cout << "  Writing HDR1 and HDR2 labels" << std::endl;
        auto now = std::chrono::system_clock::now();
        auto in_time_t = std::chrono::system_clock::to_time_t(now);
        std::stringstream ss;
        ss << std::put_time(std::localtime(&in_time_t), "%y%j");
        std::string date = ss.str();

        std::string hdr1 = "HDR1" + padRight(config.inputFile, 17) +
                           padRight(m_volser, 6) +
                           "0001" + padLeft(std::to_string(fileNumber), 4) +
                           "0001" + date + "  " + "00000" + std::string(28, ' ');

        std::string hdr2 = "HDR2" + config.recfm +
                           padLeft(std::to_string(config.blksize), 5) +
                           padLeft(std::to_string(config.lrecl), 5) +
                           "0" + std::string(66, ' ');

        writeBlock(asciiToEbcdic(hdr1), 0xA0, true);
        writeBlock(asciiToEbcdic(hdr2), 0xA0, true);
        writeTapeMark();
    }

    void writeDataBlocks(const FileConfig& config) {
        std::cout << "  Writing data blocks" << std::endl;
        std::ifstream inFile(config.inputFile, std::ios::binary);
        if (!inFile) {
            throw std::runtime_error("Unable to open input file: " + config.inputFile);
        }

        size_t totalBytesWritten = 0;
        std::vector<char> buffer(config.blksize);
        while (inFile) {
            inFile.read(buffer.data(), config.blksize);
            std::streamsize bytesRead = inFile.gcount();
            if (bytesRead > 0) {
                std::vector<uint8_t> block(buffer.begin(), buffer.begin() + bytesRead);
                if (!config.binary) {
                    for (auto& byte : block) {
                        byte = asciiToEbcdic(std::string(1, byte))[0];
                    }
                }
                writeBlock(block, 0xA0);
                totalBytesWritten += bytesRead;
            }
        }
        std::cout << "    Wrote " << totalBytesWritten << " bytes" << std::endl;
    }

    void writeEOFLabels(const FileConfig& config, int fileNumber) {
        std::cout << "  Writing EOF1 and EOF2 labels" << std::endl;
        writeTapeMark();
        std::string eof1 = "EOF1" + padRight(config.inputFile, 17) +
                           padRight(m_volser, 6) +
                           "0001" + padLeft(std::to_string(fileNumber), 4) +
                           "0001" + std::string(39, ' ');

        std::string eof2 = "EOF2" + config.recfm +
                           padLeft(std::to_string(config.blksize), 5) +
                           padLeft(std::to_string(config.lrecl), 5) +
                           "0" + std::string(66, ' ');

        writeBlock(asciiToEbcdic(eof1), 0xA0, true);
        writeBlock(asciiToEbcdic(eof2), 0xA0, true);
    }

    void writeEndOfTape() {
        std::cout << "Writing end of tape markers" << std::endl;
        writeTapeMark();
        writeTapeMark();
    }

    void writeBlock(const std::vector<uint8_t>& data, uint8_t flags, bool isLabel = false) {
        std::vector<uint8_t> paddedData = data;
        if (isLabel && paddedData.size() < 80) {
            paddedData.resize(80, 0x40);  // Pad with EBCDIC space (0x40)
        }

        AwsTapeBlockHeader header = {
            static_cast<uint16_t>(paddedData.size()),
            m_prevBlockSize,
            flags,
            0
        };
        m_outFile.write(reinterpret_cast<const char*>(&header), sizeof(header));
        m_outFile.write(reinterpret_cast<const char*>(paddedData.data()), paddedData.size());
        m_prevBlockSize = header.curblkl;

        std::cout << "    Wrote block: size=" << header.curblkl
                  << ", prev=" << header.prvblkl
                  << ", flags=0x" << std::hex << static_cast<int>(flags) << std::dec << std::endl;
    }

    void verifyTape() {
        std::cout << "Starting tape verification process..." << std::endl;
        m_outFile.close();
        std::ifstream verifyFile(m_outputFile, std::ios::binary);
        if (!verifyFile) {
            throw std::runtime_error("Unable to open tape file for verification: " + m_outputFile);
        }

        // Verify VOL1 label
        AwsTapeBlockHeader header;
        std::vector<uint8_t> data(80);
        verifyFile.read(reinterpret_cast<char*>(&header), sizeof(header));
        verifyFile.read(reinterpret_cast<char*>(data.data()), 80);
        std::string vol1Label = ebcdicToAscii(data);
        if (vol1Label.substr(0, 4) != "VOL1" || vol1Label.substr(4, 6) != m_volser) {
            throw std::runtime_error("Invalid VOL1 label");
        }
        std::cout << "VOL1 label verified" << std::endl;

        // Verify each file
        for (size_t i = 0; i < m_files.size(); ++i) {
            this->verifyFile(verifyFile, m_files[i], i + 1);
        }

        // Verify end of tape
        verifyFile.read(reinterpret_cast<char*>(&header), sizeof(header));
        if (header.flags1 != 0x40 || header.curblkl != 0) {
            throw std::runtime_error("Invalid end of tape marker");
        }
        verifyFile.read(reinterpret_cast<char*>(&header), sizeof(header));
        if (header.flags1 != 0x40 || header.curblkl != 0) {
            throw std::runtime_error("Invalid second end of tape marker");
        }

        std::cout << "Tape verification completed successfully" << std::endl;
    }

    void verifyFile(std::ifstream& verifyFile, const FileConfig& config, int fileNumber) {
        std::cout << "Verifying file " << fileNumber << ": " << config.inputFile << std::endl;

        // Verify HDR1 and HDR2
        verifyLabel(verifyFile, "HDR1", config.inputFile, fileNumber);
        verifyLabel(verifyFile, "HDR2", config.inputFile, fileNumber);

        // Verify data blocks
        std::ifstream originalFile(config.inputFile, std::ios::binary);
        if (!originalFile) {
            throw std::runtime_error("Unable to open original file for verification: " + config.inputFile);
        }

        size_t totalBytesVerified = 0;
        std::vector<char> originalBuffer(config.blksize);
        std::vector<uint8_t> tapeBuffer(config.blksize);
        AwsTapeBlockHeader header;

        while (originalFile) {
            originalFile.read(originalBuffer.data(), config.blksize);
            std::streamsize bytesRead = originalFile.gcount();
            if (bytesRead > 0) {
                verifyFile.read(reinterpret_cast<char*>(&header), sizeof(header));
                if (header.curblkl != bytesRead) {
                    throw std::runtime_error("Block size mismatch in file: " + config.inputFile);
                }
                verifyFile.read(reinterpret_cast<char*>(tapeBuffer.data()), bytesRead);

                if (!config.binary) {
                    for (size_t i = 0; i < bytesRead; ++i) {
                        if (tapeBuffer[i] != asciiToEbcdic(std::string(1, originalBuffer[i]))[0]) {
                            throw std::runtime_error("Data mismatch in file: " + config.inputFile);
                        }
                    }
                } else {
                    if (memcmp(tapeBuffer.data(), originalBuffer.data(), bytesRead) != 0) {
                        throw std::runtime_error("Data mismatch in file: " + config.inputFile);
                    }
                }

                totalBytesVerified += bytesRead;
            }
        }

        std::cout << "  Verified " << totalBytesVerified << " bytes" << std::endl;

        // Verify EOF1 and EOF2
        verifyLabel(verifyFile, "EOF1", config.inputFile, fileNumber);
        verifyLabel(verifyFile, "EOF2", config.inputFile, fileNumber);

        // Verify tape mark
        verifyFile.read(reinterpret_cast<char*>(&header), sizeof(header));
        if (header.flags1 != 0x40 || header.curblkl != 0) {
            throw std::runtime_error("Missing tape mark after file: " + config.inputFile);
        }
    }

    void verifyLabel(std::ifstream& verifyFile, const std::string& expectedLabel, const std::string& filename, int fileNumber) {
        AwsTapeBlockHeader header;
        std::vector<uint8_t> data(80);
        verifyFile.read(reinterpret_cast<char*>(&header), sizeof(header));
        verifyFile.read(reinterpret_cast<char*>(data.data()), 80);
        std::string label = ebcdicToAscii(data);
        if (label.substr(0, 4) != expectedLabel) {
            throw std::runtime_error("Invalid " + expectedLabel + " label for file: " + filename);
        }
        std::cout << "  " << expectedLabel << " label verified" << std::endl;
    }

    void writeTapeMark() {
        AwsTapeBlockHeader header = {0, m_prevBlockSize, 0x40, 0};
        m_outFile.write(reinterpret_cast<const char*>(&header), sizeof(header));
        m_prevBlockSize = 0;
    }

    std::vector<uint8_t> asciiToEbcdic(const std::string& ascii) {
        static const uint8_t table[256] = {
            /* 00 */   0,  1,  2,  3, 55, 45, 46, 47, 22,  5, 37, 11, 12, 13, 14, 15,
            /* 10 */  16, 17, 18, 19, 60, 61, 50, 38, 24, 25, 63, 39, 28, 29, 30, 31,
            /* 20 */  64, 79,127,123, 91,108, 80,125, 77, 93, 92, 78,107, 96, 75, 97,
            /* 30 */ 240,241,242,243,244,245,246,247,248,249,122, 94, 76,126,110,111,
            /* 40 */ 124,193,194,195,196,197,198,199,200,201,209,210,211,212,213,214,
            /* 50 */ 215,216,217,226,227,228,229,230,231,232,233, 74,224, 90, 95,109,
            /* 60 */ 121,129,130,131,132,133,134,135,136,137,145,146,147,148,149,150,
            /* 70 */ 151,152,153,162,163,164,165,166,167,168,169,192,106,208,161,  7,
            /* 80 */  32, 33, 34, 35, 36, 21,  6, 23, 40, 41, 42, 43, 44,  9, 10, 27,
            /* 90 */  48, 49, 26, 51, 52, 53, 54,  8, 56, 57, 58, 59,  4, 20, 62,255,
            /* A0 */  65,170, 74,177,159,178,106,181,187,180,154,138,176,202,175,188,
            /* B0 */ 144,143,234,250,190,160,182,179,157,218,155,139,183,184,185,171,
            /* C0 */ 100,101, 98,102, 99,103,158,104,116,113,114,115,120,117,118,119,
            /* D0 */ 172,105,237,238,235,239,236,191,128,253,254,251,252,173,174,89,
            /* E0 */  68, 69, 66, 70, 67, 71,156, 72, 84, 81, 82, 83, 88, 85, 86, 87,
            /* F0 */  140,141,142,143,144,145,146,147,148,149,186,204,205,206,207,203
        };
        std::vector<uint8_t> ebcdic(ascii.size());
        for (size_t i = 0; i < ascii.size(); ++i) {
            ebcdic[i] = table[static_cast<uint8_t>(ascii[i])];
        }
        return ebcdic;
    }

    std::string ebcdicToAscii(const std::vector<uint8_t>& ebcdic) {
        static const char ascii_table[256] = {
            // ... (include the full EBCDIC to ASCII conversion table here)
        };
        std::string ascii;
        ascii.reserve(ebcdic.size());
        for (uint8_t c : ebcdic) {
            ascii.push_back(ascii_table[c]);
        }
        return ascii;
    }

    std::string padRight(const std::string& str, size_t length) {
        if (str.length() >= length) return str.substr(0, length);
        return str + std::string(length - str.length(), ' ');
    }

    std::string padLeft(const std::string& str, size_t length) {
        if (str.length() >= length) return str.substr(0, length);
        return std::string(length - str.length(), '0') + str;
    }
};

void readConfigFile(const std::string& filename, std::vector<FileConfig>& configs) {
    std::ifstream file(filename);
    std::string line;
    while (std::getline(file, line)) {
        std::istringstream iss(line);
        FileConfig config;
        iss >> config.inputFile >> config.lrecl >> config.blksize >> config.recfm;
        config.binary = false;  // Default to text mode
        std::string token;
        while (iss >> token) {
            if (token == "BINARY") config.binary = true;
        }
        configs.push_back(config);
    }
}

int main(int argc, char* argv[]) {
    if (argc != 4) {
        std::cerr << "Usage: " << argv[0] << " <volser> <output_file> <config_file>" << std::endl;
        return 1;
    }

    std::string volser = argv[1];
    std::string outputFile = argv[2];
    std::string configFile = argv[3];

    try {
        std::vector<FileConfig> configs;
        readConfigFile(configFile, configs);

        AwsTapeMaker tapeMaker(volser, outputFile);
        for (const auto& config : configs) {
            tapeMaker.addFile(config);
        }
        tapeMaker.writeTape();

        std::cout << "AWS tape file created successfully: " << outputFile << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
