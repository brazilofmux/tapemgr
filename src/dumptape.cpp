#include <algorithm>
#include <cctype>
#include <climits>
#include <cstring>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <getopt.h>
#include <vector>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

enum class VerbosityLevel {
    Summary,
    Normal,
    Detailed,
    Debug
};

// Represents a file found on the tape
struct TapeFileInfo {
    std::string datasetName;
    std::string volumeSerial;
    char recordFormat;           // F, V, or U
    char blockAttribute;         // B, S, R, or ' '
    uint16_t blockSize;
    uint16_t recordLength;
    uint32_t blockCount;
    std::streampos fileStart;    // Position in the tape file where this file starts
    std::streampos dataStart;    // Position where the actual data blocks start
    std::streampos dataEnd;      // Position where the data blocks end
};

// Define structs
struct AwsTapeBlockHeader {
    uint16_t curblkl;
    uint16_t prvblkl;
    uint8_t flags1;
    uint8_t flags2;
};

struct VOL1Label {
    unsigned char identifier[3];
    unsigned char labelNumber;
    unsigned char volumeSerial[6];
    unsigned char reserved1;
    unsigned char vtocPointer[5];
    unsigned char reserved2[25];
    unsigned char ownerCode[10];
    unsigned char reserved3[29];
};

struct HDR1Label {
    unsigned char identifier[3];
    unsigned char labelNumber;
    unsigned char dataSetIdentifier[17];
    unsigned char dataSetSerialNumber[6];
    unsigned char volumeSequenceNumber[4];
    unsigned char dataSetSequenceNumber[4];
    unsigned char generationNumber[4];
    unsigned char versionNumber[2];
    unsigned char creationDate[6];
    unsigned char expirationDate[6];
    unsigned char dataSetSecurity;
    unsigned char blockCount[6];
    unsigned char systemCode[13];
    unsigned char reserved[3];
};

struct HDR2Label {
    unsigned char identifier[3];
    unsigned char labelNumber;
    unsigned char recordFormat;
    unsigned char blockLength[5];
    unsigned char recordLength[5];
    unsigned char tapeDensity;
    unsigned char dataSetPosition;
    unsigned char jobStepIdentification[17];
    unsigned char tapeRecordingTechnique[2];
    unsigned char controlCharacter;
    unsigned char reserved1;
    unsigned char blockAttribute;
    unsigned char reserved2[2];
    unsigned char deviceSerialNumber[6];
    unsigned char checkpointDataSetId;
    unsigned char reserved3[22];
    unsigned char largeBlockLength[10];
};

// EOF1 and EOV1 have the same structure as HDR1
using EOF1Label = HDR1Label;
using EOV1Label = HDR1Label;

// EOF2 and EOV2 have the same structure as HDR2
using EOF2Label = HDR2Label;
using EOV2Label = HDR2Label;

