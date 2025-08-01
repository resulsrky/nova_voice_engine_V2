#include "UDPManager.h"
#include <iostream>
#include <cstring>
#include <unistd.h>
#include <errno.h>

namespace NovaVoice {

UDPManager::UDPManager() 
    : socketFd_(-1)
    , isRunning_(false)
    , isServer_(false)
    , sentPackets_(0)
    , receivedPackets_(0)
    , failedSends_(0) {
    memset(&localAddr_, 0, sizeof(localAddr_));
    memset(&remoteAddr_, 0, sizeof(remoteAddr_));
}

UDPManager::~UDPManager() {
    stop();
}

bool UDPManager::createSocket() {
    socketFd_ = socket(AF_INET, SOCK_DGRAM, 0);
    if (socketFd_ < 0) {
        logError("Socket oluşturulamadı: " + std::string(strerror(errno)));
        return false;
    }
    
    // Socket seçeneklerini ayarla
    int reuse = 1;
    if (setsockopt(socketFd_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
        logError("SO_REUSEADDR ayarlanamadı: " + std::string(strerror(errno)));
        closeSocket();
        return false;
    }
    
    return true;
}

void UDPManager::closeSocket() {
    if (socketFd_ >= 0) {
        close(socketFd_);
        socketFd_ = -1;
    }
}

bool UDPManager::bindSocket(uint16_t port) {
    localAddr_.sin_family = AF_INET;
    localAddr_.sin_addr.s_addr = INADDR_ANY;
    localAddr_.sin_port = htons(port);
    
    if (bind(socketFd_, (struct sockaddr*)&localAddr_, sizeof(localAddr_)) < 0) {
        logError("Socket bind edilemedi: " + std::string(strerror(errno)));
        return false;
    }
    
    return true;
}

bool UDPManager::startServer(uint16_t port) {
    if (isRunning_) {
        return false;
    }
    
    if (!createSocket()) {
        return false;
    }
    
    if (!bindSocket(port)) {
        closeSocket();
        return false;
    }
    
    isServer_ = true;
    isRunning_ = true;
    
    // Receiver thread'i başlat
    receiverThread_ = std::thread(&UDPManager::receiverLoop, this);
    
    std::cout << "UDP Server port " << port << " üzerinde başlatıldı" << std::endl;
    return true;
}

bool UDPManager::startClient(const std::string& serverIP, uint16_t port) {
    if (isRunning_) {
        return false;
    }
    
    if (!createSocket()) {
        return false;
    }
    
    // Remote address ayarla
    remoteAddr_.sin_family = AF_INET;
    remoteAddr_.sin_port = htons(port);
    
    if (inet_pton(AF_INET, serverIP.c_str(), &remoteAddr_.sin_addr) <= 0) {
        logError("Geçersiz IP adresi: " + serverIP);
        closeSocket();
        return false;
    }
    
    isServer_ = false;
    isRunning_ = true;
    
    // Receiver thread'i başlat
    receiverThread_ = std::thread(&UDPManager::receiverLoop, this);
    
    std::cout << "UDP Client " << serverIP << ":" << port << " adresine bağlandı" << std::endl;
    return true;
}

void UDPManager::stop() {
    if (!isRunning_) {
        return;
    }
    
    isRunning_ = false;
    
    // Socket'i kapat (receiver thread'in sonlanmasını sağlar)
    closeSocket();
    
    // Thread'in bitmesini bekle
    if (receiverThread_.joinable()) {
        receiverThread_.join();
    }
    
    std::cout << "UDP Manager durduruldu" << std::endl;
}

bool UDPManager::sendAudioPacket(std::shared_ptr<AudioPacket> packet) {
    if (!packet || !isRunning_) {
        return false;
    }
    
    auto serializedData = serializePacket(packet);
    return sendData(serializedData.data(), serializedData.size());
}

bool UDPManager::sendData(const uint8_t* data, size_t size) {
    if (!data || size == 0 || !isRunning_ || socketFd_ < 0) {
        return false;
    }
    
    struct sockaddr_in targetAddr;
    if (isServer_) {
        // Server modunda, bilinen remote address'e gönder
        targetAddr = remoteAddr_;
    } else {
        // Client modunda, server'a gönder
        targetAddr = remoteAddr_;
    }
    
    ssize_t bytesSent = sendto(socketFd_, data, size, 0, 
                              (struct sockaddr*)&targetAddr, sizeof(targetAddr));
    
    if (bytesSent < 0) {
        logError("Veri gönderilemedi: " + std::string(strerror(errno)));
        failedSends_++;
        return false;
    }
    
    if (static_cast<size_t>(bytesSent) != size) {
        logError("Veri kısmen gönderildi: " + std::to_string(bytesSent) + "/" + std::to_string(size));
        failedSends_++;
        return false;
    }
    
    sentPackets_++;
    return true;
}

void UDPManager::setBufferManager(std::shared_ptr<BufferManager> bufferManager) {
    bufferManager_ = bufferManager;
}

void UDPManager::setOnDataReceived(std::function<void(const uint8_t*, size_t)> callback) {
    onDataReceived_ = callback;
}

void UDPManager::setOnPacketReceived(std::function<void(std::shared_ptr<AudioPacket>)> callback) {
    onPacketReceived_ = callback;
}

void UDPManager::receiverLoop() {
    uint8_t buffer[Config::PACKET_SIZE * 2]; // Biraz daha büyük buffer
    struct sockaddr_in fromAddr;
    socklen_t fromAddrLen = sizeof(fromAddr);
    
    while (isRunning_) {
        ssize_t bytesReceived = recvfrom(socketFd_, buffer, sizeof(buffer), 0,
                                        (struct sockaddr*)&fromAddr, &fromAddrLen);
        
        if (bytesReceived < 0) {
            if (isRunning_) {
                logError("Veri alınamadı: " + std::string(strerror(errno)));
            }
            break;
        }
        
        if (bytesReceived > 0) {
            processReceivedData(buffer, bytesReceived, fromAddr);
            receivedPackets_++;
        }
    }
}

bool UDPManager::processReceivedData(const uint8_t* data, size_t size, const struct sockaddr_in& fromAddr) {
    if (!data || size == 0) {
        return false;
    }
    
    // Server modunda, remote address'i güncelle
    if (isServer_) {
        remoteAddr_ = fromAddr;
    }
    
    // Veriyi AudioPacket olarak deserialize et
    auto packet = deserializePacket(data, size);
    
    if (packet) {
        // Buffer manager'a paketi ekle
        if (bufferManager_) {
            bufferManager_->pushNetworkPacket(packet);
        }
        
        // Callback çağır
        if (onPacketReceived_) {
            onPacketReceived_(packet);
        }
    }
    
    // Ham veri callback'i de çağır
    if (onDataReceived_) {
        onDataReceived_(data, size);
    }
    
    return true;
}

std::shared_ptr<AudioPacket> UDPManager::deserializePacket(const uint8_t* data, size_t size) {
    if (!data || size < sizeof(uint32_t)) {
        return nullptr;
    }
    
    // Basit deserializasyon: [sequence_number][data_size][audio_data]
    uint32_t sequenceNumber;
    memcpy(&sequenceNumber, data, sizeof(uint32_t));
    
    size_t audioDataSize = size - sizeof(uint32_t);
    const uint8_t* audioData = data + sizeof(uint32_t);
    
    auto packet = std::make_shared<AudioPacket>(audioData, audioDataSize, sequenceNumber);
    return packet;
}

std::vector<uint8_t> UDPManager::serializePacket(std::shared_ptr<AudioPacket> packet) {
    if (!packet) {
        return {};
    }
    
    // Basit serializasyon: [sequence_number][audio_data]
    std::vector<uint8_t> serialized;
    serialized.reserve(sizeof(uint32_t) + packet->data.size());
    
    // Sequence number ekle
    uint32_t seqNum = packet->sequenceNumber;
    serialized.insert(serialized.end(), 
                     reinterpret_cast<uint8_t*>(&seqNum),
                     reinterpret_cast<uint8_t*>(&seqNum) + sizeof(uint32_t));
    
    // Audio data ekle
    serialized.insert(serialized.end(), packet->data.begin(), packet->data.end());
    
    return serialized;
}

std::string UDPManager::getAddressString(const struct sockaddr_in& addr) const {
    char ipStr[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &addr.sin_addr, ipStr, INET_ADDRSTRLEN);
    return std::string(ipStr) + ":" + std::to_string(ntohs(addr.sin_port));
}

bool UDPManager::setRemoteAddress(const std::string& ip, uint16_t port) {
    if (!isRunning_) {
        logError("UDP Manager çalışmıyor, remote address ayarlanamaz");
        return false;
    }
    
    // Remote address ayarla
    remoteAddr_.sin_family = AF_INET;
    remoteAddr_.sin_port = htons(port);
    
    if (inet_pton(AF_INET, ip.c_str(), &remoteAddr_.sin_addr) <= 0) {
        logError("Geçersiz IP adresi: " + ip);
        return false;
    }
    
    std::cout << "[UDPManager] Remote address ayarlandı: " << ip << ":" << port << std::endl;
    return true;
}

void UDPManager::logError(const std::string& message) const {
    std::cerr << "[UDPManager ERROR] " << message << std::endl;
}

} // namespace NovaVoice