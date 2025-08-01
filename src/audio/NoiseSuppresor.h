#pragma once

#include <memory>
#include <vector>
#include <atomic>
#include <mutex>
#include <string>
#include "Config.h"

// RNNoise forward declarations (conditional)
#ifdef HAVE_RNNOISE
extern "C" {
    struct ReNameNoiseDenoiseState;
    typedef struct ReNameNoiseDenoiseState ReNameNoiseDenoiseState;
}
#endif

namespace NovaVoice {

// Noise suppression metrikleri
struct NoiseMetrics {
    float noiseLevel;           // 0.0 - 1.0 (normalized)
    float speechProbability;    // 0.0 - 1.0 (speech detection confidence)
    float suppression;          // 0.0 - 1.0 (applied suppression amount)
    uint64_t processedFrames;   // İşlenen frame sayısı
    
    NoiseMetrics() : noiseLevel(0.0f), speechProbability(0.0f), suppression(0.0f), processedFrames(0) {}
};

/**
 * @brief RNNoise Tabanlı Gürültü Engelleyici
 * 
 * Bu sınıf RNNoise kütüphanesini kullanarak real-time gürültü engelleme
 * işlemi yapar. RNNoise mevcut değilse basit gürültü azaltma algoritması kullanır.
 */
class NoiseSuppresor {
public:
    NoiseSuppresor();
    ~NoiseSuppresor();
    
    // === INITIALIZATION ===
    bool initialize(uint32_t sampleRate = Config::RNNOISE_SAMPLE_RATE);
    void shutdown();
    bool isInitialized() const { return initialized_; }
    bool isRNNoiseAvailable() const;
    
    // === NOISE SUPPRESSION ===
    bool process(float* audioData, size_t frameSize);
    bool process(int16_t* audioData, size_t frameSize);
    std::vector<float> process(const std::vector<float>& audioData);
    std::vector<int16_t> process(const std::vector<int16_t>& audioData);
    
    // === CONFIGURATION ===
    void setSuppressionLevel(float level);     // 0.0 = none, 1.0 = maximum
    void setThreshold(float threshold);        // Gürültü eşiği
    void enableVAD(bool enable);               // Voice Activity Detection
    void enableAdaptive(bool enable);          // Adaptive suppression
    
    float getSuppressionLevel() const { return suppressionLevel_; }
    float getThreshold() const { return threshold_; }
    bool isVADEnabled() const { return vadEnabled_; }
    bool isAdaptiveEnabled() const { return adaptiveEnabled_; }
    
    // === METRICS ===
    NoiseMetrics getMetrics() const;
    float getCurrentNoiseLevel() const { return currentMetrics_.noiseLevel; }
    float getCurrentSpeechProbability() const { return currentMetrics_.speechProbability; }
    bool isSpeechDetected() const;
    
    // === STATISTICS ===
    uint64_t getProcessedFrames() const { return processedFrames_; }
    uint64_t getTotalSamples() const { return totalSamples_; }
    float getAverageNoiseLevel() const;
    float getAverageSpeechProbability() const;
    
    // === UTILITY ===
    size_t getRequiredFrameSize() const { return Config::RNNOISE_FRAME_SIZE; }
    uint32_t getSampleRate() const { return sampleRate_; }
    std::string getInfo() const;
    
private:
    // Configuration
    bool initialized_;
    uint32_t sampleRate_;
    float suppressionLevel_;    // 0.0 - 1.0
    float threshold_;          // Noise threshold
    bool vadEnabled_;          // Voice Activity Detection
    bool adaptiveEnabled_;     // Adaptive suppression
    
    // RNNoise state
#ifdef HAVE_RNNOISE
    ReNameNoiseDenoiseState* rnnState_;
#endif
    
    // Metrics
    NoiseMetrics currentMetrics_;
    mutable std::mutex metricsMutex_;
    
    // Statistics
    std::atomic<uint64_t> processedFrames_;
    std::atomic<uint64_t> totalSamples_;
    std::vector<float> noiseHistory_;
    std::vector<float> speechHistory_;
    size_t maxHistorySize_;
    
    // Internal buffers
    std::vector<float> tempBuffer_;
    std::vector<float> outputBuffer_;
    
    // Processing methods
    bool processRNNoise(float* audioData, size_t frameSize);
    bool processFallback(float* audioData, size_t frameSize);
    float calculateNoiseLevel(const float* audioData, size_t frameSize);
    float calculateSpeechProbability(const float* audioData, size_t frameSize);
    void applyVAD(float* audioData, size_t frameSize, float speechProb);
    void applyAdaptiveSupression(float* audioData, size_t frameSize);
    
    // Sample format conversion
    void int16ToFloat(const int16_t* input, float* output, size_t count);
    void floatToInt16(const float* input, int16_t* output, size_t count);
    
    // Utility
    void updateMetrics(float noiseLevel, float speechProb, float suppression);
    void addToHistory(float noiseLevel, float speechProb);
    void clampAudio(float* audioData, size_t frameSize);
    
    // Validation
    bool validateFrameSize(size_t frameSize) const;
    bool validateSampleRate(uint32_t sampleRate) const;
    
    // Logging
    void logError(const std::string& message) const;
    void logInfo(const std::string& message) const;
    void logDebug(const std::string& message) const;
};

// Utility functions
namespace NoiseUtils {
    // Audio analysis
    float calculateRMS(const float* audioData, size_t frameSize);
    float calculateZeroCrossingRate(const float* audioData, size_t frameSize);
    float calculateSpectralCentroid(const float* audioData, size_t frameSize, uint32_t sampleRate);
    
    // Noise detection
    bool detectNoise(const float* audioData, size_t frameSize, float threshold);
    bool detectSpeech(const float* audioData, size_t frameSize, float threshold);
    
    // Simple noise reduction (fallback)
    void simpleNoiseReduction(float* audioData, size_t frameSize, float strength);
    void spectralSubtraction(float* audioData, size_t frameSize, const float* noiseProfile, float alpha);
    
    // Audio quality metrics
    float calculateSNR(const float* signal, const float* noise, size_t frameSize);
    float calculateTHD(const float* audioData, size_t frameSize, uint32_t sampleRate);
}

} // namespace NovaVoice