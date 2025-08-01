#include "BufferManager.h"
#include <iostream>

namespace NovaVoice {

BufferManager::BufferManager() 
    : maxBufferSize_(Config::BUFFER_COUNT)
    , nextSequenceNumber_(0)
    , droppedPackets_(0)
    , totalPackets_(0) {
}

BufferManager::~BufferManager() {
    clearBuffers();
}

bool BufferManager::pushAudioPacket(std::shared_ptr<AudioPacket> packet) {
    if (!packet) {
        return false;
    }
    
    std::lock_guard<std::mutex> lock(inputMutex_);
    
    if (isBufferFull(inputBuffer_)) {
        // Buffer dolu, eski paketleri temizle
        removeOldPackets(inputBuffer_);
        droppedPackets_++;
    }
    
    inputBuffer_.push(packet);
    totalPackets_++;
    
    inputCondition_.notify_one();
    return true;
}

std::shared_ptr<AudioPacket> BufferManager::popAudioPacket() {
    std::unique_lock<std::mutex> lock(inputMutex_);
    
    if (inputBuffer_.empty()) {
        return nullptr;
    }
    
    auto packet = inputBuffer_.front();
    inputBuffer_.pop();
    
    return packet;
}

bool BufferManager::pushInputBuffer(const uint8_t* data, size_t size) {
    if (!data || size == 0) {
        return false;
    }
    
    auto packet = std::make_shared<AudioPacket>(data, size, nextSequenceNumber_++);
    return pushAudioPacket(packet);
}

std::shared_ptr<AudioPacket> BufferManager::getNextOutputPacket() {
    return popAudioPacket();
}

bool BufferManager::pushNetworkPacket(std::shared_ptr<AudioPacket> packet) {
    if (!packet) {
        return false;
    }
    
    std::lock_guard<std::mutex> lock(outputMutex_);
    
    if (isBufferFull(outputBuffer_)) {
        // Buffer dolu, eski paketleri temizle
        removeOldPackets(outputBuffer_);
        droppedPackets_++;
    }
    
    outputBuffer_.push(packet);
    outputCondition_.notify_one();
    
    return true;
}

std::shared_ptr<AudioPacket> BufferManager::getNextPlaybackPacket() {
    std::unique_lock<std::mutex> lock(outputMutex_);
    
    // Bir paket gelene kadar bekle
    outputCondition_.wait_for(lock, std::chrono::milliseconds(10), 
                             [this] { return !outputBuffer_.empty(); });
    
    if (outputBuffer_.empty()) {
        return nullptr;
    }
    
    auto packet = outputBuffer_.front();
    outputBuffer_.pop();
    
    return packet;
}

size_t BufferManager::getInputBufferSize() const {
    std::lock_guard<std::mutex> lock(inputMutex_);
    return inputBuffer_.size();
}

size_t BufferManager::getOutputBufferSize() const {
    std::lock_guard<std::mutex> lock(outputMutex_);
    return outputBuffer_.size();
}

bool BufferManager::isInputBufferFull() const {
    std::lock_guard<std::mutex> lock(inputMutex_);
    return isBufferFull(inputBuffer_);
}

bool BufferManager::isOutputBufferEmpty() const {
    std::lock_guard<std::mutex> lock(outputMutex_);
    return outputBuffer_.empty();
}

void BufferManager::clearBuffers() {
    {
        std::lock_guard<std::mutex> lock(inputMutex_);
        std::queue<std::shared_ptr<AudioPacket>> empty;
        inputBuffer_.swap(empty);
    }
    
    {
        std::lock_guard<std::mutex> lock(outputMutex_);
        std::queue<std::shared_ptr<AudioPacket>> empty;
        outputBuffer_.swap(empty);
    }
    
    nextSequenceNumber_ = 0;
}

void BufferManager::setMaxBufferSize(size_t maxSize) {
    maxBufferSize_ = maxSize;
}

bool BufferManager::isBufferFull(const std::queue<std::shared_ptr<AudioPacket>>& buffer) const {
    return buffer.size() >= maxBufferSize_;
}

void BufferManager::removeOldPackets(std::queue<std::shared_ptr<AudioPacket>>& buffer) {
    if (!buffer.empty()) {
        buffer.pop(); // En eski paketi çıkar
    }
}

} // namespace NovaVoice