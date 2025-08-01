# Nova Voice Engine V2

Modüler mimarili UDP sesli konuşma uygulaması. Bu uygulama gerçek zamanlı ses iletimi sağlar ve gelecekte görüntülü konuşmaya entegre edilebilir.

## Özellikler

- **Modüler Mimari**: Ses yakalama, çalma, ağ iletişimi ve buffer yönetimi ayrı modüllerde
- **UDP Protokolü**: Düşük gecikme için UDP tabanlı veri iletimi
- **Paket Bufferlama**: Ağ gecikmeleri için otomatik bufferlama sistemi
- **ALSA Desteği**: Linux ses sistemi entegrasyonu
- **Gerçek Zamanlı**: Minimum gecikme ile ses iletimi
- **Server/Client Modu**: Hem server hem client olarak çalışabilir

## Gereksinimler

- **Linux İşletim Sistemi**
- **CMake 3.16+**
- **C++17 Derleyici** (GCC 7+ veya Clang 6+)
- **ALSA Kütüphanesi**:
  ```bash
  # Ubuntu/Debian:
  sudo apt-get install libasound2-dev
  
  # CentOS/RHEL/Fedora:
  sudo yum install alsa-lib-devel
  # veya
  sudo dnf install alsa-lib-devel
  ```

## Derleme

```bash
# Proje dizininde:
mkdir build
cd build
cmake ..
make -j$(nproc)
```

## Kullanım

### Server Modu
```bash
# Varsayılan port (8888) ile server başlat
./nova_voice_engine --server

# Özel port ile server başlat
./nova_voice_engine --server 9999
```

### Client Modu
```bash
# Varsayılan port ile client başlat
./nova_voice_engine --client 192.168.1.100

# Özel port ile client başlat
./nova_voice_engine --client 192.168.1.100 9999
```

### Ses Cihazı Seçimi
```bash
# Özel ses cihazı kullan
./nova_voice_engine --server --device hw:1,0

# Mevcut ses cihazlarını listele
aplay -l  # Çalma cihazları
arecord -l  # Kayıt cihazları
```

## Parametre Listesi

- `-s, --server [PORT]`: Server modunda çalıştır
- `-c, --client IP [PORT]`: Client modunda çalıştır  
- `-d, --device DEVICE`: Ses cihazı adı (varsayılan: default)
- `-h, --help`: Yardım mesajını göster

## Modüler Mimari

### 1. Audio Modülleri
- **AudioCapture**: Mikrofon ses yakalama (ALSA)
- **AudioPlayer**: Hoparlör ses çalma (ALSA)

### 2. Network Modülü
- **UDPManager**: UDP paket gönderme/alma

### 3. Buffer Modülü  
- **BufferManager**: Ses paketlerini bufferlama

### 4. Config Modülü
- **Config**: Sistem konfigürasyonu ve sabitler

## Ses Formatı

- **Sample Rate**: 44.1 kHz
- **Channels**: Mono (1 kanal)
- **Bit Depth**: 16-bit
- **Format**: Signed Little Endian

## Network Protokolü

- **Transport**: UDP
- **Paket Boyutu**: 1024 byte
- **Port**: 8888 (varsayılan)
- **Paket Formatı**: [sequence_number][audio_data]

## Gelecek Özellikler

- Görüntü desteği eklenebilir
- Codec desteği (Opus, AAC)
- Şifreleme desteği
- Çoklu kullanıcı desteği
- GUI arayüz

## Sorun Giderme

### Ses Cihazı Problemi
```bash
# Ses cihazlarını kontrol et
aplay -l
arecord -l

# ALSA test et
speaker-test -c 1 -t sine
arecord -d 5 -f cd test.wav
```

### Ağ Problemi
```bash
# Port kontrolü
netstat -una | grep 8888

# Firewall kontrolü
sudo ufw status
```

### Derleme Problemi
```bash
# ALSA kütüphanesi kontrolü
pkg-config --exists alsa
pkg-config --cflags --libs alsa
```

## Lisans

Bu proje açık kaynak kodludur ve MIT lisansı altında dağıtılmaktadır.# nova_voice_engine_V2
