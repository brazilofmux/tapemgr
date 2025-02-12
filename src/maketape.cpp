#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <getopt.h>
#include <iomanip>
#include <iostream>
#include <memory>
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

// Record processing abstractions
class RecordProcessor {
public:
    virtual ~RecordProcessor() = default;

    // Main entry point for processing a record
    virtual std::vector<std::vector<uint8_t>> processRecord(const std::vector<uint8_t>& data) = 0;

    // Final cleanup/flush
    virtual std::vector<uint8_t> flush() = 0;

    // Record counting
    size_t getRecordCount() const { return m_recordCount; }

protected:
    size_t m_recordCount = 0;
    VerbosityLevel m_verbosity;
};

class BlockBuilder {
public:
    BlockBuilder(uint16_t blockSize, bool usesBDW, VerbosityLevel verbosity = VerbosityLevel::Normal)
        : m_blockSize(blockSize), m_usesBDW(usesBDW), m_verbosity(verbosity) {
        initializeNewBlock();
    }

    bool addData(const std::vector<uint8_t>& data) {
        if (!hasRoom(data.size())) {
            return false;
        }

        std::copy(data.begin(), data.end(),
                 m_currentBlock.begin() + m_currentOffset);
        m_currentOffset += data.size();
        return true;
    }

    std::vector<uint8_t> finish() {
        if (m_currentOffset <= (m_usesBDW ? 4 : 0)) {
            return std::vector<uint8_t>();
        }

        if (m_usesBDW) {
            // Write BDW
            m_currentBlock[0] = m_currentOffset >> 8;    // Length high byte
            m_currentBlock[1] = m_currentOffset;         // Length low byte
            m_currentBlock[2] = 0;                       // Flags
            m_currentBlock[3] = 0;                       // Reserved
        }

        std::vector<uint8_t> block(m_currentBlock.begin(),
                                  m_currentBlock.begin() + m_currentOffset);
        initializeNewBlock();
        return block;
    }

    bool hasRoom(size_t size) const {
        return (m_currentOffset + size) <= m_blockSize;
    }

private:
    void initializeNewBlock() {
        m_currentBlock.clear();
        m_currentBlock.resize(m_blockSize, 0x40);  // Initialize with EBCDIC space
        m_currentOffset = m_usesBDW ? 4 : 0;       // Start after BDW if used
    }

    std::vector<uint8_t> m_currentBlock;
    size_t m_currentOffset;
    uint16_t m_blockSize;
    bool m_usesBDW;
    VerbosityLevel m_verbosity;
};

class FixedRecordProcessor : public RecordProcessor {
public:
    FixedRecordProcessor(const FileConfig& config, VerbosityLevel verbosity = VerbosityLevel::Normal)
        : m_config(config), m_blockBuilder(config.blksize, false, verbosity) {
        m_verbosity = verbosity;
    }

    std::vector<std::vector<uint8_t>> processRecord(const std::vector<uint8_t>& data) override {
        std::vector<std::vector<uint8_t>> completeBlocks;
        bool isBlocked = m_config.recfm.find('B') != std::string::npos;

        // For unblocked F, each record gets its own block
        if (!isBlocked && m_blockBuilder.hasRoom(data.size())) {
            std::vector<uint8_t> block = m_blockBuilder.finish();
            if (!block.empty()) {
                completeBlocks.push_back(block);
            }
        }

        if (!m_blockBuilder.hasRoom(data.size())) {
            std::vector<uint8_t> block = m_blockBuilder.finish();
            if (!block.empty()) {
                completeBlocks.push_back(block);
            }
        }

        m_blockBuilder.addData(data);
        m_recordCount++;

        // For unblocked F, finish the block immediately
        if (!isBlocked) {
            std::vector<uint8_t> block = m_blockBuilder.finish();
            completeBlocks.push_back(block);
        }

        return completeBlocks;
    }

    std::vector<uint8_t> flush() override {
        return m_blockBuilder.finish();
    }

private:
    const FileConfig& m_config;
    BlockBuilder m_blockBuilder;
};

