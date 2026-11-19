// UserTracks.h - Reproductor de User Tracks propio para VHudRadio v1.
// Se incluye DENTRO de VHudRadio.cpp, despues de las globales que usa
// (Log, g_slEngine/g_slEngineItf/g_slMix, SoundInit, FindPlayerVehicle,
// g_startTrack, g_trackManager, g_current, g_inVehicle, g_paused,
// g_userPause, g_codePause, aml). Todo es 'static': una sola TU.
//
// Diseno (copia el comportamiento de SAUserTracks, con el buffering arreglado):
// - Engancha CAERadioTrackManager::StartRadio: la 12 arranca el reproductor,
//   cualquier otra estacion lo para. En vehiculo bloqueado la 12 se redirige
//   a Radio Off (13), igual que SAUserTracks.
// - Carpetas de musica: las mismas 5 que SAUserTracks (la primera con audio
//   gana). Solo MP3 (minimp3); .wav/.ogg/.flac/.m4a no se decodifican en v1.
// - Playlist alfabetica por nombre de archivo; aleatoria (Fisher-Yates) por
//   defecto; infinita (al terminar rebaraja sin repetir la ultima).
// - Decodificador minimp3 en un hilo aparte; anillo de 512 KB; pre-llenado
//   de ~0.5 s antes de abrir el dispositivo; OpenSL con 4 buffers x 20 ms
//   (80 ms) en vez de 2 x 10 ms (20 ms). Al cambiar de pista NO se vacia el
//   anillo: todo se remuestrea a 44100 Hz estereo, asi el dispositivo se crea
//   una sola vez y nunca cambia de formato.
// - Pausa con g_paused (menu / m_UserPause / m_CodePause) y con
//   CLoadingScreen::m_bActive si resuelve; al salir del vehiculo se para.
// - Comparte el motor OpenSL de los clics (un solo motor en el proceso).
#pragma once

#define MINIMP3_IMPLEMENTATION
#include "minimp3.h"

#include <dirent.h>
#include <link.h>
#include <pthread.h>
#include <time.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <algorithm>
#include <chrono>

// ---- constantes ----
static const int UT_OUT_RATE = 44100;          // todo se remuestrea a 44100 Hz estereo
static const int UT_OUT_CH = 2;
static const size_t UT_RING_BYTES = 512 * 1024;  // anillo de 512 KB (~2.9 s)
static const size_t UT_PREFILL_BYTES = 88200;    // ~0.5 s antes de abrir el dispositivo
static const int UT_BUF_FRAMES = 882;            // 20 ms a 44100 Hz
static const int UT_BUF_SAMPLES = UT_BUF_FRAMES * UT_OUT_CH;  // 1764 muestras
static const size_t UT_BUF_BYTES = UT_BUF_SAMPLES * 2;         // 3528 bytes
static const int UT_NUM_BUFS = 4;                // 4 buffers x 20 ms = 80 ms en cola

// ---- carpetas de musica (las 5 de SAUserTracks, en orden; gana la primera con audio) ----
static const char* UT_MUSIC_DIRS[5] = {
    "/sdcard/Android_unprotected/data/com.rockstargames.gtasa/files/music",
    "/storage/emulated/0/Android_unprotected/data/com.rockstargames.gtasa/files/music",
    "/sdcard/Android/data/com.rockstargames.gtasa/files/music",
    "/storage/emulated/0/Android/data/com.rockstargames.gtasa/files/music",
    "/storage/emulated/0/GTASA",
};

// ---- blocklist de vehiculos sin radio (41 modelos, veh+0x32; misma que SAUserTracks) ----
static const int UT_BLOCKED_MODELS[41] = {
    407, 416, 425, 427, 430, 432, 433, 435, 449, 450, 472, 481, 486, 490, 497, 509, 510,
    523, 528, 530, 531, 532, 537, 538, 544, 565, 569, 570, 584, 590, 591, 596, 597, 598,
    599, 601, 606, 607, 608, 610, 611
};
static bool UT_ModelBlocked(int model) {
    for (int i = 0; i < 41; i++) if (UT_BLOCKED_MODELS[i] == model) return true;
    return false;
}
// Modelo del vehiculo del jugador (-1 si no hay vehiculo). Lo usa la rueda
// (ApplyStation) y el hook de StartRadio.
static int UT_PlayerVehicleModel() {
    if (!FindPlayerVehicle) return -1;
    void* veh = FindPlayerVehicle(-1, false);
    if (!veh) return -1;
    return (int)*(const uint16_t*)((const char*)veh + 0x32);
}
static bool UT_VehicleBlocked() {
    int m = UT_PlayerVehicleModel();
    return m >= 0 && UT_ModelBlocked(m);
}
static int UT_BlockedModel() { return UT_PlayerVehicleModel(); }

