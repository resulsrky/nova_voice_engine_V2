#include <iostream>
#include <memory>
#include <signal.h>
#include <thread>
#include <chrono>
#include <string>

#include "Config.h"
#include "BufferManager.h"
#include "UDPManager.h"
#include "AudioCapture.h"
#include "AudioPlayer.h"

using namespace NovaVoice;

// Global değişkenler
std::atomic<bool> g_running(true);
std::shared_ptr<BufferManager> g_bufferManager;
std::shared_ptr<UDPManager> g_udpManager;
std::shared_ptr<AudioCapture> g_audioCapture;
std::shared_ptr<AudioPlayer> g_audioPlayer;

// Signal handler
void signalHandler(int signal) {
    std::cout << "\nÇıkış sinyali alındı (" << signal << "). Program sonlandırılıyor..." << std::endl;
    g_running = false;
}

// Yardım mesajı
void printUsage(const char* programName) {
    std::cout << "Kullanım: " << programName << " [SEÇENEKLER]" << std::endl;
    std::cout << std::endl;
    std::cout << "Seçenekler:" << std::endl;
    std::cout << "  -s, --server [PORT]     Server modunda çalıştır (varsayılan port: " << Config::DEFAULT_PORT << ")" << std::endl;
    std::cout << "  -c, --client IP [PORT]  Client modunda çalıştır" << std::endl;
    std::cout << "  -d, --device DEVICE     Ses cihazı adı (varsayılan: default)" << std::endl;
    std::cout << "  -h, --help             Bu yardım mesajını göster" << std::endl;
    std::cout << std::endl;
    std::cout << "Örnekler:" << std::endl;
    std::cout << "  " << programName << " --server                # Server modu, port 8888" << std::endl;
    std::cout << "  " << programName << " --server 9999           # Server modu, port 9999" << std::endl;
    std::cout << "  " << programName << " --client 192.168.1.100  # Client modu, port 8888" << std::endl;
    std::cout << "  " << programName << " --client 192.168.1.100 9999  # Client modu, port 9999" << std::endl;
}

// Sistem başlatma
bool initializeSystem(const std::string& audioDevice) {
    std::cout << "=== Nova Voice Engine V2 Başlatılıyor ===" << std::endl;
    
    // Buffer Manager oluştur
    g_bufferManager = std::make_shared<BufferManager>();
    std::cout << "✓ Buffer Manager başlatıldı" << std::endl;
    
    // UDP Manager oluştur
    g_udpManager = std::make_shared<UDPManager>();
    g_udpManager->setBufferManager(g_bufferManager);
    std::cout << "✓ UDP Manager başlatıldı" << std::endl;
    
    // Audio Capture oluştur
    g_audioCapture = std::make_shared<AudioCapture>();
    if (!g_audioCapture->initialize(audioDevice)) {
        std::cerr << "✗ Audio Capture başlatılamadı" << std::endl;
        return false;
    }
    g_audioCapture->setBufferManager(g_bufferManager);
    std::cout << "✓ Audio Capture başlatıldı" << std::endl;
    
    // Audio Player oluştur
    g_audioPlayer = std::make_shared<AudioPlayer>();
    if (!g_audioPlayer->initialize(audioDevice)) {
        std::cerr << "✗ Audio Player başlatılamadı" << std::endl;
        return false;
    }
    g_audioPlayer->setBufferManager(g_bufferManager);
    std::cout << "✓ Audio Player başlatıldı" << std::endl;
    
    // Ses yakalama ve çalmayı başlat
    if (!g_audioCapture->start()) {
        std::cerr << "✗ Audio Capture başlatılamadı" << std::endl;
        return false;
    }
    
    if (!g_audioPlayer->start()) {
        std::cerr << "✗ Audio Player başlatılamadı" << std::endl;
        return false;
    }
    
    std::cout << "✓ Ses sistemi başlatıldı" << std::endl;
    return true;
}

// Sistem kapatma
void shutdownSystem() {
    std::cout << "\n=== Sistem Kapatılıyor ===" << std::endl;
    
    if (g_audioCapture) {
        g_audioCapture->stop();
        std::cout << "✓ Audio Capture durduruldu" << std::endl;
    }
    
    if (g_audioPlayer) {
        g_audioPlayer->stop();
        std::cout << "✓ Audio Player durduruldu" << std::endl;
    }
    
    if (g_udpManager) {
        g_udpManager->stop();
        std::cout << "✓ UDP Manager durduruldu" << std::endl;
    }
    
    if (g_bufferManager) {
        g_bufferManager->clearBuffers();
        std::cout << "✓ Buffer Manager temizlendi" << std::endl;
    }
    
    std::cout << "=== Sistem Kapatıldı ===" << std::endl;
}

