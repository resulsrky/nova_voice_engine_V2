#include "AudioPlayer.h"
#include <iostream>
#include <cstring>
#include <algorithm>

namespace NovaVoice {

AudioPlayer::AudioPlayer()
    : pcmHandle_(nullptr)
    , hwParams_(nullptr)
    , deviceName_("default")
    , isInitialized_(false)
    , isPlaying_(false)
    , volume_(Config::VOLUME_GAIN)
    , isMuted_(false)
    , playedFrames_(0)
    , bufferUnderruns_(0)
    , droppedPackets_(0) {
    
    size_t bufferSize = Config::FRAMES_PER_BUFFER * Config::CHANNELS * (Config::BITS_PER_SAMPLE / 8);
    playbackBuffer_.resize(bufferSize);
    silenceBuffer_.resize(bufferSize, 0); // Sessizlik için sıfırlar
}

AudioPlayer::~AudioPlayer() {
    stop();
    cleanup();
}

bool AudioPlayer::initialize(const std::string& deviceName) {
    if (isInitialized_) {
        logError("AudioPlayer zaten başlatılmış");
        return false;
    }
    
    deviceName_ = deviceName;
    
    // PCM handle oluştur
    int error = snd_pcm_open(&pcmHandle_, deviceName_.c_str(), SND_PCM_STREAM_PLAYBACK, 0);
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
    logInfo("AudioPlayer başarıyla başlatıldı - Device: " + deviceName_);
    
    return true;
}

bool AudioPlayer::configureDevice() {
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

void AudioPlayer::cleanup() {
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

bool AudioPlayer::start() {
    if (!isInitialized_) {
        logError("AudioPlayer başlatılmamış");
        return false;
    }
    
    if (isPlaying_) {
        logError("AudioPlayer zaten çalışıyor");
        return false;
    }
    
    // PCM'i hazırla
    int error = snd_pcm_prepare(pcmHandle_);
    if (error < 0) {
        handleAlsaError("snd_pcm_prepare", error);
        return false;
    }
    
    isPlaying_ = true;
    playbackThread_ = std::thread(&AudioPlayer::playbackLoop, this);
    
    logInfo("AudioPlayer başlatıldı");
    return true;
}

void AudioPlayer::stop() {
    if (!isPlaying_) {
        return;
    }
    
    isPlaying_ = false;
    
    // PCM'i durdur
    if (pcmHandle_) {
        snd_pcm_drop(pcmHandle_);
    }
    
    // Thread'in bitmesini bekle
    if (playbackThread_.joinable()) {
        playbackThread_.join();
    }
    
    logInfo("AudioPlayer durduruldu");
}

void AudioPlayer::setBufferManager(std::shared_ptr<BufferManager> bufferManager) {
    bufferManager_ = bufferManager;
}

bool AudioPlayer::playData(const uint8_t* data, size_t size) {
    if (!data || size == 0 || !isInitialized_) {
        return false;
    }
    
    return writeAudioData(data, size);
}

bool AudioPlayer::playPacket(std::shared_ptr<AudioPacket> packet) {
    if (!packet || packet->data.empty()) {
        return false;
    }
    
    return playData(packet->data.data(), packet->data.size());
}

void AudioPlayer::setVolume(float volume) {
    volume_ = std::max(0.0f, std::min(2.0f, volume)); // 0.0 - 2.0 arası sınırla
}

void AudioPlayer::setMuted(bool muted) {
    isMuted_ = muted;
}

void AudioPlayer::setOnAudioPlayed(std::function<void(size_t)> callback) {
    onAudioPlayed_ = callback;
}

void AudioPlayer::playbackLoop() {
    while (isPlaying_) {
        size_t dataSize = playbackBuffer_.size();
        
        if (getNextAudioData(playbackBuffer_.data(), dataSize)) {
            // Ses verisi mevcut, çal
            processAudioData(playbackBuffer_.data(), dataSize);
            writeAudioData(playbackBuffer_.data(), dataSize);
        } else {
            // Ses verisi yok, sessizlik çal
            playSilence();
        }
    }
}

bool AudioPlayer::getNextAudioData(uint8_t* buffer, size_t& size) {
    if (!bufferManager_) {
        return false;
    }
    
    auto packet = bufferManager_->getNextPlaybackPacket();
    if (!packet || packet->data.empty()) {
        return false;
    }
    
    size_t copySize = std::min(size, packet->data.size());
    std::memcpy(buffer, packet->data.data(), copySize);
    size = copySize;
    
    return true;
}

void AudioPlayer::processAudioData(uint8_t* data, size_t size) {
    if (!data || size == 0) {
        return;
    }
    
    // Volume uygula (mute değilse)
    if (!isMuted_ && volume_ != 1.0f) {
        applyVolume(data, size);
    } else if (isMuted_) {
        // Mute ise sessizlik
        std::memset(data, 0, size);
    }
}

void AudioPlayer::applyVolume(uint8_t* data, size_t size) {
    // 16-bit signed samples için volume uygula
    int16_t* samples = reinterpret_cast<int16_t*>(data);
    size_t sampleCount = size / sizeof(int16_t);
    
    for (size_t i = 0; i < sampleCount; ++i) {
        float sample = static_cast<float>(samples[i]) * volume_;
        
        // Clipping önle
        sample = std::max(-32768.0f, std::min(32767.0f, sample));
        
        samples[i] = static_cast<int16_t>(sample);
    }
}

bool AudioPlayer::writeAudioData(const uint8_t* data, size_t size) {
    if (!pcmHandle_ || !isPlaying_ || !data || size == 0) {
        return false;
    }
    
    size_t framesToWrite = size / (Config::CHANNELS * (Config::BITS_PER_SAMPLE / 8));
    
    snd_pcm_sframes_t framesWritten = snd_pcm_writei(pcmHandle_, data, framesToWrite);
    
    if (framesWritten < 0) {
        if (framesWritten == -EPIPE) {
            // Buffer underrun
            bufferUnderruns_++;
            logError("Buffer underrun oluştu");
            
            // PCM'i yeniden hazırla
            int error = snd_pcm_prepare(pcmHandle_);
            if (error < 0) {
                handleAlsaError("snd_pcm_prepare (recovery)", error);
                return false;
            }
        } else {
            handleAlsaError("snd_pcm_writei", framesWritten);
            return false;
        }
    } else if (framesWritten > 0) {
        playedFrames_ += framesWritten;
        
        // Callback çağır
        if (onAudioPlayed_) {
            onAudioPlayed_(framesWritten * Config::CHANNELS * (Config::BITS_PER_SAMPLE / 8));
        }
    }
    
    return true;
}

void AudioPlayer::playSilence() {
    // Kısa sessizlik çal
    writeAudioData(silenceBuffer_.data(), silenceBuffer_.size());
    
    // Kısa bekle
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
}

void AudioPlayer::handleAlsaError(const std::string& operation, int error) const {
    std::string errorMsg = operation + " başarısız: " + snd_strerror(error);
    logError(errorMsg);
}

void AudioPlayer::logError(const std::string& message) const {
    std::cerr << "[AudioPlayer ERROR] " << message << std::endl;
}

void AudioPlayer::logInfo(const std::string& message) const {
    std::cout << "[AudioPlayer INFO] " << message << std::endl;
}

} // namespace NovaVoice