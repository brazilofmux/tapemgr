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
#include "ebcdic_converter.h"

const char* VERSION = "1.00";

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

enum class RecordMode {
    Fixed,
    Variable
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
    std::cout << "tapemgr version " << VERSION << "\n"
              << "\nUsage: " << progName << " <command> [options] <files...>\n"
              << "\nCommands:\n"
              << "  create    Create an AWS tape file from input files\n"
              << "  extract   Extract files from an AWS tape using JSON config\n"
              << "  scan      Display contents of AWS tape file(s)\n"
              << "  init      Create JSON template from tape for later extraction\n"
              << "\nCommon Options:\n"
              << "  -v, --verbose     Increase verbosity (can be used multiple times)\n"
              << "  -h, --help        Show command-specific help\n"
              << "  -V, --version     Show version information\n"
              << "  -c, --config=FILE Configuration file (required for create/extract)\n"
              << "\nCreate Options:\n"
              << "  --volser=VOL      Volume serial number (required)\n"
              << "  --owner=OWNER     Owner code (default: TAPEOWNR)\n"
              << "  -o, --output=FILE Output tape file (required)\n"
              << "  Input files can be specified either in the config file's local_file field\n"
              << "  or as additional arguments on the command line. Command line files will be\n"
              << "  processed in addition to any files specified in the config.\n"
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
    std::string creationDate;
    std::string expirationDate;
    uint16_t lrecl;
    uint16_t blksize;
    std::string recfm;      // F, FB, V, VB, etc.
    char recordFormat;      // F, V, or U
    char blockAttribute;    // B, S, R, or ' '
    bool binary;
    size_t recordCount;
    std::string targetUnit = "3380";    // Default device type
    std::string targetVolser = "";      // Empty means use default
    EbcdicCodePage codepage = EbcdicCodePage::CP037; // Default to CP037
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
    std::string creationDate;    // CYYDDD format from HDR1
    std::string expirationDate;  // CYYDDD format from HDR1
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

struct DasdInfo {
    std::string deviceType;
    int halfTrackSize;
};

const std::map<std::string, DasdInfo> DASD_TYPES = {
    {"3350", {
        "3350",
        19069
    }},
    {"3380", {
        "3380",
        23476
    }},
    {"3390", {
        "3390",
        27998
    }}
};

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

std::string createTapeDate(int year, int dayOfYear) {
    char century = (year >= 2000) ? '0' : ' ';  // Assume 1900s or 2000s
    std::ostringstream ss;
    ss << century
       << std::setfill('0') << std::setw(2) << (year % 100)
       << std::setfill('0') << std::setw(3) << dayOfYear;
    return ss.str();
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

class VariableRecordProcessor : public RecordProcessor {
public:
    VariableRecordProcessor(uint16_t maxRecordLength) : m_maxRecordLength(maxRecordLength) {}

    std::vector<std::vector<uint8_t>> processBlock(const std::vector<uint8_t>& blockData) override {
        std::vector<std::vector<uint8_t>> records;

        // Skip BDW
        size_t offset = 4;

        while (offset < blockData.size()) {
            // Get RDW/SDW
            uint16_t recordLength = (blockData[offset] << 8) | blockData[offset + 1];
            if (recordLength < 4) {
                throw std::runtime_error("Invalid record length");
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
        return {};  // Variable records don't span blocks
    }

private:
    uint16_t m_maxRecordLength;
};

class SpannedRecordProcessor : public RecordProcessor {
public:
    SpannedRecordProcessor(uint16_t maxRecordLength) : m_maxRecordLength(maxRecordLength) {}

    std::vector<std::vector<uint8_t>> processBlock(const std::vector<uint8_t>& blockData) override {
        std::vector<std::vector<uint8_t>> completeRecords;

        // Skip BDW
        size_t offset = 4;

        while (offset < blockData.size()) {
            // Read SDW
            uint16_t segmentLength = (blockData[offset] << 8) | blockData[offset + 1];
            uint8_t segmentControl = blockData[offset + 2] & 0x03;

            // Extract pure data (skip SDW)
            size_t dataLength = segmentLength - 4;  // Remove SDW size
            std::vector<uint8_t> segmentData(
                blockData.begin() + offset + 4,
                blockData.begin() + offset + 4 + dataLength
            );

            // Process based on segment type
            switch (segmentControl) {
                case 0b00:  // Complete logical record
                    completeRecords.push_back(std::move(segmentData));
                    break;
                case 0b01:  // First segment
                    m_currentRecord = std::move(segmentData);
                    break;
                case 0b10:  // Last segment
                    m_currentRecord.insert(m_currentRecord.end(),
                                         segmentData.begin(), segmentData.end());
                    completeRecords.push_back(std::move(m_currentRecord));
                    m_currentRecord.clear();
                    break;
                case 0b11:  // Middle segment
                    m_currentRecord.insert(m_currentRecord.end(),
                                         segmentData.begin(), segmentData.end());
                    break;
            }

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

class TextRecordTransformer : public RecordTransformer {
public:
    TextRecordTransformer(EbcdicCodePage codepage) {
        converter = IEbcdicConverter::create(codepage);
    }

    std::vector<uint8_t> transform(const std::vector<uint8_t>& record) override {
        std::string unicode = converter->ebcdicToUtf8String(
            record.data(),
            record.size(),
            true
        );

        // Convert to vector<uint8_t> and add newline
        std::vector<uint8_t> result(unicode.begin(), unicode.end());
        result.push_back('\n');
        return result;
    }

private:
    std::shared_ptr<IEbcdicConverter> converter;
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

class RecordTransformerFactory {
public:
    static std::unique_ptr<RecordTransformer> create(const json& fileConfig) {
        bool isBinary = fileConfig.value("binary", false);
        std::string recordFormat = fileConfig["record_format"].get<std::string>();
        RecordMode mode = (recordFormat[0] == 'V') ? RecordMode::Variable : RecordMode::Fixed;

        if (isBinary) {
            return std::make_unique<BinaryRecordTransformer>(mode == RecordMode::Variable);
        }

        // Get codepage from config or default to CP037
        EbcdicCodePage codepage = EbcdicCodePage::CP037;
        if (fileConfig.contains("codepage")) {
            std::string cp = fileConfig["codepage"].get<std::string>();
            if (cp == "CP273") codepage = EbcdicCodePage::CP273;
            else if (cp == "CP277") codepage = EbcdicCodePage::CP277;
            else if (cp == "CP285") codepage = EbcdicCodePage::CP285;
        }

        return std::make_unique<TextRecordTransformer>(codepage);
    }
};

class OutputWriter {
public:
    virtual ~OutputWriter() = default;
    virtual void writeRecord(const std::vector<uint8_t>& record) = 0;
    virtual void close() = 0;

protected:
    OutputWriter() = default;
};

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
    }

    void close() override {
        if (m_outFile.is_open()) {
            m_outFile.close();
        }
    }

private:
    std::ofstream m_outFile;
};

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

class VariableBinaryWriter : public OutputWriter {
public:
    explicit VariableBinaryWriter(const std::string& filename) {
        m_outFile.open(filename, std::ios::out | std::ios::binary);
        if (!m_outFile) {
            throw std::runtime_error("Unable to open output file: " + filename);
        }
    }

    void writeRecord(const std::vector<uint8_t>& record) override {
        // Add RDW for variable binary files on disk
        std::vector<uint8_t> withRDW(record.size() + 4);
        uint16_t totalLength = record.size() + 4;
        withRDW[0] = totalLength >> 8;
        withRDW[1] = totalLength & 0xFF;
        withRDW[2] = 0;  // Control bytes
        withRDW[3] = 0;
        std::copy(record.begin(), record.end(), withRDW.begin() + 4);
        m_outFile.write(reinterpret_cast<const char*>(withRDW.data()), withRDW.size());
    }

    void close() override {
        if (m_outFile.is_open()) {
            m_outFile.close();
        }
    }

private:
    std::ofstream m_outFile;
};

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
            // Variable binary files always have RDWs on disk
            return std::make_unique<VariableBinaryWriter>(outputPath);
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
    AwsTapeDumper(const std::string& inputFile,
                 VerbosityLevel verbosity = VerbosityLevel::Normal,
                 const std::string& outputDir = "");
    ~AwsTapeDumper();

    // Primary operations
    bool scanTape();                          // First pass: scan and build table of contents
    const std::vector<TapeFileInfo>& getFiles() const {
        return m_files;
    }
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

    // Combine scan and extract for better efficiency
    bool extractFiles(const json& config, bool validateOnly = false) {
        // First scan the tape to get positions
        if (!scanTape()) {
            throw std::runtime_error("Failed to scan tape");
        }

        // Generate scan config
        json scanConfig = generateConfig();

        // Merge the configs
        json mergedConfig = mergeConfigs(config, scanConfig);

        if (m_verbosity >= VerbosityLevel::Debug) {
            std::cout << "Using merged config:\n" << mergedConfig.dump(2) << std::endl;
        }

        // Verify volume serial matches if specified
        if (mergedConfig.contains("volume_serial") &&
            mergedConfig["volume_serial"] != m_currentVolser) {
            std::cerr << "Warning: Config volume serial " << mergedConfig["volume_serial"]
                      << " doesn't match tape volume " << m_currentVolser << std::endl;
        }

        if (validateOnly) {
            return true;  // Just checking structure
        }

        bool success = true;
        for (const auto& fileConfig : mergedConfig["files"]) {
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
                if (m_verbosity < VerbosityLevel::Detailed) {
                    break;  // Stop on first error unless detailed mode
                }
            }
        }

        return success;
    }

    // Add getter for volume serial
    std::string getVolumeSerial() const {
        return m_currentVolser;
    }

    std::string getOwnerCode() const {
        return m_ownerCode;
    }

    json mergeConfigs(const json& createConfig, const json& scanConfig) {
        json merged = scanConfig;  // Start with scan data (has positions)

        // For each file in scan config, find matching dataset in create config
        for (auto& file : merged["files"]) {
            const std::string& dsn = file["dataset_name"];
            // Find matching file in create config
            auto it = std::find_if(createConfig["files"].begin(),
                                  createConfig["files"].end(),
                                  [&dsn](const json& f) {
                                      return f["dataset_name"] == dsn;
                                  });
            if (it != createConfig["files"].end()) {
                // Copy over the output file path and any other needed fields.
                // "binary" and "codepage" decide how the records are written
                // out; without them a binary V dataset came back EBCDIC-
                // translated whatever the extract config said.
                file["local_file"] = (*it)["local_file"];
                if (it->contains("binary")) file["binary"] = (*it)["binary"];
                if (it->contains("codepage")) file["codepage"] = (*it)["codepage"];
            }
        }
        return merged;
    }

protected:
    void extractFile(const json& fileConfig) {
        if (m_verbosity >= VerbosityLevel::Normal) {
            std::cout << "Extracting dataset: " << fileConfig["dataset_name"] << std::endl;
            if (m_verbosity >= VerbosityLevel::Debug) {
                std::cout << "Using config: " << fileConfig.dump(2) << std::endl;
            }
        }

        // Validate required fields
        const std::vector<std::string> requiredFields = {
            "dataset_name",
            "local_file",
            "record_format",
            "record_length",
            "block_size",
            "file_position"
        };

        for (const auto& field : requiredFields) {
            if (!fileConfig.contains(field)) {
                throw std::runtime_error("Missing required field in config: " + field);
            }
        }

        // Get the adjusted output path using the output directory
        std::string outputPath = getOutputPath(fileConfig["local_file"]);

        if (m_verbosity >= VerbosityLevel::Detailed) {
            std::cout << "  Output will be written to: " << outputPath << std::endl;
        }

        // Create a modified config with the adjusted output path
        json modifiedConfig = fileConfig;
        modifiedConfig["local_file"] = outputPath;

        // Convert position from JSON integer to streampos
        auto fileStart = static_cast<std::streampos>(fileConfig["file_position"].get<int64_t>());
        m_tapeFile.clear();
        m_tapeFile.seekg(fileStart);

        // Create parent directories if they don't exist
        std::filesystem::path outputFilePath(outputPath);
        std::filesystem::create_directories(outputFilePath.parent_path());

        ExtractionPipeline pipeline(m_tapeFile, modifiedConfig, m_verbosity);
        pipeline.extract();
    }

    std::string getOutputPath(const std::string& configFilePath) {
        if (m_outputDir.empty()) {
            return configFilePath;
        }

        // Get just the filename part, not the full path
        std::filesystem::path path(configFilePath);
        std::string filename = path.filename().string();

        // Combine the output directory with the filename
        std::filesystem::path outputPath = std::filesystem::path(m_outputDir) / filename;

        // Create the output directory if it doesn't exist
        std::filesystem::create_directories(std::filesystem::path(m_outputDir));

        return outputPath.string();
    }

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
    std::string m_outputDir;

    // Tape state tracking
    std::string m_currentVolser;
    uint32_t m_currentBlockCount;
    bool m_inDataBlocks;

    // Current file being processed
    TapeFileInfo m_currentFile;

    std::shared_ptr<IEbcdicConverter> labelConverter;
    std::shared_ptr<IEbcdicConverter> getConverter(EbcdicCodePage codepage) {
        return IEbcdicConverter::create(codepage);
    }

    std::string m_ownerCode;
};

AwsTapeDumper::AwsTapeDumper(const std::string& inputFile,
                           VerbosityLevel verbosity,
                           const std::string& outputDir)
    : m_inputFile(inputFile)
    , m_verbosity(verbosity)
    , m_currentBlockCount(0)
    , m_inDataBlocks(false)
    , m_outputDir(outputDir) {

    m_tapeFile.open(inputFile, std::ios::binary);
    if (!m_tapeFile) {
        throw std::runtime_error("Error opening file: " + inputFile);
    }

    if (m_verbosity >= VerbosityLevel::Normal) {
        std::cout << "Processing AWSTAPE file: " << inputFile << std::endl;
    }
    labelConverter = getConverter(EbcdicCodePage::CP037);
}

AwsTapeDumper::~AwsTapeDumper() {
    if (m_tapeFile.is_open()) {
        m_tapeFile.close();
    }
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
    bool afterHdr2 = false;

    while (readBlock(header, buffer)) {
        if (m_verbosity >= VerbosityLevel::Detailed) {
            printDetail(header, m_verbosity);
        }

        // Process tape marks specifically
        if (header.flags1 & 0x40) {
            if (m_verbosity >= VerbosityLevel::Detailed) {
                std::cout << "TAPE MARK at position " << (m_tapeFile.tellg() - std::streampos(sizeof(header))) << std::endl;
            }

            if (prevFlags == header.flags1) {
                // Two consecutive tape marks indicate end of tape
                if (m_verbosity >= VerbosityLevel::Detailed) {
                    std::cout << "End of tape" << std::endl;
                }
                break;
            }

            // Single tape mark might indicate start of data blocks
            if (afterHdr2) {
                // This tape mark follows HDR2 - the next block will be data
                m_currentFile.dataStart = m_tapeFile.tellg();
                m_inDataBlocks = true;
                afterHdr2 = false;  // Reset for next file

                if (m_verbosity >= VerbosityLevel::Detailed) {
                    std::cout << "Data blocks start at " << m_currentFile.dataStart << std::endl;
                }
            } else if (m_inDataBlocks) {
                // This tape mark follows data blocks - end of data
                m_inDataBlocks = false;
                m_currentFile.dataEnd = m_tapeFile.tellg() -
                    std::streampos(sizeof(header));

                if (m_verbosity >= VerbosityLevel::Detailed) {
                    std::cout << "Data blocks end at " << m_currentFile.dataEnd << std::endl;
                }
            }

            prevFlags = header.flags1;
            continue;
        }

        if (header.curblkl > 0) {
            std::string labelIdentifier = labelConverter->ebcdicToUtf8String(buffer.data(), 4, true);

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

                // Reset data block tracking for this file
                m_inDataBlocks = false;
                m_currentBlockCount = 0;
                afterHdr2 = false;
            }
            else if (labelIdentifier == "HDR2") {
                const HDR2Label* hdr2 = reinterpret_cast<const HDR2Label*>(buffer.data());
                if (validateHDR2Label(*hdr2)) {
                    processHDR2Label(*hdr2);
                }
                afterHdr2 = true;
            }
            else if (labelIdentifier == "EOF1") {
                const EOF1Label* eof1 = reinterpret_cast<const EOF1Label*>(buffer.data());
                processEOF1Label(*eof1);
            }
            else if (labelIdentifier == "EOF2") {
                const EOF2Label* eof2 = reinterpret_cast<const EOF2Label*>(buffer.data());
                processEOF2Label(*eof2);

                // End of the current file - make sure all properties are set
                if (m_currentFile.dataEnd == 0) {
                    m_currentFile.dataEnd = m_tapeFile.tellg() -
                        (std::streampos)(sizeof(AwsTapeBlockHeader) + header.curblkl);
                }

                // Save this file
                m_files.push_back(m_currentFile);

                if (m_verbosity >= VerbosityLevel::Detailed) {
                    std::cout << "File " << m_currentFile.datasetName
                             << " at " << m_currentFile.fileStart
                             << ", data from " << m_currentFile.dataStart
                             << " to " << m_currentFile.dataEnd
                             << " (" << m_currentFile.blockCount << " blocks)" << std::endl;
                }
            }
            else if (m_inDataBlocks) {
                // Count data blocks between HDR and EOF labels
                m_currentBlockCount++;
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
    auto validateTapeDate = [](const unsigned char* date, const char* fieldName) {
        // First byte should be space or digit for century
        if (date[0] != 0x40 && (date[0] < 0xF0 || date[0] > 0xF9)) {
            std::cout << "Warning: Invalid century code in " << fieldName << std::endl;
            return false;
        }

        // Next two bytes should be digits for year
        if ((date[1] < 0xF0 || date[1] > 0xF9) ||
            (date[2] < 0xF0 || date[2] > 0xF9)) {
            std::cout << "Warning: Invalid year in " << fieldName << std::endl;
            return false;
        }

        // Last three bytes should be digits for day
        for (int i = 3; i < 6; i++) {
            if (date[i] < 0xF0 || date[i] > 0xF9) {
                std::cout << "Warning: Invalid day in " << fieldName << std::endl;
                return false;
            }
        }

        return true;
    };

    isValid &= validateTapeDate(label.creationDate, "creation date");
    isValid &= validateTapeDate(label.expirationDate, "expiration date");

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
    config["owner_code"] = m_ownerCode;

    // Add files array
    json files = json::array();
    for (const auto& file : m_files) {
        json fileObj;

        // Required fields
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

        // Optional fields with sensible defaults
        fileObj["binary"] = false;  // Default to text mode
        fileObj["block_attribute"] = std::string(1, file.blockAttribute);
        fileObj["dataset_org"] = "PS";  // Physical Sequential

        // Metadata fields
        fileObj["block_count"] = file.blockCount;
        fileObj["file_position"] = static_cast<int64_t>(file.dataStart);

        // Add creation/expiration dates if available from HDR1
        if (!file.creationDate.empty()) {
            fileObj["creation_date"] = file.creationDate;
        }
        if (!file.expirationDate.empty()) {
            fileObj["expiration_date"] = file.expirationDate;
        }

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
        for (const auto& field : {"volume_serial", "files"}) {
            if (!config.contains(field)) {
                error = "Missing required field '" + std::string(field) + "'";
                return false;
            }
        }

        // Validate volume_serial
        if (!config["volume_serial"].is_string() ||
            config["volume_serial"].get<std::string>().length() > 6) {
            error = "volume_serial must be 1-6 characters";
            return false;
        }

        // Optional owner_code
        if (config.contains("owner_code")) {
            if (!config["owner_code"].is_string() ||
                config["owner_code"].get<std::string>().length() > 10) {
                error = "owner_code must be 1-10 characters";
                return false;
            }
        }

        const std::set<std::string> validRecFM = {"F", "FB", "V", "VB", "VS", "VBS", "U"};
        const std::set<std::string> validBlockAttrs = {"B", "S", "R", " "};
        const std::set<std::string> validDatasetOrgs = {"PS"};  // Could expand later
        const std::map<std::string, EbcdicCodePage> validCodePages = {
            {"CP037", EbcdicCodePage::CP037},
            {"CP273", EbcdicCodePage::CP273},
            {"CP277", EbcdicCodePage::CP277},
            {"CP285", EbcdicCodePage::CP285}
        };

        // Validate files array
        if (!config["files"].is_array()) {
            error = "files must be an array";
            return false;
        }

        for (const auto& file : config["files"]) {
            // Required fields
            for (const auto& field : {"dataset_name", "local_file", "record_format",
                                    "record_length", "block_size"}) {
                if (!file.contains(field)) {
                    error = std::string("Missing required field '") + field + "' in file entry";
                    return false;
                }
            }

            // Validate dataset_name
            if (!file["dataset_name"].is_string() ||
                file["dataset_name"].get<std::string>().length() > 44) {
                error = "dataset_name must be 1-44 characters";
                return false;
            }

            // Validate record_format
            if (!file["record_format"].is_string() ||
                validRecFM.find(file["record_format"].get<std::string>()) == validRecFM.end()) {
                error = "Invalid record_format. Must be one of: F, FB, V, VB, VS, VBS, U";
                return false;
            }

            // Validate block_attribute if present
            if (file.contains("block_attribute")) {
                if (!file["block_attribute"].is_string() ||
                    validBlockAttrs.find(file["block_attribute"].get<std::string>()) == validBlockAttrs.end()) {
                    error = "Invalid block_attribute. Must be one of: B, S, R, or space";
                    return false;
                }
            }

            // Validate dataset_org if present
            if (file.contains("dataset_org")) {
                if (!file["dataset_org"].is_string() ||
                    validDatasetOrgs.find(file["dataset_org"].get<std::string>()) == validDatasetOrgs.end()) {
                    error = "Invalid dataset_org. Must be: PS";
                    return false;
                }
            }

            auto validateDate = [&error](const json& file, const char* field) {
                if (file.contains(field)) {
                    if (!file[field].is_string()) {
                        error = std::string("Field '") + field + "' must be a string";
                        return false;
                    }
                    std::string date = file[field];

                    // Special case: all zeros means never expires
                    if (date == "00000" || date == " 00000" || date == "000000") {
                        return true;
                    }

                    if (date.length() != 6) {
                        error = std::string("Invalid ") + field + " length. Must be 6 digits (cyyddd)";
                        return false;
                    }

                    // Check century code
                    char c = date[0];
                    if (c != ' ' && c != '0' && c != '1') {  // Most common cases
                        if (!std::isdigit(c)) {
                            error = std::string("Invalid century code in ") + field;
                            return false;
                        }
                    }

                    // Check year
                    if (!std::isdigit(date[1]) || !std::isdigit(date[2])) {
                        error = std::string("Invalid year in ") + field;
                        return false;
                    }

                    // Check day of year
                    int ddd;
                    try {
                        ddd = std::stoi(date.substr(3,3));
                        if (ddd < 1 || ddd > 366) {
                            error = std::string("Invalid day in ") + field + " (must be 001-366)";
                            return false;
                        }
                    } catch (...) {
                        error = std::string("Invalid day format in ") + field;
                        return false;
                    }
                }
                return true;
            };

            if (!validateDate(file, "creation_date") || !validateDate(file, "expiration_date")) {
                return false;
            }

            // Validate numeric fields
            if (!file["record_length"].is_number_integer() ||
                file["record_length"].get<int>() < 1 ||
                file["record_length"].get<int>() > 32760) {
                error = "record_length must be between 1 and 32760";
                return false;
            }

            if (!file["block_size"].is_number_integer() ||
                file["block_size"].get<int>() < 1 ||
                file["block_size"].get<int>() > 32760) {
                error = "block_size must be between 1 and 32760";
                return false;
            }

            // Validate binary flag if present
            if (file.contains("binary") && !file["binary"].is_boolean()) {
                error = "binary must be a boolean value";
                return false;
            }

            // Validate codepage if present
            if (file.contains("codepage")) {
                if (!file["codepage"].is_string()) {
                    error = "codepage must be a string value";
                    return false;
                }
                std::string cp = file["codepage"].get<std::string>();
                if (validCodePages.find(cp) == validCodePages.end()) {
                    error = "Invalid codepage. Must be one of: CP037, CP273, CP277, CP285";
                    return false;
                }
            }
        }

        return true;
    }
    catch (const json::exception& e) {
        error = std::string("JSON validation error: ") + e.what();
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
    m_currentVolser = labelConverter->ebcdicToUtf8String(label.volumeSerial, 6, true);
    m_ownerCode = labelConverter->ebcdicToUtf8String(label.ownerCode, 10, true);

    if (m_verbosity >= VerbosityLevel::Detailed) {
	std::cout << "VOL1 Label found" << std::endl;
	std::cout << "  Volume Serial: " << m_currentVolser << std::endl;
	std::cout << "  Owner Code: " << m_ownerCode << std::endl;
    }
}

void AwsTapeDumper::processHDR1Label(const HDR1Label& label) {
    m_currentFile.datasetName = labelConverter->ebcdicToUtf8String(label.dataSetIdentifier, 17, true);
    m_currentFile.volumeSerial = labelConverter->ebcdicToUtf8String(label.dataSetSerialNumber, 6, true);
    m_currentFile.creationDate = labelConverter->ebcdicToUtf8String(label.creationDate, 6);
    m_currentFile.expirationDate = labelConverter->ebcdicToUtf8String(label.expirationDate, 6);

    if (m_verbosity >= VerbosityLevel::Detailed) {
        std::cout << "HDR1 Label found" << std::endl;
        std::cout << "  Dataset Name: " << m_currentFile.datasetName << std::endl;
        std::cout << "  Dataset Serial Number: " << m_currentFile.volumeSerial << std::endl;
        std::cout << "  Creation Date: " << m_currentFile.creationDate << std::endl;
        std::cout << "  Expiration Date: " << m_currentFile.expirationDate << std::endl;
        std::cout << "  Dataset Security: " << labelConverter->ebcdicToUtf8String(&label.dataSetSecurity, 1) << std::endl;
    }
}

void AwsTapeDumper::processHDR2Label(const HDR2Label& label) {
    m_currentFile.recordFormat = labelConverter->ebcdicToAscii(label.recordFormat);
    m_currentFile.blockAttribute = labelConverter->ebcdicToAscii(label.blockAttribute);

    // Convert EBCDIC numeric strings to integers
    std::string blksize = labelConverter->ebcdicToUtf8String(label.blockLength, 5, true);
    std::string lrecl = labelConverter->ebcdicToUtf8String(label.recordLength, 5, true);
    m_currentFile.blockSize = std::stoi(blksize);
    m_currentFile.recordLength = std::stoi(lrecl);

    if (m_verbosity >= VerbosityLevel::Detailed) {
        std::cout << "HDR2 Label found" << std::endl;
        std::cout << "  Record Format: " << m_currentFile.recordFormat << std::endl;
        std::cout << "  Block Attribute: " << m_currentFile.blockAttribute << std::endl;
        std::cout << "  Block Length: " << m_currentFile.blockSize << std::endl;
        std::cout << "  Record Length: " << m_currentFile.recordLength << std::endl;
        std::cout << "  Tape Density: " << labelConverter->ebcdicToUtf8String(&label.tapeDensity, 1) << std::endl;
        std::cout << "  Job/Step: " << labelConverter->ebcdicToUtf8String(label.jobStepIdentification, 17, true) << std::endl;
        std::cout << "  Tape Recording Technique: " << labelConverter->ebcdicToUtf8String(label.tapeRecordingTechnique, 2, true) << std::endl;
        std::cout << "  Control Character: " << labelConverter->ebcdicToUtf8String(&label.controlCharacter, 1) << std::endl;
        std::cout << "  Device Serial Number: " << labelConverter->ebcdicToUtf8String(label.deviceSerialNumber, 6, true) << std::endl;
    }
}

void AwsTapeDumper::processEOF1Label(const EOF1Label& label) {
    std::string blockCount = labelConverter->ebcdicToUtf8String(label.blockCount, 6, true);
    m_currentFile.blockCount = std::stoi(blockCount);

    if (m_verbosity >= VerbosityLevel::Detailed) {
        std::cout << "EOF1 Label found" << std::endl;
        std::cout << "  Dataset Name: " << labelConverter->ebcdicToUtf8String(label.dataSetIdentifier, 17, true) << std::endl;
        std::cout << "  Block Count: " << m_currentFile.blockCount << std::endl;
    }
}

void AwsTapeDumper::processEOF2Label(const EOF2Label& label) {
    if (m_verbosity >= VerbosityLevel::Detailed) {
        std::cout << "EOF2 Label found" << std::endl;
        std::cout << "  Record Format: " << labelConverter->ebcdicToUtf8String(&label.recordFormat, 1) << std::endl;
        std::cout << "  Block Attribute: " << labelConverter->ebcdicToUtf8String(&label.blockAttribute, 1) << std::endl;
        std::cout << "  Block Length: " << labelConverter->ebcdicToUtf8String(label.blockLength, 5, true) << std::endl;
        std::cout << "  Record Length: " << labelConverter->ebcdicToUtf8String(label.recordLength, 5, true) << std::endl;
        std::cout << "  Tape Density: " << labelConverter->ebcdicToUtf8String(&label.tapeDensity, 1) << std::endl;
        std::cout << "  Job/Step: " << labelConverter->ebcdicToUtf8String(label.jobStepIdentification, 17, true) << std::endl;
        std::cout << "  Tape Recording Technique: " << labelConverter->ebcdicToUtf8String(label.tapeRecordingTechnique, 2, true) << std::endl;
        std::cout << "  Control Character: " << labelConverter->ebcdicToUtf8String(&label.controlCharacter, 1)<< std::endl;
        std::cout << "  Device Serial Number: " << labelConverter->ebcdicToUtf8String(label.deviceSerialNumber, 6, true) << std::endl;
    }
}

int calculateOptimalBlksize(int lrecl, const std::string& deviceType = "3380") {
    auto it = DASD_TYPES.find(deviceType);
    if (it == DASD_TYPES.end()) {
        // Fall back to 3380 if unknown
        return lrecl * (23476/lrecl);
    }

    const DasdInfo& info = it->second;

    // Default half-track calculation
    return lrecl * (info.halfTrackSize/lrecl);
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

std::string generateMultiFileRestoreJCL(const std::vector<FileConfig>& configs,
                                      const std::string& defaultVolser = "SVD002",
                                      const std::string& defaultUnit = "3380") {
    std::stringstream jcl;

    // Job card
    jcl << "//REST    JOB (001),'TAPE RESTORE',CLASS=A,MSGLEVEL=(1,1),MSGCLASS=A\n";
    jcl << "//JOBLIB  DD  DSN=SYS1.LINKLIB,DISP=SHR\n";

    int stepNumber = 1;
    for (const auto& config : configs) {
        // Use dataset-specific values or defaults
        std::string volser = config.targetVolser.empty() ? defaultVolser : config.targetVolser;
        std::string unit = config.targetUnit.empty() ? defaultUnit : config.targetUnit;

        // Delete step
        jcl << "//STEP" << std::setfill('0') << std::setw(2) << stepNumber++ << "   EXEC PGM=IEFBR14\n";
        jcl << "//SYSPRINT DD  SYSOUT=*\n";
        jcl << "//DSN2DEL  DD  DSN=" << config.datasetName << ",DISP=(MOD,DELETE,DELETE),\n";
        jcl << "//             UNIT=" << unit << ",VOL=SER=" << volser << "\n";

        // Restore step
        jcl << "//STEP" << std::setfill('0') << std::setw(2) << stepNumber++ << "   EXEC PGM=IEBGENER\n";
        jcl << "//SYSPRINT DD  SYSOUT=A\n";
        jcl << "//SYSIN    DD  DUMMY\n";
        jcl << "//SYSUT1   DD  DSN=" << config.datasetName << ",UNIT=TAPE,\n";
        jcl << "//             VOL=(PRIVATE,RETAIN,SER=240001),LABEL=(" << ((stepNumber-1)/2) << ",SL),\n";
        jcl << "//             DCB=(RECFM=" << config.recfm << ",LRECL=" << config.lrecl
            << ",BLKSIZE=" << config.blksize << "),\n";
        jcl << "//             DISP=OLD\n";
        jcl << "//SYSUT2   DD  DSN=" << config.datasetName << ",UNIT=" << unit << ",\n";
        jcl << "//             VOL=SER=" << volser << ",DISP=(NEW,CATLG),\n";
        jcl << "//             DCB=(RECFM=" << config.recfm << ",LRECL=" << config.lrecl
            << ",DSORG=PS,BLKSIZE=" << calculateOptimalBlksize(config.lrecl, unit) << "),\n";
        jcl << "//             SPACE=(" << calculateSpace(config) << ")\n";
    }

    return jcl.str();
}

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
        // For binary variable files on disk, RDWs mark record boundaries
        uint8_t rdw[4];
        if (!m_inFile.read(reinterpret_cast<char*>(rdw), 4)) {
            return false;
        }

        uint16_t recordLength = (rdw[0] << 8) | rdw[1];
        if (recordLength < 4) {
            throw std::runtime_error("Binary VB record length must be at least 4 bytes (got " +
                                   std::to_string(recordLength) + ")");
        }

        // Return just the data portion
        if (recordLength == 4) {
            record.clear();
        } else {
            record.resize(recordLength - 4);
            if (!m_inFile.read(reinterpret_cast<char*>(record.data()), recordLength - 4)) {
                throw std::runtime_error("Unexpected end of file while reading VB record");
            }
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
        : m_config(config), m_verbosity(verbosity) {
        converter = IEbcdicConverter::create(config.codepage);
    }

    const FileConfig& m_config;
    VerbosityLevel m_verbosity;
    std::shared_ptr<IEbcdicConverter> converter;
};

class FixedRecordFormatter : public RecordFormatter {
public:
    FixedRecordFormatter(const FileConfig& config, VerbosityLevel verbosity = VerbosityLevel::Normal)
        : RecordFormatter(config, verbosity) {}

    std::vector<uint8_t> formatRecord(const std::vector<uint8_t>& rawData) override {
        if (m_config.binary) {
            // Binary data - just pass through
            return rawData;
        } else {
            // Text data - convert to EBCDIC and pad
            std::vector<uint8_t> formattedRecord(m_config.lrecl, 0x40);  // Initialize with EBCDIC spaces
            auto ebcdicData = converter->utf8ToEbcdic(rawData);
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
            return rawData;
        } else {
            std::string text(rawData.begin(), rawData.end());

            // Right trim spaces, but ensure at least one space for empty
            while (!text.empty() && text.back() == ' ') {
                text.pop_back();
            }
            if (text.empty()) {
                text = " ";  // Single UTF-8 space (will become 0x40 in EBCDIC)
            }

            return converter->utf8ToEbcdic(std::vector<uint8_t>(text.begin(), text.end()));
        }
    }
};

static std::unique_ptr<RecordFormatter> createFormatter(const FileConfig& config,
                                                      VerbosityLevel verbosity) {
    if (config.recordFormat == 'F') {
        return std::make_unique<FixedRecordFormatter>(config, verbosity);
    } else if (config.recordFormat == 'V') {
        return std::make_unique<VariableRecordFormatter>(config, verbosity);
    }
    throw std::runtime_error("Unsupported record format: " + config.recfm);
}

// Record processing abstractions
class BlockProcessor {
public:
    virtual ~BlockProcessor() = default;

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
            std::stringstream ss;
            ss << "Block overflow: tried to add " << data.size()
               << " bytes when only " << (m_blockSize - m_currentOffset)
               << " bytes remain (block size: " << m_blockSize << ")";
            throw std::runtime_error(ss.str());
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

class FixedBlockProcessor : public BlockProcessor {
public:
    FixedBlockProcessor(const FileConfig& config, VerbosityLevel verbosity = VerbosityLevel::Normal)
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

class VariableBlockProcessor : public BlockProcessor {
public:
    VariableBlockProcessor(const FileConfig& config, VerbosityLevel verbosity = VerbosityLevel::Normal)
        : m_config(config), m_blockBuilder(config.blksize, true, verbosity) {
        m_verbosity = verbosity;
    }

    std::vector<std::vector<uint8_t>> processRecord(const std::vector<uint8_t>& data) override {
        std::vector<std::vector<uint8_t>> completeBlocks;

        // Create record with RDW/SDW
        std::vector<uint8_t> recordWithRDW;
        recordWithRDW.reserve(data.size() + 4);

        // Add RDW/SDW
        uint16_t totalLength = data.size() + 4;  // Include RDW in length
        recordWithRDW.push_back(totalLength >> 8);
        recordWithRDW.push_back(totalLength & 0xFF);
        recordWithRDW.push_back(0x00);  // Control bits - single record
        recordWithRDW.push_back(0x00);  // Reserved

        // Add the data
        recordWithRDW.insert(recordWithRDW.end(), data.begin(), data.end());

        // If won't fit in current block, finish the block
        if (!m_blockBuilder.hasRoom(recordWithRDW.size())) {
            std::vector<uint8_t> block = m_blockBuilder.finish();
            if (!block.empty()) {
                completeBlocks.push_back(block);
            }
        }

        // Add to current block
        m_blockBuilder.addData(recordWithRDW);

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

class SpannedBlockProcessor : public BlockProcessor {
public:
    SpannedBlockProcessor(const FileConfig& config, VerbosityLevel verbosity = VerbosityLevel::Normal)
        : m_config(config), m_blockBuilder(config.blksize, true, verbosity) {
        m_verbosity = verbosity;
    }

    std::vector<std::vector<uint8_t>> processRecord(const std::vector<uint8_t>& data) override {
        std::vector<std::vector<uint8_t>> completeBlocks;

        // Normal record processing for non-empty records (unchanged)
        size_t maxDataPerSegment = m_config.blksize - 8;
        size_t remainingData = data.size();
        size_t dataOffset = 0;
        bool isFirstSegment = true;

        while (remainingData > 0) {
            size_t dataInSegment = std::min(remainingData, maxDataPerSegment);
            bool isLastSegment = (dataInSegment == remainingData);

            std::vector<uint8_t> segment;
            segment.reserve(dataInSegment + 4);

            // Add SDW
            uint16_t totalLength = dataInSegment + 4;
            segment.push_back(totalLength >> 8);
            segment.push_back(totalLength & 0xFF);

            if (isFirstSegment && isLastSegment) {
                segment.push_back(0x00);  // Complete record
            } else if (isFirstSegment) {
                segment.push_back(0x01);  // First segment
            } else if (isLastSegment) {
                segment.push_back(0x02);  // Last segment
            } else {
                segment.push_back(0x03);  // Middle segment
            }
            segment.push_back(0x00);

            segment.insert(segment.end(),
                         data.begin() + dataOffset,
                         data.begin() + dataOffset + dataInSegment);

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

static std::unique_ptr<BlockProcessor> createRecordProcessor(const FileConfig& config, VerbosityLevel verbosity) {
    if (config.recfm.find('S') != std::string::npos) {
        return std::make_unique<SpannedBlockProcessor>(config, verbosity);
    } else if (config.recordFormat == 'V') {
        return std::make_unique<VariableBlockProcessor>(config, verbosity);
    } else if (config.recordFormat == 'F') {
        return std::make_unique<FixedBlockProcessor>(config, verbosity);
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
    std::ifstream file(filename, std::ios::binary | std::ios::ate);
    if (!file) {
        throw std::runtime_error("Unable to open binary file: " + filename);
    }

    size_t fileSize = file.tellg();
    file.seekg(0, std::ios::beg);
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
        labelConverter = getConverter(EbcdicCodePage::CP037); // Always use CP037 for labels
    }

    void addFile(const json& fileConfig) {
        FileConfig config;

        // Required fields
        config.datasetName = fileConfig["dataset_name"].get<std::string>();
        config.inputFile = fileConfig["local_file"].get<std::string>();
        config.recfm = fileConfig["record_format"].get<std::string>();
        config.lrecl = fileConfig["record_length"].get<uint16_t>();
        config.blksize = fileConfig["block_size"].get<uint16_t>();

        // Optional fields with defaults
        config.binary = fileConfig.value("binary", false);

        // Handle block attribute
        if (fileConfig.contains("block_attribute")) {
            config.blockAttribute = fileConfig["block_attribute"].get<std::string>()[0];
        } else {
            // Derive from record format
            config.blockAttribute = ' ';
            if (config.recfm.find('B') != std::string::npos) {
                config.blockAttribute = 'B';
            }
            if (config.recfm.find('S') != std::string::npos) {
                config.blockAttribute = (config.blockAttribute == 'B') ? 'R' : 'S';
            }
        }

        // Base record format
        config.recordFormat = config.recfm[0];  // F, V, or U

        // Store creation/expiration dates for label creation
        if (fileConfig.contains("creation_date")) {
            config.creationDate = fileConfig["creation_date"].get<std::string>();
        }
        if (fileConfig.contains("expiration_date")) {
            config.expirationDate = fileConfig["expiration_date"].get<std::string>();
        }

        // Parse codepage
        if (fileConfig.contains("codepage")) {
            std::string cp = fileConfig["codepage"].get<std::string>();
            if (cp == "CP273") config.codepage = EbcdicCodePage::CP273;
            else if (cp == "CP277") config.codepage = EbcdicCodePage::CP277;
            else if (cp == "CP285") config.codepage = EbcdicCodePage::CP285;
            else config.codepage = EbcdicCodePage::CP037; // Default
        }

        addFile(config);  // Call the FileConfig version
    }

    // Add FileConfig version
    void addFile(const FileConfig& config) {
        // Verify input file exists
        if (!std::filesystem::exists(config.inputFile)) {
            throw std::runtime_error("Input file not found: " + config.inputFile);
        }

        validateFileConfig(config);
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

    // Add validation method
    void validateFileConfig(const FileConfig& config) {
        // Verify record format combinations
        if (config.recordFormat == 'F') {
            if (config.recfm == "F" && !config.binary && config.blksize != config.lrecl) {
                // Only enforce BLKSIZE = LRECL for unblocked F records
                throw std::runtime_error("For F format, BLKSIZE must equal LRECL");
            }
            if (config.recfm == "FB" && config.blksize % config.lrecl != 0) {
                // For FB, ensure BLKSIZE is multiple of LRECL
                throw std::runtime_error("For FB format, BLKSIZE must be multiple of LRECL");
            }
        }
    }

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

        auto normalizeLabelDate = [](const std::string& date) {
            // IBM standard label date format: cyyddd (6 chars)
            // c = century (' ' for 19xx, '0' for 20xx, '1' for 21xx)
            // yy = year within century, ddd = day of year (001-366)
            if (date.size() == 5) {
                // Validate yyddd format (all digits)
                for (char c : date) {
                    if (!std::isdigit(static_cast<unsigned char>(c))) {
                        throw std::runtime_error("Invalid label date format: " + date +
                            " (expected yyddd with all digits)");
                    }
                }
                return " " + date;
            }
            if (date.size() == 6) {
                // First char must be space or digit (century indicator)
                if (date[0] != ' ' && !std::isdigit(static_cast<unsigned char>(date[0]))) {
                    throw std::runtime_error("Invalid label date format: " + date +
                        " (first character must be space or digit)");
                }
                // Remaining 5 chars must be digits (yyddd)
                for (size_t i = 1; i < 6; ++i) {
                    if (!std::isdigit(static_cast<unsigned char>(date[i]))) {
                        throw std::runtime_error("Invalid label date format: " + date +
                            " (expected cyyddd with digits after century)");
                    }
                }
                return date;
            }
            throw std::runtime_error("Invalid label date format: " + date +
                " (expected 5 or 6 characters, got " + std::to_string(date.size()) + ")");
        };

        std::string label = "HDR1";
        label += padRight(config.datasetName, 17);  // Data Set Identifier
        label += padRight(m_volser, 6);             // Data Set Serial Number
        label += padLeft("0001", 4);                // Volume Sequence Number
        label += padLeft(std::to_string(fileNumber), 4);  // Data Set Sequence Number
        label += "0001";                            // Generation Number
        label += "00";                              // Version Number
        label += config.creationDate.empty()
            ? formatDate(tm)
            : normalizeLabelDate(config.creationDate);
        label += config.expirationDate.empty()
            ? formatExpirationDate(30)
            : normalizeLabelDate(config.expirationDate);
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
        if (daysToKeep < 0) {  // Special value to indicate never expires
            return " 00000";    // Space for century + all zeros
        }
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
        writeBlock(labelConverter->utf8ToEbcdic(label), 0xA0, true);
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

        writeBlock(labelConverter->utf8ToEbcdic(hdr1), 0xA0, true);
        writeBlock(labelConverter->utf8ToEbcdic(hdr2), 0xA0, true);
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
        writeBlock(labelConverter->utf8ToEbcdic(eof1), 0xA0, true);
        writeBlock(labelConverter->utf8ToEbcdic(eof2), 0xA0, true);
        writeTapeMark();
    }

    void writeEndOfTape() {
        std::cout << "Writing end of tape markers" << std::endl;
        writeTapeMark();
    }

    std::shared_ptr<IEbcdicConverter> labelConverter;
    std::shared_ptr<IEbcdicConverter> getConverter(EbcdicCodePage codepage) {
        return IEbcdicConverter::create(codepage);
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

    // Check for --help or --version before command parsing
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            showUsage(argv[0]);
            exit(0);
        }
        if (arg == "--version" || arg == "-V") {
            std::cout << "tapemgr version " << VERSION << std::endl;
            exit(0);
        }
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
        {"version", no_argument, 0, 'V'},
        {"config", required_argument, 0, 'c'},
        {"volser", required_argument, 0, 's'},
        {"owner", required_argument, 0, 'w'},
        {"output", required_argument, 0, 'o'},
        {"dir", required_argument, 0, 'd'},
        {0, 0, 0, 0}
    };

    std::string optstring = "vhVc:o:d:";
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
            case 'V':
                std::cout << "tapemgr version " << VERSION << std::endl;
                exit(0);
            case 'c':
                options.configFile = optarg;
                break;
            case 's':
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
                std::string error;
                json config = AwsTapeDumper::loadConfig(options.configFile, error);
                if (config.is_null()) {
                    throw std::runtime_error("Error loading configuration: " + error);
                }

                AwsTapeMaker tapeMaker(options.volser, options.outputFile,
                                      options.ownerCode.empty() ? "TAPEOWNER" : options.ownerCode,
                                      "TAPEMGR/CREATE", options.verbosity);

                for (const auto& fileConfig : config["files"]) {
                    tapeMaker.addFile(fileConfig);  // Now using JSON directly
                }
                tapeMaker.writeTape();
                break;
            }

            case OperationMode::Extract: {
                if (options.inputFiles.size() != 1) {
                    throw std::runtime_error("Extract mode requires exactly one input tape file");
                }

                std::string error;
                json config = AwsTapeDumper::loadConfig(options.configFile, error);
                if (config.is_null()) {
                    throw std::runtime_error("Error loading configuration: " + error);
                }

                // Pass the output directory to the constructor
                AwsTapeDumper tapeDumper(options.inputFiles[0], options.verbosity, options.outputDir);
                if (!tapeDumper.scanTape()) {
                    throw std::runtime_error("No valid files found on tape");
                }

                // For each requested file in config, extract it
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
                for (const auto& inputFile : options.inputFiles) {
                    // Use the output directory for init mode as well
                    AwsTapeDumper tapeDumper(inputFile, options.verbosity,
                                           options.mode == OperationMode::Init ? options.outputDir : "");

                    if (!tapeDumper.scanTape()) {
                        std::cerr << "Error: No valid files found on tape: " << inputFile << std::endl;
                        continue;
                    }

                    if (options.mode == OperationMode::Init) {
                        // Generate standardized JSON config
                        std::string configFile = options.outputFile.empty() ?
                                               inputFile + ".json" : options.outputFile;
                        tapeDumper.writeConfig(configFile);
                        if (options.verbosity >= VerbosityLevel::Normal) {
                            std::cout << "Configuration template written to: " << configFile << std::endl;
                        }
                    }

                    // Display tape contents if requested
                    if (options.verbosity >= VerbosityLevel::Normal ||
                        options.mode == OperationMode::Scan) {
                        auto files = tapeDumper.getFiles();
                        std::cout << "\nTape: " << inputFile << std::endl;
                        std::cout << "Volume Serial: " << tapeDumper.getVolumeSerial() << std::endl;
                        std::cout << "Files: " << files.size() << std::endl;

                        for (const auto& file : files) {
                            std::cout << "\nDataset: " << file.datasetName << std::endl;
                            std::cout << "  Format: " << file.recordFormat
                                     << (file.blockAttribute != ' ' ?
                                        std::string(1, file.blockAttribute) : "") << std::endl;
                            std::cout << "  Record Length: " << file.recordLength << std::endl;
                            std::cout << "  Block Size: " << file.blockSize << std::endl;
                            std::cout << "  Blocks: " << file.blockCount << std::endl;
                            if (!file.creationDate.empty()) {
                                std::cout << "  Created: " << file.creationDate << std::endl;
                            }
                            if (!file.expirationDate.empty()) {
                                std::cout << "  Expires: " << file.expirationDate << std::endl;
                            }
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
