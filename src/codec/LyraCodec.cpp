#include "LyraCodec.h"
#include <iostream>
#include <cstring>
#include <algorithm>
#include <chrono>

// Lyra headers (conditional)
#ifdef HAVE_LYRA
// Buraya Lyra header'ları eklenecek
// Şimdilik dummy implementation kullanacağız
#endif

namespace NovaVoice {

LyraCodec::LyraCodec()
    : initialized_(false)
    , sampleRate_(Config::LYRA_SAMPLE_RATE)
    , channels_(Config::CHANNELS)
    , currentBitrate_(Config::LYRA_DEFAULT_BITRATE)
    , frameSize_(Config::LYRA_FRAME_SIZE)
    , nextSequenceNumber_(0)
    , encodedFrames_(0)
    , decodedFrames_(0)
    , encodingErrors_(0)
    , decodingErrors_(0) {
#ifdef HAVE_LYRA
    lyraEncoder_ = nullptr;
    lyraDecoder_ = nullptr;
    modelPath_ = "external/lyra/lyra/model_coeffs";
#endif
}

LyraCodec::~LyraCodec() {
    cleanup();
}

bool LyraCodec::initialize(uint32_t sampleRate, uint32_t channels, uint32_t bitrate) {
    std::lock_guard<std::mutex> lock(codecMutex_);
    
    if (initialized_) {
        logError("Codec zaten başlatılmış");
        return false;
    }
    
    if (!validateParameters(sampleRate, channels, bitrate)) {
        logError("Geçersiz codec parametreleri");
        return false;
    }
    
    sampleRate_ = sampleRate;
    channels_ = channels;
    currentBitrate_ = bitrate;
    frameSize_ = (sampleRate_ * Config::LYRA_FRAME_SIZE_MS) / 1000;
    
    // Lyra'yı initialize et (eğer mevcut ise)
    bool lyraOk = initializeLyra();
    
    if (!lyraOk) {
        logInfo("Lyra mevcut değil, ham ses verisi kullanılacak");
    }
    
    initialized_ = true;
    logInfo("LyraCodec başarıyla başlatıldı - Sample Rate: " + std::to_string(sampleRate_) + 
            " Hz, Bitrate: " + std::to_string(currentBitrate_) + " bps");
    
    return true;
}

bool LyraCodec::isLyraAvailable() const {
#ifdef HAVE_LYRA
    return lyraEncoder_ != nullptr && lyraDecoder_ != nullptr;
#else
    return false;
#endif
}

std::optional<EncodedPacket> LyraCodec::encode(const int16_t* audioData, size_t sampleCount) {
    if (!initialized_) {
        logError("Codec başlatılmamış");
        encodingErrors_++;
        return std::nullopt;
    }
    
    if (!audioData || sampleCount == 0) {
        logError("Geçersiz audio data");
        encodingErrors_++;
        return std::nullopt;
    }
    
    if (!validateInputSize(sampleCount)) {
        logError("Geçersiz input boyutu: " + std::to_string(sampleCount));
        encodingErrors_++;
        return std::nullopt;
    }
    
    std::lock_guard<std::mutex> lock(codecMutex_);
    
    try {
        std::vector<uint8_t> encodedData;
        
#ifdef HAVE_LYRA
        if (isLyraAvailable()) {
            // Lyra encoding kullan
            // TODO: Gerçek Lyra API'si ile değiştir
            encodedData = encodeRaw(audioData, sampleCount);
        } else {
            // Fallback: Ham veri
            encodedData = encodeRaw(audioData, sampleCount);
        }
#else
        // Lyra mevcut değil, ham veri kullan
        encodedData = encodeRaw(audioData, sampleCount);
#endif
        
        if (encodedData.empty()) {
            logError("Encoding başarısız");
            encodingErrors_++;
            return std::nullopt;
        }
        
        EncodedPacket packet(encodedData, nextSequenceNumber_++, currentBitrate_);
        packet.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        
        encodedFrames_++;
        return packet;
        
    } catch (const std::exception& e) {
        logError("Encoding exception: " + std::string(e.what()));
        encodingErrors_++;
        return std::nullopt;
    }
}

std::optional<EncodedPacket> LyraCodec::encode(const std::vector<int16_t>& audioSamples) {
    return encode(audioSamples.data(), audioSamples.size());
}

std::optional<std::vector<int16_t>> LyraCodec::decode(const EncodedPacket& packet) {
    return decode(packet.data.data(), packet.data.size());
}

std::optional<std::vector<int16_t>> LyraCodec::decode(const uint8_t* encodedData, size_t dataSize) {
    if (!initialized_) {
        logError("Codec başlatılmamış");
        decodingErrors_++;
        return std::nullopt;
    }
    
    if (!encodedData || dataSize == 0) {
        logError("Geçersiz encoded data");
        decodingErrors_++;
        return std::nullopt;
    }
    
    std::lock_guard<std::mutex> lock(codecMutex_);
    
    try {
        std::vector<int16_t> decodedAudio;
        
#ifdef HAVE_LYRA
        if (isLyraAvailable()) {
            // Lyra decoding kullan
            // TODO: Gerçek Lyra API'si ile değiştir
            decodedAudio = decodeRaw(encodedData, dataSize);
        } else {
            // Fallback: Ham veri
            decodedAudio = decodeRaw(encodedData, dataSize);
        }
#else
        // Lyra mevcut değil, ham veri kullan
        decodedAudio = decodeRaw(encodedData, dataSize);
#endif
        
        if (decodedAudio.empty()) {
            logError("Decoding başarısız");
            decodingErrors_++;
            return std::nullopt;
        }
        
        decodedFrames_++;
        return decodedAudio;
        
    } catch (const std::exception& e) {
        logError("Decoding exception: " + std::string(e.what()));
        decodingErrors_++;
        return std::nullopt;
    }
}

bool LyraCodec::setBitrate(uint32_t bitrate) {
    if (!CodecUtils::isValidBitrate(bitrate)) {
        logError("Geçersiz bitrate: " + std::to_string(bitrate));
        return false;
    }
    
    std::lock_guard<std::mutex> lock(codecMutex_);
    currentBitrate_ = bitrate;
    
#ifdef HAVE_LYRA
    if (isLyraAvailable()) {
        // TODO: Lyra encoder'da bitrate güncelle
        logInfo("Lyra bitrate güncellendi: " + std::to_string(bitrate) + " bps");
    }
#endif
    
    return true;
}

size_t LyraCodec::getExpectedInputSize() const {
    return frameSize_ * channels_;
}

size_t LyraCodec::getExpectedOutputSize() const {
    // Tahmini output boyutu (bitrate'e göre)
    return (currentBitrate_ * Config::LYRA_FRAME_SIZE_MS) / (8 * 1000);
}

std::string LyraCodec::getCodecInfo() const {
    std::string info = "LyraCodec v2.0\n";
    info += "Sample Rate: " + std::to_string(sampleRate_) + " Hz\n";
    info += "Channels: " + std::to_string(channels_) + "\n";
    info += "Bitrate: " + std::to_string(currentBitrate_) + " bps\n";
    info += "Frame Size: " + std::to_string(frameSize_) + " samples\n";
    info += "Lyra Available: " + std::string(isLyraAvailable() ? "Yes" : "No") + "\n";
    info += "Encoded Frames: " + std::to_string(encodedFrames_) + "\n";
    info += "Decoded Frames: " + std::to_string(decodedFrames_) + "\n";
    info += "Encoding Errors: " + std::to_string(encodingErrors_) + "\n";
    info += "Decoding Errors: " + std::to_string(decodingErrors_);
    return info;
}

std::vector<int16_t> LyraCodec::resampleTo16kHz(const int16_t* input, size_t inputSamples, uint32_t inputSampleRate) {
    if (inputSampleRate == Config::LYRA_SAMPLE_RATE) {
        return std::vector<int16_t>(input, input + inputSamples);
    }
    
    return simpleSampleRateConversion(input, inputSamples, inputSampleRate, Config::LYRA_SAMPLE_RATE);
}

std::vector<int16_t> LyraCodec::resampleFromLyra(const int16_t* input, size_t inputSamples, uint32_t targetSampleRate) {
    if (targetSampleRate == Config::LYRA_SAMPLE_RATE) {
        return std::vector<int16_t>(input, input + inputSamples);
    }
    
    return simpleSampleRateConversion(input, inputSamples, Config::LYRA_SAMPLE_RATE, targetSampleRate);
}

// PRIVATE METHODS

bool LyraCodec::initializeLyra() {
#ifdef HAVE_LYRA
    try {
        // TODO: Gerçek Lyra initialization
        // lyraEncoder_ = LyraEncoder::Create(sampleRate_, channels_, currentBitrate_, false, modelPath_);
        // lyraDecoder_ = LyraDecoder::Create(sampleRate_, channels_, modelPath_);
        
        // Şimdilik dummy implementation
        logInfo("Lyra v2 initialization (dummy)");
        return false; // Gerçek implementation gelene kadar false döndür
    } catch (const std::exception& e) {
        logError("Lyra initialization hatası: " + std::string(e.what()));
        return false;
    }
#else
    return false;
#endif
}

void LyraCodec::cleanup() {
    std::lock_guard<std::mutex> lock(codecMutex_);
    
#ifdef HAVE_LYRA
    // TODO: Clean up Lyra instances when implemented
    lyraEncoder_ = nullptr;
    lyraDecoder_ = nullptr;
#endif
    
    initialized_ = false;
}

std::vector<uint8_t> LyraCodec::encodeRaw(const int16_t* audioData, size_t sampleCount) {
    // Basit "encoding": int16 -> uint8 dönüşümü (geçici çözüm)
    std::vector<uint8_t> result;
    result.reserve(sampleCount * 2);
    
    const uint8_t* byteData = reinterpret_cast<const uint8_t*>(audioData);
    result.assign(byteData, byteData + (sampleCount * 2));
    
    return result;
}

std::vector<int16_t> LyraCodec::decodeRaw(const uint8_t* encodedData, size_t dataSize) {
    // Basit "decoding": uint8 -> int16 dönüşümü (geçici çözüm)
    if (dataSize % 2 != 0) {
        logError("Invalid encoded data size for int16 conversion");
        return {};
    }
    
    std::vector<int16_t> result;
    size_t sampleCount = dataSize / 2;
    result.resize(sampleCount);
    
    std::memcpy(result.data(), encodedData, dataSize);
    return result;
}

std::vector<int16_t> LyraCodec::simpleSampleRateConversion(const int16_t* input, size_t inputSamples, 
                                                         uint32_t inputRate, uint32_t outputRate) {
    if (inputRate == outputRate) {
        return std::vector<int16_t>(input, input + inputSamples);
    }
    
    // Basit linear interpolation
    float ratio = static_cast<float>(outputRate) / static_cast<float>(inputRate);
    size_t outputSamples = static_cast<size_t>(inputSamples * ratio);
    
    std::vector<int16_t> output;
    output.reserve(outputSamples);
    
    for (size_t i = 0; i < outputSamples; ++i) {
        float sourceIndex = i / ratio;
        size_t index = static_cast<size_t>(sourceIndex);
        
        if (index >= inputSamples - 1) {
            output.push_back(input[inputSamples - 1]);
        } else {
            float fraction = sourceIndex - index;
            int16_t sample = static_cast<int16_t>(
                input[index] * (1.0f - fraction) + input[index + 1] * fraction
            );
            output.push_back(sample);
        }
    }
    
    return output;
}

bool LyraCodec::validateParameters(uint32_t sampleRate, uint32_t channels, uint32_t bitrate) const {
    if (channels != 1) {
        logError("Sadece mono (1 kanal) destekleniyor");
        return false;
    }
    
    if (sampleRate != 16000 && sampleRate != 32000 && sampleRate != 48000) {
        logError("Desteklenmeyen sample rate: " + std::to_string(sampleRate));
        return false;
    }
    
    if (!CodecUtils::isValidBitrate(bitrate)) {
        logError("Geçersiz bitrate: " + std::to_string(bitrate));
        return false;
    }
    
    return true;
}

bool LyraCodec::validateInputSize(size_t sampleCount) const {
    size_t expectedSize = getExpectedInputSize();
    return sampleCount == expectedSize;
}

void LyraCodec::logError(const std::string& message) const {
    std::cerr << "[LyraCodec ERROR] " << message << std::endl;
}

void LyraCodec::logInfo(const std::string& message) const {
    std::cout << "[LyraCodec INFO] " << message << std::endl;
}

// CODEC UTILS

namespace CodecUtils {

std::string statusToString(CodecStatus status) {
    switch (status) {
        case CodecStatus::SUCCESS: return "Success";
        case CodecStatus::ERROR_INIT: return "Initialization Error";
        case CodecStatus::ERROR_ENCODE: return "Encoding Error";
        case CodecStatus::ERROR_DECODE: return "Decoding Error";
        case CodecStatus::ERROR_INVALID_PARAMS: return "Invalid Parameters";
        case CodecStatus::ERROR_NOT_AVAILABLE: return "Codec Not Available";
        default: return "Unknown Error";
    }
}

bool isValidBitrate(uint32_t bitrate) {
    return bitrate >= Config::LYRA_MIN_BITRATE && bitrate <= Config::LYRA_MAX_BITRATE;
}

uint32_t calculateOptimalBitrate(uint32_t sampleRate, uint32_t channels, float qualityFactor) {
    // Basit bitrate hesaplama
    qualityFactor = std::max(0.0f, std::min(1.0f, qualityFactor));
    
    uint32_t minBitrate = Config::LYRA_MIN_BITRATE;
    uint32_t maxBitrate = Config::LYRA_MAX_BITRATE;
    
    uint32_t calculatedBitrate = static_cast<uint32_t>(
        minBitrate + (maxBitrate - minBitrate) * qualityFactor
    );
    
    return calculatedBitrate;
}

size_t calculateMaxPacketSize(uint32_t bitrate, uint32_t frameRate) {
    // Frame başına maksimum byte sayısı
    return (bitrate / 8) / frameRate + 64; // +64 header için
}

} // namespace CodecUtils

} // namespace NovaVoice