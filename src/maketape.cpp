#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <getopt.h>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "utf8tables.h"

enum class VerbosityLevel {
    Summary,
    Normal,
    Detailed,
    Debug
};

struct ProgramOptions {
    VerbosityLevel verbosity = VerbosityLevel::Normal;
    std::string volser;
    std::string outputFile;
    std::string configFile;
};

struct FileConfig {
    std::string inputFile;
    std::string datasetName;
    uint16_t lrecl;
    uint16_t blksize;
    std::string recfm;      // F, FB, V, VB, etc.
    char recordFormat;      // F, V, or U
    char blockAttribute;    // B, S, R, or ' '
    bool binary;
    size_t recordCount;
};

struct AwsTapeBlockHeader {
    uint16_t curblkl;
    uint16_t prvblkl;
    uint8_t flags1;
    uint8_t flags2;
};

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
const unsigned char utf8_FirstByte[256] =
{
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
#define UTF8_SIZE1     1
#define UTF8_SIZE2     2
#define UTF8_SIZE3     3
#define UTF8_SIZE4     4
#define UTF8_CONTINUE  5
#define UTF8_ILLEGAL   6

#define EBCDIC_SUB (63)

int calculateOptimalBlksize(int lrecl)
{
    int halfTrack = 23476; // 3380 half-track.
    int recordsPerHalfTrack = halfTrack/lrecl;
    return lrecl*recordsPerHalfTrack;
}

std::string calculateSpace(const FileConfig& config) {
    const int BYTES_PER_TRACK = 47476;  // For 3380
    const int TRACKS_PER_CYLINDER = 15;

    std::cout << "Record Count=" << config.recordCount << std::endl;

    // Calculate total bytes
    size_t totalBytes = config.recordCount * config.lrecl;

    // Calculate tracks needed
    int tracksNeeded = (totalBytes + BYTES_PER_TRACK - 1) / BYTES_PER_TRACK;

    // Add 15% for safety margin
    tracksNeeded = static_cast<int>(tracksNeeded * 1.15);

    // Convert to cylinders if it's more than one cylinder
    if (tracksNeeded > TRACKS_PER_CYLINDER) {
        int cylinders = (tracksNeeded + TRACKS_PER_CYLINDER - 1) / TRACKS_PER_CYLINDER;
        int extraTracks = tracksNeeded % TRACKS_PER_CYLINDER;
        return "CYL,(" + std::to_string(cylinders) + "," + std::to_string(cylinders/2) + ")";
    } else {
        return "TRK,(" + std::to_string(tracksNeeded) + "," + std::to_string(tracksNeeded/2) + ")";
    }
}

std::string generateMultiFileRestoreJCL(const std::vector<FileConfig>& configs) {
    std::stringstream jcl;

    // Job card
    jcl << "//REST    JOB (001),'STEPHEN DENNIS',CLASS=A,MSGLEVEL=(1,1),MSGCLASS=A\n";
    jcl << "//JOBLIB  DD  DSN=SYS1.LINKLIB,DISP=SHR\n";

    int stepNumber = 1;
    for (const auto& config : configs) {
        // Delete step
        jcl << "//STEP" << std::setfill('0') << std::setw(2) << stepNumber++ << "   EXEC PGM=IEFBR14\n";
        jcl << "//SYSPRINT DD  SYSOUT=*\n";
        jcl << "//DSN2DEL  DD  DSN=" << config.datasetName << ",DISP=(MOD,DELETE,DELETE),\n";
        jcl << "//             UNIT=3380,VOL=SER=SVD002\n";

        // Restore step
        jcl << "//STEP" << std::setfill('0') << std::setw(2) << stepNumber++ << "   EXEC PGM=IEBGENER\n";
        jcl << "//SYSPRINT DD  SYSOUT=A\n";
        jcl << "//SYSIN    DD  DUMMY\n";
        jcl << "//SYSUT1   DD  DSN=" << config.datasetName << ",UNIT=TAPE,\n";
        jcl << "//             VOL=(PRIVATE,RETAIN,SER=240001),LABEL=(" << ((stepNumber-1)/2) << ",SL),\n";
        jcl << "//             DCB=(RECFM=" << config.recfm << ",LRECL=" << config.lrecl
            << ",BLKSIZE=" << config.blksize << "),\n";
        jcl << "//             DISP=OLD\n";
        jcl << "//SYSUT2   DD  DSN=" << config.datasetName << ",UNIT=3380,\n";
        jcl << "//             VOL=SER=SVD002,DISP=(NEW,CATLG),\n";
        jcl << "//             DCB=(RECFM=" << config.recfm << ",LRECL=" << config.lrecl
            << ",DSORG=PS,BLKSIZE=" << calculateOptimalBlksize(config.lrecl) << "),\n";
        jcl << "//             SPACE=(" << calculateSpace(config) << ")\n";
    }

    return jcl.str();
}

class RecordBlockBuilder {
public:
    RecordBlockBuilder(const FileConfig& config, VerbosityLevel verbosity = VerbosityLevel::Normal)
        : m_config(config), m_verbosity(verbosity) {
        initializeNewBlock();
        if (m_verbosity >= VerbosityLevel::Detailed) {
            std::cout << "Initializing RecordBlockBuilder with:"
                      << "\n  Record Format: " << m_config.recordFormat
                      << "\n  Block Size: " << m_config.blksize
                      << "\n  Record Length: " << m_config.lrecl
                      << std::endl;
        }
    }

    std::vector<std::vector<uint8_t>> addRecord(const std::vector<uint8_t>& record) {
        std::vector<std::vector<uint8_t>> completeBlocks;

        switch(m_config.recordFormat) {
            case 'V':
                return addVariableRecord(record, false); // Not blocked
            case 'F':
                return addFixedRecord(record);
            default:
                if (m_config.recfm.find('B') != std::string::npos) {
                    if (m_config.recordFormat == 'V') {
                        return addVariableRecord(record, true); // Blocked
                    }
                }
                throw std::runtime_error("Unsupported record format: " + m_config.recfm);
        }

        return completeBlocks;
    }

    std::vector<uint8_t> flush() {
        if (m_currentBlockOffset > 4) {  // If we have any data beyond BDW
            if (m_verbosity >= VerbosityLevel::Debug) {
                std::cout << "Flushing final block of size: " << m_currentBlockOffset << std::endl;
            }
            finishCurrentBlock();
            std::vector<uint8_t> finalBlock = m_currentBlock;
            initializeNewBlock();
            return finalBlock;
        }
        return std::vector<uint8_t>();
    }

private:
    const FileConfig& m_config;
    VerbosityLevel m_verbosity;
    std::vector<uint8_t> m_currentBlock;
    size_t m_currentBlockOffset;
    int m_recordCount = 0;  // For logging

    void initializeNewBlock() {
        m_currentBlock.clear();
        m_currentBlock.resize(m_config.blksize, 0x40);  // Initialize with EBCDIC space
        m_currentBlockOffset = 4;  // Start after BDW
        m_recordCount = 0;

        if (m_verbosity >= VerbosityLevel::Debug) {
            std::cout << "Initialized new block with size: " << m_config.blksize << std::endl;
        }
    }

    std::vector<std::vector<uint8_t>> addVariableRecord(const std::vector<uint8_t>& record, bool blocked) {
        std::vector<std::vector<uint8_t>> completeBlocks;

        // Calculate record length including RDW
        uint16_t recordLength = record.size() + 4;

        if (m_verbosity >= VerbosityLevel::Debug) {
            std::cout << "Processing " << (blocked ? "VB" : "V") << " record:"
                      << "\n  Record length (with RDW): " << recordLength
                      << "\n  Current block offset: " << m_currentBlockOffset
                      << "\n  Space remaining: " << (m_config.blksize - m_currentBlockOffset)
                      << std::endl;
        }

        // For non-blocked format or if record won't fit in current block
        if (!blocked || (m_currentBlockOffset + recordLength > m_config.blksize)) {
            if (m_currentBlockOffset > 4) {  // If we have data in the current block
                finishCurrentBlock();
                completeBlocks.push_back(m_currentBlock);
                initializeNewBlock();
            }
        }

        // Safety check
        if (m_currentBlockOffset + recordLength > m_config.blksize) {
            throw std::runtime_error("Record too large for block size");
        }

        // Add RDW
        m_currentBlock[m_currentBlockOffset] = recordLength >> 8;     // Length high byte
        m_currentBlock[m_currentBlockOffset + 1] = recordLength;      // Length low byte
        m_currentBlock[m_currentBlockOffset + 2] = 0;                 // Flags
        m_currentBlock[m_currentBlockOffset + 3] = 0;                 // Reserved

        // Add record data
        if (record.size() > 0) {
            std::copy(record.begin(), record.end(),
                     m_currentBlock.begin() + m_currentBlockOffset + 4);
        }

        m_currentBlockOffset += recordLength;
        m_recordCount++;

        if (m_verbosity >= VerbosityLevel::Debug) {
            std::cout << "Added record #" << m_recordCount << " to block"
                      << "\n  New block offset: " << m_currentBlockOffset
                      << std::endl;
        }

        return completeBlocks;
    }

    std::vector<std::vector<uint8_t>> addFixedRecord(const std::vector<uint8_t>& record) {
        std::vector<std::vector<uint8_t>> completeBlocks;

        if (m_verbosity >= VerbosityLevel::Debug) {
            std::cout << "Processing fixed record:"
                      << "\n  Record length: " << record.size()
                      << "\n  Current block offset: " << m_currentBlockOffset
                      << std::endl;
        }

        // Safety check
        if (m_currentBlockOffset + record.size() > m_config.blksize) {
            finishCurrentBlock();
            completeBlocks.push_back(m_currentBlock);
            initializeNewBlock();
        }

        // For fixed records, just copy the data
        if (record.size() > 0) {
            std::copy(record.begin(), record.end(),
                     m_currentBlock.begin() + m_currentBlockOffset);
        }

        m_currentBlockOffset += record.size();
        m_recordCount++;

        // If block is full, finish it
        if (m_currentBlockOffset == m_config.blksize) {
            finishCurrentBlock();
            completeBlocks.push_back(m_currentBlock);
            initializeNewBlock();
        }

        if (m_verbosity >= VerbosityLevel::Debug) {
            std::cout << "Added fixed record #" << m_recordCount << " to block"
                      << "\n  New block offset: " << m_currentBlockOffset
                      << std::endl;
        }

        return completeBlocks;
    }

    void finishCurrentBlock() {
        // Set BDW
        uint16_t blockLength = m_currentBlockOffset;
        m_currentBlock[0] = blockLength >> 8;    // Length high byte
        m_currentBlock[1] = blockLength;         // Length low byte
        m_currentBlock[2] = 0;                   // Flags
        m_currentBlock[3] = 0;                   // Reserved

        // Ensure block is properly sized
        m_currentBlock.resize(m_currentBlockOffset);

        if (m_verbosity >= VerbosityLevel::Debug) {
            std::cout << "Finishing block:"
                      << "\n  Final length: " << blockLength
                      << "\n  Records in block: " << m_recordCount
                      << std::endl;
        }
    }
};

class AwsTapeMaker {
public:
    AwsTapeMaker(const std::string& volser, const std::string& outputFile,
                 const std::string ownerCode = "TAPEOWNER",
                 const std::string jobId = "MAJESTY/MAKETAPE",
                 VerbosityLevel verbosity = VerbosityLevel::Normal)
        : m_volser(volser), m_outputFile(outputFile),
          m_prevBlockSize(0), m_blockCount(0),
          m_ownerCode(ownerCode), m_jobId(jobId),
          m_verbosity(verbosity) {
        if (m_verbosity >= VerbosityLevel::Normal) {
            std::cout << "Initializing tape maker for volume " << volser << std::endl;
        }
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

            std::string jcl = generateMultiFileRestoreJCL(m_files);

            // Write the JCL to a file
            std::ofstream outFile("RESTORE.JCL");
            outFile << jcl;
            outFile.close();

        } catch (const std::exception& e) {
            std::cerr << "Error during tape writing: " << e.what() << std::endl;
            throw;
        }

        std::cout << "Tape writing process completed." << std::endl;
    }

private:
    VerbosityLevel m_verbosity;
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
        label += padLeft("0001", 4);                // Volume Sequence Number
        label += padLeft(std::to_string(fileNumber), 4);  // Data Set Sequence Number
        label += "0001";                            // Generation Number
        label += "00";                              // Version Number
        label += formatDate(tm);                    // Creation Date
        label += formatExpirationDate(30);          // Default expriation to 30 days from now
        label += "0";                               // Data Set Security
        label += "000000";                          // Block Count
        label += "IBM OS/VS 370";                   // System Code
        label += std::string(3, ' ');               // Reserved
        label += std::string(4, ' ');               // Block Count, High Order
        std::cout << label << std::endl;
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
        writeBlock(utf8ToEbcdic(label), 0xA0, true);
    }

    void writeFile(FileConfig& config, int fileNumber) {
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

        writeBlock(utf8ToEbcdic(hdr1), 0xA0, true);
        writeBlock(utf8ToEbcdic(hdr2), 0xA0, true);
        writeTapeMark();
    }

    void writeDataBlocks(FileConfig& config) {
        if (m_verbosity >= VerbosityLevel::Normal) {
            std::cout << "  Writing data blocks" << std::endl;
        }
        std::ifstream inFile(config.inputFile, config.binary ? std::ios::binary : std::ios::in);
        RecordBlockBuilder blockBuilder(config, m_verbosity);
        std::vector<uint8_t> record(config.lrecl);

        if (config.binary) {
            while (inFile.read(reinterpret_cast<char*>(record.data()), config.lrecl)) {
                auto completeBlocks = blockBuilder.addRecord(record);
                for (const auto& block : completeBlocks) {
                    writeBlock(block, 0xA0);
                    m_blockCount++;
                }
            }
        } else {
            std::string line;
            while (std::getline(inFile, line)) {
                // Remove Windows-style line ending if present
                if (!line.empty() && line.back() == '\r') {
                    line.pop_back();
                }
                auto ebcdicData = utf8ToEbcdic(line);
                record.assign(config.lrecl, 0x40);  // Fill with EBCDIC spaces
                std::copy(ebcdicData.begin(),
                         ebcdicData.begin() + std::min(ebcdicData.size(), (size_t)config.lrecl),
                         record.begin());

                auto completeBlocks = blockBuilder.addRecord(record);
                for (const auto& block : completeBlocks) {
                    writeBlock(block, 0xA0);
                    m_blockCount++;
                }
            }
        }

        // Handle any remaining partial block
        auto finalBlock = blockBuilder.flush();
        if (!finalBlock.empty()) {
            writeBlock(finalBlock, 0xA0);
            m_blockCount++;
        }
    }

    void writeEOFLabels(const FileConfig& config, int fileNumber) {
        std::cout << "  Writing EOF1 and EOF2 labels" << std::endl;
        writeTapeMark();
        std::string eof1 = createEOF1Label(config, fileNumber);
        std::string eof2 = createEOF2Label(config);
        writeBlock(utf8ToEbcdic(eof1), 0xA0, true);
        writeBlock(utf8ToEbcdic(eof2), 0xA0, true);
        writeTapeMark();
    }

    void writeEndOfTape() {
        std::cout << "Writing end of tape markers" << std::endl;
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

    std::vector<uint8_t> utf8ToEbcdic(const std::string& input) {
        std::vector<uint8_t> ebcdic;
        ebcdic.reserve(input.size());

        // Use string length instead of NUL termination
        const uint8_t* pString = reinterpret_cast<const uint8_t*>(input.data());
        const uint8_t* pEnd = pString + input.length();

        while (pString < pEnd) {
            const uint8_t* p = pString;
            uint8_t t = utf8_FirstByte[*p];
            if (UTF8_CONTINUE <= t) {
                // Unexpected/malformed byte.
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
                        // RUN phrase
                        if (iColumn < y) {
                            iState = tr_cp031_sbt[iOffset+1];
                            break;
                        } else {
                            iColumn = static_cast<unsigned char>(iColumn - y);
                            iOffset += 2;
                        }
                    } else {
                        // COPY phrase
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

            // Convert state to EBCDIC value
            ebcdic.push_back(static_cast<uint8_t>(iState - TR_CP031_ACCEPTING_STATES_START));

            // Move to next UTF-8 sequence
            pString = pString + t;
        }

        return ebcdic;
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
    if (!file.is_open()) {
        throw std::runtime_error("Unable to open configuration file: " + filename);
    }
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
                std::cout << "Usage: " << argv[0] << " [-v] [-vv] [-vvv] [--verbose] [--help] volser output_file config_file" << std::endl;
                std::cout << "  -v, --verbose  Increase verbosity (can be used multiple times)" << std::endl;
                std::cout << "  -h, --help     Display this help message" << std::endl;
                exit(0);
            default:
                std::cerr << "Unknown option. Use --help for usage information." << std::endl;
                exit(1);
        }
    }

    // Need exactly 3 non-option arguments
    if (argc - optind != 3) {
        std::cerr << "Error: Need volser, output file, and config file arguments" << std::endl;
        std::cerr << "Use --help for usage information." << std::endl;
        exit(1);
    }

    options.volser = argv[optind];
    options.outputFile = argv[optind + 1];
    options.configFile = argv[optind + 2];
}

// Modified main function
int main(int argc, char* argv[]) {
    try {
        ProgramOptions options;
        parseCommandLine(argc, argv, options);

        std::vector<FileConfig> configs;
        readConfigFile(options.configFile, configs);

        AwsTapeMaker tapeMaker(options.volser, options.outputFile, "TAPEOWNER", "MAJESTY/MAKETAPE", options.verbosity);
        for (const auto& config : configs) {
            tapeMaker.addFile(config);
        }
        tapeMaker.writeTape();

        std::cout << "AWS tape file created successfully: " << options.outputFile << std::endl;

        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
}
