#include "NoiseSuppresor.h"
#include <iostream>
#include <algorithm>
#include <cmath>
#include <numeric>

// RNNoise includes (conditional)
#ifdef HAVE_RNNOISE
extern "C" {
#include "renamenoise.h"
}
#endif

namespace NovaVoice {

NoiseSuppresor::NoiseSuppresor()
    : initialized_(false)
    , sampleRate_(Config::RNNOISE_SAMPLE_RATE)
    , suppressionLevel_(0.8f)
    , threshold_(0.5f)
    , vadEnabled_(true)
    , adaptiveEnabled_(true)
#ifdef HAVE_RNNOISE
    , rnnState_(nullptr)
#endif
    , processedFrames_(0)
    , totalSamples_(0)
    , maxHistorySize_(100) {
    
    noiseHistory_.reserve(maxHistorySize_);
    speechHistory_.reserve(maxHistorySize_);
    tempBuffer_.resize(Config::RNNOISE_FRAME_SIZE);
    outputBuffer_.resize(Config::RNNOISE_FRAME_SIZE);
}

NoiseSuppresor::~NoiseSuppresor() {
    shutdown();
}

bool NoiseSuppresor::initialize(uint32_t sampleRate) {
    if (initialized_) {
        logError("NoiseSuppresor zaten başlatılmış");
        return false;
    }
    
    if (!validateSampleRate(sampleRate)) {
        logError("Desteklenmeyen sample rate: " + std::to_string(sampleRate));
        return false;
    }
    
    sampleRate_ = sampleRate;
    
#ifdef HAVE_RNNOISE
    // RNNoise'i initialize et
    try {
        rnnState_ = renamenoise_create(nullptr);
        if (!rnnState_) {
            logError("RNNoise state oluşturulamadı");
            return false;
        }
        logInfo("RNNoise başarıyla başlatıldı");
    } catch (const std::exception& e) {
        logError("RNNoise initialization hatası: " + std::string(e.what()));
        return false;
    }
#else
    logInfo("RNNoise mevcut değil, fallback algoritma kullanılacak");
#endif
    
    initialized_ = true;
    logInfo("NoiseSuppresor başlatıldı - Sample Rate: " + std::to_string(sampleRate_) + " Hz");
    
    return true;
}

void NoiseSuppresor::shutdown() {
    if (!initialized_) {
        return;
    }
    
#ifdef HAVE_RNNOISE
    if (rnnState_) {
        renamenoise_destroy(rnnState_);
        rnnState_ = nullptr;
    }
#endif
    
    initialized_ = false;
    noiseHistory_.clear();
    speechHistory_.clear();
    
    logInfo("NoiseSuppresor kapatıldı");
}

bool NoiseSuppresor::isRNNoiseAvailable() const {
#ifdef HAVE_RNNOISE
    return rnnState_ != nullptr;
#else
    return false;
#endif
}

bool NoiseSuppresor::process(float* audioData, size_t frameSize) {
    if (!initialized_) {
        logError("NoiseSuppresor başlatılmamış");
        return false;
    }
    
    if (!audioData || !validateFrameSize(frameSize)) {
        logError("Geçersiz audio data veya frame size");
        return false;
    }
    
    try {
        bool success = false;
        
#ifdef HAVE_RNNOISE
        if (isRNNoiseAvailable()) {
            success = processRNNoise(audioData, frameSize);
        } else {
            success = processFallback(audioData, frameSize);
        }
#else
        success = processFallback(audioData, frameSize);
#endif
        
        if (success) {
            processedFrames_++;
            totalSamples_ += frameSize;
        }
        
        return success;
        
    } catch (const std::exception& e) {
        logError("Process exception: " + std::string(e.what()));
        return false;
    }
}

bool NoiseSuppresor::process(int16_t* audioData, size_t frameSize) {
    if (!validateFrameSize(frameSize)) {
        return false;
    }
    
    // int16 -> float conversion
    int16ToFloat(audioData, tempBuffer_.data(), frameSize);
    
    // Process
    bool success = process(tempBuffer_.data(), frameSize);
    
    if (success) {
        // float -> int16 conversion
        floatToInt16(tempBuffer_.data(), audioData, frameSize);
    }
    
    return success;
}

std::vector<float> NoiseSuppresor::process(const std::vector<float>& audioData) {
    std::vector<float> result = audioData;
    
    if (process(result.data(), result.size())) {
        return result;
    }
    
    return {}; // Return empty on error
}

std::vector<int16_t> NoiseSuppresor::process(const std::vector<int16_t>& audioData) {
    std::vector<int16_t> result = audioData;
    
    if (process(result.data(), result.size())) {
        return result;
    }
    
    return {}; // Return empty on error
}

void NoiseSuppresor::setSuppressionLevel(float level) {
    suppressionLevel_ = std::max(0.0f, std::min(1.0f, level));
    logInfo("Suppression level ayarlandı: " + std::to_string(suppressionLevel_));
}

void NoiseSuppresor::setThreshold(float threshold) {
    threshold_ = std::max(0.0f, std::min(1.0f, threshold));
    logInfo("Threshold ayarlandı: " + std::to_string(threshold_));
}

void NoiseSuppresor::enableVAD(bool enable) {
    vadEnabled_ = enable;
    logInfo("VAD " + std::string(enable ? "etkinleştirildi" : "devre dışı bırakıldı"));
}

void NoiseSuppresor::enableAdaptive(bool enable) {
    adaptiveEnabled_ = enable;
    logInfo("Adaptive suppression " + std::string(enable ? "etkinleştirildi" : "devre dışı bırakıldı"));
}

NoiseMetrics NoiseSuppresor::getMetrics() const {
    std::lock_guard<std::mutex> lock(metricsMutex_);
    return currentMetrics_;
}

bool NoiseSuppresor::isSpeechDetected() const {
    return getCurrentSpeechProbability() > threshold_;
}

float NoiseSuppresor::getAverageNoiseLevel() const {
    if (noiseHistory_.empty()) {
        return 0.0f;
    }
    
    float sum = std::accumulate(noiseHistory_.begin(), noiseHistory_.end(), 0.0f);
    return sum / static_cast<float>(noiseHistory_.size());
}

float NoiseSuppresor::getAverageSpeechProbability() const {
    if (speechHistory_.empty()) {
        return 0.0f;
    }
    
    float sum = std::accumulate(speechHistory_.begin(), speechHistory_.end(), 0.0f);
    return sum / static_cast<float>(speechHistory_.size());
}

std::string NoiseSuppresor::getInfo() const {
    std::string info = "NoiseSuppresor Info:\n";
    info += "Sample Rate: " + std::to_string(sampleRate_) + " Hz\n";
    info += "Frame Size: " + std::to_string(Config::RNNOISE_FRAME_SIZE) + " samples\n";
    info += "RNNoise Available: " + std::string(isRNNoiseAvailable() ? "Yes" : "No") + "\n";
    info += "Suppression Level: " + std::to_string(suppressionLevel_) + "\n";
    info += "Threshold: " + std::to_string(threshold_) + "\n";
    info += "VAD Enabled: " + std::string(vadEnabled_ ? "Yes" : "No") + "\n";
    info += "Adaptive Enabled: " + std::string(adaptiveEnabled_ ? "Yes" : "No") + "\n";
    info += "Processed Frames: " + std::to_string(processedFrames_) + "\n";
    info += "Total Samples: " + std::to_string(totalSamples_) + "\n";
    info += "Current Noise Level: " + std::to_string(getCurrentNoiseLevel()) + "\n";
    info += "Current Speech Probability: " + std::to_string(getCurrentSpeechProbability());
    
    return info;
}

// PRIVATE METHODS

bool NoiseSuppresor::processRNNoise(float* audioData, size_t frameSize) {
#ifdef HAVE_RNNOISE
    if (!rnnState_ || frameSize != Config::RNNOISE_FRAME_SIZE) {
        return false;
    }
    
    try {
        // RNNoise process (returns speech probability)
        float speechProb = renamenoise_process_frame(rnnState_, audioData, audioData);
        
        // Calculate noise level
        float noiseLevel = calculateNoiseLevel(audioData, frameSize);
        
        // Calculate applied suppression
        float appliedSuppression = suppressionLevel_ * (1.0f - speechProb);
        
        // Update metrics
        updateMetrics(noiseLevel, speechProb, appliedSuppression);
        
        // Apply additional processing if enabled
        if (vadEnabled_) {
            applyVAD(audioData, frameSize, speechProb);
        }
        
        if (adaptiveEnabled_) {
            applyAdaptiveSupression(audioData, frameSize);
        }
        
        // Clamp audio to prevent overflow
        clampAudio(audioData, frameSize);
        
        return true;
        
    } catch (const std::exception& e) {
        logError("RNNoise processing error: " + std::string(e.what()));
        return false;
    }
#else
    return false;
#endif
}

bool NoiseSuppresor::processFallback(float* audioData, size_t frameSize) {
    // Simple noise reduction as fallback
    try {
        // Calculate noise and speech characteristics
        float noiseLevel = calculateNoiseLevel(audioData, frameSize);
        float speechProb = calculateSpeechProbability(audioData, frameSize);
        
        // Apply simple noise reduction
        NoiseUtils::simpleNoiseReduction(audioData, frameSize, suppressionLevel_);
        
        // Calculate applied suppression (estimated)
        float appliedSuppression = suppressionLevel_ * noiseLevel;
        
        // Update metrics
        updateMetrics(noiseLevel, speechProb, appliedSuppression);
        
        // Apply VAD if enabled
        if (vadEnabled_) {
            applyVAD(audioData, frameSize, speechProb);
        }
        
        // Clamp audio
        clampAudio(audioData, frameSize);
        
        return true;
        
    } catch (const std::exception& e) {
        logError("Fallback processing error: " + std::string(e.what()));
        return false;
    }
}

float NoiseSuppresor::calculateNoiseLevel(const float* audioData, size_t frameSize) {
    // Simple noise level calculation based on RMS
    float rms = NoiseUtils::calculateRMS(audioData, frameSize);
    return std::min(1.0f, rms * 10.0f); // Scale and clamp
}

float NoiseSuppresor::calculateSpeechProbability(const float* audioData, size_t frameSize) {
    // Simple speech detection based on multiple features
    float rms = NoiseUtils::calculateRMS(audioData, frameSize);
    float zcr = NoiseUtils::calculateZeroCrossingRate(audioData, frameSize);
    
    // Combine features for speech probability
    float speechProb = 0.0f;
    
    // RMS contribution (higher RMS suggests speech)
    speechProb += std::min(1.0f, rms * 5.0f) * 0.6f;
    
    // ZCR contribution (moderate ZCR suggests speech)
    float normalizedZCR = zcr / (sampleRate_ * 0.1f); // Normalize by 10% of sample rate
    speechProb += (1.0f - std::abs(normalizedZCR - 0.1f) / 0.1f) * 0.4f;
    
    return std::max(0.0f, std::min(1.0f, speechProb));
}

void NoiseSuppresor::applyVAD(float* audioData, size_t frameSize, float speechProb) {
    if (speechProb < threshold_) {
        // Low speech probability, apply strong suppression
        for (size_t i = 0; i < frameSize; ++i) {
            audioData[i] *= 0.1f; // Strong attenuation
        }
    }
}

void NoiseSuppresor::applyAdaptiveSupression(float* audioData, size_t frameSize) {
    // Adaptive suppression based on noise history
    float avgNoise = getAverageNoiseLevel();
    float currentNoise = calculateNoiseLevel(audioData, frameSize);
    
    if (currentNoise > avgNoise * 1.5f) {
        // Higher than average noise, apply stronger suppression
        float extraSuppression = std::min(0.5f, (currentNoise - avgNoise) / avgNoise);
        for (size_t i = 0; i < frameSize; ++i) {
            audioData[i] *= (1.0f - extraSuppression);
        }
    }
}

void NoiseSuppresor::int16ToFloat(const int16_t* input, float* output, size_t count) {
    constexpr float scale = 1.0f / 32768.0f;
    for (size_t i = 0; i < count; ++i) {
        output[i] = static_cast<float>(input[i]) * scale;
    }
}

void NoiseSuppresor::floatToInt16(const float* input, int16_t* output, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        float sample = std::max(-1.0f, std::min(1.0f, input[i]));
        output[i] = static_cast<int16_t>(sample * 32767.0f);
    }
}

