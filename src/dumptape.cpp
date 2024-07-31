#include <algorithm>
#include <climits>
#include <cstring>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <vector>
#include <stdexcept>
#include <getopt.h>
#include <cctype>

enum class VerbosityLevel {
    Summary,
    Normal,
    Detailed,
    Debug
};

struct ProgramOptions {
    VerbosityLevel verbosity = VerbosityLevel::Normal;
    std::vector<std::string> inputFiles;
};

// Define constants
constexpr int MAX_BLOCK_SIZE = 65535;
constexpr int STANDARD_LABEL_LENGTH = 80;

// Define structs
struct AwsTapeBlockHeader {
    uint16_t curblkl;
    uint16_t prvblkl;
    uint8_t flags1;
    uint8_t flags2;
};

// Define enums
enum class PrintOption {
    Summary,
    Detail
};

struct VOL1Label {
    char identifier[3];
    char labelNumber;
    char volumeSerial[6];
    char reserved1;
    char vtocPointer[5];
    char reserved2[25];
    char ownerCode[10];
    char reserved3[29];
};

struct HDR1Label {
    char identifier[3];
    char labelNumber;
    char dataSetIdentifier[17];
    char dataSetSerialNumber[6];
    char volumeSequenceNumber[4];
    char dataSetSequenceNumber[4];
    char generationNumber[4];
    char versionNumber[2];
    char creationDate[6];
    char expirationDate[6];
    char dataSetSecurity;
    char blockCount[6];
    char systemCode[13];
    char reserved[3];
};

struct HDR2Label {
    char identifier[3];
    char labelNumber;
    char recordFormat;
    char blockLength[5];
    char recordLength[5];
    char tapeDensity;
    char dataSetPosition;
    char jobStepIdentification[17];
    char tapeRecordingTechnique[2];
    char controlCharacter;
    char reserved1;
    char blockAttribute;
    char reserved2[2];
    char deviceSerialNumber[6];
    char checkpointDataSetId;
    char reserved3[22];
    char largeBlockLength[10];
};

// EOF1 and EOV1 have the same structure as HDR1
using EOF1Label = HDR1Label;
using EOV1Label = HDR1Label;

// EOF2 and EOV2 have the same structure as HDR2
using EOF2Label = HDR2Label;
using EOV2Label = HDR2Label;

// Function prototypes
void processTape(const std::string& inputFile, VerbosityLevel verbosity);
size_t readBlockHeader(std::ifstream& file, AwsTapeBlockHeader& header);
size_t readDataBlock(std::ifstream& file, std::vector<uint8_t>& buffer, int length);
void printSummary(const std::string& labelFileName, const std::string& labelFileNumber,
                  char labelRECFM, unsigned int labelBLKSIZE, unsigned int labelLRECL,
                  unsigned int labelBLKCOUNT, unsigned int auditBLKCOUNT);
void printDetail(const AwsTapeBlockHeader& header, const std::vector<uint8_t>& buffer, VerbosityLevel verbosity);
std::string ebcdicToAsciiString(const unsigned char* ebcdicStr, size_t length);

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

int safeStoi(const std::string& str, const std::string& fieldName) {
    try {
        return std::stoi(str);
    } catch (const std::invalid_argument& e) {
        std::cout << "Error: Invalid " << fieldName << ": '" << str
                  << "' is not a valid integer." << std::endl;
        return 0; // or some other appropriate default value
    } catch (const std::out_of_range& e) {
        std::cout << "Error: " << fieldName << " out of range: '" << str << "'" << std::endl;
        return 0; // or some other appropriate default value
    }
}

std::string trimRight(const std::string& str) {
    size_t end = str.find_last_not_of(" ");
    return (end == std::string::npos) ? "" : str.substr(0, end + 1);
}

void processVOL1Label(const VOL1Label& label, VerbosityLevel verbosity) {
    if (verbosity >= VerbosityLevel::Normal) {
        std::cout << "VOL1 Label found" << std::endl;
        std::cout << "  Volume Serial: " << ebcdicToAsciiString(reinterpret_cast<const unsigned char*>(label.volumeSerial), 6) << std::endl;
    }
    if (verbosity >= VerbosityLevel::Detailed) {
        std::cout << "  Owner Code: " << ebcdicToAsciiString(reinterpret_cast<const unsigned char*>(label.ownerCode), 10) << std::endl;
    }
}

