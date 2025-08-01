#include "AudioCapture.h"
#include <iostream>
#include <cstring>
#include <algorithm>

namespace NovaVoice {

AudioCapture::AudioCapture()
    : pcmHandle_(nullptr)
    , hwParams_(nullptr)
    , deviceName_("default")
    , isInitialized_(false)
    , isCapturing_(false)
    , gain_(Config::VOLUME_GAIN)
    , capturedFrames_(0)
    , bufferOverruns_(0) {
    
    captureBuffer_.resize(Config::FRAMES_PER_BUFFER * Config::CHANNELS * (Config::BITS_PER_SAMPLE / 8));
}

AudioCapture::~AudioCapture() {
    stop();
    cleanup();
}

bool AudioCapture::initialize(const std::string& deviceName) {
    if (isInitialized_) {
        logError("AudioCapture zaten başlatılmış");
        return false;
    }
    
    deviceName_ = deviceName;
    
    // PCM handle oluştur
    int error = snd_pcm_open(&pcmHandle_, deviceName_.c_str(), SND_PCM_STREAM_CAPTURE, 0);
    if (error < 0) {
        handleAlsaError("snd_pcm_open", error);
        return false;
    }
    
    // Hardware parametrelerini ayarla
    if (!configureDevice()) {
        cleanup();
        return false;
    }
    
    isInitialized_ = true;
    logInfo("AudioCapture başarıyla başlatıldı - Device: " + deviceName_);
    
    return true;
}

bool AudioCapture::configureDevice() {
    int error;
    
    // Hardware parametreleri için alan ayır
    error = snd_pcm_hw_params_malloc(&hwParams_);
    if (error < 0) {
        handleAlsaError("snd_pcm_hw_params_malloc", error);
        return false;
    }
    
    // Mevcut hardware parametrelerini al
    error = snd_pcm_hw_params_any(pcmHandle_, hwParams_);
    if (error < 0) {
        handleAlsaError("snd_pcm_hw_params_any", error);
        return false;
    }
    
    // Interleaved access ayarla
    error = snd_pcm_hw_params_set_access(pcmHandle_, hwParams_, SND_PCM_ACCESS_RW_INTERLEAVED);
    if (error < 0) {
        handleAlsaError("snd_pcm_hw_params_set_access", error);
        return false;
    }
    
    // Format ayarla (16-bit signed little endian)
    error = snd_pcm_hw_params_set_format(pcmHandle_, hwParams_, SND_PCM_FORMAT_S16_LE);
    if (error < 0) {
        handleAlsaError("snd_pcm_hw_params_set_format", error);
        return false;
    }
    
    // Kanal sayısını ayarla
    error = snd_pcm_hw_params_set_channels(pcmHandle_, hwParams_, Config::CHANNELS);
    if (error < 0) {
        handleAlsaError("snd_pcm_hw_params_set_channels", error);
        return false;
    }
    
    // Sample rate ayarla
    unsigned int sampleRate = Config::SAMPLE_RATE;
    error = snd_pcm_hw_params_set_rate_near(pcmHandle_, hwParams_, &sampleRate, nullptr);
    if (error < 0) {
        handleAlsaError("snd_pcm_hw_params_set_rate_near", error);
        return false;
    }
    
    if (sampleRate != Config::SAMPLE_RATE) {
        logInfo("Sample rate ayarlandı: " + std::to_string(sampleRate) + " Hz (istenen: " + std::to_string(Config::SAMPLE_RATE) + " Hz)");
    }
    
    // Buffer boyutunu ayarla
    snd_pcm_uframes_t frames = Config::FRAMES_PER_BUFFER;
    error = snd_pcm_hw_params_set_period_size_near(pcmHandle_, hwParams_, &frames, nullptr);
    if (error < 0) {
        handleAlsaError("snd_pcm_hw_params_set_period_size_near", error);
        return false;
    }
    
    // Hardware parametrelerini uygula
    error = snd_pcm_hw_params(pcmHandle_, hwParams_);
    if (error < 0) {
        handleAlsaError("snd_pcm_hw_params", error);
        return false;
    }
    
    return true;
}

void AudioCapture::cleanup() {
    if (hwParams_) {
        snd_pcm_hw_params_free(hwParams_);
        hwParams_ = nullptr;
    }
    
    if (pcmHandle_) {
        snd_pcm_close(pcmHandle_);
        pcmHandle_ = nullptr;
    }
    
    isInitialized_ = false;
}

bool AudioCapture::start() {
    if (!isInitialized_) {
        logError("AudioCapture başlatılmamış");
        return false;
    }
    
    if (isCapturing_) {
        logError("AudioCapture zaten çalışıyor");
        return false;
    }
    
    // PCM'i hazırla
    int error = snd_pcm_prepare(pcmHandle_);
    if (error < 0) {
        handleAlsaError("snd_pcm_prepare", error);
        return false;
    }
    
    isCapturing_ = true;
    captureThread_ = std::thread(&AudioCapture::captureLoop, this);
    
    logInfo("AudioCapture başlatıldı");
    return true;
}

void AudioCapture::stop() {
    if (!isCapturing_) {
        return;
    }
    
    isCapturing_ = false;
    
    // PCM'i durdur
    if (pcmHandle_) {
        snd_pcm_drop(pcmHandle_);
    }
    
    // Thread'in bitmesini bekle
    if (captureThread_.joinable()) {
        captureThread_.join();
    }
    
    logInfo("AudioCapture durduruldu");
}

void AudioCapture::setBufferManager(std::shared_ptr<BufferManager> bufferManager) {
    bufferManager_ = bufferManager;
}

void AudioCapture::setOnAudioCaptured(std::function<void(const uint8_t*, size_t)> callback) {
    onAudioCaptured_ = callback;
}

void AudioCapture::setGain(float gain) {
    gain_ = std::max(0.0f, std::min(2.0f, gain)); // 0.0 - 2.0 arası sınırla
}

void AudioCapture::captureLoop() {
    while (isCapturing_) {
        if (!readAudioData()) {
            // Hata durumunda kısa bekle
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
}

bool AudioCapture::readAudioData() {
    if (!pcmHandle_ || !isCapturing_) {
        return false;
    }
    
    snd_pcm_sframes_t framesRead = snd_pcm_readi(pcmHandle_, 
                                                 captureBuffer_.data(), 
                                                 Config::FRAMES_PER_BUFFER);
    
    if (framesRead < 0) {
        if (framesRead == -EPIPE) {
            // Buffer overrun
            bufferOverruns_++;
            logError("Buffer overrun oluştu");
            
            // PCM'i yeniden hazırla
            int error = snd_pcm_prepare(pcmHandle_);
            if (error < 0) {
                handleAlsaError("snd_pcm_prepare (recovery)", error);
                return false;
            }
        } else {
            handleAlsaError("snd_pcm_readi", framesRead);
            return false;
        }
    } else if (framesRead > 0) {
        size_t bytesRead = framesRead * Config::CHANNELS * (Config::BITS_PER_SAMPLE / 8);
        processAudioData(captureBuffer_.data(), bytesRead);
        capturedFrames_ += framesRead;
    }
    
    return true;
}

void AudioCapture::processAudioData(const uint8_t* data, size_t size) {
    if (!data || size == 0) {
        return;
    }
    
    // Gain uygulamak için kopya oluştur
    std::vector<uint8_t> processedData(data, data + size);
    
    // Gain uygula
    if (gain_ != 1.0f) {
        applyGain(processedData.data(), size);
    }
    
    // Buffer manager'a gönder
    if (bufferManager_) {
        bufferManager_->pushInputBuffer(processedData.data(), size);
    }
    
    // Callback çağır
    if (onAudioCaptured_) {
        onAudioCaptured_(processedData.data(), size);
    }
}

void AudioCapture::applyGain(uint8_t* data, size_t size) {
    // 16-bit signed samples için gain uygula
    int16_t* samples = reinterpret_cast<int16_t*>(data);
    size_t sampleCount = size / sizeof(int16_t);
    
    for (size_t i = 0; i < sampleCount; ++i) {
        float sample = static_cast<float>(samples[i]) * gain_;
        
        // Clipping önle
        sample = std::max(-32768.0f, std::min(32767.0f, sample));
        
        samples[i] = static_cast<int16_t>(sample);
    }
}

void AudioCapture::handleAlsaError(const std::string& operation, int error) const {
    std::string errorMsg = operation + " başarısız: " + snd_strerror(error);
    logError(errorMsg);
}

void AudioCapture::logError(const std::string& message) const {
    std::cerr << "[AudioCapture ERROR] " << message << std::endl;
}

void AudioCapture::logInfo(const std::string& message) const {
    std::cout << "[AudioCapture INFO] " << message << std::endl;
}

} // namespace NovaVoice