cmake_minimum_required(VERSION 3.16)
project(NovaVoiceEngine)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Build tipini ayarla
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# Bağımlılıkları bul
find_package(PkgConfig REQUIRED)

# ALSA kütüphanesini bul (Linux ses sistemi için)
pkg_check_modules(ALSA REQUIRED alsa)

# Python3 gerekli (Lyra için)
find_package(Python3 COMPONENTS Interpreter REQUIRED)

# Git gerekli (external deps için)
find_package(Git QUIET)

# External dependencies klasörü
set(EXTERNAL_DIR ${CMAKE_SOURCE_DIR}/external)

# External dependencies'leri fetch et
if(NOT EXISTS ${EXTERNAL_DIR}/lyra OR NOT EXISTS ${EXTERNAL_DIR}/rnnoise)
    message(STATUS "External dependencies bulunamadı, indiriliyor...")
    execute_process(
        COMMAND ${CMAKE_SOURCE_DIR}/scripts/fetch_deps.sh
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        RESULT_VARIABLE FETCH_RESULT
    )
    if(NOT FETCH_RESULT EQUAL 0)
        message(FATAL_ERROR "External dependencies indirilemedi!")
    endif()
endif()

# RNNoise dahil et
if(EXISTS ${EXTERNAL_DIR}/rnnoise/CMakeLists.txt)
    message(STATUS "RNNoise bulundu, ekleniyor...")
    add_subdirectory(${EXTERNAL_DIR}/rnnoise EXCLUDE_FROM_ALL)
    set(RNNOISE_FOUND TRUE)
else()
    message(WARNING "RNNoise bulunamadı")
    set(RNNOISE_FOUND FALSE)
endif()

# Lyra wrapper'ı için include
set(LYRA_FOUND FALSE)
if(EXISTS ${EXTERNAL_DIR}/lyra)
    message(STATUS "Lyra bulundu")
    set(LYRA_FOUND TRUE)
    set(LYRA_DIR ${EXTERNAL_DIR}/lyra)
endif()

# Kaynak dosyaları
set(SOURCES
    src/main.cpp
    src/audio/AudioCapture.cpp
    src/audio/AudioPlayer.cpp
    src/network/UDPManager.cpp
    src/buffer/BufferManager.cpp
    src/config/Config.cpp
)

# Lyra wrapper ekle (eğer varsa)
if(LYRA_FOUND)
    list(APPEND SOURCES
        src/codec/LyraCodec.cpp
        src/codec/BitrateCalculator.cpp
    )
endif()

# Noise suppression ekle (eğer varsa)  
if(RNNOISE_FOUND)
    list(APPEND SOURCES
        src/audio/NoiseSuppresor.cpp
        src/audio/AudioPreprocessor.cpp
    )
endif()

# Header dosyaları için include dizinleri
include_directories(
    include
    src/audio
    src/network
    src/buffer
    src/config
    ${ALSA_INCLUDE_DIRS}
)

# Lyra include paths ekle
if(LYRA_FOUND)
    include_directories(
        src/codec
        ${LYRA_DIR}
        ${LYRA_DIR}/lyra
    )
endif()

# RNNoise include paths ekle  
if(RNNOISE_FOUND)
    include_directories(
        ${EXTERNAL_DIR}/rnnoise/include
    )
endif()

# Çalıştırılabilir dosya oluştur
add_executable(nova_voice_engine ${SOURCES})

# Kütüphaneleri bağla
set(LINK_LIBRARIES
    ${ALSA_LIBRARIES}
    pthread
)

# RNNoise kütüphanesini bağla
if(RNNOISE_FOUND)
    list(APPEND LINK_LIBRARIES renamenoise)
    target_compile_definitions(nova_voice_engine PRIVATE HAVE_RNNOISE=1)
endif()

# Lyra linking (manual olarak yapacağız çünkü Bazel kullanıyor)
if(LYRA_FOUND)
    target_compile_definitions(nova_voice_engine PRIVATE HAVE_LYRA=1)
    message(STATUS "Lyra etkin - wrapper modülü kullanılacak")
endif()

target_link_libraries(nova_voice_engine ${LINK_LIBRARIES})

# Derleme bayrakları
target_compile_options(nova_voice_engine PRIVATE ${ALSA_CFLAGS_OTHER})

# Debug için compile flags
set(CMAKE_CXX_FLAGS_DEBUG "-g -O0 -Wall -Wextra")
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG")

# Lyra için ek bayraklar
if(LYRA_FOUND)
    target_compile_options(nova_voice_engine PRIVATE -DHAVE_LYRA)
endif()

# RNNoise için ek bayraklar
if(RNNOISE_FOUND)
    target_compile_options(nova_voice_engine PRIVATE -DHAVE_RNNOISE)
endif()

# Post-build mesajları
add_custom_command(TARGET nova_voice_engine POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E echo "=== Nova Voice Engine V2 Build Tamamlandı ==="
    COMMAND ${CMAKE_COMMAND} -E echo "Lyra v2 Codec: ${LYRA_FOUND}"
    COMMAND ${CMAKE_COMMAND} -E echo "RNNoise: ${RNNOISE_FOUND}"
    COMMAND ${CMAKE_COMMAND} -E echo "ALSA: Evet"
    COMMAND ${CMAKE_COMMAND} -E echo "Çıktı: ${CMAKE_BINARY_DIR}/nova_voice_engine"
)