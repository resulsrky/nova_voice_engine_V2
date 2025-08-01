#pragma once

#include <string>
#include <cstdint>

namespace NovaVoice {

class Config {
public:
    // === TEMEL SES KONFIGÜRASYONU ===
    static constexpr uint32_t SAMPLE_RATE = 48000;      // Hz (RNNoise için 48kHz gerekli)
    static constexpr uint16_t CHANNELS = 1;             // Mono
    static constexpr uint16_t BITS_PER_SAMPLE = 16;     // 16-bit
    static constexpr uint32_t FRAMES_PER_BUFFER = 1024; // Buffer boyutu
    
    // === LYRA V2 CODEC KONFIGÜRASYONU ===
    static constexpr uint32_t LYRA_SAMPLE_RATE = 16000; // Lyra için optimize edilmiş
    static constexpr uint32_t LYRA_FRAME_RATE = 50;     // 50Hz (20ms frames)
    static constexpr uint32_t LYRA_FRAME_SIZE_MS = 20;  // 20ms
    static constexpr uint32_t LYRA_FRAME_SIZE = (LYRA_SAMPLE_RATE * LYRA_FRAME_SIZE_MS) / 1000; // 320 samples
    
    // Lyra bitrate aralığı (kbps)
    static constexpr uint32_t LYRA_MIN_BITRATE = 3200;  // 3.2 kbps
    static constexpr uint32_t LYRA_MAX_BITRATE = 9200;  // 9.2 kbps
    static constexpr uint32_t LYRA_DEFAULT_BITRATE = 6000; // 6 kbps (orta kalite)
    
    // === RNNOISE KONFIGÜRASYONU ===
    static constexpr uint32_t RNNOISE_SAMPLE_RATE = 48000; // RNNoise 48kHz'de çalışır
    static constexpr uint32_t RNNOISE_FRAME_SIZE = 480;    // 10ms @ 48kHz
    static constexpr float RNNOISE_THRESHOLD = 0.5f;       // Gürültü eşiği
    
    // === AĞ KONFIGÜRASYONU ===
    static constexpr uint16_t DEFAULT_PORT = 8888;
    static constexpr size_t PACKET_SIZE = 1024;         // UDP paket boyutu
    static constexpr size_t BUFFER_COUNT = 10;          // Buffer sayısı
    
    // === TIMEOUT DEĞERLERİ (ms) ===
    static constexpr uint32_t NETWORK_TIMEOUT = 5000;
    static constexpr uint32_t AUDIO_TIMEOUT = 1000;
    static constexpr uint32_t CODEC_TIMEOUT = 100;      // Codec timeout
    
    // === SES KALİTESİ ===
    static constexpr float VOLUME_GAIN = 1.0f;
    static constexpr bool ENABLE_NOISE_REDUCTION = true;
    static constexpr bool ENABLE_CODEC = true;
    
    // === PERFORMANS ===
    static constexpr bool AUTO_BITRATE_ADJUSTMENT = true; // Otomatik bitrate ayarlama
    static constexpr uint32_t BITRATE_UPDATE_INTERVAL_MS = 5000; // 5 saniye
    
    // Ağ adresleri
    static const std::string DEFAULT_LOCAL_IP;
    static const std::string BROADCAST_IP;
    
private:
    Config() = default;
    ~Config() = default;
};

} // namespace NovaVoice