// ---- anillo (ring buffer) ----
struct UTRing {
    uint8_t* buf = nullptr;
    size_t cap = 0;
    size_t r = 0, w = 0;  // posiciones absolutas (monotonas); el modulo se aplica al acceder
    bool closed = false;
    pthread_mutex_t m = PTHREAD_MUTEX_INITIALIZER;
    pthread_cond_t notEmpty = PTHREAD_COND_INITIALIZER;
    pthread_cond_t notFull = PTHREAD_COND_INITIALIZER;
    bool init(size_t bytes) {
        buf = (uint8_t*)malloc(bytes);
        if (!buf) return false;
        cap = bytes; r = w = 0; closed = false;
        return true;
    }
    void free() { if (buf) { ::free(buf); buf = nullptr; } cap = 0; r = w = 0; }
    size_t availLocked() const { return w - r; }
    size_t avail() { pthread_mutex_lock(&m); size_t a = availLocked(); pthread_mutex_unlock(&m); return a; }
    void close() { pthread_mutex_lock(&m); closed = true; pthread_cond_broadcast(&notEmpty); pthread_cond_broadcast(&notFull); pthread_mutex_unlock(&m); }
    void discard() { pthread_mutex_lock(&m); r = w; pthread_cond_broadcast(&notFull); pthread_mutex_unlock(&m); }
    // Escritura bloqueante; false solo si se cerro el anillo a mitad.
    bool write(const uint8_t* p, size_t n) {
        size_t done = 0;
        while (done < n) {
            pthread_mutex_lock(&m);
            while (!closed && (cap - (w - r)) == 0) pthread_cond_wait(&notFull, &m);
            if (closed) { pthread_mutex_unlock(&m); return false; }
            size_t space = cap - (w - r);
            size_t chunk = n - done;
            if (chunk > space) chunk = space;
            size_t off = w % cap;
            size_t first = cap - off;
            if (first > chunk) first = chunk;
            memcpy(buf + off, p + done, first);
            if (first < chunk) memcpy(buf, p + done + first, chunk - first);
            w += chunk; done += chunk;
            pthread_cond_signal(&notEmpty);
            pthread_mutex_unlock(&m);
        }
        return true;
    }
    // Lectura no bloqueante (para el callback de OpenSL): lo que haya, o 0.
    size_t readNonblock(uint8_t* p, size_t n) {
        pthread_mutex_lock(&m);
        size_t a = availLocked();
        if (a > n) a = n;
        size_t off = r % cap;
        size_t first = cap - off;
        if (first > a) first = a;
        if (a) {
            memcpy(p, buf + off, first);
            if (first < a) memcpy(p + first, buf, a - first);
            r += a;
            pthread_cond_signal(&notFull);
        }
        pthread_mutex_unlock(&m);
        return a;
    }
};

// ---- estado del reproductor ----
static UTRing ut_ring;
static std::vector<std::string> ut_tracks;   // rutas completas, orden alfabetico por basename
static std::vector<int> ut_order;            // orden de reproduccion (indices en ut_tracks)
static int ut_orderPos = 0;
static bool ut_random = true;                // aleatorio por defecto, como SAUserTracks
static bool ut_active = false;               // estacion 12 sonando
static bool ut_pausedDev = false;            // dispositivo OpenSL en pausa
static bool ut_decPaused = false;            // hilo decodificador en pausa
static bool ut_stopThread = false;           // pedir salida al hilo decodificador
static bool ut_wantStop = false;             // el hilo pide parar (sin pistas decodificables)
static bool ut_threadRunning = false;
static pthread_t ut_thread;
static char ut_curTrack[256] = "";           // basename de la pista actual
static int ut_underruns = 0;                 // callbacks cortos (ring vacio) desde el inicio
static bool* ut_loadingActive = nullptr;     // CLoadingScreen::m_bActive (si resuelve)
static std::chrono::steady_clock::time_point ut_lastStatus = std::chrono::steady_clock::now();
static std::chrono::steady_clock::time_point ut_startedAt = std::chrono::steady_clock::now();
static bool ut_diagDone = true;  // diagnostico: lectura nativa con retraso (una vez por arranque)

// ---- OpenSL (reproductor de musica; comparte motor y mezcla con los clics) ----
static SLObjectItf ut_player = nullptr;
static SLPlayItf ut_play = nullptr;
static SLBufferQueueItf ut_queue = nullptr;
static int16_t ut_buf[UT_NUM_BUFS][UT_BUF_SAMPLES];
static int ut_bufIdx = 0;
static pthread_mutex_t ut_cbMutex = PTHREAD_MUTEX_INITIALIZER;

