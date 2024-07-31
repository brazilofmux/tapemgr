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
    std::string datasetName;
    uint16_t lrecl;
    uint16_t blksize;
    std::string recfm;      // F, FB, V, VB, etc.
    char recordFormat;      // F, V, or U
    char blockAttribute;    // B, S, R, or ' '
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
    AwsTapeMaker(const std::string& volser, const std::string& outputFile,
                 const std::string ownerCode = "TAPEOWNER",
                 const std::string jobId = "MAJESTY/MAKETAPE")
        : m_volser(volser), m_outputFile(outputFile),
          m_prevBlockSize(0), m_blockCount(0),
          m_ownerCode(ownerCode), m_jobId(jobId) {
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
        if (config.recordFormat != 'F' && config.recordFormat != 'V' && config.recordFormat != 'U') {
            throw std::runtime_error("Invalid RECFM for file: " + config.inputFile);
        }

        // Verify Block Attribute
        if (config.blockAttribute != ' ' && config.blockAttribute != 'B' &&
            config.blockAttribute != 'S' && config.blockAttribute != 'R') {
            throw std::runtime_error("Invalid Block Attribute for file: " + config.inputFile);
        }

        m_files.push_back(config);
    }

    void writeTape() {
        std::cout << "Starting tape writing process..." << std::endl;

        try {
            writeVolumeLabel();
            for (size_t i = 0; i < m_files.size(); ++i) {
                writeFile(m_files[i], i + 1);
            }
            writeEndOfTape();
        } catch (const std::exception& e) {
            std::cerr << "Error during tape writing: " << e.what() << std::endl;
            throw;
        }

        std::cout << "Tape writing process completed." << std::endl;
    }

