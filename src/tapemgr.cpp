#include <algorithm>
#include <cctype>
#include <chrono>
#include <climits>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <getopt.h>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>


#define UTF8_SIZE1     1
#define UTF8_SIZE2     2
#define UTF8_SIZE3     3
#define UTF8_SIZE4     4
#define UTF8_CONTINUE  5
#define UTF8_ILLEGAL   6

#define TR_CP031_START_STATE (0)
#define TR_CP031_ACCEPTING_STATES_START (3)

#define EBCDIC_SUB (63)

using json = nlohmann::json;

enum class VerbosityLevel {
    Summary,
    Normal,
    Detailed,
    Debug
};

enum class OperationMode {
    Create,     // Create a tape from files (was maketape)
    Extract,    // Extract files from tape using JSON config
    Scan,       // Examine tape without extraction
    Init,       // Create JSON template from tape for later extraction
};

struct ProgramOptions {
    VerbosityLevel verbosity = VerbosityLevel::Normal;
    OperationMode mode = OperationMode::Scan;

    // Common options
    std::string configFile;    // Used by create & extract
    std::vector<std::string> inputFiles;  // Input tape(s) for scan/extract/init,

    // Create-specific options
    std::string volser;        // Volume serial for create
    std::string outputFile;    // Output tape file for create
    std::string ownerCode;     // Optional owner code for create

    // Extract-specific options
    std::string outputDir;     // Optional directory for extracted files
};

const struct option COMMON_OPTIONS[] = {
    {"verbose", no_argument, 0, 'v'},
    {"help", no_argument, 0, 'h'},
    {"config", required_argument, 0, 'c'},
    {0, 0, 0, 0}
};

void showUsage(const char* progName) {
    std::cout << "Usage: " << progName << " <command> [options] <files...>\n"
              << "\nCommands:\n"
              << "  create    Create an AWS tape file from input files\n"
              << "  extract   Extract files from an AWS tape using JSON config\n"
              << "  scan      Display contents of AWS tape file(s)\n"
              << "  init      Create JSON template from tape for later extraction\n"
              << "\nCommon Options:\n"
              << "  -v, --verbose     Increase verbosity (can be used multiple times)\n"
              << "  -h, --help        Show command-specific help\n"
              << "  -c, --config=FILE Configuration file (required for create/extract)\n"
              << "\nCreate Options:\n"
              << "  --volser=VOL      Volume serial number (required)\n"
              << "  --owner=OWNER     Owner code (default: TAPEOWNR)\n"
              << "  -o, --output=FILE Output tape file (required)\n"
              << "\nExtract Options:\n"
              << "  -d, --dir=DIR     Output directory for extracted files\n"
              << "\nExamples:\n"
              << "  " << progName << " create --volser=VOL001 -o tape.aws -c files.json input1.txt input2.dat\n"
              << "  " << progName << " extract -c config.json tape.aws\n"
              << "  " << progName << " scan tape.aws\n"
              << "  " << progName << " init -o config.json tape.aws\n";
}

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

// Block reading abstraction
class BlockReader {
public:
    virtual ~BlockReader() = default;

    // Read next block from tape
    virtual bool readNextBlock(std::vector<uint8_t>& blockData) = 0;

    // Get current block's properties
    virtual uint16_t getBlockSize() const = 0;
    virtual bool isDataBlock() const = 0;
    virtual bool isTapeMark() const = 0;

protected:
    BlockReader() = default;
};

// AWS tape-specific block reader
class AwsBlockReader : public BlockReader {
public:
    explicit AwsBlockReader(std::ifstream& tapeFile) : m_tapeFile(tapeFile) {}

    bool readNextBlock(std::vector<uint8_t>& blockData) override {
        AwsTapeBlockHeader header;
        m_tapeFile.read(reinterpret_cast<char*>(&header), sizeof(header));
        if (m_tapeFile.eof()) {
            return false;
        }

        m_currentHeader = header;

        if (header.curblkl > 0) {
            blockData.resize(header.curblkl);
            m_tapeFile.read(reinterpret_cast<char*>(blockData.data()), header.curblkl);
            return !m_tapeFile.eof();
        } else {
            blockData.clear();
            return true;
        }
    }

    uint16_t getBlockSize() const override { return m_currentHeader.curblkl; }
    bool isDataBlock() const override { return !(m_currentHeader.flags1 & 0x40); }
    bool isTapeMark() const override { return (m_currentHeader.flags1 & 0x40); }

private:
    std::ifstream& m_tapeFile;
    AwsTapeBlockHeader m_currentHeader{};
};

// Record Processing interface
class RecordProcessor {
public:
    virtual ~RecordProcessor() = default;

    // Process a block and extract records
    virtual std::vector<std::vector<uint8_t>> processBlock(const std::vector<uint8_t>& blockData) = 0;

    // Flush any remaining records (e.g., for spanned records crossing blocks)
    virtual std::vector<std::vector<uint8_t>> flush() = 0;

protected:
    RecordProcessor() = default;
};

// Fixed Record Processor
class FixedRecordProcessor : public RecordProcessor {
public:
    explicit FixedRecordProcessor(uint16_t recordLength) : m_recordLength(recordLength) {}