// ---- hook de StartRadio ----
static StartRadioTrackFn ut_origStartRadio = nullptr;
static bool ut_hookInstalled = false;
// DisplayRadioStationName del juego (muestra el nombre de la estacion en el
// HUD, como "K-JAH WEST"). En la 12 mostraria "Radio Off" (la nativa es 13),
// asi que mientras suena el reproductor la funcion ve la estacion 12: solo
// durante esa llamada los GetCurrentRadioStationID devuelven 12. Fuera de la
// llamada ven la nativa real (13), asi que el resto del juego no se entera.
typedef void (*DisplayNameFn)(void*);
static DisplayNameFn ut_origDisplayName = nullptr;
static bool ut_displayAs12 = false;
static GetStationFn ut_origGetStationTrack = nullptr;
static GetStationFn ut_origGetStationAudio = nullptr;
static signed char UT_GetStationTrack_hook(void* mgr) {
    if (ut_displayAs12) return 12;
    return ut_origGetStationTrack ? ut_origGetStationTrack(mgr) : 0;
}
static signed char UT_GetStationAudio_hook(void* eng) {
    if (ut_displayAs12) return 12;
    return ut_origGetStationAudio ? ut_origGetStationAudio(eng) : 0;
}
static void UT_DisplayName_hook(void* trackMgr) {
    if (ut_active && ut_origDisplayName) {
        ut_displayAs12 = true;
        ut_origDisplayName(trackMgr);
        ut_displayAs12 = false;
        return;
    }
    if (ut_origDisplayName) ut_origDisplayName(trackMgr);
}

// ---- utilidades ----
static const char* UT_Basename(const char* path) {
    const char* b = strrchr(path, '/');
    return b ? b + 1 : path;
}
static uint32_t UT_Xorshift(uint32_t& x) {
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return x;
}
static bool UT_HasMp3Ext(const char* name) {
    const char* dot = strrchr(name, '.');
    if (!dot) return false;
    return strcmp(dot, ".mp3") == 0 || strcmp(dot, ".MP3") == 0;
}

// Escanea las 5 carpetas en orden; la primera con al menos un MP3 gana.
// Rellena ut_tracks (alfabetico por basename) y el orden de reproduccion.
// Devuelve true si hay al menos una pista.
static bool UT_ScanMusic() {
    ut_tracks.clear();
    ut_order.clear();
    ut_orderPos = 0;
    char dirUsed[512] = "";
    for (int d = 0; d < 5; d++) {
        DIR* dir = opendir(UT_MUSIC_DIRS[d]);
        if (!dir) continue;
        std::vector<std::string> found;
        struct dirent* ent;
        while ((ent = readdir(dir)) != nullptr) {
            if (ent->d_name[0] == '.') continue;
            if (!UT_HasMp3Ext(ent->d_name)) continue;
            char full[1024];
            snprintf(full, sizeof full, "%s/%s", UT_MUSIC_DIRS[d], ent->d_name);
            struct stat st;
            if (stat(full, &st) != 0 || !S_ISREG(st.st_mode)) continue;
            found.push_back(full);
        }
        closedir(dir);
        if (!found.empty()) {
            snprintf(dirUsed, sizeof dirUsed, "%s", UT_MUSIC_DIRS[d]);
            ut_tracks = found;
            break;
        }
    }
    if (ut_tracks.empty()) return false;
    std::sort(ut_tracks.begin(), ut_tracks.end(), [](const std::string& a, const std::string& b) {
        return strcmp(UT_Basename(a.c_str()), UT_Basename(b.c_str())) < 0;
    });
    ut_order.resize(ut_tracks.size());
    for (size_t i = 0; i < ut_order.size(); i++) ut_order[i] = (int)i;
    Log("UT: carpeta de musica: %s (%d pistas MP3)", dirUsed, (int)ut_tracks.size());
    return true;
}

// Rebaraja el orden (Fisher-Yates). Si avoidFirst >= 0, ese indice no queda
// primero (no repetir la ultima pista al rebarajar).
static void UT_ShuffleOrder(int avoidFirst) {
    if (ut_order.size() < 2) return;
    uint32_t x = (uint32_t)(time(nullptr) ^ 0x9e3779b9 ^ (uintptr_t)&ut_order);
    if (x == 0) x = 0x9e3779b9;
    for (size_t i = ut_order.size() - 1; i > 0; i--) {
        size_t j = UT_Xorshift(x) % (i + 1);
        int t = ut_order[i]; ut_order[i] = ut_order[j]; ut_order[j] = t;
    }
    if (avoidFirst >= 0 && ut_order[0] == avoidFirst) {
        size_t j = 1 + (UT_Xorshift(x) % (ut_order.size() - 1));
        int t = ut_order[0]; ut_order[0] = ut_order[j]; ut_order[j] = t;
    }
}

