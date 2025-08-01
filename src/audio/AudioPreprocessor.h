#pragma once

#include <memory>
#include <vector>
#include <atomic>
#include <mutex>
#include <functional>
#include <chrono>
#include "Config.h"
#include "NoiseSuppresor.h"
#include "LyraCodec.h"
#include "BitrateCalculator.h"

namespace NovaVoice {

// Audio preprocessing chain configuration
struct PreprocessingConfig {
    bool enableNoiseSupression = true;
    bool enableCodec = true;
    bool enableBitrateAdaptation = true;
    bool enableVAD = true;
    bool enableAGC = true;  // Automatic Gain Control
    bool enableEcho = false; // Echo cancellation (future)
    
    float noiseSuppressionLevel = 0.8f;
    float vadThreshold = 0.5f;
    float agcTargetLevel = 0.7f;
    uint32_t targetBitrate = Config::LYRA_DEFAULT_BITRATE;
    
    PreprocessingConfig() = default;
};

// Audio statistics
struct AudioStats {
    uint64_t totalSamplesProcessed;
    uint64_t totalFramesProcessed;
    float averageNoiseLevel;
    float averageSpeechProbability;
    float averageGain;
    uint32_t currentBitrate;
    float processingLatency; // ms
    
    AudioStats() : totalSamplesProcessed(0), totalFramesProcessed(0),
                  averageNoiseLevel(0.0f), averageSpeechProbability(0.0f),
                  averageGain(1.0f), currentBitrate(0), processingLatency(0.0f) {}
};

/**
 * @brief Unified Audio Preprocessing Pipeline
 * 
 * Bu sınıf tüm audio preprocessing işlemlerini koordine eder:
 * 1. Noise Suppression (RNNoise)
 * 2. Voice Activity Detection (VAD)
 * 3. Automatic Gain Control (AGC)
 * 4. Audio Codec (Lyra v2)
 * 5. Bitrate Adaptation
 */
class AudioPreprocessor {
public:
    AudioPreprocessor();
    ~AudioPreprocessor();
    
    // === INITIALIZATION ===
    bool initialize(const PreprocessingConfig& config = PreprocessingConfig());
    void shutdown();
    bool isInitialized() const { return initialized_; }
    
    // === PROCESSING ===
    bool processInput(int16_t* audioData, size_t sampleCount);
    bool processInput(float* audioData, size_t sampleCount);
    std::vector<int16_t> processInput(const std::vector<int16_t>& audioData);
    
    bool processOutput(int16_t* audioData, size_t sampleCount);
    bool processOutput(float* audioData, size_t sampleCount);
    std::vector<int16_t> processOutput(const std::vector<int16_t>& audioData);
    
    // === ENCODING/DECODING ===
    std::optional<EncodedPacket> encode(const int16_t* audioData, size_t sampleCount);
    std::optional<std::vector<int16_t>> decode(const EncodedPacket& packet);
    std::optional<std::vector<int16_t>> decode(const uint8_t* encodedData, size_t dataSize);
    
    // === CONFIGURATION ===
    void updateConfig(const PreprocessingConfig& config);
    PreprocessingConfig getConfig() const;
    
    void setNoiseSuppressionLevel(float level);
    void setVADThreshold(float threshold);
    void setTargetGain(float gain);
    void setBitrate(uint32_t bitrate);
    
    // === NETWORK ADAPTATION ===
    void updateNetworkMetrics(const NetworkMetrics& metrics);
    void reportPacketLoss(uint32_t totalPackets, uint32_t lostPackets);
    void reportLatency(uint32_t latencyMs);
    void reportBandwidth(float bandwidthKbps);
    
    // === STATISTICS ===
    AudioStats getStatistics() const;
    NoiseMetrics getNoiseMetrics() const;
    uint32_t getCurrentBitrate() const;
    float getCurrentGain() const;
    bool isSpeechDetected() const;
    