void processHDR1Label(const HDR1Label& label, VerbosityLevel verbosity) {
    if (verbosity >= VerbosityLevel::Normal) {
        std::cout << "HDR1 Label found" << std::endl;
        std::cout << "  Dataset Name: " << ebcdicToAsciiString(reinterpret_cast<const unsigned char*>(label.dataSetIdentifier), 17) << std::endl;
    }
    if (verbosity >= VerbosityLevel::Detailed) {
        std::cout << "  Dataset Serial Number: " << ebcdicToAsciiString(reinterpret_cast<const unsigned char*>(label.dataSetSerialNumber), 6) << std::endl;
        std::cout << "  Volume Sequence Number: " << ebcdicToAsciiString(reinterpret_cast<const unsigned char*>(label.volumeSequenceNumber), 4) << std::endl;
        std::cout << "  Dataset Sequence Number: " << ebcdicToAsciiString(reinterpret_cast<const unsigned char*>(label.dataSetSequenceNumber), 4) << std::endl;
        std::cout << "  Creation Date: " << ebcdicToAsciiString(reinterpret_cast<const unsigned char*>(label.creationDate), 6) << std::endl;
        std::cout << "  Expiration Date: " << ebcdicToAsciiString(reinterpret_cast<const unsigned char*>(label.expirationDate), 6) << std::endl;
        std::cout << "  Dataset Security: " << ebcdicToAsciiTable[static_cast<unsigned char>(label.dataSetSecurity)] << std::endl;
    }
}

void processHDR2Label(const HDR2Label& label, VerbosityLevel verbosity) {
    if (verbosity >= VerbosityLevel::Normal) {
        std::cout << "HDR2 Label found" << std::endl;
        std::cout << "  Record Format: " << ebcdicToAsciiTable[static_cast<unsigned char>(label.recordFormat)] << std::endl;
        std::cout << "  Block Attribute: " << ebcdicToAsciiTable[static_cast<unsigned char>(label.blockAttribute)] << std::endl;
    }
    if (verbosity >= VerbosityLevel::Detailed) {
        std::cout << "  Block Length: " << ebcdicToAsciiString(reinterpret_cast<const unsigned char*>(label.blockLength), 5) << std::endl;
        std::cout << "  Record Length: " << ebcdicToAsciiString(reinterpret_cast<const unsigned char*>(label.recordLength), 5) << std::endl;
        std::cout << "  Tape Density: " << ebcdicToAsciiTable[static_cast<unsigned char>(label.tapeDensity)] << std::endl;
        std::cout << "  Job/Step: " << ebcdicToAsciiString(reinterpret_cast<const unsigned char*>(label.jobStepIdentification), 17) << std::endl;
        std::cout << "  Tape Recording Technique: " << ebcdicToAsciiString(reinterpret_cast<const unsigned char*>(label.tapeRecordingTechnique), 2) << std::endl;
        std::cout << "  Control Character: " << ebcdicToAsciiTable[static_cast<unsigned char>(label.controlCharacter)] << std::endl;
        std::cout << "  Device Serial Number: " << ebcdicToAsciiString(reinterpret_cast<const unsigned char*>(label.deviceSerialNumber), 6) << std::endl;
    }
}

void processEOF1Label(const EOF1Label& label, VerbosityLevel verbosity) {
    if (verbosity >= VerbosityLevel::Normal) {
        std::cout << "EOF1 Label found" << std::endl;
        std::cout << "  Dataset Name: " << ebcdicToAsciiString(reinterpret_cast<const unsigned char*>(label.dataSetIdentifier), 17) << std::endl;
    }
    if (verbosity >= VerbosityLevel::Detailed) {
        std::cout << "  Block Count: " << ebcdicToAsciiString(reinterpret_cast<const unsigned char*>(label.blockCount), 6) << std::endl;
    }
}

void processEOF2Label(const EOF2Label& label, VerbosityLevel verbosity) {
    if (verbosity >= VerbosityLevel::Normal) {
        std::cout << "EOF2 Label found" << std::endl;
    }
    if (verbosity >= VerbosityLevel::Detailed) {
        processHDR2Label(label, verbosity);  // EOF2 has the same structure as HDR2
    }
}