// Siguiente ruta de la lista (avanza y rebaraja/envuelve al final). "" si no hay pistas.
static std::string UT_NextPath() {
    if (ut_tracks.empty() || ut_order.empty()) return std::string();
    if (ut_orderPos >= (int)ut_order.size()) {
        int last = ut_order.back();
        if (ut_random) UT_ShuffleOrder(last);
        ut_orderPos = 0;
    }
    return ut_tracks[ut_order[ut_orderPos++]];
}

// ---- callback de OpenSL (hilo interno de OpenSL) ----
// slBufferQueueCallback: void (SLBufferQueueItf, void*). Mismo uso que el
// ejemplo native-audio del NDK (SL_IID_BUFFERQUEUE + RegisterCallback).
static void UT_BufferCallback(SLBufferQueueItf bq, void* /*ctx*/) {
    pthread_mutex_lock(&ut_cbMutex);
    if (!ut_player || bq != ut_queue) { pthread_mutex_unlock(&ut_cbMutex); return; }
    int16_t* buf = ut_buf[ut_bufIdx];
    ut_bufIdx = (ut_bufIdx + 1) % UT_NUM_BUFS;
    size_t got = ut_ring.readNonblock((uint8_t*)buf, UT_BUF_BYTES);
    if (got < UT_BUF_BYTES) {
        memset((uint8_t*)buf + got, 0, UT_BUF_BYTES - got);
        ut_underruns++;
    }
    (*bq)->Enqueue(bq, buf, UT_BUF_BYTES);
    pthread_mutex_unlock(&ut_cbMutex);
}

// ---- remuestreo a 44100 Hz estereo (interpolacion lineal) ----
// Estado persistente entre frames: posicion fraccional y ultima muestra.
static double ut_rsPos = 0.0;       // posicion de la siguiente salida, en muestras de origen
static int16_t ut_rsPrev[2] = {0, 0};  // muestra en la posicion -1 (ultima del frame anterior)
static bool ut_rsFirst = true;
static int ut_rsRate = 0;
static void UT_ResampleReset() { ut_rsPos = 0.0; ut_rsPrev[0] = ut_rsPrev[1] = 0; ut_rsFirst = true; ut_rsRate = 0; }
// Decodifica un frame de minimp3 a 44100 estereo en out (capacidad outCap muestras
// por canal). Devuelve las muestras por canal escritas.
static int UT_ResampleFrame(const int16_t* pcm, int samples, int channels, int rate, int16_t* out, int outCap) {
    if (samples <= 0 || outCap <= 0) return 0;
    if (rate != ut_rsRate) { UT_ResampleReset(); ut_rsRate = rate; }
    if (ut_rsFirst) {
        ut_rsPrev[0] = pcm[0];
        ut_rsPrev[1] = (channels >= 2) ? pcm[1] : pcm[0];
        ut_rsFirst = false;
    }
    double ratio = (double)rate / (double)UT_OUT_RATE;
    int written = 0;
    auto sampleAt = [&](int i, int ch) -> int16_t {
        if (i < 0) return ut_rsPrev[ch];
        if (channels >= 2) return pcm[i * 2 + ch];
        return pcm[i];
    };
    while (ut_rsPos < samples - 1 && written < outCap) {
        double p = ut_rsPos;
        int i = (int)floor(p);
        double f = p - (double)i;
        for (int ch = 0; ch < 2; ch++) {
            int16_t s0 = sampleAt(i, ch);
            int16_t s1 = sampleAt(i + 1, ch);
            double v = (double)s0 * (1.0 - f) + (double)s1 * f;
            if (v > 32767.0) v = 32767.0;
            if (v < -32768.0) v = -32768.0;
            out[written * 2 + ch] = (int16_t)v;
        }
        written++;
        ut_rsPos += ratio;
    }
    ut_rsPos -= samples;
    ut_rsPrev[0] = (channels >= 2) ? pcm[(samples - 1) * 2] : pcm[samples - 1];
    ut_rsPrev[1] = (channels >= 2) ? pcm[(samples - 1) * 2 + 1] : pcm[samples - 1];
    return written;
}