    std::vector<std::vector<uint8_t>> processBlock(const std::vector<uint8_t>& blockData) override {
        std::vector<std::vector<uint8_t>> records;

        // For fixed records, use the entire block - no BDW
        for (size_t offset = 0; offset < blockData.size(); offset += m_recordLength) {
            if (offset + m_recordLength > blockData.size()) {
                throw std::runtime_error("Incomplete record at end of block");
            }
            std::vector<uint8_t> record(blockData.begin() + offset,
                                      blockData.begin() + offset + m_recordLength);
            records.push_back(std::move(record));
        }

        return records;
    }

    std::vector<std::vector<uint8_t>> flush() override {
        return {}; // Fixed records don't span blocks
    }

private:
    uint16_t m_recordLength;
};

// Variable Record Processor
class VariableRecordProcessor : public RecordProcessor {
public:
    VariableRecordProcessor(uint16_t maxRecordLength) : m_maxRecordLength(maxRecordLength) {}

    std::vector<std::vector<uint8_t>> processBlock(const std::vector<uint8_t>& blockData) override {
        std::vector<std::vector<uint8_t>> records;

        // Skip BDW (first 4 bytes of block)
        size_t offset = 4;

        while (offset < blockData.size()) {
            // Get RDW
            if (offset + 4 > blockData.size()) {
                throw std::runtime_error("Incomplete RDW at end of block");
            }

            // Extract record length from RDW
            uint16_t recordLength = (blockData[offset] << 8) | blockData[offset + 1];
            if (recordLength < 4) {
                throw std::runtime_error("Invalid RDW length");
            }

            // Validate record fits in block
            if (offset + recordLength > blockData.size()) {
                throw std::runtime_error("Record extends beyond block boundary");
            }

            // Extract just the data portion (skip RDW)
            std::vector<uint8_t> record(blockData.begin() + offset + 4,
                                      blockData.begin() + offset + recordLength);
            records.push_back(std::move(record));

            offset += recordLength;
        }

        return records;
    }

    std::vector<std::vector<uint8_t>> flush() override {
        return {}; // Variable records don't span blocks
    }

private:
    uint16_t m_maxRecordLength;
};

// Spanned Record Processor
class SpannedRecordProcessor : public RecordProcessor {
public:
    SpannedRecordProcessor(uint16_t maxRecordLength) : m_maxRecordLength(maxRecordLength) {}

    std::vector<std::vector<uint8_t>> processBlock(const std::vector<uint8_t>& blockData) override {
        std::vector<std::vector<uint8_t>> completeRecords;

        // Skip BDW
        size_t offset = 4;

        while (offset < blockData.size()) {
            // Get SDW
            if (offset + 4 > blockData.size()) {
                throw std::runtime_error("Incomplete SDW at end of block");
            }

            // Extract segment length and control information from SDW
            uint16_t segmentLength = (blockData[offset] << 8) | blockData[offset + 1];
            uint8_t segmentControl = blockData[offset + 2] & 0x03;

            if (segmentLength < 4) {
                throw std::runtime_error("Invalid SDW length");
            }

            // Validate segment fits in block
            if (offset + segmentLength > blockData.size()) {
                throw std::runtime_error("Segment extends beyond block boundary");
            }

            // Process segment (skipping SDW) based on control bits
            processSegment(blockData, offset + 4, segmentLength - 4, segmentControl, completeRecords);

            offset += segmentLength;
        }

        return completeRecords;
    }

    std::vector<std::vector<uint8_t>> flush() override {
        std::vector<std::vector<uint8_t>> records;

        if (!m_currentRecord.empty()) {
            throw std::runtime_error("Incomplete spanned record at end of file");
        }

        return records;
    }

private:
    void processSegment(const std::vector<uint8_t>& blockData, size_t offset, size_t length,
                       uint8_t segmentControl, std::vector<std::vector<uint8_t>>& completeRecords) {
        switch (segmentControl) {
            case 0b00: // Complete logical record
                completeRecords.push_back(std::vector<uint8_t>(
                    blockData.begin() + offset,
                    blockData.begin() + offset + length));
                break;

            case 0b01: // First segment
                if (!m_currentRecord.empty()) {
                    throw std::runtime_error("First segment received while processing previous record");
                }
                m_currentRecord.insert(m_currentRecord.end(),
                                     blockData.begin() + offset,
                                     blockData.begin() + offset + length);
                break;

            case 0b10: // Last segment
                if (m_currentRecord.empty()) {
                    throw std::runtime_error("Last segment received without first segment");
                }
                m_currentRecord.insert(m_currentRecord.end(),
                                     blockData.begin() + offset,
                                     blockData.begin() + offset + length);
                completeRecords.push_back(std::move(m_currentRecord));
                m_currentRecord.clear();
                break;

            case 0b11: // Middle segment
                if (m_currentRecord.empty()) {
                    throw std::runtime_error("Middle segment received without first segment");
                }
                m_currentRecord.insert(m_currentRecord.end(),
                                     blockData.begin() + offset,
                                     blockData.begin() + offset + length);
                break;
        }
    }

    uint16_t m_maxRecordLength;
    std::vector<uint8_t> m_currentRecord;
};