    // === COMPONENT ACCESS ===
    std::shared_ptr<NoiseSuppresor> getNoiseSuppresor() { return noiseSuppresor_; }
    std::shared_ptr<LyraCodec> getCodec() { return codec_; }
    std::shared_ptr<BitrateCalculator> getBitrateCalculator() { return bitrateCalculator_; }
    
    // === CALLBACKS ===
    void setOnSpeechDetected(std::function<void(bool)> callback);
    void setOnBitrateChanged(std::function<void(uint32_t)> callback);
    void setOnQualityChanged(std::function<void(float)> callback);
    
    // === UTILITY ===
    std::string getInfo() const;
    void printStatistics() const;
    
private:
    // Configuration
    bool initialized_;
    PreprocessingConfig config_;
    mutable std::mutex configMutex_;
    
    // Components
    std::shared_ptr<NoiseSuppresor> noiseSuppresor_;
    std::shared_ptr<LyraCodec> codec_;
    std::shared_ptr<BitrateCalculator> bitrateCalculator_;
    
    // Audio statistics
    AudioStats stats_;
    std::atomic<uint64_t> totalProcessedSamples_;
    std::atomic<uint64_t> totalProcessedFrames_;
    
    // AGC state
    float currentGain_;
    float targetGain_;
    std::vector<float> gainHistory_;
    size_t maxGainHistorySize_;
    
    // Processing buffers
    std::vector<float> tempBuffer_;
    std::vector<int16_t> tempBufferInt16_;
    std::vector<float> processBuffer_;
    
    // Sample rate conversion
    std::vector<float> resampleBuffer_;
    std::vector<int16_t> resampleBufferInt16_;
    
    // Timing
    std::chrono::steady_clock::time_point lastProcessTime_;
    std::vector<float> processingTimes_;
    size_t maxTimingHistorySize_;
    
    // Callbacks
    std::function<void(bool)> onSpeechDetected_;
    std::function<void(uint32_t)> onBitrateChanged_;
    std::function<void(float)> onQualityChanged_;
    
    // Processing methods
    bool processAudioChain(float* audioData, size_t sampleCount, bool isInput);
    bool resampleAudio(const float* input, size_t inputSize, uint32_t inputRate,
                      float* output, size_t& outputSize, uint32_t outputRate);
    bool applyAGC(float* audioData, size_t sampleCount);
    bool applyVAD(float* audioData, size_t sampleCount, float speechProbability);
    
    // Utility methods
    void updateStatistics();
    void updateGainControl(const float* audioData, size_t sampleCount);
    void updateBitrateFromNetworkConditions();
    float calculateAudioLevel(const float* audioData, size_t sampleCount);
    void addProcessingTime(float timeMs);
    
    // Sample format conversion
    void int16ToFloat(const int16_t* input, float* output, size_t count);
    void floatToInt16(const float* input, int16_t* output, size_t count);
    
    // Validation
    bool validateConfig(const PreprocessingConfig& config) const;
    bool validateSampleCount(size_t sampleCount) const;
    
    // Component initialization
    bool initializeComponents();
    void shutdownComponents();
    
    // Logging
    void logError(const std::string& message) const;
    void logInfo(const std::string& message) const;
    void logDebug(const std::string& message) const;
};

// Utility functions
namespace PreprocessingUtils {
    // Audio level calculations
    float calculateRMS(const float* audioData, size_t sampleCount);
    float calculatePeak(const float* audioData, size_t sampleCount);
    float dbToLinear(float db);
    float linearToDb(float linear);
    
    // Sample rate conversion
    std::vector<float> resample(const std::vector<float>& input, uint32_t inputRate, uint32_t outputRate);
    std::vector<int16_t> resample(const std::vector<int16_t>& input, uint32_t inputRate, uint32_t outputRate);
    
    // Audio quality assessment
    float calculateQualityScore(const AudioStats& stats, const NetworkMetrics& network);
    std::string formatAudioStats(const AudioStats& stats);
    
    // Configuration helpers
    PreprocessingConfig createLowLatencyConfig();
    PreprocessingConfig createHighQualityConfig();
    PreprocessingConfig createPowerSaveConfig();
}

} // namespace NovaVoice