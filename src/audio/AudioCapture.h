#pragma once

#include <memory>
#include <thread>
#include <atomic>
#include <functional>
#include <alsa/asoundlib.h>
#include "Config.h"
#include "BufferManager.h"

namespace NovaVoice {

class AudioCapture {
public:
    AudioCapture();
    ~AudioCapture();
    
    // Ses yakalama yönetimi
    bool initialize(const std::string& deviceName = "default");
    bool start();
    void stop();
    
    // Buffer manager bağlantısı
    void setBufferManager(std::shared_ptr<BufferManager> bufferManager);
    
    // Callback ayarlama
    void setOnAudioCaptured(std::function<void(const uint8_t*, size_t)> callback);
    
    // Durum kontrolü
    bool isInitialized() const { return isInitialized_; }
    bool isCapturing() const { return isCapturing_; }
    
    // Ses seviyesi kontrolü
    void setGain(float gain);
    float getGain() const { return gain_; }
    
    // İstatistikler
    uint64_t getCapturedFrames() const { return capturedFrames_; }
    uint64_t getBufferOverruns() const { return bufferOverruns_; }
    
    // Cihaz bilgileri
    std::string getDeviceName() const { return deviceName_; }
    uint32_t getSampleRate() const { return Config::SAMPLE_RATE; }
    uint16_t getChannels() const { return Config::CHANNELS; }
    uint16_t getBitsPerSample() const { return Config::BITS_PER_SAMPLE; }
    
private:
    // ALSA handle
    snd_pcm_t* pcmHandle_;
    snd_pcm_hw_params_t* hwParams_;
    
    // Cihaz ayarları
    std::string deviceName_;
    bool isInitialized_;
    
    // Thread yönetimi
    std::thread captureThread_;
    std::atomic<bool> isCapturing_;
    
    // Buffer yönetimi
    std::shared_ptr<BufferManager> bufferManager_;
    std::vector<uint8_t> captureBuffer_;
    
    // Ses ayarları
    float gain_;
    
    // Callback fonksiyonu
    std::function<void(const uint8_t*, size_t)> onAudioCaptured_;
    
    // İstatistikler
    std::atomic<uint64_t> capturedFrames_;
    std::atomic<uint64_t> bufferOverruns_;
    
    // İç metodlar
    bool configureDevice();
    void cleanup();
    void captureLoop();
    bool readAudioData();
    void processAudioData(const uint8_t* data, size_t size);
    void applyGain(uint8_t* data, size_t size);
    
    // Hata yönetimi
    void handleAlsaError(const std::string& operation, int error) const;
    void logError(const std::string& message) const;
    void logInfo(const std::string& message) const;
};

} // namespace NovaVoice