bool isValidVolumeSerialChar(unsigned char c) {
    return (c >= 0xC1 && c <= 0xC9) || (c >= 0xD1 && c <= 0xD9) || (c >= 0xE2 && c <= 0xE9) || (c >= 0xF0 && c <= 0xF9) || c == 0x40;
}

bool validateVOL1Label(const VOL1Label& label) {
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
    if (label.reserved1 != 0x40) {
        std::cout << "Warning: Reserved field 1 in VOL1 label is not blank." << std::endl;
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

bool validateHDR1Label(const HDR1Label& label) {
    bool isValid = true;

    // Validate creation date and expiration date
    auto validateDate = [](const unsigned char* date, const char* fieldName) {
        if (!std::all_of(date, date + 6, [](unsigned char c) { return (c >= 0xF0 && c <= 0xF9) || c == 0x40; })) {
            std::cout << "Warning: Invalid " << fieldName << " format in HDR1 label. Expected format is CYYDDD." << std::endl;
            return false;
        }
        return true;
    };

    isValid &= validateDate(reinterpret_cast<const unsigned char*>(label.creationDate), "creation date");
    isValid &= validateDate(reinterpret_cast<const unsigned char*>(label.expirationDate), "expiration date");

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

    isValid &= validateSequenceNumber(reinterpret_cast<const unsigned char*>(label.volumeSequenceNumber), "volume sequence number");
    isValid &= validateSequenceNumber(reinterpret_cast<const unsigned char*>(label.dataSetSequenceNumber), "dataset sequence number");

    return isValid;
}

bool validateHDR2Label(const HDR2Label& label) {
    bool isValid = true;

    // Validate record format (F: 0xC6, V: 0xE5, U: 0xE4)
    unsigned char recordFormat = static_cast<unsigned char>(label.recordFormat);
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

    isValid &= validateLength(reinterpret_cast<const unsigned char*>(label.blockLength), "block length");
    isValid &= validateLength(reinterpret_cast<const unsigned char*>(label.recordLength), "record length");

    // Validate tape density (0-9: 0xF0-0xF9, space: 0x40)
    unsigned char tapeDensity = static_cast<unsigned char>(label.tapeDensity);
    if (!((tapeDensity >= 0xF0 && tapeDensity <= 0xF9) || tapeDensity == 0x40)) {
        std::cout << "Warning: Invalid tape density in HDR2 label. Expected a digit or space." << std::endl;
        std::cout << "Actual value: 0x" << std::hex << static_cast<int>(tapeDensity) << std::dec << std::endl;
        isValid = false;
    }

    // Validate data set position (0: 0xF0, 1: 0xF1)
    unsigned char dataSetPosition = static_cast<unsigned char>(label.dataSetPosition);
    if (dataSetPosition != 0xF0 && dataSetPosition != 0xF1) {
        std::cout << "Warning: Invalid data set position in HDR2 label. Expected 0 or 1." << std::endl;
        std::cout << "Actual value: 0x" << std::hex << static_cast<int>(dataSetPosition) << std::dec << std::endl;
        isValid = false;
    }

    // Validate block attribute (B: 0xC2, S: 0xE2, R: 0xD9, space: 0x40)
    unsigned char blockAttribute = static_cast<unsigned char>(label.blockAttribute);
    if (blockAttribute != 0xC2 && blockAttribute != 0xE2 && 
        blockAttribute != 0xD9 && blockAttribute != 0x40) {
        std::cout << "Warning: Invalid block attribute in HDR2 label. Expected B, S, R, or space." << std::endl;
        std::cout << "Actual value: 0x" << std::hex << static_cast<int>(blockAttribute) << std::dec << std::endl;
        isValid = false;
    }

    return isValid;
}

void processTape(const std::string& inputFile, VerbosityLevel verbosity) {
    std::ifstream file(inputFile, std::ios::binary);
    if (!file) {
        std::cerr << "Error opening file: " << inputFile << std::endl;
        return;
    }

    if (verbosity >= VerbosityLevel::Normal) {
        std::cout << "Processing AWSTAPE file: " << inputFile << std::endl;
    }

    std::streampos filePosition = 0;  // Track file position

    AwsTapeBlockHeader header;
    std::vector<uint8_t> buffer(MAX_BLOCK_SIZE);
    uint8_t prevFlags = 0;
    unsigned int auditBLKCOUNT = 0;
    std::string labelFileName, labelFileNumber;
    char labelRECFM = ' ';
    unsigned int labelBLKSIZE = 0, labelLRECL = 0, labelBLKCOUNT = 0;
    int fileNo = 0;
    uint64_t fileBytes = 0;
    uint16_t minBlockSize = std::numeric_limits<uint16_t>::max();
    uint16_t maxBlockSize = 0;

    auto printHexDump = [](const std::vector<uint8_t>& data, size_t start, size_t length) {
        std::cout << "  Hex: ";
        for (size_t i = start; i < start + length && i < data.size(); ++i) {
            std::cout << std::setfill('0') << std::setw(2) << std::uppercase << std::hex << static_cast<int>(data[i]) << " ";
        }
        std::cout << std::dec << std::endl;
    };

    uint64_t totalBlocks = 0;
    uint64_t totalBytes = 0;
    int fileCount = 0;

    while (true) {
        std::streampos blockStart = file.tellg();

        size_t headerSize = readBlockHeader(file, header);
        if (file.eof()) break;

        if (verbosity >= VerbosityLevel::Debug) {
            std::cout << "Block starts at file position: 0x" << std::hex << blockStart << std::dec << " (" << blockStart << ")" << std::endl;
            std::cout << "Header size: " << headerSize << " bytes" << std::endl;
        }

        printDetail(header, buffer, verbosity);

        if (header.curblkl > 0) {
            size_t dataSize = readDataBlock(file, buffer, header.curblkl);

            totalBlocks++;
            totalBytes += dataSize;

            if (verbosity >= VerbosityLevel::Debug) {
                std::cout << "Data size: " << dataSize << " bytes" << std::endl;
                std::cout << "Block ends at file position: 0x" << std::hex << file.tellg() << std::dec << " (" << file.tellg() << ")" << std::endl;
            }

            std::string labelIdentifier = ebcdicToAsciiString(reinterpret_cast<const unsigned char*>(buffer.data()), 4);


            if (labelIdentifier == "VOL1") {
                const VOL1Label* vol1 = reinterpret_cast<const VOL1Label*>(buffer.data());
                if (validateVOL1Label(*vol1)) {
                    processVOL1Label(*vol1, verbosity);
                } else {
                    std::cout << "VOL1 label validation failed" << std::endl;
                }
            } else if (labelIdentifier == "HDR1") {
                const HDR1Label* hdr1 = reinterpret_cast<const HDR1Label*>(buffer.data());
                if (validateHDR1Label(*hdr1)) {
                    processHDR1Label(*hdr1, verbosity);
                    fileCount++;
                } else {
                    std::cout << "HDR1 label validation failed" << std::endl;
                }
            } else if (labelIdentifier == "HDR2") {
                const HDR2Label* hdr2 = reinterpret_cast<const HDR2Label*>(buffer.data());
                if (validateHDR2Label(*hdr2)) {
                    processHDR2Label(*hdr2, verbosity);
                } else {
                    std::cout << "HDR2 label validation failed" << std::endl;
                }
            } else if (labelIdentifier == "EOF1") {
                const EOF1Label* eof1 = reinterpret_cast<const EOF1Label*>(buffer.data());
                processEOF1Label(*eof1, verbosity);
            } else if (labelIdentifier == "EOF2") {
                const EOF2Label* eof2 = reinterpret_cast<const EOF2Label*>(buffer.data());
                processEOF2Label(*eof2, verbosity);
            } else {
                // Data block
                if (verbosity >= VerbosityLevel::Detailed) {
                    std::cout << "Data block: " << dataSize << " bytes" << std::endl;
                    if (verbosity >= VerbosityLevel::Debug) {
                        std::cout << "First 32 bytes: ";
                        for (size_t i = 0; i < std::min<size_t>(32, dataSize); ++i) {
                            std::cout << std::setw(2) << std::setfill('0') << std::hex << static_cast<int>(buffer[i]) << " ";
                        }
                        std::cout << std::dec << std::endl;
                    }
                }
            }

            if (verbosity >= VerbosityLevel::Debug) {
                std::cout << "Raw data (first 80 bytes):" << std::endl;
                for (size_t i = 0; i < std::min<size_t>(80, dataSize); ++i) {
                    std::cout << std::setw(2) << std::setfill('0') << std::hex << static_cast<int>(buffer[i]) << " ";
                    if ((i + 1) % 16 == 0) std::cout << std::endl;
                }
                std::cout << std::dec << std::endl;
            }
        }

        if (verbosity >= VerbosityLevel::Detailed) {
            if (header.flags1 & 0x40) {  // Tape mark
                std::cout << "TAPE MARK" << std::endl;
                if (prevFlags == header.flags1) {
                    std::cout << "End of tape" << std::endl;
                    break;  // Two consecutive tape marks
                }
            }
        }

        prevFlags = header.flags1;
    }

    // Print summary
    std::cout << "\nTape Summary:" << std::endl;
    std::cout << "  Total Files: " << fileCount << std::endl;
    std::cout << "  Total Blocks: " << totalBlocks << std::endl;
    std::cout << "  Total Bytes: " << totalBytes << " (" << (totalBytes / 1024.0 / 1024.0) << " MB)" << std::endl;
}

size_t readBlockHeader(std::ifstream& file, AwsTapeBlockHeader& header) {
    file.read(reinterpret_cast<char*>(&header), sizeof(AwsTapeBlockHeader));
    return file.gcount();
}

size_t readDataBlock(std::ifstream& file, std::vector<uint8_t>& buffer, int length) {
    file.read(reinterpret_cast<char*>(buffer.data()), length);
    return file.gcount();
}

void printSummary(const std::string& labelFileName, const std::string& labelFileNumber,
                  char labelRECFM, unsigned int labelBLKSIZE, unsigned int labelLRECL,
                  unsigned int labelBLKCOUNT, unsigned int auditBLKCOUNT) {
    std::cout << std::setw(4) << labelFileNumber << "  "
              << std::setw(17) << labelFileName << "   "
              << labelRECFM << "   "
              << std::setw(7) << labelBLKSIZE << " "
              << std::setw(5) << labelLRECL << "       "
              << std::setw(7) << labelBLKCOUNT << "   "
              << std::setw(7) << auditBLKCOUNT << std::endl;
}

void printDetail(const AwsTapeBlockHeader& header, const std::vector<uint8_t>& buffer, VerbosityLevel verbosity) {
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

std::string ebcdicToAsciiString(const unsigned char* ebcdicStr, size_t length) {
    std::string result(length, ' ');
    for (size_t i = 0; i < length; ++i) {
        result[i] = ebcdicToAsciiTable[ebcdicStr[i]];
    }
    return trimRight(result);
}

void parseCommandLine(int argc, char* argv[], ProgramOptions& options) {
    static struct option long_options[] = {
        {"verbose", no_argument, 0, 'v'},
        {"help", no_argument, 0, 'h'},
        {0, 0, 0, 0}
    };

    int opt;
    int option_index = 0;

    while ((opt = getopt_long(argc, argv, "vh", long_options, &option_index)) != -1) {
        switch (opt) {
            case 'v':
                if (options.verbosity < VerbosityLevel::Debug) {
                    options.verbosity = static_cast<VerbosityLevel>(static_cast<int>(options.verbosity) + 1);
                }
                break;
            case 'h':
                std::cout << "Usage: " << argv[0] << " [-v] [-vv] [-vvv] [--verbose] [--help] file1 [file2 ...]" << std::endl;
                std::cout << "  -v, --verbose  Increase verbosity (can be used multiple times)" << std::endl;
                std::cout << "  -h, --help     Display this help message" << std::endl;
                exit(0);
            default:
                std::cerr << "Unknown option. Use --help for usage information." << std::endl;
                exit(1);
        }
    }

    // Collect input files
    for (int i = optind; i < argc; i++) {
        options.inputFiles.push_back(argv[i]);
    }

    if (options.inputFiles.empty()) {
        std::cerr << "Error: No input files specified. Use --help for usage information." << std::endl;
        exit(1);
    }
}

int main(int argc, char* argv[]) {
    ProgramOptions options;
    parseCommandLine(argc, argv, options);

    for (const auto& file : options.inputFiles) {
        if (options.verbosity >= VerbosityLevel::Normal) {
            std::cout << "Processing file: " << file << std::endl;
        }
        processTape(file, options.verbosity);
    }

    return 0;
}