// ---- hilo decodificador ----
static void* UT_DecoderThread(void* /*arg*/) {
    uint8_t inbuf[16384];
    int16_t outbuf[8192 * 2];  // hasta 8192 muestras por canal (peor caso: 8000 Hz -> 6351)
    mp3dec_t dec;
    int consecutiveFails = 0;
    while (!ut_stopThread) {
        if (ut_decPaused) { struct timespec ts = {0, 20 * 1000 * 1000}; nanosleep(&ts, nullptr); continue; }
        std::string path = UT_NextPath();
        if (path.empty()) { ut_wantStop = true; break; }
        FILE* f = fopen(path.c_str(), "rb");
        if (!f) { if (++consecutiveFails >= (int)ut_tracks.size()) { ut_wantStop = true; break; } continue; }
        snprintf(ut_curTrack, sizeof ut_curTrack, "%s", UT_Basename(path.c_str()));
        mp3dec_init(&dec);
        UT_ResampleReset();
        int inLen = 0, inPos = 0;
        bool trackOk = false, trackDone = false;
        while (!ut_stopThread && !trackDone) {
            if (ut_decPaused) { struct timespec ts = {0, 20 * 1000 * 1000}; nanosleep(&ts, nullptr); continue; }
            // Asegurar datos: si queda poco, compactar lo que queda y rellenar.
            // (Un frame puede quedar partido entre dos lecturas; si se corta la
            // pista ahi, la cancion se truncaria al primer cambio de buffer.)
            if (inLen - inPos < 4096) {
                if (inPos > 0) { memmove(inbuf, inbuf + inPos, inLen - inPos); inLen -= inPos; inPos = 0; }
                int r = (int)fread(inbuf + inLen, 1, sizeof inbuf - inLen, f);
                if (r > 0) inLen += r;
                else if (inLen == 0) { trackDone = true; break; }  // EOF y sin datos
            }
            if (inPos >= inLen) { trackDone = true; break; }  // sin datos (EOF)
            mp3d_sample_t pcm[MINIMP3_MAX_SAMPLES_PER_FRAME];
            mp3dec_frame_info_t info;
            int samples = mp3dec_decode_frame(&dec, inbuf + inPos, inLen - inPos, pcm, &info);
            inPos += info.frame_bytes;
            if (samples <= 0) {
                if (info.frame_bytes <= 0) {
                    // Sin frame completo: o es el final del archivo, o basura sin
                    // sincronia. Con el buffer lleno, avanzar 1 byte para no atascarse.
                    if (inLen - inPos >= 4096) { inPos++; continue; }
                    trackDone = true; break;
                }
                continue;  // frame invalido saltado por minimp3
            }
            trackOk = true;
            int outSamples = UT_ResampleFrame(pcm, samples, info.channels, info.hz, outbuf, 8192);
            if (outSamples > 0) {
                if (!ut_ring.write((const uint8_t*)outbuf, (size_t)outSamples * 2 * 2)) break;  // anillo cerrado
            }
        }
        fclose(f);
        if (ut_stopThread) break;
        if (!trackOk) {
            if (++consecutiveFails >= (int)ut_tracks.size()) { ut_wantStop = true; break; }
        } else {
            consecutiveFails = 0;
            Log("UT: pista terminada: %s", ut_curTrack);
        }
    }
    return nullptr;
}

// ---- control del reproductor ----
static void UT_PauseDevice(bool pause) {
    if (!ut_player || !ut_play) return;
    if (pause && !ut_pausedDev) {
        ut_decPaused = true;
        (*ut_play)->SetPlayState(ut_play, SL_PLAYSTATE_PAUSED);
        ut_pausedDev = true;
        Log("UT: pausa (juego en pausa)");
    } else if (!pause && ut_pausedDev) {
        (*ut_play)->SetPlayState(ut_play, SL_PLAYSTATE_PLAYING);
        ut_decPaused = false;
        ut_pausedDev = false;
        Log("UT: reanudado");
    }
}

