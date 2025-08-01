#pragma once

#include <string>
#include <memory>
#include <thread>
#include <atomic>
#include <functional>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "Config.h"
#include "BufferManager.h"

namespace NovaVoice {

class UDPManager {
public:
    UDPManager();
    ~UDPManager();
    
    // Bağlantı yönetimi
    bool startServer(uint16_t port = Config::DEFAULT_PORT);
    bool startClient(const std::string& serverIP, uint16_t port = Config::DEFAULT_PORT);
    void stop();
    
    // Veri gönderme/alma
    bool sendAudioPacket(std::shared_ptr<AudioPacket> packet);
    bool sendData(const uint8_t* data, size_t size);
    
    // Buffer manager bağlantısı
    void setBufferManager(std::shared_ptr<BufferManager> bufferManager);
    
    // Durum kontrolü
    bool isRunning() const { return isRunning_; }
    bool isServer() const { return isServer_; }
    
    // İstatistikler
    uint64_t getSentPackets() const { return sentPackets_; }
    uint64_t getReceivedPackets() const { return receivedPackets_; }
    uint64_t getFailedSends() const { return failedSends_; }
    
    // Callback ayarlama
    void setOnDataReceived(std::function<void(const uint8_t*, size_t)> callback);
    void setOnPacketReceived(std::function<void(std::shared_ptr<AudioPacket>)> callback);
    
    // P2P için remote address ayarlama
    bool setRemoteAddress(const std::string& ip, uint16_t port);
    
private:
    // Socket yönetimi
    int socketFd_;
    struct sockaddr_in localAddr_;
    struct sockaddr_in remoteAddr_;
    
    // Thread yönetimi
    std::thread receiverThread_;
    std::atomic<bool> isRunning_;
    std::atomic<bool> isServer_;
    
    // Buffer manager
    std::shared_ptr<BufferManager> bufferManager_;
    
    // Callback fonksiyonları
    std::function<void(const uint8_t*, size_t)> onDataReceived_;
    std::function<void(std::shared_ptr<AudioPacket>)> onPacketReceived_;
    
    // İstatistikler
    std::atomic<uint64_t> sentPackets_;
    std::atomic<uint64_t> receivedPackets_;
    std::atomic<uint64_t> failedSends_;
    
    // İç metodlar
    bool createSocket();
    void closeSocket();
    bool bindSocket(uint16_t port);
    void receiverLoop();
    bool processReceivedData(const uint8_t* data, size_t size, const struct sockaddr_in& fromAddr);
    
    // Paket işleme
    std::shared_ptr<AudioPacket> deserializePacket(const uint8_t* data, size_t size);
    std::vector<uint8_t> serializePacket(std::shared_ptr<AudioPacket> packet);
    
    // Yardımcı metodlar
    std::string getAddressString(const struct sockaddr_in& addr) const;
    void logError(const std::string& message) const;
};

} // namespace NovaVoice