private:
    std::string m_volser;
    std::string m_outputFile;
    std::vector<FileConfig> m_files;
    std::ofstream m_outFile;
    uint16_t m_prevBlockSize;
    int m_blockCount;
    std::string m_ownerCode;
    std::string m_jobId;

    std::string createVOL1Label() {
        std::string label = "VOL1";
        label += padRight(m_volser, 6);       // Volume Serial Number
        label += " ";                         // Reserved
        label += std::string(5, ' ');         // VTOC Pointer (blank for tape)
        label += std::string(25, ' ');        // Reserved
        label += padRight(m_ownerCode, 10);   // Owner Name and Address Code
        label += std::string(29, ' ');        // Reserved
        return label.substr(0, 80);
    }

    std::string createHDR1Label(const FileConfig& config, int fileNumber) {
        auto now = std::chrono::system_clock::now();
        auto in_time_t = std::chrono::system_clock::to_time_t(now);
        std::tm* tm = std::localtime(&in_time_t);

        std::string label = "HDR1";
        label += padRight(config.datasetName, 17);  // Data Set Identifier
        label += padRight(m_volser, 6);             // Data Set Serial Number
        label += padLeft(std::to_string(fileNumber), 4);  // Volume Sequence Number
        label += padLeft(std::to_string(fileNumber), 4);  // Data Set Sequence Number
        label += "0001";                            // Generation Number
        label += "00";                              // Version Number
        label += formatDate(tm);                    // Creation Date
        label += formatExpirationDate(30);          // Default expriation to 30 days from now
        label += "99365";                           // Expiration Date
        label += "0";                               // Data Set Security
        label += "000000";                          // Block Count
        label += "IBM OS/VS 370";                   // System Code
        label += std::string(3, ' ');               // Reserved
        return label.substr(0, 80);
    }

    std::string formatDate(const std::tm* tm) {
        std::stringstream ss;
        ss << std::put_time(tm, "%y%j");
        return (tm->tm_year >= 100 ? "0" : " ") + ss.str();  // Century + yyddd
    }

    std::string formatExpirationDate(int daysToKeep) {
        auto now = std::chrono::system_clock::now();
        auto expiration = now + std::chrono::hours(24 * daysToKeep);
        auto in_time_t = std::chrono::system_clock::to_time_t(expiration);
        std::tm* tm = std::localtime(&in_time_t);
        return formatDate(tm);
    }

    std::string createHDR2Label(const FileConfig& config) {
        std::string label = "HDR2";
        label += config.recordFormat;               // Record Format
        label += padLeft(std::to_string(config.blksize), 5);  // Block Length
        label += padLeft(std::to_string(config.lrecl), 5);    // Record Length
        label += "0";                               // Tape Density
        label += "0";                               // Data Set Position
        label += padRight(m_jobId, 17);             // Job/Job Step Identification
        label += "  ";                              // Tape Recording Technique
        label += " ";                               // Control Character
        label += " ";                               // Reserved
        label += config.blockAttribute;
        label += std::string(2, ' ');               // Reserved
        label += std::string(6, ' ');               // Device Serial Number
        label += " ";                               // Checkpoint Data Set Identifier
        label += std::string(22, ' ');              // Reserved
        label += padLeft(std::to_string(config.blksize), 10);  // Large Block Length
        return label.substr(0, 80);
    }

    std::string createEOF1Label(const FileConfig& config, int fileNumber) {
        std::string label = createHDR1Label(config, fileNumber);
        label.replace(0, 3, "EOF");
        label.replace(54, 6, padLeft(std::to_string(m_blockCount), 6));
        return label;
    }

    std::string createEOF2Label(const FileConfig& config) {
        std::string label = createHDR2Label(config);
        label.replace(0, 3, "EOF");
        return label;
    }

    void writeVolumeLabel() {
        std::cout << "Writing VOL1 label" << std::endl;
        std::string label = createVOL1Label();
        writeBlock(asciiToEbcdic(label), 0xA0, true);
    }

    void writeFile(const FileConfig& config, int fileNumber) {
        std::cout << "Writing file " << fileNumber << ": "
                  << config.inputFile << " (Dataset: " << config.datasetName << ")" << std::endl;
        m_blockCount = 0;
        writeHeaderLabels(config, fileNumber);
        writeDataBlocks(config);
        writeEOFLabels(config, fileNumber);
    }

    void writeHeaderLabels(const FileConfig& config, int fileNumber) {
        std::cout << "  Writing HDR1 and HDR2 labels" << std::endl;
        std::string hdr1 = createHDR1Label(config, fileNumber);
        std::string hdr2 = createHDR2Label(config);

        writeBlock(asciiToEbcdic(hdr1), 0xA0, true);
        writeBlock(asciiToEbcdic(hdr2), 0xA0, true);
        writeTapeMark();
    }

    void writeDataBlocks(const FileConfig& config) {
        std::cout << "  Writing data blocks" << std::endl;
        std::ifstream inFile(config.inputFile, config.binary ? std::ios::binary : std::ios::in);
        std::vector<uint8_t> block(config.blksize, 0x40);  // Initialize with EBCDIC space
        std::vector<uint8_t> record(config.lrecl);
        size_t blockOffset = 0;

        auto writeCurrentBlock = [&]() {
            if (blockOffset > 0) {
                writeBlock(std::vector<uint8_t>(block.begin(), block.begin() + blockOffset), 0xA0);
                m_blockCount++;
                blockOffset = 0;
            }
        };

        if (config.binary) {
            while (inFile.read(reinterpret_cast<char*>(record.data()), config.lrecl)) {
                std::copy(record.begin(), record.end(), block.begin() + blockOffset);
                blockOffset += config.lrecl;
                if (blockOffset == config.blksize) {
                    writeCurrentBlock();
                }
            }
        } else {
            std::string line;
            while (std::getline(inFile, line)) {
                // Remove Windows-style line ending if present
                if (!line.empty() && line.back() == '\r') {
                    line.pop_back();
                }

                std::vector<uint8_t> ebcdicLine = asciiToEbcdic(line);
                size_t lineSize = std::min(ebcdicLine.size(), static_cast<size_t>(config.lrecl));
                std::copy(ebcdicLine.begin(), ebcdicLine.begin() + lineSize, record.begin());

                // Pad with EBCDIC spaces if necessary
                if (lineSize < static_cast<size_t>(config.lrecl)) {
                    std::fill(record.begin() + lineSize, record.end(), 0x40);
                }

                std::copy(record.begin(), record.end(), block.begin() + blockOffset);
                blockOffset += config.lrecl;
                if (blockOffset == config.blksize) {
                    writeCurrentBlock();
                }
            }
        }

        // Write any remaining partial block
        writeCurrentBlock();
    }

    void writeEOFLabels(const FileConfig& config, int fileNumber) {
        std::cout << "  Writing EOF1 and EOF2 labels" << std::endl;
        writeTapeMark();
        std::string eof1 = createEOF1Label(config, fileNumber);
        std::string eof2 = createEOF2Label(config);

        writeBlock(asciiToEbcdic(eof1), 0xA0, true);
        writeBlock(asciiToEbcdic(eof2), 0xA0, true);
    }

    void writeEndOfTape() {
        std::cout << "Writing end of tape markers" << std::endl;
        writeTapeMark();
        writeTapeMark();
    }

    void writeBlock(const std::vector<uint8_t>& data, uint8_t flags, bool isLabel = false) {
        if (isLabel && data.size() != 80) {
            throw std::runtime_error("Label block size must be 80 bytes");
        }

        AwsTapeBlockHeader header = {
            static_cast<uint16_t>(data.size()),
            m_prevBlockSize,
            flags,
            0
        };
        m_outFile.write(reinterpret_cast<const char*>(&header), sizeof(header));
        m_outFile.write(reinterpret_cast<const char*>(data.data()), data.size());
        m_prevBlockSize = header.curblkl;
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
        iss >> config.inputFile >> config.datasetName >> config.lrecl >> config.blksize >> config.recfm;

        // Set record format
        config.recordFormat = config.recfm[0];  // F, V, or U

        // Set block attribute
        config.blockAttribute = ' ';
        if (config.recfm.find('B') != std::string::npos) {
            config.blockAttribute = 'B';  // Blocked
        }
        if (config.recfm.find('S') != std::string::npos) {
            config.blockAttribute = (config.blockAttribute == 'B') ? 'R' : 'S';  // Spanned or Blocked and Spanned
        }

        // Warnings and fixups.
        if (config.recfm[0] == 'F') {
            if (config.blksize > config.lrecl) {
                if (config.recfm == "F") {
                    std::cout << "Warning: BLKSIZE > LRECL for file " << config.inputFile
                              << ". Changing RECFM from 'F' to 'FB'." << std::endl;
                    config.recfm = "FB";
                }
                config.blockAttribute = 'B';
            } else if (config.blksize == config.lrecl && config.recfm == "FB") {
                std::cout << "Note: BLKSIZE == LRECL for file " << config.inputFile
                          << ". 'FB' is accepted but 'F' would be more typical." << std::endl;
            }
        }

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

    try {
        std::string volser = argv[1];
        std::string outputFile = argv[2];
        std::string configFile = argv[3];

        std::vector<FileConfig> configs;
        readConfigFile(configFile, configs);

        AwsTapeMaker tapeMaker(volser, outputFile);
        for (const auto& config : configs) {
            tapeMaker.addFile(config);
        }
        tapeMaker.writeTape();

        std::cout << "AWS tape file created successfully: " << outputFile << std::endl;
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
}
