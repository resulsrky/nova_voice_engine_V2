#include "AudioPreprocessor.h"
#include <iostream>
#include <algorithm>
#include <cmath>
#include <numeric>
#include <cstring>

namespace NovaVoice {

AudioPreprocessor::AudioPreprocessor()
    : initialized_(false)
    , totalProcessedSamples_(0)
    , totalProcessedFrames_(0)
    , currentGain_(1.0f)
    , targetGain_(1.0f)
    , maxGainHistorySize_(50)
    , maxTimingHistorySize_(100) {
    
    gainHistory_.reserve(maxGainHistorySize_);
    processingTimes_.reserve(maxTimingHistorySize_);
    
    // Initialize buffers
    tempBuffer_.resize(Config::FRAMES_PER_BUFFER);
    tempBufferInt16_.resize(Config::FRAMES_PER_BUFFER);
    processBuffer_.resize(Config::FRAMES_PER_BUFFER);
    resampleBuffer_.resize(Config::FRAMES_PER_BUFFER * 2);
    resampleBufferInt16_.resize(Config::FRAMES_PER_BUFFER * 2);
}

AudioPreprocessor::~AudioPreprocessor() {
    shutdown();
}

bool AudioPreprocessor::initialize(const PreprocessingConfig& config) {
    if (initialized_) {
        logError("AudioPreprocessor zaten başlatılmış");
        return false;
    }
    
    if (!validateConfig(config)) {
        logError("Geçersiz preprocessing config");
        return false;
    }
    
    config_ = config;
    currentGain_ = 1.0f;
    targetGain_ = config_.agcTargetLevel;
    
    // Initialize components
    if (!initializeComponents()) {
        logError("Component initialization başarısız");
        shutdownComponents();
        return false;
    }
    
    // Initialize statistics
    stats_ = AudioStats();
    lastProcessTime_ = std::chrono::steady_clock::now();
    
    initialized_ = true;
    logInfo("AudioPreprocessor başarıyla başlatıldı");
    
    return true;
}

void AudioPreprocessor::shutdown() {
    if (!initialized_) {
        return;
    }
    
    shutdownComponents();
    
    initialized_ = false;
    gainHistory_.clear();
    processingTimes_.clear();
    
    logInfo("AudioPreprocessor kapatıldı");
}

bool AudioPreprocessor::processInput(int16_t* audioData, size_t sampleCount) {
    if (!initialized_ || !audioData) {
        return false;
    }
    
    if (!validateSampleCount(sampleCount)) {
        logError("Geçersiz sample count: " + std::to_string(sampleCount));
        return false;
    }
    
    auto startTime = std::chrono::steady_clock::now();
    
    try {
        // Convert to float
        int16ToFloat(audioData, tempBuffer_.data(), sampleCount);
        
        // Process audio chain
        bool success = processAudioChain(tempBuffer_.data(), sampleCount, true);
        
        if (success) {
            // Convert back to int16
            floatToInt16(tempBuffer_.data(), audioData, sampleCount);
            
            totalProcessedSamples_ += sampleCount;
            totalProcessedFrames_++;
            
            // Update timing
            auto endTime = std::chrono::steady_clock::now();
            float processingTime = std::chrono::duration<float, std::milli>(endTime - startTime).count();
            addProcessingTime(processingTime);
        }
        
        return success;
        
    } catch (const std::exception& e) {
        logError("Input processing exception: " + std::string(e.what()));
        return false;
    }
}

bool AudioPreprocessor::processInput(float* audioData, size_t sampleCount) {
    if (!initialized_ || !audioData) {
        return false;
    }
    
    if (!validateSampleCount(sampleCount)) {
        return false;
    }
    
    auto startTime = std::chrono::steady_clock::now();
    
    try {
        bool success = processAudioChain(audioData, sampleCount, true);
        
        if (success) {
            totalProcessedSamples_ += sampleCount;
            totalProcessedFrames_++;
            
            // Update timing
            auto endTime = std::chrono::steady_clock::now();
            float processingTime = std::chrono::duration<float, std::milli>(endTime - startTime).count();
            addProcessingTime(processingTime);
        }
        
        return success;
        
    } catch (const std::exception& e) {
        logError("Input processing exception: " + std::string(e.what()));
        return false;
    }
}

std::vector<int16_t> AudioPreprocessor::processInput(const std::vector<int16_t>& audioData) {
    std::vector<int16_t> result = audioData;
    
    if (processInput(result.data(), result.size())) {
        return result;
    }
    
    return {}; // Return empty on error
}

bool AudioPreprocessor::processOutput(int16_t* audioData, size_t sampleCount) {
    if (!initialized_ || !audioData) {
        return false;
    }
    
    if (!validateSampleCount(sampleCount)) {
        return false;
    }
    
    try {
        // Convert to float
        int16ToFloat(audioData, tempBuffer_.data(), sampleCount);
        
        // Process audio chain (output path - less processing)
        bool success = processAudioChain(tempBuffer_.data(), sampleCount, false);
        
        if (success) {
            // Convert back to int16
            floatToInt16(tempBuffer_.data(), audioData, sampleCount);
        }
        
        return success;
        
    } catch (const std::exception& e) {
        logError("Output processing exception: " + std::string(e.what()));
        return false;
    }
}

bool AudioPreprocessor::processOutput(float* audioData, size_t sampleCount) {
    if (!initialized_ || !audioData) {
        return false;
    }
    
    if (!validateSampleCount(sampleCount)) {
        return false;
    }
    
    try {
        return processAudioChain(audioData, sampleCount, false);
        
    } catch (const std::exception& e) {
        logError("Output processing exception: " + std::string(e.what()));
        return false;
    }
}

std::vector<int16_t> AudioPreprocessor::processOutput(const std::vector<int16_t>& audioData) {
    std::vector<int16_t> result = audioData;
    
    if (processOutput(result.data(), result.size())) {
        return result;
    }
    
    return {}; // Return empty on error
}

std::optional<EncodedPacket> AudioPreprocessor::encode(const int16_t* audioData, size_t sampleCount) {
    if (!initialized_ || !codec_) {
        return std::nullopt;
    }
    
    if (!config_.enableCodec) {
        // Codec disabled, create raw packet
        std::vector<uint8_t> rawData(reinterpret_cast<const uint8_t*>(audioData),
                                   reinterpret_cast<const uint8_t*>(audioData) + sampleCount * 2);
        return EncodedPacket(rawData, 0, 0);
    }
    
    try {
        // Process input first
        std::vector<int16_t> processedData(audioData, audioData + sampleCount);
        if (!processInput(processedData.data(), sampleCount)) {
            logError("Input processing failed before encoding");
            return std::nullopt;
        }
        
        // Resample to Lyra sample rate if necessary
        if (Config::SAMPLE_RATE != Config::LYRA_SAMPLE_RATE) {
            auto resampled = codec_->resampleTo16kHz(processedData.data(), sampleCount, Config::SAMPLE_RATE);
            return codec_->encode(resampled);
        } else {
            return codec_->encode(processedData);
        }
        
    } catch (const std::exception& e) {
        logError("Encoding exception: " + std::string(e.what()));
        return std::nullopt;
    }
}

std::optional<std::vector<int16_t>> AudioPreprocessor::decode(const EncodedPacket& packet) {
    if (!initialized_ || !codec_) {
        return std::nullopt;
    }
    
    if (!config_.enableCodec) {
        // Codec disabled, treat as raw data
        if (packet.data.size() % 2 != 0) {
            logError("Invalid raw packet size");
            return std::nullopt;
        }
        
        size_t sampleCount = packet.data.size() / 2;
        std::vector<int16_t> rawSamples(sampleCount);
        std::memcpy(rawSamples.data(), packet.data.data(), packet.data.size());
        
        // Process output
        if (processOutput(rawSamples.data(), sampleCount)) {
            return rawSamples;
        }
        return std::nullopt;
    }
    
    try {
        auto decoded = codec_->decode(packet);
        if (!decoded) {
            return std::nullopt;
        }
        
        // Resample from Lyra sample rate if necessary
        if (Config::SAMPLE_RATE != Config::LYRA_SAMPLE_RATE) {
            auto resampled = codec_->resampleFromLyra(decoded->data(), decoded->size(), Config::SAMPLE_RATE);
            
            // Process output
            if (processOutput(resampled.data(), resampled.size())) {
                return resampled;
            }
            return std::nullopt;
        } else {
            // Process output
            if (processOutput(decoded->data(), decoded->size())) {
                return decoded;
            }
            return std::nullopt;
        }
        
    } catch (const std::exception& e) {
        logError("Decoding exception: " + std::string(e.what()));
        return std::nullopt;
    }
}

std::optional<std::vector<int16_t>> AudioPreprocessor::decode(const uint8_t* encodedData, size_t dataSize) {
    if (!encodedData || dataSize == 0) {
        return std::nullopt;
    }
    
    // Create temporary packet
    EncodedPacket packet;
    packet.data.assign(encodedData, encodedData + dataSize);
    
    return decode(packet);
}

void AudioPreprocessor::updateConfig(const PreprocessingConfig& config) {
    if (!validateConfig(config)) {
        logError("Geçersiz config");
        return;
    }
    
    std::lock_guard<std::mutex> lock(configMutex_);
    config_ = config;
    
    // Update component configurations
    if (noiseSuppresor_) {
        noiseSuppresor_->setSuppressionLevel(config_.noiseSuppressionLevel);
        noiseSuppresor_->setThreshold(config_.vadThreshold);
        noiseSuppresor_->enableVAD(config_.enableVAD);
    }
    
    if (codec_) {
        codec_->setBitrate(config_.targetBitrate);
    }
    
    targetGain_ = config_.agcTargetLevel;
    
    logInfo("Config güncellendi");
}

PreprocessingConfig AudioPreprocessor::getConfig() const {
    std::lock_guard<std::mutex> lock(configMutex_);
    return config_;
}

void AudioPreprocessor::setNoiseSuppressionLevel(float level) {
    if (noiseSuppresor_) {
        noiseSuppresor_->setSuppressionLevel(level);
        config_.noiseSuppressionLevel = level;
    }
}

void AudioPreprocessor::setVADThreshold(float threshold) {
    if (noiseSuppresor_) {
        noiseSuppresor_->setThreshold(threshold);
        config_.vadThreshold = threshold;
    }
}

void AudioPreprocessor::setTargetGain(float gain) {
    targetGain_ = std::max(0.1f, std::min(2.0f, gain));
    config_.agcTargetLevel = targetGain_;
}

void AudioPreprocessor::setBitrate(uint32_t bitrate) {
    if (codec_) {
        codec_->setBitrate(bitrate);
        config_.targetBitrate = bitrate;
    }
}

void AudioPreprocessor::updateNetworkMetrics(const NetworkMetrics& metrics) {
    if (bitrateCalculator_) {
        bitrateCalculator_->updateNetworkMetrics(metrics);
        updateBitrateFromNetworkConditions();
    }
}

void AudioPreprocessor::reportPacketLoss(uint32_t totalPackets, uint32_t lostPackets) {
    if (bitrateCalculator_) {
        bitrateCalculator_->reportPacketLoss(totalPackets, lostPackets);
    }
}

void AudioPreprocessor::reportLatency(uint32_t latencyMs) {
    if (bitrateCalculator_) {
        bitrateCalculator_->reportLatency(latencyMs);
    }
}

void AudioPreprocessor::reportBandwidth(float bandwidthKbps) {
    if (bitrateCalculator_) {
        bitrateCalculator_->reportBandwidth(bandwidthKbps);
    }
}

AudioStats AudioPreprocessor::getStatistics() const {
    AudioStats stats = stats_;
    
    stats.totalSamplesProcessed = totalProcessedSamples_;
    stats.totalFramesProcessed = totalProcessedFrames_;
    
    if (noiseSuppresor_) {
        auto noiseMetrics = noiseSuppresor_->getMetrics();
        stats.averageNoiseLevel = noiseMetrics.noiseLevel;
        stats.averageSpeechProbability = noiseMetrics.speechProbability;
    }
    
    if (codec_) {
        stats.currentBitrate = codec_->getBitrate();
    }
    
    stats.averageGain = currentGain_;
    
    // Calculate average processing latency
    if (!processingTimes_.empty()) {
        float sum = std::accumulate(processingTimes_.begin(), processingTimes_.end(), 0.0f);
        stats.processingLatency = sum / static_cast<float>(processingTimes_.size());
    }
    
    return stats;
}

NoiseMetrics AudioPreprocessor::getNoiseMetrics() const {
    if (noiseSuppresor_) {
        return noiseSuppresor_->getMetrics();
    }
    return NoiseMetrics();
}

uint32_t AudioPreprocessor::getCurrentBitrate() const {
    if (codec_) {
        return codec_->getBitrate();
    }
    return config_.targetBitrate;
}

float AudioPreprocessor::getCurrentGain() const {
    return currentGain_;
}

bool AudioPreprocessor::isSpeechDetected() const {
    if (noiseSuppresor_) {
        return noiseSuppresor_->isSpeechDetected();
    }
    return false;
}

void AudioPreprocessor::setOnSpeechDetected(std::function<void(bool)> callback) {
    onSpeechDetected_ = callback;
}

void AudioPreprocessor::setOnBitrateChanged(std::function<void(uint32_t)> callback) {
    onBitrateChanged_ = callback;
}

void AudioPreprocessor::setOnQualityChanged(std::function<void(float)> callback) {
    onQualityChanged_ = callback;
}

std::string AudioPreprocessor::getInfo() const {
    std::string info = "AudioPreprocessor Info:\n";
    info += "Initialized: " + std::string(initialized_ ? "Yes" : "No") + "\n";
    
    if (initialized_) {
        info += "Noise Suppression: " + std::string(config_.enableNoiseSupression ? "Enabled" : "Disabled") + "\n";
        info += "Codec: " + std::string(config_.enableCodec ? "Enabled" : "Disabled") + "\n";
        info += "VAD: " + std::string(config_.enableVAD ? "Enabled" : "Disabled") + "\n";
        info += "AGC: " + std::string(config_.enableAGC ? "Enabled" : "Disabled") + "\n";
        info += "Bitrate Adaptation: " + std::string(config_.enableBitrateAdaptation ? "Enabled" : "Disabled") + "\n";
        
        auto stats = getStatistics();
        info += "Processed Samples: " + std::to_string(stats.totalSamplesProcessed) + "\n";
        info += "Processed Frames: " + std::to_string(stats.totalFramesProcessed) + "\n";
        info += "Current Bitrate: " + std::to_string(stats.currentBitrate) + " bps\n";
        info += "Current Gain: " + std::to_string(stats.averageGain) + "\n";
        info += "Processing Latency: " + std::to_string(stats.processingLatency) + " ms\n";
        info += "Speech Detected: " + std::string(isSpeechDetected() ? "Yes" : "No");
    }
    
    return info;
}

void AudioPreprocessor::printStatistics() const {
    auto stats = getStatistics();
    std::cout << "\n=== AudioPreprocessor Statistics ===" << std::endl;
    std::cout << PreprocessingUtils::formatAudioStats(stats) << std::endl;
    std::cout << "====================================" << std::endl;
}

// PRIVATE METHODS

bool AudioPreprocessor::processAudioChain(float* audioData, size_t sampleCount, bool isInput) {
    if (!audioData || sampleCount == 0) {
        return false;
    }
    
    try {
        bool speechDetected = false;
        
        if (isInput) {
            // Input processing chain
            
            // 1. Automatic Gain Control (AGC)
            if (config_.enableAGC) {
                applyAGC(audioData, sampleCount);
            }
            
            // 2. Noise Suppression
            if (config_.enableNoiseSupression && noiseSuppresor_) {
                // Resample to RNNoise sample rate if necessary
                if (Config::SAMPLE_RATE != Config::RNNOISE_SAMPLE_RATE) {
                    // TODO: Implement sample rate conversion for RNNoise
                    logDebug("Sample rate conversion needed for RNNoise");
                }
                
                // Process in RNNoise frame sizes
                size_t frameSize = Config::RNNOISE_FRAME_SIZE;
                if (sampleCount >= frameSize) {
                    noiseSuppresor_->process(audioData, frameSize);
                    speechDetected = noiseSuppresor_->isSpeechDetected();
                }
            }
            
            // 3. Voice Activity Detection
            if (config_.enableVAD) {
                float speechProb = noiseSuppresor_ ? noiseSuppresor_->getCurrentSpeechProbability() : 0.5f;
                applyVAD(audioData, sampleCount, speechProb);
                speechDetected = speechProb > config_.vadThreshold;
            }
            
            // Call speech detection callback
            if (onSpeechDetected_) {
                onSpeechDetected_(speechDetected);
            }
        } else {
            // Output processing chain (simpler)
            
            // Apply volume control
            if (config_.enableAGC) {
                for (size_t i = 0; i < sampleCount; ++i) {
                    audioData[i] *= currentGain_;
                }
            }
        }
        
        // Update statistics
        updateStatistics();
        
        return true;
        
    } catch (const std::exception& e) {
        logError("Audio chain processing error: " + std::string(e.what()));
        return false;
    }
}

bool AudioPreprocessor::applyAGC(float* audioData, size_t sampleCount) {
    if (!audioData || sampleCount == 0) {
        return false;
    }
    
    // Calculate current audio level
    float currentLevel = calculateAudioLevel(audioData, sampleCount);
    
    // Update gain control
    updateGainControl(audioData, sampleCount);
    
    // Apply gain
    for (size_t i = 0; i < sampleCount; ++i) {
        audioData[i] *= currentGain_;
        
        // Prevent clipping
        audioData[i] = std::max(-1.0f, std::min(1.0f, audioData[i]));
    }
    
    return true;
}

bool AudioPreprocessor::applyVAD(float* audioData, size_t sampleCount, float speechProbability) {
    if (!audioData || sampleCount == 0) {
        return false;
    }
    
    if (speechProbability < config_.vadThreshold) {
        // Low speech probability, apply strong attenuation
        float attenuation = 0.1f; // -20dB
        for (size_t i = 0; i < sampleCount; ++i) {
            audioData[i] *= attenuation;
        }
    }
    
    return true;
}

void AudioPreprocessor::updateStatistics() {
    // This would be called from audio processing thread
    // Update internal statistics here
}

void AudioPreprocessor::updateGainControl(const float* audioData, size_t sampleCount) {
    float currentLevel = calculateAudioLevel(audioData, sampleCount);
    
    if (currentLevel > 0.0f) {
        float desiredGain = targetGain_ / currentLevel;
        
        // Smooth gain changes
        float alpha = 0.1f; // Smoothing factor
        currentGain_ = alpha * desiredGain + (1.0f - alpha) * currentGain_;
        
        // Limit gain range
        currentGain_ = std::max(0.1f, std::min(2.0f, currentGain_));
        
        // Add to history
        gainHistory_.push_back(currentGain_);
        if (gainHistory_.size() > maxGainHistorySize_) {
            gainHistory_.erase(gainHistory_.begin());
        }
    }
}

void AudioPreprocessor::updateBitrateFromNetworkConditions() {
    if (!bitrateCalculator_ || !config_.enableBitrateAdaptation) {
        return;
    }
    
    uint32_t newBitrate = bitrateCalculator_->getRecommendedBitrate();
    uint32_t currentBitrate = getCurrentBitrate();
    
    if (newBitrate != currentBitrate) {
        setBitrate(newBitrate);
        
        if (onBitrateChanged_) {
            onBitrateChanged_(newBitrate);
        }
    }
}

float AudioPreprocessor::calculateAudioLevel(const float* audioData, size_t sampleCount) {
    return PreprocessingUtils::calculateRMS(audioData, sampleCount);
}

void AudioPreprocessor::addProcessingTime(float timeMs) {
    processingTimes_.push_back(timeMs);
    if (processingTimes_.size() > maxTimingHistorySize_) {
        processingTimes_.erase(processingTimes_.begin());
    }
}

void AudioPreprocessor::int16ToFloat(const int16_t* input, float* output, size_t count) {
    constexpr float scale = 1.0f / 32768.0f;
    for (size_t i = 0; i < count; ++i) {
        output[i] = static_cast<float>(input[i]) * scale;
    }
}

void AudioPreprocessor::floatToInt16(const float* input, int16_t* output, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        float sample = std::max(-1.0f, std::min(1.0f, input[i]));
        output[i] = static_cast<int16_t>(sample * 32767.0f);
    }
}

