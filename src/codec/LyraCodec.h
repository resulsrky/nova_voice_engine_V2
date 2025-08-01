#pragma once

#include <memory>
#include <string>
#include <vector>
#include <optional>
#include <atomic>
#include <mutex>
#include "Config.h"
#include "BufferManager.h"

// Lyra v2 forward declarations (conditional)
#ifdef HAVE_LYRA
// Lyra implementation currently disabled - using dummy implementation
// TODO: Implement real Lyra v2 integration
#endif

namespace NovaVoice {

// Codec status
enum class CodecStatus {
    SUCCESS = 0,
    ERROR_INIT = -1,
    ERROR_ENCODE = -2,
    ERROR_DECODE = -3,
    ERROR_INVALID_PARAMS = -4,
    ERROR_NOT_AVAILABLE = -5
};

// Encoded packet structure
struct EncodedPacket {
    std::vector<uint8_t> data;
    uint32_t sequenceNumber;
    uint32_t bitrate;
    uint64_t timestamp;
    
    EncodedPacket() : sequenceNumber(0), bitrate(0), timestamp(0) {}
    
    EncodedPacket(const std::vector<uint8_t>& packetData, uint32_t seqNum, uint32_t br)
        : data(packetData), sequenceNumber(seqNum), bitrate(br), timestamp(0) {}
};

/**
 * @brief Lyra v2 Codec Wrapper
 * 
 * Bu sınıf Google Lyra v2 codec'ini NovaVoice sistemine entegre eder.
 * Lyra mevcut değilse fallback olarak ham ses verisi kullanır.
 */
class LyraCodec {
public:
    LyraCodec();
    ~LyraCodec();
    
    // === INITIALIZATION ===
    bool initialize(uint32_t sampleRate = Config::LYRA_SAMPLE_RATE, 
                   uint32_t channels = Config::CHANNELS,
                   uint32_t bitrate = Config::LYRA_DEFAULT_BITRATE);
    
    bool isInitialized() const { return initialized_; }
    bool isLyraAvailable() const;
    
    // === ENCODING ===
    std::optional<EncodedPacket> encode(const int16_t* audioData, size_t sampleCount);
    std::optional<EncodedPacket> encode(const std::vector<int16_t>& audioSamples);
    
    // === DECODING ===
    std::optional<std::vector<int16_t>> decode(const EncodedPacket& packet);
    std::optional<std::vector<int16_t>> decode(const uint8_t* encodedData, size_t dataSize);
    
    // === CONFIGURATION ===
    bool setBitrate(uint32_t bitrate);
    uint32_t getBitrate() const { return currentBitrate_; }
    uint32_t getSampleRate() const { return sampleRate_; }
    uint32_t getChannels() const { return channels_; }
    uint32_t getFrameSize() const { return frameSize_; }
    
    // === STATISTICS ===
    uint64_t getEncodedFrames() const { return encodedFrames_; }
    uint64_t getDecodedFrames() const { return decodedFrames_; }
    uint64_t getEncodingErrors() const { return encodingErrors_; }
    uint64_t getDecodingErrors() const { return decodingErrors_; }
    
    // === UTILITY ===
    size_t getExpectedInputSize() const;
    size_t getExpectedOutputSize() const;
    std::string getCodecInfo() const;
    
    // === SAMPLE RATE CONVERSION ===
    std::vector<int16_t> resampleTo16kHz(const int16_t* input, size_t inputSamples, uint32_t inputSampleRate);
    std::vector<int16_t> resampleFromLyra(const int16_t* input, size_t inputSamples, uint32_t targetSampleRate);
    
private:
    // Configuration
    bool initialized_;
    uint32_t sampleRate_;
    uint32_t channels_;
    uint32_t currentBitrate_;
    uint32_t frameSize_;
    
    // Sequence numbering
    std::atomic<uint32_t> nextSequenceNumber_;
    
    // Statistics
    std::atomic<uint64_t> encodedFrames_;
    std::atomic<uint64_t> decodedFrames_;
    std::atomic<uint64_t> encodingErrors_;
    std::atomic<uint64_t> decodingErrors_;
    
    // Thread safety
    mutable std::mutex codecMutex_;
    
#ifdef HAVE_LYRA
    // Lyra encoder/decoder instances (void pointers for now)
    void* lyraEncoder_;
    void* lyraDecoder_;
    std::string modelPath_;
#endif
    
    // Internal methods
    bool initializeLyra();
    void cleanup();
    std::vector<uint8_t> encodeRaw(const int16_t* audioData, size_t sampleCount);
    std::vector<int16_t> decodeRaw(const uint8_t* encodedData, size_t dataSize);
    
    // Sample rate conversion helpers
    std::vector<int16_t> simpleSampleRateConversion(const int16_t* input, size_t inputSamples, 
                                                   uint32_t inputRate, uint32_t outputRate);
    
    // Validation
    bool validateParameters(uint32_t sampleRate, uint32_t channels, uint32_t bitrate) const;
    bool validateInputSize(size_t sampleCount) const;
    
    // Error handling
    void logError(const std::string& message) const;
    void logInfo(const std::string& message) const;
};

// Helper functions
namespace CodecUtils {
    std::string statusToString(CodecStatus status);
    bool isValidBitrate(uint32_t bitrate);
    uint32_t calculateOptimalBitrate(uint32_t sampleRate, uint32_t channels, float qualityFactor = 0.5f);
    size_t calculateMaxPacketSize(uint32_t bitrate, uint32_t frameRate);
}

} // namespace NovaVoice