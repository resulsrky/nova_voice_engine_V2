#pragma once

#include <memory>
#include <thread>
#include <atomic>
#include <functional>
#include <alsa/asoundlib.h>
#include "Config.h"
#include "BufferManager.h"

namespace NovaVoice {

class AudioPlayer {
public:
    AudioPlayer();
    ~AudioPlayer();
    
    // Ses çalma yönetimi
    bool initialize(const std::string& deviceName = "default");
    bool start();
    void stop();
    
    // Buffer manager bağlantısı
    void setBufferManager(std::shared_ptr<BufferManager> bufferManager);
    
    // Veri çalma
    bool playData(const uint8_t* data, size_t size);
    bool playPacket(std::shared_ptr<AudioPacket> packet);
    
    // Durum kontrolü
    bool isInitialized() const { return isInitialized_; }
    bool isPlaying() const { return isPlaying_; }
    
    // Ses seviyesi kontrolü
    void setVolume(float volume);
    float getVolume() const { return volume_; }
    void setMuted(bool muted);
    bool isMuted() const { return isMuted_; }
    
    // İstatistikler
    uint64_t getPlayedFrames() const { return playedFrames_; }
    uint64_t getBufferUnderruns() const { return bufferUnderruns_; }
    uint64_t getDroppedPackets() const { return droppedPackets_; }
    
    // Cihaz bilgileri
    std::string getDeviceName() const { return deviceName_; }
    uint32_t getSampleRate() const { return Config::SAMPLE_RATE; }
    uint16_t getChannels() const { return Config::CHANNELS; }
    uint16_t getBitsPerSample() const { return Config::BITS_PER_SAMPLE; }
    
    // Callback ayarlama
    void setOnAudioPlayed(std::function<void(size_t)> callback);
    
private:
    // ALSA handle
    snd_pcm_t* pcmHandle_;
    snd_pcm_hw_params_t* hwParams_;
    
    // Cihaz ayarları
    std::string deviceName_;
    bool isInitialized_;
    
    // Thread yönetimi
    std::thread playbackThread_;
    std::atomic<bool> isPlaying_;
    
    // Buffer yönetimi
    std::shared_ptr<BufferManager> bufferManager_;
    std::vector<uint8_t> playbackBuffer_;
    std::vector<uint8_t> silenceBuffer_;
    
    // Ses ayarları
    float volume_;
    bool isMuted_;
    
    // Callback fonksiyonu
    std::function<void(size_t)> onAudioPlayed_;
    
    // İstatistikler
    std::atomic<uint64_t> playedFrames_;
    std::atomic<uint64_t> bufferUnderruns_;
    std::atomic<uint64_t> droppedPackets_;
    
    // İç metodlar
    bool configureDevice();
    void cleanup();
    void playbackLoop();
    bool writeAudioData(const uint8_t* data, size_t size);
    void processAudioData(uint8_t* data, size_t size);
    void applyVolume(uint8_t* data, size_t size);
    void playSilence();
    
    // Buffer yönetimi
    bool getNextAudioData(uint8_t* buffer, size_t& size);
    
    // Hata yönetimi
    void handleAlsaError(const std::string& operation, int error) const;
    void logError(const std::string& message) const;
    void logInfo(const std::string& message) const;
};

} // namespace NovaVoice