void NoiseSuppresor::updateMetrics(float noiseLevel, float speechProb, float suppression) {
    std::lock_guard<std::mutex> lock(metricsMutex_);
    
    currentMetrics_.noiseLevel = noiseLevel;
    currentMetrics_.speechProbability = speechProb;
    currentMetrics_.suppression = suppression;
    currentMetrics_.processedFrames = processedFrames_;
    
    // Add to history
    addToHistory(noiseLevel, speechProb);
}

void NoiseSuppresor::addToHistory(float noiseLevel, float speechProb) {
    noiseHistory_.push_back(noiseLevel);
    speechHistory_.push_back(speechProb);
    
    // Limit history size
    if (noiseHistory_.size() > maxHistorySize_) {
        noiseHistory_.erase(noiseHistory_.begin());
    }
    if (speechHistory_.size() > maxHistorySize_) {
        speechHistory_.erase(speechHistory_.begin());
    }
}

void NoiseSuppresor::clampAudio(float* audioData, size_t frameSize) {
    for (size_t i = 0; i < frameSize; ++i) {
        audioData[i] = std::max(-1.0f, std::min(1.0f, audioData[i]));
    }
}

bool NoiseSuppresor::validateFrameSize(size_t frameSize) const {
    return frameSize == Config::RNNOISE_FRAME_SIZE;
}

