#ifndef EBCDIC_CONVERTER_H
#define EBCDIC_CONVERTER_H

#include <memory>
#include <vector>
#include <string>
#include <cstdint>
#include <unordered_map>

// Forward declarations
class EbcdicConverter;
class StreamingEbcdicConverter;

enum class EbcdicCodePage {
    CP037,
    CP273,
    CP277,
    CP285
    // Add others as needed
};

// Abstract base class defining the interface
class IEbcdicConverter {
public:
    virtual ~IEbcdicConverter() = default;
    
    // Core conversion methods
    virtual std::vector<uint8_t> utf8ToEbcdic(const std::vector<uint8_t>& input) = 0;
    virtual std::vector<uint8_t> ebcdicToUtf8(const unsigned char* ebcdicStr, size_t length) = 0;
    
    // Helper methods that can be implemented in terms of core methods
    virtual std::vector<uint8_t> utf8ToEbcdic(const std::string& input);
    virtual std::string ebcdicToUtf8String(const unsigned char* ebcdicStr, size_t length, bool trim = false);
    virtual std::string ebcdicToUtf8String(const std::vector<uint8_t>& ebcdicData, bool trim = false);
    
    // Common utilities
    virtual char ebcdicToAscii(unsigned char eb) = 0;
    
    // Factory method to create appropriate converter
    static std::shared_ptr<IEbcdicConverter> create(
        EbcdicCodePage codepage,
        bool streamingMode = false,
        size_t bufferSize = 1024 * 1024  // 1MB default buffer
    );

protected:
    // Protected constructor to prevent direct instantiation
    IEbcdicConverter() = default;
};

// Implementation for memory-based conversion (current approach)
class EbcdicConverter : public IEbcdicConverter {
public:
    explicit EbcdicConverter(EbcdicCodePage codepage);
    
    std::vector<uint8_t> utf8ToEbcdic(const std::vector<uint8_t>& input) override;
    std::vector<uint8_t> ebcdicToUtf8(const unsigned char* ebcdicStr, size_t length) override;
    char ebcdicToAscii(unsigned char eb) override;

private:
    EbcdicCodePage codepage_;
    const unsigned char* utf8_FirstByte_;
    const unsigned char* tr_itt_;
    const unsigned short* tr_sot_;
    const unsigned short* tr_sbt_;
    const uint8_t* toUtf8Lengths_;
    const uint8_t (*toUtf8Bytes_)[2];
};

// Implementation for streaming conversion of large files
class StreamingEbcdicConverter : public IEbcdicConverter {
public:
    StreamingEbcdicConverter(EbcdicCodePage codepage, size_t bufferSize);
    
    // Override core methods with streaming implementations
    std::vector<uint8_t> utf8ToEbcdic(const std::vector<uint8_t>& input) override;
    std::vector<uint8_t> ebcdicToUtf8(const unsigned char* ebcdicStr, size_t length) override;
    char ebcdicToAscii(unsigned char eb) override;

private:
    EbcdicCodePage codepage_;
    size_t bufferSize_;
    std::vector<uint8_t> buffer_;
    
    // Tables same as EbcdicConverter
    const unsigned char* utf8_FirstByte_;
    const unsigned char* tr_itt_;
    const unsigned short* tr_sot_;
    const unsigned short* tr_sbt_;
    const uint8_t* toUtf8Lengths_;
    const uint8_t (*toUtf8Bytes_)[2];
    
    // Streaming helper methods
    void processBuffer(const unsigned char* input, size_t length, 
                      std::vector<uint8_t>& output, bool isLastChunk);
    size_t findSafeBufferEnd(const unsigned char* buffer, size_t length);
};

// Factory implementation
inline std::shared_ptr<IEbcdicConverter> IEbcdicConverter::create(
    EbcdicCodePage codepage,
    bool streamingMode,
    size_t bufferSize
) {
    if (streamingMode) {
        return std::make_shared<StreamingEbcdicConverter>(codepage, bufferSize);
    }
    return std::make_shared<EbcdicConverter>(codepage);
}

// Table registry to manage codepage-specific conversion tables
class EbcdicTableRegistry {
public:
    static EbcdicTableRegistry& getInstance() {
        static EbcdicTableRegistry instance;
        return instance;
    }
    
    struct ConversionTables {
        const unsigned char* utf8_FirstByte;
        const unsigned char* tr_itt;
        const unsigned short* tr_sot;
        const unsigned short* tr_sbt;
        const uint8_t* toUtf8Lengths;
        const uint8_t (*toUtf8Bytes)[2];
    };
    
    const ConversionTables& getTables(EbcdicCodePage codepage);

private:
    EbcdicTableRegistry();  // Private constructor
    std::unordered_map<EbcdicCodePage, ConversionTables> tables_;
};

#endif // EBCDIC_CONVERTER_H