bool AudioPreprocessor::validateConfig(const PreprocessingConfig& config) const {
    // Validate configuration parameters
    if (config.noiseSuppressionLevel < 0.0f || config.noiseSuppressionLevel > 1.0f) {
        return false;
    }
    
    if (config.vadThreshold < 0.0f || config.vadThreshold > 1.0f) {
        return false;
    }
    
    if (config.agcTargetLevel < 0.1f || config.agcTargetLevel > 2.0f) {
        return false;
    }
    
    if (config.targetBitrate < Config::LYRA_MIN_BITRATE || config.targetBitrate > Config::LYRA_MAX_BITRATE) {
        return false;
    }
    
    return true;
}

bool AudioPreprocessor::validateSampleCount(size_t sampleCount) const {
    // Allow various frame sizes, but prefer standard ones
    return sampleCount > 0 && sampleCount <= Config::FRAMES_PER_BUFFER * 4;
}

bool AudioPreprocessor::initializeComponents() {
    try {
        // Initialize Noise Suppressor
        if (config_.enableNoiseSupression) {
            noiseSuppresor_ = std::make_shared<NoiseSuppresor>();
            if (!noiseSuppresor_->initialize()) {
                logError("NoiseSuppresor initialization failed");
                return false;
            }
            
            noiseSuppresor_->setSuppressionLevel(config_.noiseSuppressionLevel);
            noiseSuppresor_->setThreshold(config_.vadThreshold);
            noiseSuppresor_->enableVAD(config_.enableVAD);
        }
        
        // Initialize Codec
        if (config_.enableCodec) {
            codec_ = std::make_shared<LyraCodec>();
            if (!codec_->initialize(Config::LYRA_SAMPLE_RATE, Config::CHANNELS, config_.targetBitrate)) {
                logError("LyraCodec initialization failed");
                return false;
            }
        }
        
        // Initialize Bitrate Calculator
        if (config_.enableBitrateAdaptation) {
            bitrateCalculator_ = std::make_shared<BitrateCalculator>();
            if (!bitrateCalculator_->initialize(config_.targetBitrate)) {
                logError("BitrateCalculator initialization failed");
                return false;
            }
        }
        
        return true;
        
    } catch (const std::exception& e) {
        logError("Component initialization exception: " + std::string(e.what()));
        return false;
    }
}

