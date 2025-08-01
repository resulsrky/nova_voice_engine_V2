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
    
    // Hızlı kapatma için sistemleri zorla durdur
    if (g_audioCapture) {
        g_audioCapture->stop();
    }
    if (g_audioPlayer) {
        g_audioPlayer->stop();
    }
    if (g_udpManager) {
        g_udpManager->stop();
    }
}

// Yardım mesajı
void printUsage(const char* programName) {
    std::cout << "Nova Voice Engine V2 - Sesli Konuşma Uygulaması" << std::endl;
    std::cout << "==================================================" << std::endl;
    std::cout << std::endl;
    std::cout << "Kullanım 1 (P2P Modu - Önerilen):" << std::endl;
    std::cout << "  " << programName << " <REMOTE_IP> <LOCAL_PORT> <REMOTE_PORT> [OPTIONS]" << std::endl;
    std::cout << std::endl;
    std::cout << "Kullanım 2 (Klasik Server/Client):" << std::endl;
    std::cout << "  " << programName << " [SEÇENEKLER]" << std::endl;
    std::cout << std::endl;
    std::cout << "P2P Modu Parametreleri:" << std::endl;
    std::cout << "  REMOTE_IP               Karşı tarafın IP adresi" << std::endl;
    std::cout << "  LOCAL_PORT              Bu makinenin dinleme portu (gönderme portu da)" << std::endl;
    std::cout << "  REMOTE_PORT             Karşı tarafın dinleme portu (hedef port)" << std::endl;
    std::cout << std::endl;
    std::cout << "Klasik Mod Seçenekleri:" << std::endl;
    std::cout << "  -s, --server [PORT]     Server modunda çalıştır (varsayılan port: " << Config::DEFAULT_PORT << ")" << std::endl;
    std::cout << "  -c, --client IP [PORT]  Client modunda çalıştır" << std::endl;
    std::cout << std::endl;
    std::cout << "Genel Seçenekler:" << std::endl;
    std::cout << "  -d, --device DEVICE     Ses cihazı adı (varsayılan: default)" << std::endl;
    std::cout << "  -h, --help             Bu yardım mesajını göster" << std::endl;
    std::cout << std::endl;
    std::cout << "P2P Örnekleri (Eşzamanlı çalıştırın):" << std::endl;
    std::cout << "  Senaryonuz:" << std::endl;
    std::cout << "    Sen (192.168.1.100): " << programName << " 192.168.1.15 45000 11111" << std::endl;
    std::cout << "    Karşı taraf (192.168.1.15): " << programName << " 192.168.1.100 11111 45000" << std::endl;
    std::cout << std::endl;
    std::cout << "  Genel örnek:" << std::endl;
    std::cout << "    Makine 1: " << programName << " 192.168.1.200 8888 9999" << std::endl;
    std::cout << "    Makine 2: " << programName << " 192.168.1.100 9999 8888" << std::endl;
    std::cout << std::endl;
    std::cout << "Klasik Örnekler:" << std::endl;
    std::cout << "  " << programName << " --server                # Server modu, port 8888" << std::endl;
    std::cout << "  " << programName << " --client 192.168.1.100  # Client modu" << std::endl;
    std::cout << std::endl;
    std::cout << "Özellikler:" << std::endl;
    std::cout << "  ✓ Lyra v2 Neural Codec (3.2-9.2 kbps)" << std::endl;
    std::cout << "  ✓ RNNoise Gürültü Engelleyici" << std::endl;
    std::cout << "  ✓ Otomatik Bitrate Adaptasyonu" << std::endl;
    std::cout << "  ✓ Real-time Voice Processing" << std::endl;
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
        // 5 saniye bekle ama her 100ms'de g_running kontrol et
        for (int i = 0; i < 50 && g_running; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        
        if (!g_running) break;
        
        std::cout << "\n=== İstatistikler ===" << std::endl;
        
        if (g_bufferManager) {
            std::cout << "Buffer - Input: " << g_bufferManager->getInputBufferSize() 
                     << ", Output: " << g_bufferManager->getOutputBufferSize()
                     << ", Dropped: " << g_bufferManager->getDroppedPackets() << std::endl;
        }
        
        if (g_udpManager) {
            auto sent = g_udpManager->getSentPackets();
            auto received = g_udpManager->getReceivedPackets();
            auto failed = g_udpManager->getFailedSends();
            
            std::cout << "Network - Sent: " << sent
                     << ", Received: " << received
                     << ", Failed: " << failed;
            
            // Ses akışı durumu
            if (sent > 0) std::cout << " 📤";
            if (received > 0) std::cout << " 📥";
            if (failed > 0) std::cout << " ❌";
            
            std::cout << std::endl;
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
    bool isPeerToPeer = false;
    std::string remoteIP;
    uint16_t localPort = Config::DEFAULT_PORT;
    uint16_t remotePort = Config::DEFAULT_PORT;
    std::string audioDevice = "default";
    
    // P2P modu kontrolü (ilk argüman IP adresi mi?)
    if (argc >= 4 && std::string(argv[1]).find('.') != std::string::npos) {
        // P2P Modu: ./program remote_ip local_port remote_port
        isPeerToPeer = true;
        remoteIP = argv[1];
        
        try {
            localPort = static_cast<uint16_t>(std::stoi(argv[2]));
            remotePort = static_cast<uint16_t>(std::stoi(argv[3]));
        } catch (const std::exception& e) {
            std::cerr << "Hata: Geçersiz port numarası" << std::endl;
            printUsage(argv[0]);
            return 1;
        }
        
        // Kalan argümanları kontrol et (device vs.)
        for (int i = 4; i < argc; ++i) {
            std::string arg = argv[i];
            if (arg == "-d" || arg == "--device") {
                if (i + 1 >= argc) {
                    std::cerr << "Hata: Cihaz adı gerekli" << std::endl;
                    printUsage(argv[0]);
                    return 1;
                }
                audioDevice = argv[++i];
            } else if (arg == "-h" || arg == "--help") {
                printUsage(argv[0]);
                return 0;
            } else {
                std::cerr << "Hata: Bilinmeyen parametre: " << arg << std::endl;
                printUsage(argv[0]);
                return 1;
            }
        }
    } else {
        // Klasik Server/Client Modu
        for (int i = 1; i < argc; ++i) {
            std::string arg = argv[i];
            
            if (arg == "-h" || arg == "--help") {
                printUsage(argv[0]);
                return 0;
            } else if (arg == "-s" || arg == "--server") {
                isServer = true;
                if (i + 1 < argc && argv[i + 1][0] != '-') {
                    localPort = static_cast<uint16_t>(std::stoi(argv[++i]));
                }
            } else if (arg == "-c" || arg == "--client") {
                if (i + 1 >= argc) {
                    std::cerr << "Hata: Client modu için IP adresi gerekli" << std::endl;
                    printUsage(argv[0]);
                    return 1;
                }
                isServer = false;
                remoteIP = argv[++i];
                if (i + 1 < argc && argv[i + 1][0] != '-') {
                    remotePort = static_cast<uint16_t>(std::stoi(argv[++i]));
                    localPort = remotePort; // Client modunda aynı port
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
        
        // Klasik mod kontrolü
        if (!isServer && remoteIP.empty()) {
            std::cerr << "Hata: Server (-s) veya Client (-c) modu seçmelisiniz" << std::endl;
            std::cerr << "Veya P2P modu için: " << argv[0] << " <IP> <LOCAL_PORT> <REMOTE_PORT>" << std::endl;
            printUsage(argv[0]);
            return 1;
        }
    }
    
    // Sistemi başlat
    if (!initializeSystem(audioDevice)) {
        std::cerr << "Sistem başlatılamadı!" << std::endl;
        return 1;
    }
    
    // Network bağlantısını başlat
    bool networkOk = false;
    
    if (isPeerToPeer) {
        // P2P Modu: Hem dinle hem bağlan
        std::cout << "🔗 P2P Modu Başlatılıyor..." << std::endl;
        std::cout << "   📥 Dinleme: Port " << localPort << " (gelen sesler)" << std::endl;
        std::cout << "   📤 Gönderim: " << remoteIP << ":" << remotePort << " (giden sesler)" << std::endl;
        
        // Network erişilebilirlik kontrolü
        std::cout << "🔍 Network erişilebilirlik kontrol ediliyor..." << std::endl;
        std::string pingCmd = "ping -c 1 -W 2 " + remoteIP + " > /dev/null 2>&1";
        if (system(pingCmd.c_str()) == 0) {
            std::cout << "✅ " << remoteIP << " erişilebilir" << std::endl;
        } else {
            std::cout << "⚠️  " << remoteIP << " ping yanıt vermiyor - firewall olabilir" << std::endl;
        }
        
        // Server olarak başlat (kendi portumuzda dinle)
        networkOk = g_udpManager->startServer(localPort);
        if (networkOk) {
            // P2P için remote address'i ayarla
            if (g_udpManager->setRemoteAddress(remoteIP, remotePort)) {
                std::cout << "✓ UDP Server port " << localPort << " üzerinde hazır" << std::endl;
                std::cout << "✓ " << remoteIP << ":" << remotePort << " hedefine paket göndermeye hazır" << std::endl;
                std::cout << "💡 P2P Bağlantısı: Her iki taraf da konuşmaya başladığında otomatik eşleşecek" << std::endl;
            } else {
                std::cerr << "✗ Remote address ayarlanamadı" << std::endl;
                networkOk = false;
            }
        }
    } else if (isServer) {
        // Klasik Server Modu
        std::cout << "Server modu - Port: " << localPort << " dinleniyor..." << std::endl;
        networkOk = g_udpManager->startServer(localPort);
    } else {
        // Klasik Client Modu  
        std::cout << "Client modu - " << remoteIP << ":" << remotePort << " adresine bağlanılıyor..." << std::endl;
        networkOk = g_udpManager->startClient(remoteIP, remotePort);
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
    
    // Ana loop - Hızlı yanıt için kısa sleep
    while (g_running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    
    // Temizlik
    if (statsThread.joinable()) {
        statsThread.join();
    }
    
    shutdownSystem();
    
    std::cout << "Program sonlandı." << std::endl;
    return 0;
}