static bool UT_Start() {
    if (ut_active) return true;
    SoundInit();
    if (!g_slEngine || !g_slEngineItf || !g_slMix) { Log("UT: sin motor OpenSL: no hay reproductor"); return false; }
    if (!UT_ScanMusic()) { Log("UT: sin musica MP3 en las carpetas (se revisaron las 5)"); return false; }
    if (ut_random) UT_ShuffleOrder(-1);
    ut_orderPos = 0;
    ut_underruns = 0;
    ut_wantStop = false;
    ut_stopThread = false;
    ut_decPaused = false;
    ut_pausedDev = false;
    ut_startedAt = std::chrono::steady_clock::now();
    ut_diagDone = false;
    if (!ut_ring.init(UT_RING_BYTES)) { Log("UT: sin memoria para el anillo"); return false; }
    ut_threadRunning = false;
    if (pthread_create(&ut_thread, nullptr, UT_DecoderThread, nullptr) != 0) {
        Log("UT: no se pudo crear el hilo decodificador");
        ut_ring.free();
        return false;
    }
    ut_threadRunning = true;
    // Pre-llenado: esperar ~0.5 s de audio en el anillo antes de abrir el dispositivo.
    for (int i = 0; i < 300 && ut_ring.avail() < UT_PREFILL_BYTES; i++) {
        if (ut_stopThread || ut_wantStop) break;
        struct timespec ts = {0, 10 * 1000 * 1000};
        nanosleep(&ts, nullptr);
    }
    if (ut_wantStop || ut_ring.avail() == 0) {
        Log("UT: no se pudo decodificar ninguna pista");
        ut_stopThread = true;
        ut_ring.close();
        if (ut_threadRunning) { pthread_join(ut_thread, nullptr); ut_threadRunning = false; }
        ut_ring.free();
        return false;
    }
    // Crear el reproductor OpenSL (44100 Hz estereo, 4 buffers x 20 ms).
    SLDataLocator_AndroidSimpleBufferQueue locBuf = {SL_DATALOCATOR_ANDROIDSIMPLEBUFFERQUEUE, (SLuint32)UT_NUM_BUFS};
    SLDataFormat_PCM fmt = {SL_DATAFORMAT_PCM, (SLuint32)UT_OUT_CH, SL_SAMPLINGRATE_44_1,
        SL_PCMSAMPLEFORMAT_FIXED_16, SL_PCMSAMPLEFORMAT_FIXED_16,
        (SLuint32)(SL_SPEAKER_FRONT_LEFT | SL_SPEAKER_FRONT_RIGHT), SL_BYTEORDER_LITTLEENDIAN};
    SLDataSource src = {&locBuf, &fmt};
    SLDataLocator_OutputMix locMix = {SL_DATALOCATOR_OUTPUTMIX, g_slMix};
    SLDataSink sink = {&locMix, nullptr};
    const SLInterfaceID ids[1] = {SL_IID_BUFFERQUEUE};
    const SLboolean req[1] = {SL_BOOLEAN_TRUE};
    if ((*g_slEngineItf)->CreateAudioPlayer(g_slEngineItf, &ut_player, &src, &sink, 1, ids, req) != SL_RESULT_SUCCESS || !ut_player) {
        Log("UT: no se pudo crear el reproductor OpenSL");
        ut_stopThread = true; ut_ring.close();
        if (ut_threadRunning) { pthread_join(ut_thread, nullptr); ut_threadRunning = false; }
        ut_ring.free();
        return false;
    }
    if ((*ut_player)->Realize(ut_player, SL_BOOLEAN_FALSE) != SL_RESULT_SUCCESS) {
        Log("UT: no se pudo realizar el reproductor OpenSL");
        (*ut_player)->Destroy(ut_player); ut_player = nullptr;
        ut_stopThread = true; ut_ring.close();
        if (ut_threadRunning) { pthread_join(ut_thread, nullptr); ut_threadRunning = false; }
        ut_ring.free();
        return false;
    }
    if ((*ut_player)->GetInterface(ut_player, SL_IID_PLAY, &ut_play) != SL_RESULT_SUCCESS ||
        (*ut_player)->GetInterface(ut_player, SL_IID_BUFFERQUEUE, &ut_queue) != SL_RESULT_SUCCESS) {
        Log("UT: no se pudo obtener PLAY/BUFFERQUEUE");
        (*ut_player)->Destroy(ut_player); ut_player = nullptr; ut_play = nullptr; ut_queue = nullptr;
        ut_stopThread = true; ut_ring.close();
        if (ut_threadRunning) { pthread_join(ut_thread, nullptr); ut_threadRunning = false; }
        ut_ring.free();
        return false;
    }
    (*ut_queue)->RegisterCallback(ut_queue, UT_BufferCallback, nullptr);
    ut_bufIdx = 0;
    for (int i = 0; i < UT_NUM_BUFS; i++) {
        size_t got = ut_ring.readNonblock((uint8_t*)ut_buf[i], UT_BUF_BYTES);
        if (got < UT_BUF_BYTES) memset((uint8_t*)ut_buf[i] + got, 0, UT_BUF_BYTES - got);
        (*ut_queue)->Enqueue(ut_queue, ut_buf[i], UT_BUF_BYTES);
    }
    (*ut_play)->SetPlayState(ut_play, SL_PLAYSTATE_PLAYING);
    ut_active = true;
    ut_lastStatus = std::chrono::steady_clock::now();
    Log("UT: reproduciendo User Tracks (%d pistas, aleatorio=%d, anillo %.0f KB, %d buffers x 20 ms)", (int)ut_tracks.size(), ut_random ? 1 : 0, (double)UT_RING_BYTES / 1024.0, UT_NUM_BUFS);
    return true;
}