void AudioPreprocessor::shutdownComponents() {
    if (noiseSuppresor_) {
        noiseSuppresor_->shutdown();
        noiseSuppresor_.reset();
    }
    
    if (codec_) {
        codec_.reset();
    }
    
    if (bitrateCalculator_) {
        bitrateCalculator_->shutdown();
        bitrateCalculator_.reset();
    }
}

void AudioPreprocessor::logError(const std::string& message) const {
    std::cerr << "[AudioPreprocessor ERROR] " << message << std::endl;
}

void AudioPreprocessor::logInfo(const std::string& message) const {
    std::cout << "[AudioPreprocessor INFO] " << message << std::endl;
}

void AudioPreprocessor::logDebug(const std::string& message) const {
    std::cout << "[AudioPreprocessor DEBUG] " << message << std::endl;
}

// UTILITY FUNCTIONS

namespace PreprocessingUtils {

float calculateRMS(const float* audioData, size_t sampleCount) {
    if (!audioData || sampleCount == 0) {
        return 0.0f;
    }
    
    float sum = 0.0f;
    for (size_t i = 0; i < sampleCount; ++i) {
        sum += audioData[i] * audioData[i];
    }
    
    return std::sqrt(sum / static_cast<float>(sampleCount));
}

float calculatePeak(const float* audioData, size_t sampleCount) {
    if (!audioData || sampleCount == 0) {
        return 0.0f;
    }
    
    float peak = 0.0f;
    for (size_t i = 0; i < sampleCount; ++i) {
        peak = std::max(peak, std::abs(audioData[i]));
    }
    
    return peak;
}

float dbToLinear(float db) {
    return std::pow(10.0f, db / 20.0f);
}

float linearToDb(float linear) {
    if (linear <= 0.0f) {
        return -100.0f; // Very quiet
    }
    return 20.0f * std::log10(linear);
}

float calculateQualityScore(const AudioStats& stats, const NetworkMetrics& network) {
    // Simple quality score calculation
    float bitrateScore = static_cast<float>(stats.currentBitrate - Config::LYRA_MIN_BITRATE) /
                        static_cast<float>(Config::LYRA_MAX_BITRATE - Config::LYRA_MIN_BITRATE);
    
    float latencyScore = std::max(0.0f, 1.0f - network.averageLatency / 1000.0f);
    float lossScore = std::max(0.0f, 1.0f - network.packetLossRate);
    float processingScore = std::max(0.0f, 1.0f - stats.processingLatency / 50.0f);
    
    return (bitrateScore * 0.3f + latencyScore * 0.3f + lossScore * 0.3f + processingScore * 0.1f);
}

std::string formatAudioStats(const AudioStats& stats) {
    std::string formatted;
    formatted += "Total Samples Processed: " + std::to_string(stats.totalSamplesProcessed) + "\n";
    formatted += "Total Frames Processed: " + std::to_string(stats.totalFramesProcessed) + "\n";
    formatted += "Average Noise Level: " + std::to_string(stats.averageNoiseLevel) + "\n";
    formatted += "Average Speech Probability: " + std::to_string(stats.averageSpeechProbability) + "\n";
    formatted += "Current Bitrate: " + std::to_string(stats.currentBitrate) + " bps\n";
    formatted += "Average Gain: " + std::to_string(stats.averageGain) + "\n";
    formatted += "Processing Latency: " + std::to_string(stats.processingLatency) + " ms";
    
    return formatted;
}

PreprocessingConfig createLowLatencyConfig() {
    PreprocessingConfig config;
    config.enableNoiseSupression = false; // Disable for lower latency
    config.enableCodec = true;
    config.enableBitrateAdaptation = true;
    config.enableVAD = false; // Disable for lower latency
    config.enableAGC = true;
    config.targetBitrate = Config::LYRA_MAX_BITRATE; // High bitrate for quality
    config.noiseSuppressionLevel = 0.5f;
    config.vadThreshold = 0.3f;
    config.agcTargetLevel = 0.8f;
    
    return config;
}

PreprocessingConfig createHighQualityConfig() {
    PreprocessingConfig config;
    config.enableNoiseSupression = true;
    config.enableCodec = true;
    config.enableBitrateAdaptation = true;
    config.enableVAD = true;
    config.enableAGC = true;
    config.targetBitrate = Config::LYRA_MAX_BITRATE;
    config.noiseSuppressionLevel = 0.9f;
    config.vadThreshold = 0.5f;
    config.agcTargetLevel = 0.7f;
    
    return config;
}

PreprocessingConfig createPowerSaveConfig() {
    PreprocessingConfig config;
    config.enableNoiseSupression = true;
    config.enableCodec = true;
    config.enableBitrateAdaptation = true;
    config.enableVAD = true;
    config.enableAGC = false; // Disable AGC to save power
    config.targetBitrate = Config::LYRA_MIN_BITRATE;
    config.noiseSuppressionLevel = 0.6f;
    config.vadThreshold = 0.6f;
    config.agcTargetLevel = 0.5f;
    
    return config;
}

} // namespace PreprocessingUtils

} // namespace NovaVoice