#pragma once

#include <vector>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <memory>
#include <chrono>
#include "Config.h"

namespace NovaVoice {

// Ses paketi yapısı
struct AudioPacket {
    std::vector<uint8_t> data;
    uint32_t sequenceNumber;
    std::chrono::steady_clock::time_point timestamp;
    size_t size;
    
    AudioPacket() : sequenceNumber(0), size(0) {
        data.reserve(Config::PACKET_SIZE);
        timestamp = std::chrono::steady_clock::now();
    }
    
    AudioPacket(const uint8_t* audioData, size_t dataSize, uint32_t seqNum)
        : sequenceNumber(seqNum), size(dataSize) {
        data.assign(audioData, audioData + dataSize);
        timestamp = std::chrono::steady_clock::now();
    }
};

class BufferManager {
public:
    BufferManager();
    ~BufferManager();
    
    // Buffer işlemleri
    bool pushAudioPacket(std::shared_ptr<AudioPacket> packet);
    std::shared_ptr<AudioPacket> popAudioPacket();
    
    // Input buffer (mikrofon -> ağ)
    bool pushInputBuffer(const uint8_t* data, size_t size);
    std::shared_ptr<AudioPacket> getNextOutputPacket();
    
    // Output buffer (ağ -> hoparlör)
    bool pushNetworkPacket(std::shared_ptr<AudioPacket> packet);
    std::shared_ptr<AudioPacket> getNextPlaybackPacket();
    
    // Buffer durumu
    size_t getInputBufferSize() const;
    size_t getOutputBufferSize() const;
    bool isInputBufferFull() const;
    bool isOutputBufferEmpty() const;
    
    // Buffer yönetimi
    void clearBuffers();
    void setMaxBufferSize(size_t maxSize);
    
    // İstatistikler
    uint64_t getDroppedPackets() const { return droppedPackets_; }
    uint64_t getTotalPackets() const { return totalPackets_; }
    
private:
    // Input buffer (mikrofon ses verisi)
    std::queue<std::shared_ptr<AudioPacket>> inputBuffer_;
    mutable std::mutex inputMutex_;
    std::condition_variable inputCondition_;
    
    // Output buffer (ağdan gelen ses verisi)
    std::queue<std::shared_ptr<AudioPacket>> outputBuffer_;
    mutable std::mutex outputMutex_;
    std::condition_variable outputCondition_;
    
    // Buffer boyut limitleri
    size_t maxBufferSize_;
    
    // Paket numaralandırma
    uint32_t nextSequenceNumber_;
    
    // İstatistikler
    uint64_t droppedPackets_;
    uint64_t totalPackets_;
    
    // Yardımcı metodlar
    bool isBufferFull(const std::queue<std::shared_ptr<AudioPacket>>& buffer) const;
    void removeOldPackets(std::queue<std::shared_ptr<AudioPacket>>& buffer);
};

} // namespace NovaVoice