static void UT_Stop(const char* reason) {
    if (!ut_active && !ut_threadRunning && !ut_player) return;
    ut_stopThread = true;
    ut_ring.close();
    if (ut_threadRunning) { pthread_join(ut_thread, nullptr); ut_threadRunning = false; }
    if (ut_player) {
        if (ut_play) (*ut_play)->SetPlayState(ut_play, SL_PLAYSTATE_STOPPED);
        pthread_mutex_lock(&ut_cbMutex);
        if (ut_queue) (*ut_queue)->Clear(ut_queue);
        (*ut_player)->Destroy(ut_player);
        ut_player = nullptr; ut_play = nullptr; ut_queue = nullptr;
        pthread_mutex_unlock(&ut_cbMutex);
    }
    ut_ring.free();
    ut_active = false;
    ut_pausedDev = false;
    ut_decPaused = false;
    Log("UT: parado (%s; underruns=%d)", reason ? reason : "?", ut_underruns);
}

// Llamado cada frame desde Swap_hook (corre siempre, aunque DrawFrame retorne antes).
static void UT_Frame() {
    if (!ut_active) return;
    if (ut_wantStop) { UT_Stop("sin pistas decodificables"); return; }
    if (!g_inVehicle) { UT_Stop("salio del vehiculo"); return; }
    bool paused = g_paused || (ut_loadingActive && *ut_loadingActive);
    UT_PauseDevice(paused);
    auto now = std::chrono::steady_clock::now();
    if (!ut_diagDone && now - ut_startedAt > std::chrono::seconds(3)) {
        ut_diagDone = true;
        int va = (g_getStationAudio && g_audioEngine) ? (int)g_getStationAudio(g_audioEngine) : -1;
        int vt = (g_getStationTrack && g_trackManager) ? (int)g_getStationTrack(g_trackManager) : -1;
        Log("UT: diagnostico: nativa audio=%d track=%d (rueda=12)", va, vt);
    }
    if (now - ut_lastStatus > std::chrono::seconds(60)) {
        ut_lastStatus = now;
        Log("UT: estado: pista=%s underruns=%d anillo=%d KB", ut_curTrack, ut_underruns, (int)(ut_ring.avail() / 1024));
    }
}

// ---- hook de StartRadio ----
static void UT_StartRadio_hook(void* trackMgr, signed char station, signed char a2, float a3, unsigned char a4) {
    if (!ut_origStartRadio) return;
    if (station == 12) {
        if (UT_VehicleBlocked()) {
            Log("UT: vehiculo sin radio (modelo %d): 12 -> Radio Off", UT_BlockedModel());
            g_current = 13;
            ut_origStartRadio(trackMgr, 13, a2, a3, a4);
            return;
        }
        UT_Start();
        g_current = 12;
        // v1: la radio nativa NUNCA cambia a la 12 (el diagnostico lo probo:
        // nativa audio=10/11/13 con la rueda en 12), asi que se queda sonando
        // la emisora anterior por debajo de las canciones. Se le pide Radio
        // Off (13) a la nativa: silencio, y la musica la pone el reproductor.
        Log("UT: 12 -> nativa 13 (Radio Off; la musica la pone el reproductor)");
        ut_origStartRadio(trackMgr, 13, a2, a3, a4);
        return;
    }
    if (ut_active) UT_Stop("cambio de estacion");
    g_current = station;
    ut_origStartRadio(trackMgr, station, a2, a3, a4);
}