class VariableRecordProcessor : public RecordProcessor {
public:
    VariableRecordProcessor(const FileConfig& config, VerbosityLevel verbosity = VerbosityLevel::Normal)
        : m_config(config), m_blockBuilder(config.blksize, true, verbosity) {
        m_verbosity = verbosity;
    }

    std::vector<std::vector<uint8_t>> processRecord(const std::vector<uint8_t>& data) override {
        std::vector<std::vector<uint8_t>> completeBlocks;
        bool isBlocked = m_config.recfm.find('B') != std::string::npos;

        if (m_config.binary) {
            // For binary VB, data already includes RDW
            // Just need to handle blocking
            if (!isBlocked || !m_blockBuilder.hasRoom(data.size())) {
                std::vector<uint8_t> block = m_blockBuilder.finish();
                if (!block.empty()) {
                    completeBlocks.push_back(block);
                }
            }
            m_blockBuilder.addData(data);
        } else {
            // For text VB, need to add RDW
            uint16_t recordLength = data.size() + 4;  // Include RDW size
            std::vector<uint8_t> record(recordLength);

            // Create RDW
            record[0] = recordLength >> 8;     // Length high byte
            record[1] = recordLength;          // Length low byte
            record[2] = 0;                     // Flags
            record[3] = 0;                     // Reserved

            // Add the actual data
            std::copy(data.begin(), data.end(), record.begin() + 4);

            if (!isBlocked || !m_blockBuilder.hasRoom(recordLength)) {
                std::vector<uint8_t> block = m_blockBuilder.finish();
                if (!block.empty()) {
                    completeBlocks.push_back(block);
                }
            }
            m_blockBuilder.addData(record);
        }

        m_recordCount++;
        return completeBlocks;
    }

    std::vector<uint8_t> flush() override {
        return m_blockBuilder.finish();
    }

private:
    const FileConfig& m_config;
    BlockBuilder m_blockBuilder;
};

class SpannedRecordProcessor : public RecordProcessor {
public:
    SpannedRecordProcessor(const FileConfig& config, VerbosityLevel verbosity = VerbosityLevel::Normal)
        : m_config(config), m_blockBuilder(config.blksize, true, verbosity) {
        m_verbosity = verbosity;
    }

    std::vector<std::vector<uint8_t>> processRecord(const std::vector<uint8_t>& data) override {
        std::vector<std::vector<uint8_t>> completeBlocks;
        bool isBlocked = m_config.recfm.find('B') != std::string::npos;

        // Calculate maximum data per segment (block size minus BDW and SDW)
        size_t maxDataPerSegment = m_config.blksize - 8;  // 4 for BDW, 4 for SDW
        size_t remainingData = data.size();
        size_t dataOffset = 0;
        bool isFirstSegment = true;

        while (remainingData > 0) {
            // Calculate this segment's size
            size_t dataInSegment = std::min(remainingData, maxDataPerSegment);
            bool isLastSegment = (dataInSegment == remainingData);

            // Set segment control code
            uint8_t segmentControl = 0;
            if (isFirstSegment && isLastSegment) segmentControl = 0b00;      // Complete record
            else if (isFirstSegment) segmentControl = 0b01;                  // First segment
            else if (isLastSegment) segmentControl = 0b10;                   // Last segment
            else segmentControl = 0b11;                                      // Middle segment

            // Create segment with SDW
            std::vector<uint8_t> segment(dataInSegment + 4);  // Data plus SDW
            // Add SDW
            segment[0] = ((dataInSegment + 4) >> 8) & 0xFF;  // Length including SDW
            segment[1] = (dataInSegment + 4) & 0xFF;
            segment[2] = segmentControl;
            segment[3] = 0;  // Reserved

            // Copy data
            std::copy(data.begin() + dataOffset,
                     data.begin() + dataOffset + dataInSegment,
                     segment.begin() + 4);

            // Add to block
            if (!m_blockBuilder.hasRoom(segment.size())) {
                std::vector<uint8_t> block = m_blockBuilder.finish();
                if (!block.empty()) {
                    completeBlocks.push_back(block);
                }
            }
            m_blockBuilder.addData(segment);

            dataOffset += dataInSegment;
            remainingData -= dataInSegment;
            isFirstSegment = false;
        }

        m_recordCount++;
        return completeBlocks;
    }

