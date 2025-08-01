#pragma once

#include <atomic>
#include <chrono>
#include <memory>
#include <vector>
#include <mutex>
#include "Config.h"

namespace NovaVoice {

// Network kalitesi metrikleri
struct NetworkMetrics {
    float packetLossRate;        // 0.0 - 1.0
    uint32_t averageLatency;     // ms
    uint32_t jitter;             // ms
    float bandwidth;             // kbps
    
    NetworkMetrics() : packetLossRate(0.0f), averageLatency(0), jitter(0), bandwidth(0.0f) {}
};

// Audio kalitesi metrikleri
struct AudioMetrics {
    float signalToNoiseRatio;    // dB
    float averageVolume;         // 0.0 - 1.0
    bool speechDetected;         // Konuşma algılandı mı
    float compressionRatio;      // Sıkıştırma oranı
    
    AudioMetrics() : signalToNoiseRatio(0.0f), averageVolume(0.0f), speechDetected(false), compressionRatio(1.0f) {}
};

/**
 * @brief Otomatik Bitrate Hesaplama ve Ayarlama Sistemi
 * 
 * Bu sınıf ağ koşulları, ses kalitesi ve performans metriklerine göre
 * optimal bitrate'i hesaplar ve Lyra codec'ine uygular.
 */
class BitrateCalculator {
public:
    BitrateCalculator();
    ~BitrateCalculator();
    
    // === INITIALIZATION ===
    bool initialize(uint32_t initialBitrate = Config::LYRA_DEFAULT_BITRATE);
    void shutdown();
    
    // === BITRATE CALCULATION ===
    uint32_t calculateOptimalBitrate();
    uint32_t calculateOptimalBitrate(const NetworkMetrics& network, const AudioMetrics& audio);
    
    // === METRICS INPUT ===
    void updateNetworkMetrics(const NetworkMetrics& metrics);
    void updateAudioMetrics(const AudioMetrics& metrics);
    void reportPacketLoss(uint32_t totalPackets, uint32_t lostPackets);
    void reportLatency(uint32_t latencyMs);
    void reportBandwidth(float bandwidthKbps);
    
    // === CONFIGURATION ===
    void setTargetQuality(float quality); // 0.0 = minimum, 1.0 = maximum
    void setAdaptationSpeed(float speed);  // 0.0 = slow, 1.0 = fast
    void setStabilityThreshold(float threshold); // Bitrate değişim eşiği
    
    // === GETTERS ===
    uint32_t getCurrentBitrate() const { return currentBitrate_; }
    uint32_t getRecommendedBitrate() const { return recommendedBitrate_; }
    NetworkMetrics getNetworkMetrics() const;
    AudioMetrics getAudioMetrics() const;
    
    // === STATISTICS ===
    uint64_t getBitrateChanges() const { return bitrateChanges_; }
    float getAverageBitrate() const;
    std::vector<uint32_t> getBitrateHistory() const;
    
    // === ADAPTIVE FEATURES ===
    void enableAutoAdaptation(bool enable);
    bool isAutoAdaptationEnabled() const { return autoAdaptationEnabled_; }
    
    // === QUALITY MODES ===
    enum class QualityMode {
        POWER_SAVE,    // En düşük bitrate
        BALANCED,      // Dengelenmiş
        HIGH_QUALITY,  // En yüksek kalite
        ADAPTIVE       // Otomatik uyarlanabilir
    };
    
    void setQualityMode(QualityMode mode);
    QualityMode getQualityMode() const { return qualityMode_; }
    
private:
    // Configuration
    bool initialized_;
    std::atomic<uint32_t> currentBitrate_;
    std::atomic<uint32_t> recommendedBitrate_;
    
    // Adaptation parameters
    float targetQuality_;      // 0.0 - 1.0
    float adaptationSpeed_;    // 0.0 - 1.0
    float stabilityThreshold_; // Minimum değişim eşiği
    
    // Quality mode
    QualityMode qualityMode_;
    bool autoAdaptationEnabled_;
    
    // Metrics
    NetworkMetrics networkMetrics_;
    AudioMetrics audioMetrics_;
    mutable std::mutex metricsMutex_;
    
    // History tracking
    std::vector<uint32_t> bitrateHistory_;
    std::vector<std::chrono::steady_clock::time_point> bitrateTimestamps_;
    size_t maxHistorySize_;
    
    // Statistics
    std::atomic<uint64_t> bitrateChanges_;
    std::chrono::steady_clock::time_point lastUpdateTime_;
    std::chrono::steady_clock::time_point startTime_;
    
    // Calculation helpers
    uint32_t calculateNetworkBasedBitrate(const NetworkMetrics& metrics);
    uint32_t calculateAudioBasedBitrate(const AudioMetrics& metrics);
    uint32_t applyQualityMode(uint32_t baseBitrate);
    uint32_t smoothBitrateTransition(uint32_t newBitrate);
    
    // Validation
    uint32_t clampBitrate(uint32_t bitrate);
    bool shouldUpdateBitrate(uint32_t newBitrate);
    
    // History management
    void addToHistory(uint32_t bitrate);
    void cleanupHistory();
    
    // Utility
    float calculateWeightedAverage(const std::vector<uint32_t>& values, 
                                  const std::vector<float>& weights);
    void logBitrateChange(uint32_t oldBitrate, uint32_t newBitrate, const std::string& reason);
};

// Utility functions
namespace BitrateUtils {
    // Ağ koşulları değerlendirme
    float evaluateNetworkQuality(const NetworkMetrics& metrics);
    float evaluateAudioComplexity(const AudioMetrics& metrics);
    
    // Bitrate önerileri
    uint32_t getBitrateForLatency(uint32_t latencyMs);
    uint32_t getBitrateForPacketLoss(float lossRate);
    uint32_t getBitrateForBandwidth(float bandwidthKbps);
    
    // Kalite hesaplamaları
    float calculateQualityScore(uint32_t bitrate, const NetworkMetrics& network, const AudioMetrics& audio);
    std::string qualityModeToString(BitrateCalculator::QualityMode mode);
}

} // namespace NovaVoice