bool NoiseSuppresor::validateSampleRate(uint32_t sampleRate) const {
    return sampleRate == Config::RNNOISE_SAMPLE_RATE;
}

void NoiseSuppresor::logError(const std::string& message) const {
    std::cerr << "[NoiseSuppresor ERROR] " << message << std::endl;
}

void NoiseSuppresor::logInfo(const std::string& message) const {
    std::cout << "[NoiseSuppresor INFO] " << message << std::endl;
}

void NoiseSuppresor::logDebug(const std::string& message) const {
    std::cout << "[NoiseSuppresor DEBUG] " << message << std::endl;
}

// UTILITY FUNCTIONS

namespace NoiseUtils {

float calculateRMS(const float* audioData, size_t frameSize) {
    if (!audioData || frameSize == 0) {
        return 0.0f;
    }
    
    float sum = 0.0f;
    for (size_t i = 0; i < frameSize; ++i) {
        sum += audioData[i] * audioData[i];
    }
    
    return std::sqrt(sum / static_cast<float>(frameSize));
}

float calculateZeroCrossingRate(const float* audioData, size_t frameSize) {
    if (!audioData || frameSize < 2) {
        return 0.0f;
    }
    
    size_t crossings = 0;
    for (size_t i = 1; i < frameSize; ++i) {
        if ((audioData[i] >= 0.0f) != (audioData[i-1] >= 0.0f)) {
            crossings++;
        }
    }
    
    return static_cast<float>(crossings) / static_cast<float>(frameSize - 1);
}

float calculateSpectralCentroid(const float* audioData, size_t frameSize, uint32_t sampleRate) {
    // Simplified spectral centroid calculation
    // In a real implementation, you would use FFT
    
    if (!audioData || frameSize == 0) {
        return 0.0f;
    }
    
    // Simple approximation based on high-frequency content
    float highFreqEnergy = 0.0f;
    float totalEnergy = 0.0f;
    
    for (size_t i = 0; i < frameSize; ++i) {
        float sample = audioData[i];
        float energy = sample * sample;
        totalEnergy += energy;
        
        // Weight higher frequencies more (simple approximation)
        float freq = static_cast<float>(i) / static_cast<float>(frameSize) * static_cast<float>(sampleRate) / 2.0f;
        highFreqEnergy += energy * freq;
    }
    
    return (totalEnergy > 0.0f) ? (highFreqEnergy / totalEnergy) : 0.0f;
}

bool detectNoise(const float* audioData, size_t frameSize, float threshold) {
    float rms = calculateRMS(audioData, frameSize);
    return rms > threshold;
}

bool detectSpeech(const float* audioData, size_t frameSize, float threshold) {
    float rms = calculateRMS(audioData, frameSize);
    float zcr = calculateZeroCrossingRate(audioData, frameSize);
    
    // Simple speech detection: moderate RMS and ZCR
    return (rms > threshold * 0.1f) && (rms < threshold * 10.0f) && 
           (zcr > 0.01f) && (zcr < 0.5f);
}

void simpleNoiseReduction(float* audioData, size_t frameSize, float strength) {
    if (!audioData || frameSize == 0) {
        return;
    }
    
    // Simple noise gate
    float threshold = 0.01f * (1.0f - strength); // Lower threshold with higher strength
    
    for (size_t i = 0; i < frameSize; ++i) {
        float sample = audioData[i];
        float magnitude = std::abs(sample);
        
        if (magnitude < threshold) {
            // Below threshold, apply suppression
            audioData[i] *= (1.0f - strength);
        }
    }
}

void spectralSubtraction(float* audioData, size_t frameSize, const float* noiseProfile, float alpha) {
    // Simplified spectral subtraction
    // In a real implementation, you would work in frequency domain
    
    if (!audioData || !noiseProfile || frameSize == 0) {
        return;
    }
    
    for (size_t i = 0; i < frameSize; ++i) {
        float signal = audioData[i];
        float noise = noiseProfile[i % frameSize]; // Use noise profile cyclically
        
        // Simple spectral subtraction approximation
        float magnitude = std::abs(signal);
        float noiseMagnitude = std::abs(noise);
        
        float suppressedMagnitude = magnitude - alpha * noiseMagnitude;
        suppressedMagnitude = std::max(0.1f * magnitude, suppressedMagnitude); // Floor
        
        // Apply suppression while preserving phase
        audioData[i] = (signal >= 0.0f ? 1.0f : -1.0f) * suppressedMagnitude;
    }
}

float calculateSNR(const float* signal, const float* noise, size_t frameSize) {
    if (!signal || !noise || frameSize == 0) {
        return 0.0f;
    }
    
    float signalPower = 0.0f;
    float noisePower = 0.0f;
    
    for (size_t i = 0; i < frameSize; ++i) {
        signalPower += signal[i] * signal[i];
        noisePower += noise[i] * noise[i];
    }
    
    signalPower /= static_cast<float>(frameSize);
    noisePower /= static_cast<float>(frameSize);
    
    if (noisePower <= 0.0f) {
        return 100.0f; // Very high SNR
    }
    
    return 10.0f * std::log10(signalPower / noisePower);
}

float calculateTHD(const float* audioData, size_t frameSize, uint32_t sampleRate) {
    // Simplified THD calculation
    // In a real implementation, you would use FFT to find harmonics
    
    if (!audioData || frameSize == 0) {
        return 0.0f;
    }
    
    // Simple approximation based on high-frequency content
    float fundamentalPower = 0.0f;
    float harmonicPower = 0.0f;
    
    // Calculate power in different frequency bands (very simplified)
    for (size_t i = 0; i < frameSize; ++i) {
        float sample = audioData[i];
        float power = sample * sample;
        
        // Fundamental (lower frequencies)
        if (i < frameSize / 4) {
            fundamentalPower += power;
        } else {
            // Harmonics (higher frequencies)
            harmonicPower += power;
        }
    }
    
    if (fundamentalPower <= 0.0f) {
        return 0.0f;
    }
    
    return std::sqrt(harmonicPower / fundamentalPower);
}

} // namespace NoiseUtils

} // namespace NovaVoice