// İstatistikleri yazdır
void printStatistics() {
    while (g_running) {
        std::this_thread::sleep_for(std::chrono::seconds(5));
        
        if (!g_running) break;
        
        std::cout << "\n=== İstatistikler ===" << std::endl;
        
        if (g_bufferManager) {
            std::cout << "Buffer - Input: " << g_bufferManager->getInputBufferSize() 
                     << ", Output: " << g_bufferManager->getOutputBufferSize()
                     << ", Dropped: " << g_bufferManager->getDroppedPackets() << std::endl;
        }
        
        if (g_udpManager) {
            std::cout << "Network - Sent: " << g_udpManager->getSentPackets()
                     << ", Received: " << g_udpManager->getReceivedPackets()
                     << ", Failed: " << g_udpManager->getFailedSends() << std::endl;
        }
        
        if (g_audioCapture) {
            std::cout << "Audio Capture - Frames: " << g_audioCapture->getCapturedFrames()
                     << ", Overruns: " << g_audioCapture->getBufferOverruns() << std::endl;
        }
        
        if (g_audioPlayer) {
            std::cout << "Audio Player - Frames: " << g_audioPlayer->getPlayedFrames()
                     << ", Underruns: " << g_audioPlayer->getBufferUnderruns() << std::endl;
        }
        
        std::cout << "===================" << std::endl;
    }
}

// Ana fonksiyon
int main(int argc, char* argv[]) {
    // Signal handler ayarla
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);
    
    // Parametreleri parse et
    bool isServer = false;
    std::string serverIP;
    uint16_t port = Config::DEFAULT_PORT;
    std::string audioDevice = "default";
    
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        
        if (arg == "-h" || arg == "--help") {
            printUsage(argv[0]);
            return 0;
        } else if (arg == "-s" || arg == "--server") {
            isServer = true;
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                port = static_cast<uint16_t>(std::stoi(argv[++i]));
            }
        } else if (arg == "-c" || arg == "--client") {
            if (i + 1 >= argc) {
                std::cerr << "Hata: Client modu için IP adresi gerekli" << std::endl;
                printUsage(argv[0]);
                return 1;
            }
            isServer = false;
            serverIP = argv[++i];
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                port = static_cast<uint16_t>(std::stoi(argv[++i]));
            }
        } else if (arg == "-d" || arg == "--device") {
            if (i + 1 >= argc) {
                std::cerr << "Hata: Cihaz adı gerekli" << std::endl;
                printUsage(argv[0]);
                return 1;
            }
            audioDevice = argv[++i];
        } else {
            std::cerr << "Hata: Bilinmeyen parametre: " << arg << std::endl;
            printUsage(argv[0]);
            return 1;
        }
    }
    
    // Server veya client modu kontrolü
    if (!isServer && serverIP.empty()) {
        std::cerr << "Hata: Server (-s) veya Client (-c) modu seçmelisiniz" << std::endl;
        printUsage(argv[0]);
        return 1;
    }
    
    // Sistemi başlat
    if (!initializeSystem(audioDevice)) {
        std::cerr << "Sistem başlatılamadı!" << std::endl;
        return 1;
    }
    
    // Network bağlantısını başlat
    bool networkOk = false;
    if (isServer) {
        std::cout << "Server modu - Port: " << port << " dinleniyor..." << std::endl;
        networkOk = g_udpManager->startServer(port);
    } else {
        std::cout << "Client modu - " << serverIP << ":" << port << " adresine bağlanılıyor..." << std::endl;
        networkOk = g_udpManager->startClient(serverIP, port);
    }
    
    if (!networkOk) {
        std::cerr << "Network bağlantısı kurulamadı!" << std::endl;
        shutdownSystem();
        return 1;
    }
    
    std::cout << "✓ Network bağlantısı kuruldu" << std::endl;
    std::cout << "\nSistem hazır! Sesli konuşma aktif..." << std::endl;
    std::cout << "Çıkmak için Ctrl+C tuşlayın." << std::endl;
    
    // İstatistik thread'i başlat
    std::thread statsThread(printStatistics);
    
    // Ana loop
    while (g_running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    // Temizlik
    if (statsThread.joinable()) {
        statsThread.join();
    }
    
    shutdownSystem();
    
    std::cout << "Program sonlandı." << std::endl;
    return 0;
}