// Update factory to include new processors
class RecordProcessorFactory {
public:
    static std::unique_ptr<RecordProcessor> create(const json& fileConfig) {
        std::string recfm = fileConfig["record_format"];
        int recordLength = fileConfig["record_length"];

        if (recfm == "F" || recfm == "FB") {
            return std::make_unique<FixedRecordProcessor>(recordLength);
        }
        if (recfm == "V" || recfm == "VB") {
            return std::make_unique<VariableRecordProcessor>(recordLength);
        }
        if (recfm == "VS" || recfm == "VBS") {
            return std::make_unique<SpannedRecordProcessor>(recordLength);
        }

        throw std::runtime_error("Unsupported record format: " + recfm);
    }
};

// Record Transformation interface
class RecordTransformer {
public:
    virtual ~RecordTransformer() = default;
    virtual std::vector<uint8_t> transform(const std::vector<uint8_t>& record) = 0;
protected:
    RecordTransformer() = default;
};

// Text record transformer (EBCDIC to UTF-8 with trimming)
class TextRecordTransformer : public RecordTransformer {
public:
    std::vector<uint8_t> transform(const std::vector<uint8_t>& record) override {
        // Convert EBCDIC to ASCII and trim trailing spaces
        std::string ascii = ebcdicToAsciiString(record.data(), record.size());
        while (!ascii.empty() && ascii.back() == ' ') {
            ascii.pop_back();
        }

        // Convert to vector<uint8_t> and add newline
        std::vector<uint8_t> result(ascii.begin(), ascii.end());
        result.push_back('\n');
        return result;
    }
};

// Binary record transformer (pass-through with optional RDW handling)
class BinaryRecordTransformer : public RecordTransformer {
public:
    explicit BinaryRecordTransformer(bool stripDescriptors = false)
        : m_stripDescriptors(stripDescriptors) {}

    std::vector<uint8_t> transform(const std::vector<uint8_t>& record) override {
        return record;  // Just pass through - all descriptor handling done by processors
    }

private:
    bool m_stripDescriptors;
};

// Factory for creating appropriate transformers
class RecordTransformerFactory {
public:
    static std::unique_ptr<RecordTransformer> create(const json& fileConfig) {
        bool isBinary = fileConfig.value("binary", false);

        if (isBinary) {
            // For variable formats, we might want to strip RDW/SDW
            bool stripDescriptors = fileConfig["record_format"].get<std::string>()[0] == 'V';
            return std::make_unique<BinaryRecordTransformer>(stripDescriptors);
        } else {
            return std::make_unique<TextRecordTransformer>();
        }
    }
};

// Output Writer interface
class OutputWriter {
public:
    virtual ~OutputWriter() = default;
    virtual void writeRecord(const std::vector<uint8_t>& record) = 0;
    virtual void close() = 0;

protected:
    OutputWriter() = default;
};

// Text file writer (handles UTF-8 with newlines)
class TextOutputWriter : public OutputWriter {
public:
    explicit TextOutputWriter(const std::string& filename) {
        m_outFile.open(filename, std::ios::out | std::ios::binary);
        if (!m_outFile) {
            throw std::runtime_error("Unable to open output file: " + filename);
        }
    }

    void writeRecord(const std::vector<uint8_t>& record) override {
        m_outFile.write(reinterpret_cast<const char*>(record.data()), record.size());
        // Note: newline should already be added by TextRecordTransformer
    }

    void close() override {
        if (m_outFile.is_open()) {
            m_outFile.close();
        }
    }

private:
    std::ofstream m_outFile;
};

// Fixed binary writer (validates record length)
class FixedBinaryWriter : public OutputWriter {
public:
    FixedBinaryWriter(const std::string& filename, uint16_t recordLength)
        : m_recordLength(recordLength) {
        m_outFile.open(filename, std::ios::out | std::ios::binary);
        if (!m_outFile) {
            throw std::runtime_error("Unable to open output file: " + filename);
        }
    }

    void writeRecord(const std::vector<uint8_t>& record) override {
        if (record.size() != m_recordLength) {
            throw std::runtime_error("Record length mismatch in fixed binary output. "
                                   "Expected: " + std::to_string(m_recordLength) +
                                   ", Got: " + std::to_string(record.size()));
        }
        m_outFile.write(reinterpret_cast<const char*>(record.data()), record.size());
    }

    void close() override {
        if (m_outFile.is_open()) {
            m_outFile.close();
        }
    }

private:
    std::ofstream m_outFile;
    uint16_t m_recordLength;
};

// Update VariableBinaryWriter to add RDWs for output if needed
class VariableBinaryWriter : public OutputWriter {
public:
    VariableBinaryWriter(const std::string& filename, bool addRDW = false)
        : m_addRDW(addRDW) {
        m_outFile.open(filename, std::ios::out | std::ios::binary);
        if (!m_outFile) {
            throw std::runtime_error("Unable to open output file: " + filename);
        }
    }