    std::vector<uint8_t> flush() override {
        return m_blockBuilder.finish();
    }

private:
    const FileConfig& m_config;
    BlockBuilder m_blockBuilder;
};

static std::unique_ptr<RecordProcessor> createRecordProcessor(const FileConfig& config, VerbosityLevel verbosity) {
    if (config.recfm.find('S') != std::string::npos) {
        return std::make_unique<SpannedRecordProcessor>(config, verbosity);
    } else if (config.recordFormat == 'V') {
        return std::make_unique<VariableRecordProcessor>(config, verbosity);
    } else if (config.recordFormat == 'F') {
        return std::make_unique<FixedRecordProcessor>(config, verbosity);
    }
    throw std::runtime_error("Unsupported record format: " + config.recfm);
}

bool validateFixedBinaryFile(const std::string& filename, uint16_t lrecl) {
    std::ifstream file(filename, std::ios::binary | std::ios::ate);
    if (!file) {
        throw std::runtime_error("Unable to open binary file: " + filename);
    }

    size_t fileSize = file.tellg();
    if (fileSize % lrecl != 0) {
        throw std::runtime_error("Fixed-length binary file size (" +
                               std::to_string(fileSize) +
                               ") must be a multiple of LRECL (" +
                               std::to_string(lrecl) + ")");
    }
    return true;
}

