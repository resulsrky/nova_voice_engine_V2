#include "BitrateCalculator.h"
#include <iostream>
#include <algorithm>
#include <numeric>
#include <cmath>

namespace NovaVoice {

BitrateCalculator::BitrateCalculator()
    : initialized_(false)
    , currentBitrate_(Config::LYRA_DEFAULT_BITRATE)
    , recommendedBitrate_(Config::LYRA_DEFAULT_BITRATE)
    , targetQuality_(0.5f)
    , adaptationSpeed_(0.3f)
    , stabilityThreshold_(0.1f)
    , qualityMode_(QualityMode::ADAPTIVE)
    , autoAdaptationEnabled_(true)
    , maxHistorySize_(100)
    , bitrateChanges_(0) {
    
    bitrateHistory_.reserve(maxHistorySize_);
    bitrateTimestamps_.reserve(maxHistorySize_);
}

BitrateCalculator::~BitrateCalculator() {
    shutdown();
}

bool BitrateCalculator::initialize(uint32_t initialBitrate) {
    if (initialized_) {
        return true;
    }
    
    currentBitrate_.store(clampBitrate(initialBitrate));
    recommendedBitrate_.store(currentBitrate_.load());
    
    startTime_ = std::chrono::steady_clock::now();
    lastUpdateTime_ = startTime_;
    
    // İlk değerleri history'ye ekle
    addToHistory(currentBitrate_);
    
    initialized_ = true;
    
    std::cout << "[BitrateCalculator] Başlatıldı - İlk bitrate: " 
              << currentBitrate_ << " bps" << std::endl;
    
    return true;
}

void BitrateCalculator::shutdown() {
    if (!initialized_) {
        return;
    }
    
    initialized_ = false;
    bitrateHistory_.clear();
    bitrateTimestamps_.clear();
    
    std::cout << "[BitrateCalculator] Kapatıldı" << std::endl;
}

uint32_t BitrateCalculator::calculateOptimalBitrate() {
    if (!initialized_) {
        return Config::LYRA_DEFAULT_BITRATE;
    }
    
    std::lock_guard<std::mutex> lock(metricsMutex_);
    return calculateOptimalBitrate(networkMetrics_, audioMetrics_);
}

uint32_t BitrateCalculator::calculateOptimalBitrate(const NetworkMetrics& network, const AudioMetrics& audio) {
    // Network koşullarına göre bitrate hesapla
    uint32_t networkBitrate = calculateNetworkBasedBitrate(network);
    
    // Audio kompleksitesine göre bitrate hesapla
    uint32_t audioBitrate = calculateAudioBasedBitrate(audio);
    
    // İkisinin ağırlıklı ortalamasını al
    float networkWeight = 0.6f;  // Network koşulları daha önemli
    float audioWeight = 0.4f;    // Audio kalitesi ikinci öncelik
    
    uint32_t combinedBitrate = static_cast<uint32_t>(
        networkBitrate * networkWeight + audioBitrate * audioWeight
    );
    
    // Quality mode'a göre ayarla
    uint32_t adjustedBitrate = applyQualityMode(combinedBitrate);
    
    // Smooth transition uygula
    uint32_t finalBitrate = smoothBitrateTransition(adjustedBitrate);
    
    // Sınırları kontrol et
    finalBitrate = clampBitrate(finalBitrate);
    
    return finalBitrate;
}

void BitrateCalculator::updateNetworkMetrics(const NetworkMetrics& metrics) {
    std::lock_guard<std::mutex> lock(metricsMutex_);
    networkMetrics_ = metrics;
    
    if (autoAdaptationEnabled_) {
        uint32_t newBitrate = calculateOptimalBitrate();
        if (shouldUpdateBitrate(newBitrate)) {
            uint32_t oldBitrate = currentBitrate_;
            currentBitrate_.store(newBitrate);
            recommendedBitrate_.store(newBitrate);
            addToHistory(newBitrate);
            bitrateChanges_++;
            
            logBitrateChange(oldBitrate, newBitrate, "Network conditions");
        }
    }
}

void BitrateCalculator::updateAudioMetrics(const AudioMetrics& metrics) {
    std::lock_guard<std::mutex> lock(metricsMutex_);
    audioMetrics_ = metrics;
    
    if (autoAdaptationEnabled_) {
        uint32_t newBitrate = calculateOptimalBitrate();
        if (shouldUpdateBitrate(newBitrate)) {
            uint32_t oldBitrate = currentBitrate_;
            currentBitrate_.store(newBitrate);
            recommendedBitrate_.store(newBitrate);
            addToHistory(newBitrate);
            bitrateChanges_++;
            
            logBitrateChange(oldBitrate, newBitrate, "Audio characteristics");
        }
    }
}

void BitrateCalculator::reportPacketLoss(uint32_t totalPackets, uint32_t lostPackets) {
    if (totalPackets == 0) return;
    
    float lossRate = static_cast<float>(lostPackets) / static_cast<float>(totalPackets);
    
    std::lock_guard<std::mutex> lock(metricsMutex_);
    networkMetrics_.packetLossRate = lossRate;
}

void BitrateCalculator::reportLatency(uint32_t latencyMs) {
    std::lock_guard<std::mutex> lock(metricsMutex_);
    
    // Exponential moving average
    float alpha = 0.3f;
    networkMetrics_.averageLatency = static_cast<uint32_t>(
        alpha * latencyMs + (1.0f - alpha) * networkMetrics_.averageLatency
    );
}

void BitrateCalculator::reportBandwidth(float bandwidthKbps) {
    std::lock_guard<std::mutex> lock(metricsMutex_);
    networkMetrics_.bandwidth = bandwidthKbps;
}

void BitrateCalculator::setTargetQuality(float quality) {
    targetQuality_ = std::max(0.0f, std::min(1.0f, quality));
}

void BitrateCalculator::setAdaptationSpeed(float speed) {
    adaptationSpeed_ = std::max(0.0f, std::min(1.0f, speed));
}

void BitrateCalculator::setStabilityThreshold(float threshold) {
    stabilityThreshold_ = std::max(0.0f, std::min(1.0f, threshold));
}

NetworkMetrics BitrateCalculator::getNetworkMetrics() const {
    std::lock_guard<std::mutex> lock(metricsMutex_);
    return networkMetrics_;
}

AudioMetrics BitrateCalculator::getAudioMetrics() const {
    std::lock_guard<std::mutex> lock(metricsMutex_);
    return audioMetrics_;
}

float BitrateCalculator::getAverageBitrate() const {
    if (bitrateHistory_.empty()) {
        return static_cast<float>(currentBitrate_);
    }
    
    uint64_t sum = std::accumulate(bitrateHistory_.begin(), bitrateHistory_.end(), 0ULL);
    return static_cast<float>(sum) / static_cast<float>(bitrateHistory_.size());
}

std::vector<uint32_t> BitrateCalculator::getBitrateHistory() const {
    return bitrateHistory_;
}

void BitrateCalculator::enableAutoAdaptation(bool enable) {
    autoAdaptationEnabled_ = enable;
    
    if (enable) {
        std::cout << "[BitrateCalculator] Otomatik adaptasyon etkinleştirildi" << std::endl;
    } else {
        std::cout << "[BitrateCalculator] Otomatik adaptasyon devre dışı bırakıldı" << std::endl;
    }
}

void BitrateCalculator::setQualityMode(QualityMode mode) {
    qualityMode_ = mode;
    
    std::cout << "[BitrateCalculator] Kalite modu değiştirildi: " 
              << BitrateUtils::qualityModeToString(mode) << std::endl;
    
    // Mode değişikliğinde bitrate'i yeniden hesapla
    if (autoAdaptationEnabled_) {
        uint32_t newBitrate = calculateOptimalBitrate();
        if (newBitrate != currentBitrate_) {
            uint32_t oldBitrate = currentBitrate_;
            currentBitrate_.store(newBitrate);
            recommendedBitrate_.store(newBitrate);
            addToHistory(newBitrate);
            bitrateChanges_++;
            
            logBitrateChange(oldBitrate, newBitrate, "Quality mode change");
        }
    }
}

// PRIVATE METHODS

uint32_t BitrateCalculator::calculateNetworkBasedBitrate(const NetworkMetrics& metrics) {
    uint32_t baseBitrate = Config::LYRA_DEFAULT_BITRATE;
    
    // Packet loss'a göre ayarlama
    if (metrics.packetLossRate > 0.05f) {  // %5'ten fazla loss
        baseBitrate = Config::LYRA_MIN_BITRATE;
    } else if (metrics.packetLossRate > 0.02f) {  // %2-5 arası loss
        baseBitrate = (Config::LYRA_MIN_BITRATE + Config::LYRA_DEFAULT_BITRATE) / 2;
    }
    
    // Latency'ye göre ayarlama
    if (metrics.averageLatency > 500) {  // 500ms'den fazla
        baseBitrate = std::min(baseBitrate, Config::LYRA_MIN_BITRATE);
    } else if (metrics.averageLatency > 200) {  // 200-500ms arası
        baseBitrate = std::min(baseBitrate, (Config::LYRA_MIN_BITRATE + Config::LYRA_DEFAULT_BITRATE) / 2);
    }
    
    // Bandwidth'e göre ayarlama
    if (metrics.bandwidth > 0) {
        uint32_t bandwidthBasedBitrate = static_cast<uint32_t>(metrics.bandwidth * 1000 * 0.8f); // %80'ini kullan
        baseBitrate = std::min(baseBitrate, bandwidthBasedBitrate);
    }
    
    return baseBitrate;
}

uint32_t BitrateCalculator::calculateAudioBasedBitrate(const AudioMetrics& metrics) {
    uint32_t baseBitrate = Config::LYRA_DEFAULT_BITRATE;
    
    // Speech detection
    if (!metrics.speechDetected) {
        // Konuşma yoksa daha düşük bitrate
        return Config::LYRA_MIN_BITRATE;
    }
    
    // Volume level'a göre
    if (metrics.averageVolume > 0.7f) {
        // Yüksek volume, daha yüksek bitrate gerekebilir
        baseBitrate = Config::LYRA_MAX_BITRATE;
    } else if (metrics.averageVolume < 0.1f) {
        // Çok düşük volume
        baseBitrate = Config::LYRA_MIN_BITRATE;
    }
    
    // SNR'a göre
    if (metrics.signalToNoiseRatio > 20.0f) {
        // İyi SNR, yüksek kalite kullanılabilir
        baseBitrate = std::max(baseBitrate, Config::LYRA_DEFAULT_BITRATE);
    } else if (metrics.signalToNoiseRatio < 10.0f) {
        // Kötü SNR, düşük bitrate kullan
        baseBitrate = Config::LYRA_MIN_BITRATE;
    }
    
    return baseBitrate;
}

uint32_t BitrateCalculator::applyQualityMode(uint32_t baseBitrate) {
    switch (qualityMode_) {
        case QualityMode::POWER_SAVE:
            return Config::LYRA_MIN_BITRATE;
            
        case QualityMode::BALANCED:
            return std::min(baseBitrate, Config::LYRA_DEFAULT_BITRATE);
            
        case QualityMode::HIGH_QUALITY:
            return std::max(baseBitrate, Config::LYRA_MAX_BITRATE);
            
        case QualityMode::ADAPTIVE:
        default:
            // Target quality'ye göre ayarla
            uint32_t minBitrate = Config::LYRA_MIN_BITRATE;
            uint32_t maxBitrate = Config::LYRA_MAX_BITRATE;
            uint32_t targetBitrate = static_cast<uint32_t>(
                minBitrate + (maxBitrate - minBitrate) * targetQuality_
            );
            return std::min(baseBitrate, targetBitrate);
    }
}

uint32_t BitrateCalculator::smoothBitrateTransition(uint32_t newBitrate) {
    uint32_t currentBitrate = currentBitrate_;
    
    // Adaptation speed'e göre geçiş hızını ayarla
    float diff = static_cast<float>(newBitrate) - static_cast<float>(currentBitrate);
    float smoothedDiff = diff * adaptationSpeed_;
    
    uint32_t smoothedBitrate = static_cast<uint32_t>(currentBitrate + smoothedDiff);
    
    return smoothedBitrate;
}

uint32_t BitrateCalculator::clampBitrate(uint32_t bitrate) {
    return std::max(Config::LYRA_MIN_BITRATE, 
                   std::min(Config::LYRA_MAX_BITRATE, bitrate));
}

bool BitrateCalculator::shouldUpdateBitrate(uint32_t newBitrate) {
    uint32_t currentBitrate = currentBitrate_;
    
    float changeRatio = std::abs(static_cast<float>(newBitrate) - static_cast<float>(currentBitrate)) 
                       / static_cast<float>(currentBitrate);
    
    return changeRatio >= stabilityThreshold_;
}

void BitrateCalculator::addToHistory(uint32_t bitrate) {
    auto now = std::chrono::steady_clock::now();
    
    bitrateHistory_.push_back(bitrate);
    bitrateTimestamps_.push_back(now);
    
    // History boyutunu sınırla
    if (bitrateHistory_.size() > maxHistorySize_) {
        bitrateHistory_.erase(bitrateHistory_.begin());
        bitrateTimestamps_.erase(bitrateTimestamps_.begin());
    }
    
    lastUpdateTime_ = now;
}

void BitrateCalculator::cleanupHistory() {
    auto now = std::chrono::steady_clock::now();
    auto cutoff = now - std::chrono::minutes(10); // 10 dakikadan eski kayıtları sil
    
    while (!bitrateTimestamps_.empty() && bitrateTimestamps_.front() < cutoff) {
        bitrateHistory_.erase(bitrateHistory_.begin());
        bitrateTimestamps_.erase(bitrateTimestamps_.begin());
    }
}

void BitrateCalculator::logBitrateChange(uint32_t oldBitrate, uint32_t newBitrate, const std::string& reason) {
    std::cout << "[BitrateCalculator] Bitrate değişti: " 
              << oldBitrate << " -> " << newBitrate << " bps (Sebep: " << reason << ")" << std::endl;
}

// UTILITY FUNCTIONS

namespace BitrateUtils {

float evaluateNetworkQuality(const NetworkMetrics& metrics) {
    float quality = 1.0f;
    
    // Packet loss penalty
    quality *= (1.0f - metrics.packetLossRate);
    
    // Latency penalty
    if (metrics.averageLatency > 100) {
        quality *= std::max(0.1f, 1.0f - (metrics.averageLatency - 100) / 1000.0f);
    }
    
    // Jitter penalty
    if (metrics.jitter > 50) {
        quality *= std::max(0.5f, 1.0f - (metrics.jitter - 50) / 500.0f);
    }
    
    return std::max(0.0f, std::min(1.0f, quality));
}

float evaluateAudioComplexity(const AudioMetrics& metrics) {
    float complexity = 0.5f; // Base complexity
    
    if (metrics.speechDetected) {
        complexity = 0.8f; // Speech is more complex than silence
    }
    
    // Volume influence
    complexity += metrics.averageVolume * 0.3f;
    
    // SNR influence (better SNR = less complex encoding needed)
    if (metrics.signalToNoiseRatio > 0) {
        complexity -= std::min(0.3f, metrics.signalToNoiseRatio / 100.0f);
    }
    
    return std::max(0.1f, std::min(1.0f, complexity));
}

uint32_t getBitrateForLatency(uint32_t latencyMs) {
    if (latencyMs > 500) return Config::LYRA_MIN_BITRATE;
    if (latencyMs > 200) return Config::LYRA_DEFAULT_BITRATE;
    return Config::LYRA_MAX_BITRATE;
}

uint32_t getBitrateForPacketLoss(float lossRate) {
    if (lossRate > 0.05f) return Config::LYRA_MIN_BITRATE;
    if (lossRate > 0.01f) return Config::LYRA_DEFAULT_BITRATE;
    return Config::LYRA_MAX_BITRATE;
}

uint32_t getBitrateForBandwidth(float bandwidthKbps) {
    uint32_t maxUsableBitrate = static_cast<uint32_t>(bandwidthKbps * 1000 * 0.8f); // %80'ini kullan
    return std::min(maxUsableBitrate, Config::LYRA_MAX_BITRATE);
}

float calculateQualityScore(uint32_t bitrate, const NetworkMetrics& network, const AudioMetrics& audio) {
    float bitrateScore = static_cast<float>(bitrate - Config::LYRA_MIN_BITRATE) / 
                        static_cast<float>(Config::LYRA_MAX_BITRATE - Config::LYRA_MIN_BITRATE);
    
    float networkScore = evaluateNetworkQuality(network);
    float audioScore = 1.0f - evaluateAudioComplexity(audio); // Less complexity = better quality
    
    return (bitrateScore * 0.4f + networkScore * 0.4f + audioScore * 0.2f);
}

std::string qualityModeToString(BitrateCalculator::QualityMode mode) {
    switch (mode) {
        case BitrateCalculator::QualityMode::POWER_SAVE: return "Power Save";
        case BitrateCalculator::QualityMode::BALANCED: return "Balanced";
        case BitrateCalculator::QualityMode::HIGH_QUALITY: return "High Quality";
        case BitrateCalculator::QualityMode::ADAPTIVE: return "Adaptive";
        default: return "Unknown";
    }
}

} // namespace BitrateUtils

} // namespace NovaVoice