    void writeRecord(const std::vector<uint8_t>& record) override {
        if (m_addRDW) {
            // RDW format: 2 bytes length (including RDW), 1 byte flags, 1 byte reserved
            std::vector<uint8_t> withRDW(record.size() + 4);
            uint16_t totalLength = record.size() + 4;
            withRDW[0] = totalLength >> 8;
            withRDW[1] = totalLength & 0xFF;
            withRDW[2] = 0;  // Flags
            withRDW[3] = 0;  // Reserved
            std::copy(record.begin(), record.end(), withRDW.begin() + 4);
            m_outFile.write(reinterpret_cast<const char*>(withRDW.data()), withRDW.size());
        } else {
            m_outFile.write(reinterpret_cast<const char*>(record.data()), record.size());
        }
    }

    void close() override {
        if (m_outFile.is_open()) {
            m_outFile.close();
        }
    }

private:
    std::ofstream m_outFile;
    bool m_addRDW;
};

// Factory for creating appropriate output writers
class OutputWriterFactory {
public:
    static std::unique_ptr<OutputWriter> create(const json& fileConfig, const std::string& outputPath) {
        std::string recfm = fileConfig["record_format"];
        bool isBinary = fileConfig.value("binary", false);

        if (!isBinary) {
            return std::make_unique<TextOutputWriter>(outputPath);
        }

        // Binary output handling
        if (recfm[0] == 'F') {
            return std::make_unique<FixedBinaryWriter>(outputPath,
                                                     fileConfig["record_length"]);
        }

        if (recfm[0] == 'V') {
            // Determine if we need to add RDWs
            // If the record processor or transformer is configured to strip RDWs,
            // we need to add them back for the output file
            bool addRDW = fileConfig.value("strip_rdw", false);
            return std::make_unique<VariableBinaryWriter>(outputPath, addRDW);
        }

        throw std::runtime_error("Unsupported output format for record format: " + recfm);
    }
};

// Extraction Pipeline class to coordinate the components
class ExtractionPipeline {
public:
    ExtractionPipeline(std::ifstream& tapeFile, const json& fileConfig,
                      VerbosityLevel verbosity = VerbosityLevel::Normal)
        : m_verbosity(verbosity) {
        // Create all components of the pipeline
        m_blockReader = std::make_unique<AwsBlockReader>(tapeFile);
        m_recordProcessor = RecordProcessorFactory::create(fileConfig);
        m_recordTransformer = RecordTransformerFactory::create(fileConfig);

        // Create output writer
        std::string outputPath = fileConfig["local_file"].get<std::string>();
        if (outputPath.empty()) {
            throw std::runtime_error("No output file specified in configuration");
        }
        m_outputWriter = OutputWriterFactory::create(fileConfig, outputPath);

        if (m_verbosity >= VerbosityLevel::Detailed) {
            std::cout << "Extraction pipeline created for dataset: "
                     << fileConfig["dataset_name"].get<std::string>() << std::endl;
            std::cout << "  Output file: " << outputPath << std::endl;
        }
    }

    ~ExtractionPipeline() {
        if (m_outputWriter) {
            m_outputWriter->close();
        }
    }