// EBCDIC to ASCII conversion table
const unsigned char ebcdicToAsciiTable[] = {
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

std::string trimRight(const std::string& str) {
    size_t end = str.find_last_not_of(" ");
    return (end == std::string::npos) ? "" : str.substr(0, end + 1);
}

std::string ebcdicToAsciiString(const unsigned char* ebcdicStr, size_t length) {
    std::string result(length, ' ');
    for (size_t i = 0; i < length; ++i) {
        result[i] = ebcdicToAsciiTable[ebcdicStr[i]];
    }
    return trimRight(result);
}

void printDetail(const AwsTapeBlockHeader& header, VerbosityLevel verbosity) {
    if (verbosity >= VerbosityLevel::Detailed) {
        std::cout << "Block Header:" << std::endl;
        std::cout << "  Current block length: " << header.curblkl << std::endl;
        std::cout << "  Previous block length: " << header.prvblkl << std::endl;
        std::cout << "  Flags: ";
        if (header.flags1 & 0x80) std::cout << "NEWREC ";
        if (header.flags1 & 0x40) std::cout << "TAPEMARK ";
        if (header.flags1 & 0x20) std::cout << "ENDREC ";
        std::cout << "(0x" << std::uppercase << std::hex << static_cast<int>(header.flags1) << std::dec << ")" << std::endl;
    }

    if (verbosity >= VerbosityLevel::Debug) {
        std::cout << "  Header raw data: ";
        for (size_t i = 0; i < sizeof(AwsTapeBlockHeader); ++i) {
            std::cout << std::setw(2) << std::setfill('0') << std::uppercase << std::hex
                      << static_cast<int>(reinterpret_cast<const uint8_t*>(&header)[i]) << " ";
        }
        std::cout << std::dec << std::endl;
    }
}

class AwsTapeDumper {
public:
    AwsTapeDumper(const std::string& inputFile, VerbosityLevel verbosity = VerbosityLevel::Normal);
    ~AwsTapeDumper();

    // Primary operations
    bool scanTape();                          // First pass: scan and build table of contents
    std::vector<TapeFileInfo> getFiles();     // Get information about files found on tape
    bool extractFile(const TapeFileInfo& file, const std::string& outputPath); // Extract specific file

    // Label validation methods (public to allow reuse)
    static bool validateVOL1Label(const VOL1Label& label);
    static bool validateHDR1Label(const HDR1Label& label);
    static bool validateHDR2Label(const HDR2Label& label);

    // Generates JSON configuration for all files found on tape
    json generateConfig() const;

    // Writes configuration to a file
    void writeConfig(const std::string& filename) const;

    // Schema validation
    static bool validateConfig(const json& config, std::string& error);

    // Config loading
    static json loadConfig(const std::string& filename, std::string& error);

private:
    // Internal processing methods
    bool readBlock(AwsTapeBlockHeader& header, std::vector<uint8_t>& data);
    void processVOL1Label(const VOL1Label& label);
    void processHDR1Label(const HDR1Label& label);
    void processHDR2Label(const HDR2Label& label);
    void processEOF1Label(const EOF1Label& label);
    void processEOF2Label(const EOF2Label& label);

    // File position tracking
    bool seekToFile(const TapeFileInfo& file);
    std::streampos getCurrentPosition();

    // Internal state
    std::string m_inputFile;
    std::ifstream m_tapeFile;
    VerbosityLevel m_verbosity;
    std::vector<TapeFileInfo> m_files;

    // Tape state tracking
    std::string m_currentVolser;
    uint32_t m_currentBlockCount;
    bool m_inDataBlocks;

    // Current file being processed
    TapeFileInfo m_currentFile;
};

AwsTapeDumper::AwsTapeDumper(const std::string& inputFile, VerbosityLevel verbosity)
    : m_inputFile(inputFile)
    , m_verbosity(verbosity)
    , m_currentBlockCount(0)
    , m_inDataBlocks(false) {

    m_tapeFile.open(inputFile, std::ios::binary);
    if (!m_tapeFile) {
        throw std::runtime_error("Error opening file: " + inputFile);
    }

    if (m_verbosity >= VerbosityLevel::Normal) {
        std::cout << "Processing AWSTAPE file: " << inputFile << std::endl;
    }
}

AwsTapeDumper::~AwsTapeDumper() {
    if (m_tapeFile.is_open()) {
        m_tapeFile.close();
    }
}

std::vector<TapeFileInfo> AwsTapeDumper::getFiles() {
    // If we haven't scanned yet, m_files will be empty
    if (m_files.empty()) {
        if (m_verbosity >= VerbosityLevel::Normal) {
            std::cout << "Warning: No files found. Has the tape been scanned?" << std::endl;
        }
    }

    return m_files;
}

bool AwsTapeDumper::readBlock(AwsTapeBlockHeader& header, std::vector<uint8_t>& data) {
    // Read the block header
    m_tapeFile.read(reinterpret_cast<char*>(&header), sizeof(AwsTapeBlockHeader));
    if (m_tapeFile.eof()) {
        return false;
    }

    if (header.curblkl > 0) {
        // Resize buffer and read the data
        data.resize(header.curblkl);
        m_tapeFile.read(reinterpret_cast<char*>(data.data()), header.curblkl);

        if (m_verbosity >= VerbosityLevel::Debug) {
            std::cout << "Block at position: 0x" << std::hex << (m_tapeFile.tellg() - static_cast<std::streamoff>(header.curblkl))
                      << ", size: " << std::dec << header.curblkl << " bytes" << std::endl;
        }
    } else {
        data.clear();
    }

    return true;
}

bool AwsTapeDumper::scanTape() {
    m_files.clear();
    m_currentBlockCount = 0;
    m_inDataBlocks = false;

    // Seek to beginning of file
    m_tapeFile.clear();
    m_tapeFile.seekg(0);

    AwsTapeBlockHeader header;
    std::vector<uint8_t> buffer;
    uint8_t prevFlags = 0;

    while (readBlock(header, buffer)) {
        if (m_verbosity >= VerbosityLevel::Detailed) {
            printDetail(header, m_verbosity);  // We'll keep using the existing helper for now
        }

        if (header.curblkl > 0) {
            std::string labelIdentifier = ebcdicToAsciiString(buffer.data(), 4);

            if (labelIdentifier == "VOL1") {
                const VOL1Label* vol1 = reinterpret_cast<const VOL1Label*>(buffer.data());
                if (validateVOL1Label(*vol1)) {
                    processVOL1Label(*vol1);
                }
            }
            else if (labelIdentifier == "HDR1") {
                // Start of a new file
                m_currentFile = TapeFileInfo();
                m_currentFile.fileStart = m_tapeFile.tellg() -
                    (std::streampos)(sizeof(AwsTapeBlockHeader) + header.curblkl);

                const HDR1Label* hdr1 = reinterpret_cast<const HDR1Label*>(buffer.data());
                if (validateHDR1Label(*hdr1)) {
                    processHDR1Label(*hdr1);
                }
            }
            else if (labelIdentifier == "HDR2") {
                const HDR2Label* hdr2 = reinterpret_cast<const HDR2Label*>(buffer.data());
                if (validateHDR2Label(*hdr2)) {
                    processHDR2Label(*hdr2);
                }
            }
            else if (labelIdentifier == "EOF1") {
                const EOF1Label* eof1 = reinterpret_cast<const EOF1Label*>(buffer.data());
                processEOF1Label(*eof1);
            }
            else if (labelIdentifier == "EOF2") {
                const EOF2Label* eof2 = reinterpret_cast<const EOF2Label*>(buffer.data());
                processEOF2Label(*eof2);

                // End of the current file
                m_currentFile.dataEnd = m_tapeFile.tellg() -
                    (std::streampos)(sizeof(AwsTapeBlockHeader) + header.curblkl);
                m_files.push_back(m_currentFile);
            }
            else if (m_inDataBlocks) {
                // Count data blocks between HDR and EOF labels
                m_currentBlockCount++;
            }
        }

        // Check for tape marks
        if (header.flags1 & 0x40) {
            if (m_verbosity >= VerbosityLevel::Detailed) {
                std::cout << "TAPE MARK" << std::endl;
            }

            if (prevFlags == header.flags1) {
                // Two consecutive tape marks indicate end of tape
                if (m_verbosity >= VerbosityLevel::Detailed) {
                    std::cout << "End of tape" << std::endl;
                }
                break;
            }

            // Single tape mark might indicate start of data blocks
            if (!m_inDataBlocks) {
                m_currentFile.dataStart = m_tapeFile.tellg();
                m_inDataBlocks = true;
            } else {
                m_inDataBlocks = false;
            }
        }

        prevFlags = header.flags1;
    }

    // Print summary
    if (m_verbosity >= VerbosityLevel::Summary) {
        std::cout << "\nTape Summary:" << std::endl;
        std::cout << "  Total Files: " << m_files.size() << std::endl;
        std::cout << "  Volume Serial: " << m_currentVolser << std::endl;
    }

    return !m_files.empty();
}

bool isValidVolumeSerialChar(unsigned char c) {
    return (c >= 0xC1 && c <= 0xC9) || (c >= 0xD1 && c <= 0xD9) || (c >= 0xE2 && c <= 0xE9) || (c >= 0xF0 && c <= 0xF9) || c == 0x40;
}

// Label validation and processing methods
bool AwsTapeDumper::validateVOL1Label(const VOL1Label& label) {
    bool isValid = true;

    // Check volume serial number
    if (!std::all_of(label.volumeSerial, label.volumeSerial + 6, isValidVolumeSerialChar)) {
        std::cout << "Warning: Invalid characters in volume serial number. Only alphanumeric characters and spaces are allowed." << std::endl;
        isValid = false;
    }

    // Check if volume serial is all spaces
    if (std::all_of(label.volumeSerial, label.volumeSerial + 6, [](unsigned char c) { return c == 0x40; })) {
        std::cout << "Warning: Volume serial number is blank." << std::endl;
        isValid = false;
    }

    // Check reserved fields
    if (label.reserved1 != 0x40 && label.reserved1 != 0xF0) {
        std::cout << "Warning: Reserved field 1 in VOL1 label (0x" << std::hex
                  << std::setw(2) << std::setfill('0') << std::uppercase
                  << static_cast<int>(label.reserved1)
                  << ") is neither blank nor zero." << std::dec << std::endl;
        isValid = false;
    }
    if (!std::all_of(label.reserved2, label.reserved2 + 25, [](unsigned char c) { return c == 0x40; })) {
        std::cout << "Warning: Reserved field 2 in VOL1 label is not blank." << std::endl;
        isValid = false;
    }
    if (!std::all_of(label.reserved3, label.reserved3 + 29, [](unsigned char c) { return c == 0x40; })) {
        std::cout << "Warning: Reserved field 3 in VOL1 label is not blank." << std::endl;
        isValid = false;
    }

    return isValid;
}

bool AwsTapeDumper::validateHDR1Label(const HDR1Label& label) {
    bool isValid = true;

    // Validate creation date and expiration date
    auto validateDate = [](const unsigned char* date, const char* fieldName) {
        if (!std::all_of(date, date + 6, [](unsigned char c) { return (c >= 0xF0 && c <= 0xF9) || c == 0x40; })) {
            std::cout << "Warning: Invalid " << fieldName << " format in HDR1 label. Expected format is CYYDDD." << std::endl;
            return false;
        }
        return true;
    };

    isValid &= validateDate(label.creationDate, "creation date");
    isValid &= validateDate(label.expirationDate, "expiration date");

    // Validate dataset name
    if (!std::all_of(label.dataSetIdentifier, label.dataSetIdentifier + 17,
                     [](unsigned char c) { return isValidVolumeSerialChar(c) || c == 0x4B; })) {
        std::cout << "Warning: Invalid characters in dataset name. Only alphanumeric characters, spaces, and periods are allowed." << std::endl;
        isValid = false;
    }

    // Validate dataset serial number
    if (!std::all_of(label.dataSetSerialNumber, label.dataSetSerialNumber + 6, isValidVolumeSerialChar)) {
        std::cout << "Warning: Invalid characters in dataset serial number. Only alphanumeric characters and spaces are allowed." << std::endl;
        isValid = false;
    }

    // Validate volume sequence number and dataset sequence number
    auto validateSequenceNumber = [](const unsigned char* number, const char* fieldName) {
        if (!std::all_of(number, number + 4, [](unsigned char c) { return c >= 0xF0 && c <= 0xF9; })) {
            std::cout << "Warning: Invalid " << fieldName << " in HDR1 label. Expected a 4-digit number." << std::endl;
            return false;
        }
        return true;
    };

    isValid &= validateSequenceNumber(label.volumeSequenceNumber, "volume sequence number");
    isValid &= validateSequenceNumber(label.dataSetSequenceNumber, "dataset sequence number");

    return isValid;
}

bool AwsTapeDumper::validateHDR2Label(const HDR2Label& label) {
    bool isValid = true;

    // Validate record format (F: 0xC6, V: 0xE5, U: 0xE4)
    unsigned char recordFormat = label.recordFormat;
    if (recordFormat != 0xC6 && recordFormat != 0xE5 && recordFormat != 0xE4) {
        std::cout << "Warning: Invalid record format in HDR2 label. Expected F, V, or U." << std::endl;
        std::cout << "Actual value: 0x" << std::hex << static_cast<int>(recordFormat) << std::dec << std::endl;
        isValid = false;
    }

    // Validate block length and record length
    auto validateLength = [](const unsigned char* length, const char* fieldName) {
        if (!std::all_of(length, length + 5, [](unsigned char c) { return c >= 0xF0 && c <= 0xF9; })) {
            std::cout << "Warning: Invalid " << fieldName << " in HDR2 label. Expected a 5-digit number." << std::endl;
            return false;
        }
        return true;
    };

    isValid &= validateLength(label.blockLength, "block length");
    isValid &= validateLength(label.recordLength, "record length");

    // Validate tape density
    unsigned char tapeDensity = label.tapeDensity;
    if (!((tapeDensity >= 0xF0 && tapeDensity <= 0xF9) || tapeDensity == 0x40)) {
        std::cout << "Warning: Invalid tape density in HDR2 label. Expected a digit or space." << std::endl;
        std::cout << "Actual value: 0x" << std::hex << static_cast<int>(tapeDensity) << std::dec << std::endl;
        isValid = false;
    }

    // Validate block attribute (B: 0xC2, S: 0xE2, R: 0xD9, space: 0x40)
    unsigned char blockAttribute = label.blockAttribute;
    if (blockAttribute != 0xC2 && blockAttribute != 0xE2 &&
        blockAttribute != 0xD9 && blockAttribute != 0x40) {
        std::cout << "Warning: Invalid block attribute in HDR2 label. Expected B, S, R, or space." << std::endl;
        std::cout << "Actual value: 0x" << std::hex << static_cast<int>(blockAttribute) << std::dec << std::endl;
        isValid = false;
    }

    return isValid;
}

json AwsTapeDumper::generateConfig() const {
    json config;

    // Add volume-level information
    config["volume_serial"] = m_currentVolser;
    config["owner_code"] = "TAPEOWNR";  // We could extract this from VOL1 if needed

    // Add files array
    json files = json::array();
    for (const auto& file : m_files) {
        json fileObj;
        fileObj["dataset_name"] = file.datasetName;
        fileObj["local_file"] = "";  // Empty by default, to be filled in by user

        // Construct full record format (e.g., FB, VB)
        std::string recfm(1, file.recordFormat);
        if (file.blockAttribute == 'B') {
            recfm += "B";
        } else if (file.blockAttribute == 'S') {
            recfm += "S";
        } else if (file.blockAttribute == 'R') {
            recfm += "BS";  // R means both blocked and spanned
        }
        fileObj["record_format"] = recfm;

        fileObj["record_length"] = file.recordLength;
        fileObj["block_size"] = file.blockSize;
        fileObj["block_count"] = file.blockCount;
        fileObj["binary"] = false;  // Default to false, user can modify

        files.push_back(fileObj);
    }
    config["files"] = files;

    return config;
}

void AwsTapeDumper::writeConfig(const std::string& filename) const {
    json config = generateConfig();

    std::ofstream out(filename);
    if (!out) {
        throw std::runtime_error("Unable to open output file: " + filename);
    }

    // Write with pretty printing (4 spaces indent)
    out << config.dump(4) << std::endl;
}

bool AwsTapeDumper::validateConfig(const json& config, std::string& error) {
    try {
        // Check required top-level fields
        if (!config.contains("volume_serial")) {
            error = "Missing required field 'volume_serial'";
            return false;
        }
        if (!config.contains("files")) {
            error = "Missing required field 'files'";
            return false;
        }

        // Validate volume_serial
        if (!config["volume_serial"].is_string()) {
            error = "Field 'volume_serial' must be a string";
            return false;
        }
        std::string volser = config["volume_serial"];
        if (volser.length() > 6 || !std::all_of(volser.begin(), volser.end(),
            [](char c) { return std::isupper(c) || std::isdigit(c); })) {
            error = "Invalid volume_serial format. Must be 1-6 uppercase letters or numbers";
            return false;
        }

        // Validate owner_code if present
        if (config.contains("owner_code")) {
            if (!config["owner_code"].is_string()) {
                error = "Field 'owner_code' must be a string";
                return false;
            }
            std::string owner = config["owner_code"];
            if (owner.length() > 10 || !std::all_of(owner.begin(), owner.end(),
                [](char c) { return std::isupper(c) || std::isdigit(c); })) {
                error = "Invalid owner_code format. Must be 1-10 uppercase letters or numbers";
                return false;
            }
        }

        // Validate files array
        if (!config["files"].is_array()) {
            error = "Field 'files' must be an array";
            return false;
        }

        const std::set<std::string> validRecFM = {"F", "FB", "V", "VB", "VS", "VBS", "U"};

        for (const auto& file : config["files"]) {
            // Check required fields
            for (const auto& field : {"dataset_name", "record_format", "record_length", "block_size"}) {
                if (!file.contains(field)) {
                    error = std::string("Missing required field '") + field + "' in file entry";
                    return false;
                }
            }

            // Validate dataset_name
            if (!file["dataset_name"].is_string()) {
                error = "Field 'dataset_name' must be a string";
                return false;
            }
            std::string dsn = file["dataset_name"];
            if (dsn.length() > 17 || !std::all_of(dsn.begin(), dsn.end(),
                [](char c) { return std::isupper(c) || std::isdigit(c) || c == '.'; })) {
                error = "Invalid dataset_name format. Must be 1-17 uppercase letters, numbers, or periods";
                return false;
            }

            // Validate record_format
            if (!file["record_format"].is_string()) {
                error = "Field 'record_format' must be a string";
                return false;
            }
            std::string recfm = file["record_format"];
            if (validRecFM.find(recfm) == validRecFM.end()) {
                error = "Invalid record_format. Must be one of: F, FB, V, VB, VS, VBS, U";
                return false;
            }

            // Validate numeric fields
            if (!file["record_length"].is_number_integer() ||
                file["record_length"] < 1 || file["record_length"] > 32760) {
                error = "record_length must be an integer between 1 and 32760";
                return false;
            }

            if (!file["block_size"].is_number_integer() ||
                file["block_size"] < 1 || file["block_size"] > 32760) {
                error = "block_size must be an integer between 1 and 32760";
                return false;
            }

            // Validate record format specific rules
            int reclen = file["record_length"];
            int blksize = file["block_size"];

            if (recfm == "F" && blksize != reclen) {
                error = "For F format, block size must equal record length";
                return false;
            }

            if (recfm == "FB" && (blksize % reclen != 0)) {
                error = "For FB format, block size must be a multiple of record length";
                return false;
            }

            // Validate local_file if present (required for extraction)
            if (file.contains("local_file")) {
                if (!file["local_file"].is_string()) {
                    error = "Field 'local_file' must be a string";
                    return false;
                }
                if (file["local_file"].get<std::string>().empty()) {
                    error = "Local file name must be specified for extraction";
                    return false;
                }
            }

            // Validate binary flag if present
            if (file.contains("binary") && !file["binary"].is_boolean()) {
                error = "Field 'binary' must be a boolean";
                return false;
            }

            // Validate dates if present
            auto validateDate = [&error](const json& file, const char* field) {
                if (file.contains(field)) {
                    if (!file[field].is_string()) {
                        error = std::string("Field '") + field + "' must be a string";
                        return false;
                    }
                    std::string date = file[field];
                    if (date.length() != 6 || !std::all_of(date.begin(), date.end(),
                        [](char c) { return std::isdigit(c); })) {
                        error = std::string("Invalid ") + field + " format. Must be 6 digits (CYYDDD)";
                        return false;
                    }
                }
                return true;
            };

            if (!validateDate(file, "creation_date") || !validateDate(file, "expiration_date")) {
                return false;
            }
        }

        return true;
    }
    catch (const json::exception& e) {
        error = std::string("JSON validation error: ") + e.what();
        return false;
    }
    catch (const std::exception& e) {
        error = std::string("Validation error: ") + e.what();
        return false;
    }
}

json AwsTapeDumper::loadConfig(const std::string& filename, std::string& error) {
    try {
        std::ifstream f(filename);
        if (!f.is_open()) {
            error = "Unable to open configuration file: " + filename;
            return json();
        }

        json config = json::parse(f);

        // Validate the loaded configuration
        if (!validateConfig(config, error)) {
            return json();
        }

        return config;
    }
    catch (const json::parse_error& e) {
        error = std::string("JSON parse error: ") + e.what();
        return json();
    }
    catch (const std::exception& e) {
        error = std::string("Error loading configuration: ") + e.what();
        return json();
    }
}

void AwsTapeDumper::processVOL1Label(const VOL1Label& label) {
    m_currentVolser = ebcdicToAsciiString(label.volumeSerial, 6);

    if (m_verbosity >= VerbosityLevel::Detailed) {
        std::cout << "VOL1 Label found" << std::endl;
        std::cout << "  Volume Serial: " << m_currentVolser << std::endl;
        std::cout << "  Owner Code: " << ebcdicToAsciiString(label.ownerCode, 10) << std::endl;
    }
}

void AwsTapeDumper::processHDR1Label(const HDR1Label& label) {
    m_currentFile.datasetName = ebcdicToAsciiString(label.dataSetIdentifier, 17);
    m_currentFile.volumeSerial = ebcdicToAsciiString(label.dataSetSerialNumber, 6);

    if (m_verbosity >= VerbosityLevel::Detailed) {
        std::cout << "HDR1 Label found" << std::endl;
        std::cout << "  Dataset Name: " << m_currentFile.datasetName << std::endl;
        std::cout << "  Dataset Serial Number: " << m_currentFile.volumeSerial << std::endl;
        std::cout << "  Volume Sequence Number: " << ebcdicToAsciiString(label.volumeSequenceNumber, 4) << std::endl;
        std::cout << "  Dataset Sequence Number: " << ebcdicToAsciiString(label.dataSetSequenceNumber, 4) << std::endl;
        std::cout << "  Creation Date: " << ebcdicToAsciiString(label.creationDate, 6) << std::endl;
        std::cout << "  Expiration Date: " << ebcdicToAsciiString(label.expirationDate, 6) << std::endl;
        std::cout << "  Dataset Security: " << ebcdicToAsciiTable[label.dataSetSecurity] << std::endl;
    }
}

void AwsTapeDumper::processHDR2Label(const HDR2Label& label) {
    m_currentFile.recordFormat = ebcdicToAsciiTable[label.recordFormat];
    m_currentFile.blockAttribute = ebcdicToAsciiTable[label.blockAttribute];

    // Convert EBCDIC numeric strings to integers
    std::string blksize = ebcdicToAsciiString(label.blockLength, 5);
    std::string lrecl = ebcdicToAsciiString(label.recordLength, 5);
    m_currentFile.blockSize = std::stoi(blksize);
    m_currentFile.recordLength = std::stoi(lrecl);

    if (m_verbosity >= VerbosityLevel::Detailed) {
        std::cout << "HDR2 Label found" << std::endl;
        std::cout << "  Record Format: " << m_currentFile.recordFormat << std::endl;
        std::cout << "  Block Attribute: " << m_currentFile.blockAttribute << std::endl;
        std::cout << "  Block Length: " << m_currentFile.blockSize << std::endl;
        std::cout << "  Record Length: " << m_currentFile.recordLength << std::endl;
        std::cout << "  Tape Density: " << ebcdicToAsciiTable[label.tapeDensity] << std::endl;
        std::cout << "  Job/Step: " << ebcdicToAsciiString(label.jobStepIdentification, 17) << std::endl;
        std::cout << "  Tape Recording Technique: " << ebcdicToAsciiString(label.tapeRecordingTechnique, 2) << std::endl;
        std::cout << "  Control Character: " << ebcdicToAsciiTable[label.controlCharacter] << std::endl;
        std::cout << "  Device Serial Number: " << ebcdicToAsciiString(label.deviceSerialNumber, 6) << std::endl;
    }
}

void AwsTapeDumper::processEOF1Label(const EOF1Label& label) {
    if (m_verbosity >= VerbosityLevel::Detailed) {
        std::cout << "EOF1 Label found" << std::endl;
        std::cout << "  Dataset Name: " << ebcdicToAsciiString(label.dataSetIdentifier, 17) << std::endl;
        std::string blockCount = ebcdicToAsciiString(label.blockCount, 6);
        m_currentFile.blockCount = std::stoi(blockCount);
        std::cout << "  Block Count: " << m_currentFile.blockCount << std::endl;
    }
}

void AwsTapeDumper::processEOF2Label(const EOF2Label& label) {
    if (m_verbosity >= VerbosityLevel::Detailed) {
        std::cout << "EOF2 Label found" << std::endl;
        std::cout << "  Record Format: " << ebcdicToAsciiTable[label.recordFormat] << std::endl;
        std::cout << "  Block Attribute: " << ebcdicToAsciiTable[label.blockAttribute] << std::endl;
        std::cout << "  Block Length: " << ebcdicToAsciiString(label.blockLength, 5) << std::endl;
        std::cout << "  Record Length: " << ebcdicToAsciiString(label.recordLength, 5) << std::endl;
        std::cout << "  Tape Density: " << ebcdicToAsciiTable[label.tapeDensity] << std::endl;
        std::cout << "  Job/Step: " << ebcdicToAsciiString(label.jobStepIdentification, 17) << std::endl;
        std::cout << "  Tape Recording Technique: " << ebcdicToAsciiString(label.tapeRecordingTechnique, 2) << std::endl;
        std::cout << "  Control Character: " << ebcdicToAsciiTable[label.controlCharacter] << std::endl;
        std::cout << "  Device Serial Number: " << ebcdicToAsciiString(label.deviceSerialNumber, 6) << std::endl;
    }
}

enum class OperationMode {
    Scan,       // Just examine the tape
    Init,       // Create JSON template
    Extract     // Extract files using JSON config
};

struct ProgramOptions {
    VerbosityLevel verbosity = VerbosityLevel::Normal;
    OperationMode mode = OperationMode::Scan;
    std::vector<std::string> inputFiles;
    std::string configFile;  // For extract mode
};

void parseCommandLine(int argc, char* argv[], ProgramOptions& options) {
    static struct option long_options[] = {
        {"verbose", no_argument, 0, 'v'},
        {"help", no_argument, 0, 'h'},
        {"scan", no_argument, 0, 's'},
        {"init", no_argument, 0, 'i'},
        {"extract", required_argument, 0, 'e'},
        {0, 0, 0, 0}
    };

    int opt;
    int option_index = 0;
    bool mode_set = false;

    while ((opt = getopt_long(argc, argv, "vhsie:", long_options, &option_index)) != -1) {
        switch (opt) {
            case 'v':
                if (options.verbosity < VerbosityLevel::Debug) {
                    options.verbosity = static_cast<VerbosityLevel>(static_cast<int>(options.verbosity) + 1);
                }
                break;
            case 'h':
                std::cout << "Usage: " << argv[0] << " [OPTIONS] file1 [file2 ...]\n"
                         << "Options:\n"
                         << "  -v, --verbose     Increase verbosity (can be used multiple times)\n"
                         << "  -s, --scan        Scan tape contents (default mode)\n"
                         << "  -i, --init        Create JSON configuration template\n"
                         << "  -e, --extract=FILE Extract files using JSON configuration\n"
                         << "  -h, --help        Display this help message\n"
                         << "\nModes:\n"
                         << "  scan   - Examine tape contents without creating output\n"
                         << "  init   - Create JSON template for later extraction\n"
                         << "  extract - Extract files according to JSON configuration\n"
                         << std::endl;
                exit(0);
            case 's':
                if (mode_set) {
                    std::cerr << "Error: Only one mode can be specified\n";
                    exit(1);
                }
                options.mode = OperationMode::Scan;
                mode_set = true;
                break;
            case 'i':
                if (mode_set) {
                    std::cerr << "Error: Only one mode can be specified\n";
                    exit(1);
                }
                options.mode = OperationMode::Init;
                mode_set = true;
                break;
            case 'e':
                if (mode_set) {
                    std::cerr << "Error: Only one mode can be specified\n";
                    exit(1);
                }
                options.mode = OperationMode::Extract;
                options.configFile = optarg;
                mode_set = true;
                break;
            default:
                std::cerr << "Unknown option. Use --help for usage information.\n";
                exit(1);
        }
    }

    // Collect input files
    for (int i = optind; i < argc; i++) {
        options.inputFiles.push_back(argv[i]);
    }

    if (options.inputFiles.empty()) {
        std::cerr << "Error: No input files specified. Use --help for usage information.\n";
        exit(1);
    }

    // Validate options based on mode
    if (options.mode == OperationMode::Extract && options.configFile.empty()) {
        std::cerr << "Error: Extract mode requires a configuration file\n";
        exit(1);
    }
}

int main(int argc, char* argv[]) {
    ProgramOptions options;
    parseCommandLine(argc, argv, options);

    try {
        switch (options.mode) {
            case OperationMode::Scan:
            case OperationMode::Init: {
                for (const auto& inputFile : options.inputFiles) {
                    AwsTapeDumper tapeDumper(inputFile, options.verbosity);

                    if (!tapeDumper.scanTape()) {
                        std::cerr << "Error: No valid files found on tape: " << inputFile << std::endl;
                        continue;
                    }

                    if (options.mode == OperationMode::Init) {
                        std::string configFile = inputFile + ".json";
                        tapeDumper.writeConfig(configFile);
                        if (options.verbosity >= VerbosityLevel::Normal) {
                            std::cout << "Configuration template written to: " << configFile << std::endl;
                        }
                    }

                    // Only show detailed information at higher verbosity levels
                    if (options.verbosity >= VerbosityLevel::Detailed) {
                        auto files = tapeDumper.getFiles();
                        std::cout << "\nFiles found on tape " << inputFile << ":" << std::endl;
                        for (const auto& file : files) {
                            std::cout << "  Dataset: " << file.datasetName << std::endl;
                            std::cout << "    Record Format: " << file.recordFormat
                                     << " Block Attribute: " << file.blockAttribute << std::endl;
                            std::cout << "    Block Size: " << file.blockSize
                                     << " Record Length: " << file.recordLength << std::endl;
                            std::cout << "    Block Count: " << file.blockCount << std::endl;
                        }
                    }
                }
                break;
            }

            case OperationMode::Extract: {
                std::string error;
                json config = AwsTapeDumper::loadConfig(options.configFile, error);
                if (config.is_null()) {
                    std::cerr << "Error loading configuration: " << error << std::endl;
                    return 1;
                }

                if (options.verbosity >= VerbosityLevel::Normal) {
                    std::cout << "Configuration validated successfully." << std::endl;
                    std::cout << "Found " << config["files"].size() << " files to extract." << std::endl;
                }

                // TODO: Proceed with extraction using validated config
                std::cout << "Extract mode validation complete. Extraction not yet implemented." << std::endl;
                break;
            }
        }

        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
}
