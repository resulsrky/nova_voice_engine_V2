#!/bin/bash

# Nova Voice Engine V2 - External Dependencies Fetcher
# Bu script Lyra v2 ve RNNoise kütüphanelerini indirir ve hazırlar

set -e  # Exit on error

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
EXTERNAL_DIR="${PROJECT_ROOT}/external"
BUILD_DIR="${PROJECT_ROOT}/build"

echo "=== Nova Voice Engine V2 Dependencies Fetcher ==="
echo "Project Root: ${PROJECT_ROOT}"
echo "External Dir: ${EXTERNAL_DIR}"

# Temizlik
echo "🧹 Eski bağımlılıkları temizliyoruz..."
rm -rf "${EXTERNAL_DIR}/lyra" "${EXTERNAL_DIR}/rnnoise" "${EXTERNAL_DIR}/build"

# Directory oluştur
mkdir -p "${EXTERNAL_DIR}"
cd "${EXTERNAL_DIR}"

# 1. Google Lyra v2 İndir
echo "📥 Google Lyra v2 indiriliyor..."
git clone --depth 1 https://github.com/google/lyra.git lyra
cd lyra

# Lyra için gerekli model dosyalarını kontrol et
echo "📋 Lyra model dosyaları kontrol ediliyor..."
if [ ! -d "lyra/model_coeffs" ]; then
    echo "⚠️  Model coefficients bulunamadı. Lyra repo'sunda model_coeffs klasörü aranıyor..."
    find . -name "*.tflite" -o -name "model_coeffs" -o -name "*model*" | head -10
fi

cd "${EXTERNAL_DIR}"

# 2. RNNoise CMake versiyonu (ReNameNoise) indir
echo "📥 RNNoise (ReNameNoise) indiriliyor..."
git clone --depth 1 https://github.com/mumble-voip/ReNameNoise.git rnnoise

# 3. Build klasörü oluştur
mkdir -p build
cd build

echo "✅ Tüm bağımlılıklar başarıyla indirildi!"
echo ""
echo "📁 İndirilen bağımlılıklar:"
echo "   - ${EXTERNAL_DIR}/lyra (Google Lyra v2)"
echo "   - ${EXTERNAL_DIR}/rnnoise (RNNoise - CMake ready)"
echo ""
echo "🔧 Sonraki adım: cmake .. && make"
echo ""

# Lyra build bilgileri
echo "ℹ️  Lyra v2 Bilgileri:"
echo "   - Bitrate: 3.2kbps - 9.2kbps"  
echo "   - Frame Rate: 50Hz (20ms)"
echo "   - Sample Rate: 16kHz, 32kHz, 48kHz destekli"
echo "   - Model: SoundStream tabanlı"
echo ""

# RNNoise bilgileri
echo "ℹ️  RNNoise Bilgileri:"
echo "   - Sample Rate: 48kHz"
echo "   - Frame Size: 480 samples (10ms @ 48kHz)"
echo "   - Model: RNN tabanlı gürültü engelleyici"
echo ""

echo "🚀 Hazır! Nova Voice Engine V2'yi derleyebilirsiniz."