// ---- parches de la estacion 12 en el juego (los mismos de SAUserTracks) ----
// Sin ellos, el motor de radio del juego trata la 12 como fuera de rango y
// NO cambia a ella: se queda sonando la estacion anterior por debajo de
// nuestras canciones (doble audio). Los parches quitan ese trato especial
// (comparan contra 0xff en vez de 12) para que la 12 sea una estacion normal
// y su audio nativo sea silencio (en Android no hay User Tracks nativo).
// Se comprueba el valor actual antes de escribir: si no coincide, no se toca.
static uintptr_t UT_LibBase(const char* libName) {
    struct Ctx { const char* lib; uintptr_t base; } c = {libName, 0};
    dl_iterate_phdr([](struct dl_phdr_info* i, size_t, void* d) -> int {
        Ctx* x = (Ctx*)d;
        if (i->dlpi_name && strstr(i->dlpi_name, x->lib)) { x->base = i->dlpi_addr; return 1; }
        return 0; }, &c);
    return c.base;
}
static bool UT_Patch32(uintptr_t addr, uint32_t expected, uint32_t replacement, const char* name) {
    uint32_t cur = *(volatile uint32_t*)addr;
    if (cur == replacement) { Log("UT: parche %s: ya aplicado", name); return true; }
    if (cur != expected) { Log("UT: parche %s: valor inesperado 0x%08X (esperaba 0x%08X): no se aplica", name, cur, expected); return false; }
    long pg = sysconf(_SC_PAGESIZE);
    uintptr_t page = addr & ~((uintptr_t)pg - 1);
    if (mprotect((void*)page, (size_t)pg, PROT_READ | PROT_WRITE | PROT_EXEC) != 0) { Log("UT: parche %s: mprotect fallo", name); return false; }
    *(volatile uint32_t*)addr = replacement;
    __builtin___clear_cache((char*)addr, (char*)addr + 4);
    mprotect((void*)page, (size_t)pg, PROT_READ | PROT_EXEC);
    uint32_t after = *(volatile uint32_t*)addr;
    if (after != replacement) { Log("UT: parche %s: no quedo (0x%08X)", name, after); return false; }
    Log("UT: parche %s aplicado (0x%08X -> 0x%08X)", name, expected, replacement);
    return true;
}
static void UT_ApplyStation12Patches() {
    uintptr_t base = UT_LibBase("libGTASA.so");
    if (!base) { Log("UT: parches 12: sin base de libGTASA"); return; }
    UT_Patch32(base + 0x47c9f8, 0x7100313F, 0x7103FD1F, "menu 12");
    UT_Patch32(base + 0x47f3ac, 0x7100311F, 0x7103FC1F, "salto 12");
}

// Instala el hook de StartRadio (se llama en Init, con los simbolos ya resueltos).
// Tambien intenta resolver CLoadingScreen::m_bActive para pausar en la carga.
static void UT_InstallHooks() {
    UT_ApplyStation12Patches();  // v1: que la 12 sea estacion normal en el juego
    if (!g_startTrack) { Log("UT: sin StartRadio: no hay reproductor de User Tracks"); return; }
    aml->Hook((void*)g_startTrack, (void*)UT_StartRadio_hook, (void**)&ut_origStartRadio);
    if (ut_origStartRadio) {
        ut_hookInstalled = true;
        Log("UT: hook StartRadio instalado (%p)", (void*)g_startTrack);
    } else {
        Log("UT: !! no se pudo instalar el hook de StartRadio");
    }
    void* h = aml->GetLibHandle("libGTASA.so");
    if (h) {
        ut_loadingActive = (bool*)aml->GetSym(h, "_ZN14CLoadingScreen9m_bActiveE");
        Log("UT: CLoadingScreen::m_bActive %s", ut_loadingActive ? "ok" : "no resolvio (sin pausa en carga)");
        uintptr_t dn = aml->GetSym(h, "_ZN20CAERadioTrackManager23DisplayRadioStationNameEv");  // GetSym devuelve uintptr_t, no void*
        if (dn) {
            aml->Hook((void*)dn, (void*)UT_DisplayName_hook, (void**)&ut_origDisplayName);
            Log("UT: hook DisplayRadioStationName %s", ut_origDisplayName ? "ok" : "no se pudo instalar (se vera Radio Off en la 12)");
        } else {
            Log("UT: DisplayRadioStationName no resolvio (se vera Radio Off en la 12)");
        }
        // v1: que DisplayRadioStationName vea la 12 (solo durante su llamada).
        // Los getters se enganchan igual que StartRadio; fuera de esa llamada
        // devuelven la nativa real, asi que el resto del juego no se entera.
        if (g_getStationTrack) { aml->Hook((void*)g_getStationTrack, (void*)UT_GetStationTrack_hook, (void**)&ut_origGetStationTrack); Log("UT: hook GetStationTrack %s", ut_origGetStationTrack ? "ok" : "no se pudo instalar"); }
        if (g_getStationAudio) { aml->Hook((void*)g_getStationAudio, (void*)UT_GetStationAudio_hook, (void**)&ut_origGetStationAudio); Log("UT: hook GetStationAudio %s", ut_origGetStationAudio ? "ok" : "no se pudo instalar"); }
    }
}