bool validateVariableBinaryFile(const std::string& filename) {
    std::ifstream file(filename, std::ios::binary);
    if (!file) {
        throw std::runtime_error("Unable to open binary file: " + filename);
    }

    size_t fileSize = file.tellg();
    size_t offset = 0;

    while (offset < fileSize) {
        // Read RDW
        uint8_t rdw[4];
        if (!file.read(reinterpret_cast<char*>(rdw), 4)) {
            throw std::runtime_error("Invalid variable-length binary file: Missing RDW at record boundary");
        }

        // Extract length (big-endian)
        uint16_t length = (rdw[0] << 8) | rdw[1];

        // Validate RDW
        if (rdw[2] != 0 || rdw[3] != 0) {
            std::cout << "Warning: Non-zero bytes in RDW reserved field - clearing" << std::endl;
        }

        // Validate length
        if (length < 4 || offset + length > fileSize) {
            throw std::runtime_error("Malformed RDW: Record length exceeds file size");
        }

        // Skip record data
        file.seekg(length - 4, std::ios::cur);
        offset += length;
    }

    // Validate we consumed exactly the whole file
    if (offset != fileSize) {
        throw std::runtime_error("Variable-length binary file size mismatch with RDW lengths");
    }

    return true;
}

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

        // Verify input file exists
        std::ifstream testFile(config.inputFile, std::ios::binary);
        if (!testFile) {
            throw std::runtime_error("Unable to open input file: " + config.inputFile);
        }
        testFile.close();

        // Basic validation - no zero lengths
        if (config.lrecl == 0 || config.blksize == 0) {
            throw std::runtime_error("LRECL and BLKSIZE must be non-zero for file: " + config.inputFile);
        }

        // Binary mode validations
        if (config.binary) {
            if (config.recordFormat == 'F') {
                validateFixedBinaryFile(config.inputFile, config.lrecl);
            } else if (config.recordFormat == 'V') {
                validateVariableBinaryFile(config.inputFile);
            } else {
                throw std::runtime_error("Binary mode only supported for F and V formats");
            }
        }
        // Text mode validations for fixed formats
        else if (config.recordFormat == 'F') {
            if (config.recfm.find('B') != std::string::npos && config.blksize % config.lrecl != 0) {
                throw std::runtime_error("For FB format, BLKSIZE must be multiple of LRECL for file: " + config.inputFile);
            }
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
        auto processor = createRecordProcessor(config, m_verbosity);

        if (config.binary) {
            if (config.recordFormat == 'V') {
                // Handle binary VB format (reading existing RDWs)
                while (inFile) {
                    uint8_t rdw[4];
                    if (!inFile.read(reinterpret_cast<char*>(rdw), 4)) {
                        break;
                    }
                    uint16_t recordLength = (rdw[0] << 8) | rdw[1];
                    if (recordLength < 4) {
                        throw std::runtime_error("Invalid RDW length in binary VB file");
                    }
                    std::vector<uint8_t> record(recordLength);
                    std::copy(rdw, rdw + 4, record.begin());
                    if (!inFile.read(reinterpret_cast<char*>(record.data() + 4), recordLength - 4)) {
                        throw std::runtime_error("Unexpected end of file while reading VB record");
                    }
                    auto blocks = processor->processRecord(record);
                    for (const auto& block : blocks) {
                        writeBlock(block, 0xA0);
                        m_blockCount++;
                    }
                }
            } else {
                // Handle binary F/FB format
                std::vector<uint8_t> record(config.lrecl);
                while (inFile.read(reinterpret_cast<char*>(record.data()), config.lrecl)) {
                    auto blocks = processor->processRecord(record);
                    for (const auto& block : blocks) {
                        writeBlock(block, 0xA0);
                        m_blockCount++;
                    }
                }
            }
        } else {
            // Handle text format
            std::string line;
            while (std::getline(inFile, line)) {
                if (!line.empty() && line.back() == '\r') {
                    line.pop_back();
                }
                auto ebcdicData = utf8ToEbcdic(line);
                auto blocks = processor->processRecord(ebcdicData);
                for (const auto& block : blocks) {
                    writeBlock(block, 0xA0);
                    m_blockCount++;
                }
            }
        }

        // Handle any remaining partial block
        auto finalBlock = processor->flush();
        if (!finalBlock.empty()) {
            writeBlock(finalBlock, 0xA0);
            m_blockCount++;
        }

        config.recordCount = processor->getRecordCount();
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

bool validateAndFixupRecordFormat(FileConfig& config) {
    // For fixed-length records (F or FB)
    if (config.recordFormat == 'F') {
        // Case 1: F format - LRECL should equal BLKSIZE
        bool isBlocked = config.recfm.find('B') != std::string::npos;

        if (isBlocked) {
            // FB format - just verify BLKSIZE is multiple of LRECL
            if (config.blksize % config.lrecl != 0) {
                std::cout << "Error: For file " << config.inputFile
                          << " (FB format), BLKSIZE (" << config.blksize
                          << ") must be a multiple of LRECL (" << config.lrecl
                          << ")" << std::endl;
                return false;
            }
        } else {
            // F format - LRECL must equal BLKSIZE
            if (config.blksize != config.lrecl) {
                std::cout << "Warning: For file " << config.inputFile
                          << " (F format), BLKSIZE (" << config.blksize
                          << ") doesn't match LRECL (" << config.lrecl << ")" << std::endl;
                std::cout << "  Setting BLKSIZE=" << config.lrecl
                          << " to match F format requirements" << std::endl;
                config.blksize = config.lrecl;
            }
        }
    }

    // Case 2: FB format - BLKSIZE must be a multiple of LRECL
    if (config.recfm.find('B') != std::string::npos &&
        config.recordFormat == 'F') {
        if (config.blksize % config.lrecl != 0) {
            std::cout << "Error: For file " << config.inputFile
                      << " (FB format), BLKSIZE (" << config.blksize
                      << ") must be a multiple of LRECL (" << config.lrecl
                      << ")" << std::endl;

            // Calculate nearest valid BLKSIZE
            int recordsPerBlock = config.blksize / config.lrecl;
            int suggestedBlockSize = config.lrecl * recordsPerBlock;
            int nextBlockSize = config.lrecl * (recordsPerBlock + 1);

            std::cout << "  Suggestions:" << std::endl;
            std::cout << "  - BLKSIZE=" << suggestedBlockSize
                      << " (" << recordsPerBlock << " records per block)" << std::endl;
            std::cout << "  - BLKSIZE=" << nextBlockSize
                      << " (" << (recordsPerBlock + 1) << " records per block)" << std::endl;

            return false;
        }
    }

    return true;
}

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

        config.binary = false;  // Default to text mode

        std::string token;
        while (iss >> token) {
            if (token == "BINARY") config.binary = true;
        }

        configs.push_back(config);
    }

    for (auto& config : configs) {
        if (!validateAndFixupRecordFormat(config)) {
            throw std::runtime_error("Invalid record format configuration");
        }
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
