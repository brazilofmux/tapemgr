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
        initialize_tables();
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
        std::cout << "  Writing HDR1 label (ASCII): " << hdr1 << std::endl;
        writeBlock(asciiToEbcdic(hdr1), 0xA0, true);

        std::string hdr2 = "HDR2" + config.recfm +
                           padLeft(std::to_string(config.blksize), 5) +
                           padLeft(std::to_string(config.lrecl), 5) +
                           "0" + std::string(66, ' ');
        std::cout << "  Writing HDR2 label (ASCII): " << hdr2 << std::endl;
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

        std::cout << "Wrote block: size=" << header.curblkl
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

        try {
            // Verify HDR1 and HDR2
            verifyLabel(verifyFile, "HDR1", config, config.inputFile, fileNumber);
            verifyLabel(verifyFile, "HDR2", config, config.inputFile, fileNumber);

            // Verify tapemark after headers
            AwsTapeBlockHeader header;
            verifyFile.read(reinterpret_cast<char*>(&header), sizeof(header));
            if (header.flags1 != 0x40 || header.curblkl != 0) {
                throw std::runtime_error("Missing tapemark after headers for file: " + config.inputFile);
            }
            std::cout << "  Verified tapemark after headers" << std::endl;

            // Verify data blocks
            std::ifstream originalFile(config.inputFile, std::ios::binary);
            if (!originalFile) {
                throw std::runtime_error("Unable to open original file for verification: " + config.inputFile);
            }

            size_t totalBytesVerified = 0;
            std::vector<char> originalBuffer(config.blksize);
            std::vector<uint8_t> tapeBuffer(config.blksize);

            while (originalFile) {
                originalFile.read(originalBuffer.data(), config.blksize);
                std::streamsize bytesRead = originalFile.gcount();
                if (bytesRead > 0) {
                    verifyFile.read(reinterpret_cast<char*>(&header), sizeof(header));
                    if (header.curblkl != bytesRead) {
                        std::cerr << "Block size mismatch: expected " << bytesRead << ", got " << header.curblkl << std::endl;
                        throw std::runtime_error("Block size mismatch in file: " + config.inputFile);
                    }
                    verifyFile.read(reinterpret_cast<char*>(tapeBuffer.data()), bytesRead);

                    if (!config.binary) {
                        for (size_t i = 0; i < bytesRead; ++i) {
                            if (tapeBuffer[i] != asciiToEbcdic(std::string(1, originalBuffer[i]))[0]) {
                                std::cerr << "Data mismatch at byte " << i << std::endl;
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
            verifyLabel(verifyFile, "EOF1", config, config.inputFile, fileNumber);
            verifyLabel(verifyFile, "EOF2", config, config.inputFile, fileNumber);

            // Verify tape mark
            verifyFile.read(reinterpret_cast<char*>(&header), sizeof(header));
            if (header.flags1 != 0x40 || header.curblkl != 0) {
                throw std::runtime_error("Missing tape mark after file: " + config.inputFile);
            }
        } catch (const std::exception& e) {
            std::cerr << "Error verifying file " << config.inputFile << ": " << e.what() << std::endl;
            throw;
        }
    }

    int parseNumericField(const std::string& field) {
        try {
            size_t start = field.find_first_not_of(" ");
            size_t end = field.find_last_not_of(" ");
            if (start == std::string::npos) {
                std::cout << "  Numeric field is all spaces" << std::endl;
                return 0; // All spaces
            }
            std::string trimmed = field.substr(start, end - start + 1);
            std::cout << "  Trimmed numeric field: '" << trimmed << "'" << std::endl;
            return std::stoi(trimmed);
        } catch (const std::exception& e) {
            std::cerr << "Failed to parse numeric field: '" << field << "'. Error: " << e.what() << std::endl;
            return -1;
        }
    }

    void verifyLabel(std::ifstream& verifyFile, const std::string& expectedLabel, const FileConfig& config, const std::string& filename, int fileNumber) {
        AwsTapeBlockHeader header;
        std::vector<uint8_t> data(80);
        verifyFile.read(reinterpret_cast<char*>(&header), sizeof(header));
        verifyFile.read(reinterpret_cast<char*>(data.data()), 80);

        std::cout << "Read block header: size=" << header.curblkl
                  << ", prev=" << header.prvblkl
                  << ", flags=0x" << std::hex << static_cast<int>(header.flags1) << std::dec << std::endl;

        std::cout << "Read label data (EBCDIC hex): ";
        for (uint8_t byte : data) {
            std::cout << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(byte) << " ";
        }
        std::cout << std::dec << std::endl;

        std::string label = ebcdicToAscii(data);
        std::cout << "Converted label (ASCII): " << label << std::endl;

        if (expectedLabel == "HDR2" || expectedLabel == "EOF2") {
            std::string blksizeStr = label.substr(5, 5);
            std::string lreclStr = label.substr(10, 5);
            std::cout << "  BLKSIZE string: '" << blksizeStr << "'" << std::endl;
            std::cout << "  LRECL string: '" << lreclStr << "'" << std::endl;

            int blksize = parseNumericField(blksizeStr);
            int lrecl = parseNumericField(lreclStr);
            std::cout << "  Parsed BLKSIZE: " << blksize << ", LRECL: " << lrecl << std::endl;
            if (blksize != config.blksize || lrecl != config.lrecl) {
                std::cerr << "BLKSIZE or LRECL mismatch in " << expectedLabel << " label" << std::endl;
                std::cerr << "Expected: BLKSIZE=" << config.blksize << ", LRECL=" << config.lrecl << std::endl;
                std::cerr << "Found: BLKSIZE=" << blksize << ", LRECL=" << lrecl << std::endl;
                throw std::runtime_error("BLKSIZE or LRECL mismatch in " + expectedLabel + " label");
            }
        }
        if (label.substr(0, 4) != expectedLabel) {
            std::cerr << "Expected " << expectedLabel << " label, but found: " << label.substr(0, 4) << std::endl;
            std::cerr << "Full label content: " << label << std::endl;
            throw std::runtime_error("Invalid " + expectedLabel + " label for file: " + filename);
        }
        std::cout << "  " << expectedLabel << " label verified: " << label << std::endl;
    }

    void writeTapeMark() {
        AwsTapeBlockHeader header = {0, m_prevBlockSize, 0x40, 0};
        m_outFile.write(reinterpret_cast<const char*>(&header), sizeof(header));
        m_prevBlockSize = 0;
    }

    std::vector<uint8_t> asciiToEbcdic(const std::string& ascii) {
        std::vector<uint8_t> ebcdic;
        ebcdic.reserve(ascii.size());
        for (char c : ascii) {
            ebcdic.push_back(ascii_to_ebcdic_table[static_cast<unsigned char>(c)]);
        }
        return ebcdic;
    }

    std::string ebcdicToAscii(const std::vector<uint8_t>& ebcdic) {
        std::string ascii;
        ascii.reserve(ebcdic.size());
        std::cout << "EBCDIC to ASCII conversion:" << std::endl;
        for (uint8_t c : ebcdic) {
            char asciiChar = ebcdic_to_ascii_table[c];
            ascii.push_back(asciiChar);
        }
        std::cout << std::dec;
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

    static const unsigned char ebcdic_to_ascii_table[];
    static unsigned char ascii_to_ebcdic_table[256];
    static bool tables_initialized;

    static void initialize_tables() {
        if (!tables_initialized) {
            for (int i = 0; i < 256; ++i) {
                unsigned char ascii = ebcdic_to_ascii_table[i];
                ascii_to_ebcdic_table[ascii] = i;
            }

            tables_initialized = true;
        }
    }
};

const unsigned char AwsTapeMaker::ebcdic_to_ascii_table[] = {
    "\x00\x01\x02\x03\xA6\x09\xA7\x7F\xA9\xB0\xB1\x0B\x0C\x0D\x0E\x0F"
    "\x10\x11\x12\x13\xB2\xB4\x08\xB7\x18\x19\x1A\xB8\xBA\x1D\xBB\x1F"
    "\xBD\xC0\x1C\xC1\xC2\x0A\x17\x1B\xC3\xC4\xC5\xC6\xC7\x05\x06\x07"
    "\xC8\xC9\x16\xCB\xCC\x1E\xCD\x04\xCE\xD0\xD1\xD2\x14\x15\xD3\xFC"
    "\x20\xD4\x83\x84\x85\xA0\xD5\x86\x87\xA4\xD6\x2E\x3C\x28\x2B\xD7"
    "\x26\x82\x88\x89\x8A\xA1\x8C\x8B\x8D\xD8\x21\x24\x2A\x29\x3B\x5E"
    "\x2D\x2F\xD9\x8E\xDB\xDC\xDD\x8F\x80\xA5\x7C\x2C\x25\x5F\x3E\x3F"
    "\xDE\x90\xDF\xE0\xE2\xE3\xE4\xE5\xE6\x60\x3A\x23\x40\x27\x3D\x22"
    "\xE7\x61\x62\x63\x64\x65\x66\x67\x68\x69\xAE\xAF\xE8\xE9\xEA\xEC"
    "\xF0\x6A\x6B\x6C\x6D\x6E\x6F\x70\x71\x72\xF1\xF2\x91\xF3\x92\xF4"
    "\xF5\x7E\x73\x74\x75\x76\x77\x78\x79\x7A\xAD\xA8\xF6\x5B\xF7\xF8"
    "\x9B\x9C\x9D\x9E\x9F\xB5\xB6\xAC\xAB\xB9\xAA\xB3\xBC\x5D\xBE\xBF"
    "\x7B\x41\x42\x43\x44\x45\x46\x47\x48\x49\xCA\x93\x94\x95\xA2\xCF"
    "\x7D\x4A\x4B\x4C\x4D\x4E\x4F\x50\x51\x52\xDA\x96\x81\x97\xA3\x98"
    "\x5C\xE1\x53\x54\x55\x56\x57\x58\x59\x5A\xFD\xEB\x99\xED\xEE\xEF"
    "\x30\x31\x32\x33\x34\x35\x36\x37\x38\x39\xFE\xFB\x9A\xF9\xFA\xFF"
};

unsigned char AwsTapeMaker::ascii_to_ebcdic_table[256];
bool AwsTapeMaker::tables_initialized = false;

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