    // Run the extraction process
    void extract() {
        std::vector<uint8_t> blockData;
        size_t blockCount = 0;
        size_t recordCount = 0;

        while (m_blockReader->readNextBlock(blockData)) {
            if (m_blockReader->isTapeMark()) {
                if (m_verbosity >= VerbosityLevel::Debug) {
                    std::cout << "Found tape mark, ending extraction" << std::endl;
                }
                break;
            }

            if (!m_blockReader->isDataBlock() || blockData.empty()) {
                continue;
            }

            blockCount++;
            if (m_verbosity >= VerbosityLevel::Debug) {
                std::cout << "Processing block " << blockCount
                         << " (size: " << blockData.size() << " bytes)" << std::endl;
            }

            try {
                // Process block into records
                auto records = m_recordProcessor->processBlock(blockData);

                // Transform and write each record
                for (const auto& record : records) {
                    auto transformedRecord = m_recordTransformer->transform(record);
                    m_outputWriter->writeRecord(transformedRecord);
                    recordCount++;
                }

            } catch (const std::exception& e) {
                std::cerr << "Error processing block " << blockCount << ": "
                         << e.what() << std::endl;
                throw;
            }
        }

        // Handle any remaining records (e.g., from spanned record processing)
        auto finalRecords = m_recordProcessor->flush();
        for (const auto& record : finalRecords) {
            auto transformedRecord = m_recordTransformer->transform(record);
            m_outputWriter->writeRecord(transformedRecord);
            recordCount++;
        }

        if (m_verbosity >= VerbosityLevel::Normal) {
            std::cout << "Extraction complete: " << std::endl
                     << "  Blocks processed: " << blockCount << std::endl
                     << "  Records extracted: " << recordCount << std::endl;
        }
    }

private:
    VerbosityLevel m_verbosity;
    std::unique_ptr<BlockReader> m_blockReader;
    std::unique_ptr<RecordProcessor> m_recordProcessor;
    std::unique_ptr<RecordTransformer> m_recordTransformer;
    std::unique_ptr<OutputWriter> m_outputWriter;
};

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

    void extractFile(const json& config);
    bool extractFiles(const json& config);

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

        // Store file position as int64_t
        fileObj["file_position"] = static_cast<int64_t>(file.dataStart);

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

            // Validate file_position if present (required for extraction)
            if (!file.contains("file_position")) {
                error = "Missing required field 'file_position' in file entry";
                return false;
            }
            if (!file["file_position"].is_number_integer()) {
                error = "Field 'file_position' must be an integer";
                return false;
            }
            // Allow only non-negative file positions
            if (file["file_position"].get<int64_t>() < 0) {
                error = "Field 'file_position' must be non-negative";
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

// In extractFile, when retrieving file position:
void AwsTapeDumper::extractFile(const json& fileConfig) {
    m_tapeFile.clear();

    // Convert int64_t to streampos
    auto fileStart = static_cast<std::streampos>(fileConfig["file_position"].get<int64_t>());
    m_tapeFile.seekg(fileStart);

    if (m_verbosity >= VerbosityLevel::Normal) {
        std::cout << "Extracting dataset: " << fileConfig["dataset_name"] << std::endl;
    }

    ExtractionPipeline pipeline(m_tapeFile, fileConfig, m_verbosity);
    pipeline.extract();
}

// Update AwsTapeDumper to support extraction mode
bool AwsTapeDumper::extractFiles(const json& config) {
    if (!config.contains("files") || !config["files"].is_array()) {
        throw std::runtime_error("Invalid configuration: missing files array");
    }

    bool success = true;
    for (const auto& fileConfig : config["files"]) {
        try {
            if (fileConfig["local_file"].empty()) {
                if (m_verbosity >= VerbosityLevel::Normal) {
                    std::cout << "Skipping dataset " << fileConfig["dataset_name"]
                             << " (no output file specified)" << std::endl;
                }
                continue;
            }

            extractFile(fileConfig);

        } catch (const std::exception& e) {
            std::cerr << "Error extracting dataset " << fileConfig["dataset_name"]
                     << ": " << e.what() << std::endl;
            success = false;
            if (m_verbosity >= VerbosityLevel::Detailed) {
                // Continue with other files in detailed mode
                continue;
            }
            break;
        }
    }

    return success;
}

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

class EbcdicUtil {
public:
    static std::vector<uint8_t> utf8ToEbcdic(const std::vector<uint8_t>& input) {
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

    static std::vector<uint8_t> utf8ToEbcdic(const std::string& input) {
        return utf8ToEbcdic(std::vector<uint8_t>(input.begin(), input.end()));
    }

private:
    static const unsigned char utf8_FirstByte[256];
    static const unsigned char tr_cp031_itt[256];
    static const unsigned short tr_cp031_sot[3];
    static const unsigned short tr_cp031_sbt[271];
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

class RecordReader {
public:
    virtual ~RecordReader() = default;
    virtual bool getNextRecord(std::vector<uint8_t>& record) = 0;
protected:
    RecordReader(const FileConfig& config, VerbosityLevel verbosity = VerbosityLevel::Normal)
        : m_config(config), m_verbosity(verbosity) {}

    const FileConfig& m_config;
    VerbosityLevel m_verbosity;
};

class TextLineReader : public RecordReader {
public:
    TextLineReader(const FileConfig& config, std::ifstream& inFile,
                  VerbosityLevel verbosity = VerbosityLevel::Normal)
        : RecordReader(config, verbosity), m_inFile(inFile) {}

    bool getNextRecord(std::vector<uint8_t>& record) override {
        std::string line;
        if (!std::getline(m_inFile, line)) {
            return false;
        }
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        record.assign(line.begin(), line.end());
        return true;
    }

private:
    std::ifstream& m_inFile;
};

class BinaryFixedReader : public RecordReader {
public:
    BinaryFixedReader(const FileConfig& config, std::ifstream& inFile,
                     VerbosityLevel verbosity = VerbosityLevel::Normal)
        : RecordReader(config, verbosity), m_inFile(inFile) {}

    bool getNextRecord(std::vector<uint8_t>& record) override {
        record.resize(m_config.lrecl);
        if (!m_inFile.read(reinterpret_cast<char*>(record.data()), m_config.lrecl)) {
            return false;
        }
        return true;
    }

private:
    std::ifstream& m_inFile;
};

class BinaryVariableReader : public RecordReader {
public:
    BinaryVariableReader(const FileConfig& config, std::ifstream& inFile,
                        VerbosityLevel verbosity = VerbosityLevel::Normal)
        : RecordReader(config, verbosity), m_inFile(inFile) {}

    bool getNextRecord(std::vector<uint8_t>& record) override {
        // Read RDW
        uint8_t rdw[4];
        if (!m_inFile.read(reinterpret_cast<char*>(rdw), 4)) {
            return false;
        }

        // Get record length from RDW (includes RDW size)
        uint16_t recordLength = (rdw[0] << 8) | rdw[1];
        if (recordLength < 4) {
            throw std::runtime_error("Invalid RDW length in binary VB file");
        }

        // Read the complete record (including RDW)
        record.resize(recordLength);
        std::copy(rdw, rdw + 4, record.begin());
        if (!m_inFile.read(reinterpret_cast<char*>(record.data() + 4), recordLength - 4)) {
            throw std::runtime_error("Unexpected end of file while reading VB record");
        }

        return true;
    }

private:
    std::ifstream& m_inFile;
};

static std::unique_ptr<RecordReader> createReader(const FileConfig& config,
                                               std::ifstream& inFile,
                                               VerbosityLevel verbosity) {
   if (config.binary) {
       if (config.recordFormat == 'V') {
           return std::make_unique<BinaryVariableReader>(config, inFile, verbosity);
       } else {
           return std::make_unique<BinaryFixedReader>(config, inFile, verbosity);
       }
   } else {
       return std::make_unique<TextLineReader>(config, inFile, verbosity);
   }
}

class RecordFormatter {
public:
    virtual ~RecordFormatter() = default;
    virtual std::vector<uint8_t> formatRecord(const std::vector<uint8_t>& rawData) = 0;

protected:
    RecordFormatter(const FileConfig& config, VerbosityLevel verbosity = VerbosityLevel::Normal)
        : m_config(config), m_verbosity(verbosity) {}

    const FileConfig& m_config;
    VerbosityLevel m_verbosity;
};

class FixedRecordFormatter : public RecordFormatter {
public:
    FixedRecordFormatter(const FileConfig& config, VerbosityLevel verbosity = VerbosityLevel::Normal)
        : RecordFormatter(config, verbosity) {}

    std::vector<uint8_t> formatRecord(const std::vector<uint8_t>& rawData) override {
        std::vector<uint8_t> formattedRecord(m_config.lrecl, 0x40);  // Initialize with EBCDIC spaces

        if (m_config.binary) {
            // Binary data - just validate length
            if (rawData.size() != m_config.lrecl) {
                throw std::runtime_error("Binary record length mismatch");
            }
            return rawData;
        } else {
            // Text data - convert to EBCDIC and pad
            auto ebcdicData = EbcdicUtil::utf8ToEbcdic(rawData);
            std::copy(ebcdicData.begin(),
                     ebcdicData.begin() + std::min(ebcdicData.size(), formattedRecord.size()),
                     formattedRecord.begin());
            return formattedRecord;
        }
    }
};

class VariableRecordFormatter : public RecordFormatter {
public:
    VariableRecordFormatter(const FileConfig& config, VerbosityLevel verbosity = VerbosityLevel::Normal)
        : RecordFormatter(config, verbosity) {}

    std::vector<uint8_t> formatRecord(const std::vector<uint8_t>& rawData) override {
        if (m_config.binary) {
            // Binary VB - validate RDW structure
            if (rawData.size() < 4) {
                throw std::runtime_error("Binary VB record too short for RDW");
            }

            uint16_t recordLength = (rawData[0] << 8) | rawData[1];
            if (recordLength != rawData.size()) {
                throw std::runtime_error("Binary VB record length mismatch with RDW");
            }
            if (recordLength > m_config.lrecl) {
                throw std::runtime_error("Binary VB record exceeds LRECL");
            }

            // RDW structure is valid, pass through
            return rawData;
        } else {
            // Text data - create RDW and convert to EBCDIC
            auto ebcdicData = EbcdicUtil::utf8ToEbcdic(rawData);
            uint16_t recordLength = ebcdicData.size() + 4;  // Add RDW size

            if (recordLength > m_config.lrecl) {
                throw std::runtime_error("Text record length would exceed LRECL");
            }

            std::vector<uint8_t> formattedRecord(recordLength);
            // Add RDW
            formattedRecord[0] = recordLength >> 8;    // Length high byte
            formattedRecord[1] = recordLength;         // Length low byte
            formattedRecord[2] = 0;                    // Flags
            formattedRecord[3] = 0;                    // Reserved

            // Copy EBCDIC data after RDW
            std::copy(ebcdicData.begin(), ebcdicData.end(),
                     formattedRecord.begin() + 4);

            return formattedRecord;
        }
    }
};

class SpannedRecordFormatter : public RecordFormatter {
public:
    SpannedRecordFormatter(const FileConfig& config, VerbosityLevel verbosity = VerbosityLevel::Normal)
        : RecordFormatter(config, verbosity) {}

    std::vector<uint8_t> formatRecord(const std::vector<uint8_t>& rawData) override {
        if (m_config.binary) {
            // Binary VS/VBS - validate and pass through
            if (rawData.size() < 4) {
                throw std::runtime_error("Binary VS record too short for SDW");
            }

            uint16_t recordLength = (rawData[0] << 8) | rawData[1];
            if (recordLength != rawData.size()) {
                throw std::runtime_error("Binary VS record length mismatch with SDW");
            }
            if (recordLength > m_config.lrecl) {
                throw std::runtime_error("Binary VS record exceeds LRECL");
            }

            // Validate segment control code
            uint8_t segmentControl = rawData[2] & 0x03;
            if (segmentControl > 0x03) {
                throw std::runtime_error("Invalid segment control code in VS record");
            }

            return rawData;
        } else {
            // Text data - create spanned record structure
            auto ebcdicData = EbcdicUtil::utf8ToEbcdic(rawData);
            uint16_t recordLength = ebcdicData.size() + 4;  // Add SDW size

            std::vector<uint8_t> formattedRecord(recordLength);
            // Add SDW
            formattedRecord[0] = recordLength >> 8;    // Length high byte
            formattedRecord[1] = recordLength;         // Length low byte
            formattedRecord[2] = 0;                    // Complete logical record (0b00)
            formattedRecord[3] = 0;                    // Reserved

            // Copy EBCDIC data after SDW
            std::copy(ebcdicData.begin(), ebcdicData.end(),
                     formattedRecord.begin() + 4);

            return formattedRecord;
        }
    }
};

static std::unique_ptr<RecordFormatter> createFormatter(const FileConfig& config,
                                                      VerbosityLevel verbosity) {
    if (config.recfm.find('S') != std::string::npos) {
        return std::make_unique<SpannedRecordFormatter>(config, verbosity);
    } else if (config.recordFormat == 'V') {
        return std::make_unique<VariableRecordFormatter>(config, verbosity);
    } else if (config.recordFormat == 'F') {
        return std::make_unique<FixedRecordFormatter>(config, verbosity);
    }
    throw std::runtime_error("Unsupported record format: " + config.recfm);
}

// Record processing abstractions
class RecordProcessor2 {
public:
    virtual ~RecordProcessor2() = default;

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

class FixedRecordProcessor2 : public RecordProcessor2 {
public:
    FixedRecordProcessor2(const FileConfig& config, VerbosityLevel verbosity = VerbosityLevel::Normal)
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

class VariableRecordProcessor2 : public RecordProcessor2 {
public:
    VariableRecordProcessor2(const FileConfig& config, VerbosityLevel verbosity = VerbosityLevel::Normal)
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

class SpannedRecordProcessor2 : public RecordProcessor2 {
public:
    SpannedRecordProcessor2(const FileConfig& config, VerbosityLevel verbosity = VerbosityLevel::Normal)
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

static std::unique_ptr<RecordProcessor2> createRecordProcessor(const FileConfig& config, VerbosityLevel verbosity) {
    if (config.recfm.find('S') != std::string::npos) {
        return std::make_unique<SpannedRecordProcessor2>(config, verbosity);
    } else if (config.recordFormat == 'V') {
        return std::make_unique<VariableRecordProcessor2>(config, verbosity);
    } else if (config.recordFormat == 'F') {
        return std::make_unique<FixedRecordProcessor2>(config, verbosity);
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
        writeBlock(EbcdicUtil::utf8ToEbcdic(label), 0xA0, true);
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

        writeBlock(EbcdicUtil::utf8ToEbcdic(hdr1), 0xA0, true);
        writeBlock(EbcdicUtil::utf8ToEbcdic(hdr2), 0xA0, true);
        writeTapeMark();
    }

    void writeDataBlocks(FileConfig& config) {
        if (m_verbosity >= VerbosityLevel::Normal) {
            std::cout << "  Writing data blocks" << std::endl;
        }

        std::ifstream inFile(config.inputFile, config.binary ? std::ios::binary : std::ios::in);
        auto reader = createReader(config, inFile, m_verbosity);
        auto formatter = createFormatter(config, m_verbosity);
        auto processor = createRecordProcessor(config, m_verbosity);

        std::vector<uint8_t> rawRecord;
        while (reader->getNextRecord(rawRecord)) {
            auto formattedRecord = formatter->formatRecord(rawRecord);
            auto blocks = processor->processRecord(formattedRecord);

            for (const auto& block : blocks) {
                writeBlock(block, 0xA0);
                m_blockCount++;
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
        writeBlock(EbcdicUtil::utf8ToEbcdic(eof1), 0xA0, true);
        writeBlock(EbcdicUtil::utf8ToEbcdic(eof2), 0xA0, true);
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

OperationMode parseCommand(const std::string& cmd) {
    if (cmd == "create") return OperationMode::Create;
    if (cmd == "extract") return OperationMode::Extract;
    if (cmd == "scan") return OperationMode::Scan;
    if (cmd == "init") return OperationMode::Init;
    throw std::runtime_error("Unknown command: " + cmd);
}

void parseCommandLine(int argc, char* argv[], ProgramOptions& options) {
    if (argc < 2) {
        showUsage(argv[0]);
        exit(1);
    }

    // First argument after program name should be the command
    try {
        options.mode = parseCommand(argv[1]);
    } catch (const std::runtime_error& e) {
        std::cerr << e.what() << "\nUse --help for usage information.\n";
        exit(1);
    }

    // Skip program name and command for getopt
    optind = 2;

    // Define common and command-specific options
    static struct option long_options[] = {
        {"verbose", no_argument, 0, 'v'},
        {"help", no_argument, 0, 'h'},
        {"config", required_argument, 0, 'c'},
        {"volser", required_argument, 0, 'V'},
        {"owner", required_argument, 0, 'w'},
        {"output", required_argument, 0, 'o'},
        {"dir", required_argument, 0, 'd'},
        {0, 0, 0, 0}
    };

    std::string optstring = "vhc:o:d:";
    int opt;
    int option_index = 0;

    while ((opt = getopt_long(argc, argv, optstring.c_str(), long_options, &option_index)) != -1) {
        switch (opt) {
            case 'v':
                if (options.verbosity < VerbosityLevel::Debug) {
                    options.verbosity = static_cast<VerbosityLevel>(static_cast<int>(options.verbosity) + 1);
                }
                break;
            case 'h':
                showUsage(argv[0]);
                exit(0);
            case 'c':
                options.configFile = optarg;
                break;
            case 'V':
                options.volser = optarg;
                break;
            case 'w':
                options.ownerCode = optarg;
                break;
            case 'o':
                options.outputFile = optarg;
                break;
            case 'd':
                options.outputDir = optarg;
                break;
            default:
                std::cerr << "Unknown option. Use --help for usage information.\n";
                exit(1);
        }
    }

    // Collect remaining arguments as input files
    while (optind < argc) {
        options.inputFiles.push_back(argv[optind++]);
    }

    // Validate options based on mode
    switch (options.mode) {
        case OperationMode::Create:
            if (options.volser.empty()) {
                std::cerr << "Error: --volser is required for create mode\n";
                exit(1);
            }
            if (options.outputFile.empty()) {
                std::cerr << "Error: --output is required for create mode\n";
                exit(1);
            }
            if (options.configFile.empty()) {
                std::cerr << "Error: --config is required for create mode\n";
                exit(1);
            }
            break;

        case OperationMode::Extract:
            if (options.configFile.empty()) {
                std::cerr << "Error: --config is required for extract mode\n";
                exit(1);
            }
            if (options.inputFiles.empty()) {
                std::cerr << "Error: Input tape file required for extract mode\n";
                exit(1);
            }
            break;

        case OperationMode::Scan:
        case OperationMode::Init:
            if (options.inputFiles.empty()) {
                std::cerr << "Error: Input tape file(s) required\n";
                exit(1);
            }
            break;
    }
}

int main(int argc, char* argv[]) {
    try {
        ProgramOptions options;
        parseCommandLine(argc, argv, options);

        switch (options.mode) {
            case OperationMode::Create: {
                // Create mode (former maketape functionality)
                if (options.verbosity >= VerbosityLevel::Normal) {
                    std::cout << "Creating AWS tape file: " << options.outputFile << std::endl;
                }

                std::string error;
                json config = AwsTapeDumper::loadConfig(options.configFile, error);
                if (config.is_null()) {
                    throw std::runtime_error("Error loading configuration: " + error);
                }

                AwsTapeMaker tapeMaker(options.volser, options.outputFile,
                                     options.ownerCode.empty() ? "TAPEOWNER" : options.ownerCode,
                                     "TAPEMGR/CREATE", options.verbosity);

                for (const auto& fileConfig : config["files"]) {
                    FileConfig fc;
                    fc.inputFile = fileConfig["local_file"].get<std::string>();
                    fc.datasetName = fileConfig["dataset_name"].get<std::string>();
                    fc.recfm = fileConfig["record_format"].get<std::string>();
                    fc.recordFormat = fc.recfm[0];  // F, V, or U
                    fc.lrecl = fileConfig["record_length"].get<uint16_t>();
                    fc.blksize = fileConfig["block_size"].get<uint16_t>();
                    fc.binary = fileConfig.value("binary", false);

                    // Set block attribute
                    fc.blockAttribute = ' ';
                    if (fc.recfm.find('B') != std::string::npos) {
                        fc.blockAttribute = 'B';  // Blocked
                    }
                    if (fc.recfm.find('S') != std::string::npos) {
                        fc.blockAttribute = (fc.blockAttribute == 'B') ? 'R' : 'S';  // Spanned or Blocked and Spanned
                    }

                    tapeMaker.addFile(fc);
                }
                tapeMaker.writeTape();

                if (options.verbosity >= VerbosityLevel::Normal) {
                    std::cout << "AWS tape file created successfully: " << options.outputFile << std::endl;
                }
                break;
            }

            case OperationMode::Extract: {
                // Extract mode (former dumptape extract functionality)
                if (options.inputFiles.size() != 1) {
                    throw std::runtime_error("Extract mode requires exactly one input tape file");
                }

                std::string error;
                json config = AwsTapeDumper::loadConfig(options.configFile, error);
                if (config.is_null()) {
                    throw std::runtime_error("Error loading configuration: " + error);
                }

                AwsTapeDumper tapeDumper(options.inputFiles[0], options.verbosity);
                if (!tapeDumper.extractFiles(config)) {
                    throw std::runtime_error("Some files failed to extract");
                }

                if (options.verbosity >= VerbosityLevel::Normal) {
                    std::cout << "All files extracted successfully" << std::endl;
                }
                break;
            }

            case OperationMode::Scan:
            case OperationMode::Init: {
                // Scan/Init mode (former dumptape scan/init functionality)
                for (const auto& inputFile : options.inputFiles) {
                    AwsTapeDumper tapeDumper(inputFile, options.verbosity);

                    if (!tapeDumper.scanTape()) {
                        std::cerr << "Error: No valid files found on tape: " << inputFile << std::endl;
                        continue;
                    }

                    if (options.mode == OperationMode::Init) {
                        std::string configFile = options.outputFile.empty() ?
                                               inputFile + ".json" : options.outputFile;
                        tapeDumper.writeConfig(configFile);
                        if (options.verbosity >= VerbosityLevel::Normal) {
                            std::cout << "Configuration template written to: " << configFile << std::endl;
                        }
                    }

                    if (options.verbosity >= VerbosityLevel::Detailed) {
                        auto files = tapeDumper.getFiles();
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
        }

        return 0;

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
}
