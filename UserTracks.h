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
//   gana). Formatos: MP3 (minimp3), WAV (dr_wav), OGG Vorbis (stb_vorbis),
//   FLAC (dr_flac) y M4A/AAC (BASS, solo decodificar: usa libbass.so si esta
//   BassMod instalado; si no hay BASS, los M4A se saltan sin romper nada).
// - Playlist alfabetica por nombre de archivo; aleatoria (Fisher-Yates) por
//   defecto; infinita (al terminar rebaraja sin repetir la ultima).
// - Siguiente/anterior: UT_NextTrack()/UT_PrevTrack() (la rueda los llama
//   con las flechas del centro cuando suena la 12). El nombre de la pista
//   actual esta en ut_curTrack / UT_TrackDisplayName().
// - Decodificador en un hilo aparte; anillo de 512 KB; pre-llenado
//   de ~0.5 s antes de abrir el dispositivo; OpenSL con 4 buffers x 20 ms
//   (80 ms) en vez de 2 x 10 ms (20 ms). Al cambiar de pista NO se vacia el
//   anillo: todo se remuestrea a 44100 Hz estereo, asi el dispositivo se crea
//   una sola vez y nunca cambia de formato. Si la pista ya es 44100 Hz
//   estereo, el PCM va directo al anillo sin remuestrear (via rapida).
// - Pausa con g_paused (menu / m_UserPause / m_CodePause) y con
//   CLoadingScreen::m_bActive si resuelve; al salir del vehiculo se para.
// - Comparte el motor OpenSL de los clics (un solo motor en el proceso).
#pragma once

#define MINIMP3_IMPLEMENTATION
#include "minimp3.h"
#define DR_WAV_IMPLEMENTATION
#include "dr_wav.h"
#define DR_FLAC_IMPLEMENTATION
#include "dr_flac.h"
#include "stb_vorbis.c"  // OGG Vorbis (dominio publico; se incluye el .c: una sola TU)
// stb_vorbis.c define L, C y R como macros (canales de reproduccion) y NO
// los quita al final: sin esto, 'float R = WheelR(H);' en VHudRadio.cpp se
// expande a 'float (PLAYBACK_RIGHT | PLAYBACK_MONO) = ...' y no compila.
// Ya estan usados dentro del .c (el #include ya se expandio), asi que se
// pueden quitar aqui sin afectar al decodificador.
#undef L
#undef C
#undef R

#include <dirent.h>
#include <dlfcn.h>
#include <link.h>
#include <pthread.h>
#include <time.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/syscall.h>  // v1.9.3 (Claude): SYS_gettid para la prioridad del hilo decodificador
#include <sys/resource.h>  // v1.9.3 (Claude): setpriority
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

// ---- carpetas de musica (GTAVRadioMusic primero, luego las 5 de SAUserTracks; gana la primera con audio) ----
// v1.4: la carpeta GTAVRadioMusic es la propia del mod (se crea sola); la
// carpeta "music" vieja sigue funcionando como respaldo.
static const char* UT_MUSIC_DIRS[10] = {
    "/sdcard/Android_unprotected/data/com.rockstargames.gtasa/files/GTAVRadioMusic",
    "/storage/emulated/0/Android_unprotected/data/com.rockstargames.gtasa/files/GTAVRadioMusic",
    "/sdcard/Android/data/com.rockstargames.gtasa/files/GTAVRadioMusic",
    "/storage/emulated/0/Android/data/com.rockstargames.gtasa/files/GTAVRadioMusic",
    "/storage/emulated/0/GTAVRadioMusic",
    "/sdcard/Android_unprotected/data/com.rockstargames.gtasa/files/music",
    "/storage/emulated/0/Android_unprotected/data/com.rockstargames.gtasa/files/music",
    "/sdcard/Android/data/com.rockstargames.gtasa/files/music",
    "/storage/emulated/0/Android/data/com.rockstargames.gtasa/files/music",
    "/storage/emulated/0/GTASA",
};
// Crea la carpeta GTAVRadioMusic (la primera que se pueda) para que el
// usuario tenga donde poner la musica sin crearla a mano. Solo lo intenta
// una vez por sesion; si ya existe alguna, no hace nada.
static void UT_EnsureRadioMusicFolder() {
    static bool done = false;
    if (done) return;
    done = true;
    for (int i = 0; i < 5; i++) {
        struct stat st;
        if (stat(UT_MUSIC_DIRS[i], &st) == 0 && S_ISDIR(st.st_mode)) return;  // ya existe una
        if (mkdir(UT_MUSIC_DIRS[i], 0755) == 0) { Log("UT: carpeta GTAVRadioMusic creada: %s", UT_MUSIC_DIRS[i]); return; }
    }
}

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
static bool ut_pausedByExit = false;         // v1.7: pausado al bajar del auto (se reanuda al subir, sin reiniciar ni cortar)
static std::chrono::steady_clock::time_point ut_resumeAt{};  // v1.8: momento de la reanudacion (gracia de 2 s contra el Radio Off del juego)
static bool ut_userStationCmd = false;        // v1.8: el cambio lo pidio el usuario (rueda/boton), no el juego al entrar
static std::chrono::steady_clock::time_point ut_lastFrameAt{};  // v1.8: ultimo cuadro del juego (si se va a fondo, la musica se pausa)
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
// HUD, como "K-JAH WEST"). En la 12 mostraria "Radio Off" (la nativa es 13).
// El display lee el campo de estacion DIRECTO del manager (el getter esta
// inlined y el hook del getter no le afecta): el campo esta en DOS offsets,
// 0xAD y 0xE9, hallados por los volcados de 256 bytes (los dos valen lo mismo
// que la estacion en los 14 volcados del log VHudRadioLog_2_ng22.txt).
// Mientras suena el reproductor se pone 12 en los dos solo durante la llamada
// para que muestre "User Tracks", y se restaura despues.
typedef void (*DisplayNameFn)(void*);
static DisplayNameFn ut_origDisplayName = nullptr;
static void UT_DisplayName_hook(void* trackMgr) {
    if (ut_active && ut_origDisplayName && trackMgr) {
        unsigned char* m = (unsigned char*)trackMgr;
        unsigned char save1 = m[0xAD], save2 = m[0xE9];
        m[0xAD] = 12; m[0xE9] = 12;
        ut_origDisplayName(trackMgr);
        m[0xAD] = save1; m[0xE9] = save2;
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
// Formatos de audio (por extension; minusculas y mayusculas).
enum UTAudioFmt { UT_FMT_UNKNOWN = 0, UT_FMT_MP3, UT_FMT_WAV, UT_FMT_OGG, UT_FMT_FLAC, UT_FMT_M4A };
static UTAudioFmt UT_AudioExt(const char* name) {
    const char* dot = strrchr(name, '.');
    if (!dot) return UT_FMT_UNKNOWN;
    if (strcmp(dot, ".mp3") == 0 || strcmp(dot, ".MP3") == 0) return UT_FMT_MP3;
    if (strcmp(dot, ".wav") == 0 || strcmp(dot, ".WAV") == 0) return UT_FMT_WAV;
    if (strcmp(dot, ".ogg") == 0 || strcmp(dot, ".OGG") == 0) return UT_FMT_OGG;
    if (strcmp(dot, ".flac") == 0 || strcmp(dot, ".FLAC") == 0) return UT_FMT_FLAC;
    if (strcmp(dot, ".m4a") == 0 || strcmp(dot, ".M4A") == 0) return UT_FMT_M4A;
    return UT_FMT_UNKNOWN;
}
static bool UT_HasAudioExt(const char* name) { return UT_AudioExt(name) != UT_FMT_UNKNOWN; }

// Escanea las 10 carpetas en orden; la primera con al menos un archivo de
// audio gana. Rellena ut_tracks (alfabetico por basename) y el orden de
// reproduccion. Devuelve true si hay al menos una pista.
static bool UT_ScanMusic() {
    UT_EnsureRadioMusicFolder();  // v1.4: crear GTAVRadioMusic si no existe
    ut_tracks.clear();
    ut_order.clear();
    ut_orderPos = 0;
    char dirUsed[512] = "";
    for (int d = 0; d < 10; d++) {
        DIR* dir = opendir(UT_MUSIC_DIRS[d]);
        if (!dir) continue;
        std::vector<std::string> found;
        struct dirent* ent;
        while ((ent = readdir(dir)) != nullptr) {
            if (ent->d_name[0] == '.') continue;
            if (!UT_HasAudioExt(ent->d_name)) continue;
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
    int nMp3 = 0, nWav = 0, nOgg = 0, nFlac = 0, nM4a = 0;
    for (size_t i = 0; i < ut_tracks.size(); i++) {
        switch (UT_AudioExt(UT_Basename(ut_tracks[i].c_str()))) {
            case UT_FMT_MP3: nMp3++; break;
            case UT_FMT_WAV: nWav++; break;
            case UT_FMT_OGG: nOgg++; break;
            case UT_FMT_FLAC: nFlac++; break;
            case UT_FMT_M4A: nM4a++; break;
            default: break;
        }
    }
    Log("UT: carpeta de musica: %s (%d pistas: mp3=%d wav=%d ogg=%d flac=%d m4a=%d)", dirUsed, (int)ut_tracks.size(), nMp3, nWav, nOgg, nFlac, nM4a);
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

// Nombre de la pista actual para mostrar (basename sin extension).
// Buffer estatico; la UI lo usa solo para dibujar en el momento.
static char ut_dispName[256];
static const char* UT_TrackDisplayName() {
    char tmp[256];
    // Copia defensiva: el hilo decodificador escribe ut_curTrack al cambiar
    // de pista; si la lectura se cruza, el nombre sale mezclado un cuadro,
    // pero nunca se lee fuera del buffer.
    strncpy(tmp, ut_curTrack, sizeof tmp - 1);
    tmp[sizeof tmp - 1] = '\0';
    const char* base = UT_Basename(tmp);
    size_t n = strlen(base);
    const char* dot = strrchr(base, '.');
    if (dot) n = (size_t)(dot - base);
    if (n >= sizeof ut_dispName) n = sizeof ut_dispName - 1;
    memcpy(ut_dispName, base, n);
    ut_dispName[n] = '\0';
    return ut_dispName;
}
// Nombre de una pista de la lista (indice en ut_tracks, alfabetico), sin
// extension. Para la lista de canciones (v1.4).
static char ut_nameFor[256];
static const char* UT_TrackNameFor(int idx) {
    if (idx < 0 || idx >= (int)ut_tracks.size()) return "";
    const char* base = UT_Basename(ut_tracks[idx].c_str());
    size_t n = strlen(base);
    const char* dot = strrchr(base, '.');
    if (dot) n = (size_t)(dot - base);
    if (n >= sizeof ut_nameFor) n = sizeof ut_nameFor - 1;
    memcpy(ut_nameFor, base, n);
    ut_nameFor[n] = '\0';
    return ut_nameFor;
}

// Siguiente/anterior (la rueda los llama con las flechas del centro cuando
// suena la 12). Contador: +1 por cada "siguiente", -1 por cada "anterior";
// el hilo decodificador lo consume. Solo vale si el reproductor esta activo.
static volatile int ut_skipReq = 0;
static void UT_NextTrack() { if (ut_active) ut_skipReq++; }
static void UT_PrevTrack() { if (ut_active) ut_skipReq--; }
// Saltar a una pista concreta (indice en ut_tracks, alfabetico). La pone
// como la siguiente en sonar. Devuelve false si no se pudo.
static bool UT_JumpToTrack(int trackIdx) {
    if (!ut_active || trackIdx < 0 || trackIdx >= (int)ut_tracks.size() || ut_order.empty()) return false;
    int p = -1;
    for (size_t i = 0; i < ut_order.size(); i++) if (ut_order[i] == trackIdx) { p = (int)i; break; }
    if (p < 0) return false;
    ut_orderPos = p;  // UT_NextPath toma esta en el siguiente ciclo
    ut_skipReq++;
    return true;
}
// Indice (en ut_tracks) de la pista que suena ahora, o -1.
static int UT_CurrentTrackIdx() {
    if (ut_order.empty()) return -1;
    int p = ut_orderPos - 1;
    if (p < 0) p += (int)ut_order.size();
    if (p < 0 || p >= (int)ut_order.size()) return -1;
    return ut_order[p];
}

// ---- callback de OpenSL (hilo interno de OpenSL) ----
// slBufferQueueCallback: void (SLBufferQueueItf, void*). Mismo uso que el
// ejemplo native-audio del NDK (SL_IID_BUFFERQUEUE + RegisterCallback).
// v1.8: si el juego lleva >3 s sin dibujar un cuadro (se fue a la pantalla
// de inicio / fondo), se pausa el reproductor. Al volver, UT_Frame lo
// reanuda. Sin esto la musica seguia sonando en la pantalla de inicio.
static void UT_PauseDevice(bool pause);  // v1.8: declaracion adelantada (se define mas abajo)
static void UT_BufferCallback(SLBufferQueueItf bq, void* /*ctx*/) {
    if (ut_active && ut_lastFrameAt.time_since_epoch().count() > 0 &&
        std::chrono::steady_clock::now() - ut_lastFrameAt > std::chrono::seconds(3)) {
        UT_PauseDevice(true);
    }
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

// ---- escritura de PCM al anillo (via rapida + remuestreo) ----
// Si el PCM ya es 44100 Hz estereo, va directo al anillo (sin el coste del
// remuestreador, que es el caso de casi todos los MP3). Si no, por
// UT_ResampleFrame. false solo si el anillo se cerro.
static bool UT_WritePCM(const int16_t* pcm, int samples, int channels, int rate) {
    if (samples <= 0) return true;
    if (rate == UT_OUT_RATE && channels == UT_OUT_CH) {
        return ut_ring.write((const uint8_t*)pcm, (size_t)samples * 2 * 2);
    }
    int16_t outbuf[8192 * 2];
    int outSamples = UT_ResampleFrame(pcm, samples, channels, rate, outbuf, 8192);
    if (outSamples > 0) return ut_ring.write((const uint8_t*)outbuf, (size_t)outSamples * 2 * 2);
    return true;
}

// ---- decodificadores por formato ----
// Cada uno devuelve 0 = pista completa, 1 = saltada (ut_skipReq), -1 = fallo.
// Leen en trozos de 1024 frames: a 8000 Hz el remuestreo da 5645 muestras,
// que caben en el outbuf de UT_ResampleFrame (8192). Solo mono/estereo.
static int UT_DecodeMP3(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) return -1;
    uint8_t inbuf[16384];
    mp3dec_t dec;
    mp3dec_init(&dec);
    UT_ResampleReset();
    int inLen = 0, inPos = 0;
    bool trackOk = false;
    int rc = 0;
    while (!ut_stopThread) {
        if (ut_skipReq != 0) { rc = 1; break; }
        if (ut_decPaused) { struct timespec ts = {0, 20 * 1000 * 1000}; nanosleep(&ts, nullptr); continue; }
        // Asegurar datos: si queda poco, compactar lo que queda y rellenar.
        if (inLen - inPos < 4096) {
            if (inPos > 0) { memmove(inbuf, inbuf + inPos, inLen - inPos); inLen -= inPos; inPos = 0; }
            int r = (int)fread(inbuf + inLen, 1, sizeof inbuf - inLen, f);
            if (r > 0) inLen += r;
            else if (inLen == 0) { rc = 0; break; }  // EOF y sin datos
        }
        if (inPos >= inLen) { rc = 0; break; }  // sin datos (EOF)
        mp3d_sample_t pcm[MINIMP3_MAX_SAMPLES_PER_FRAME];
        mp3dec_frame_info_t info;
        int samples = mp3dec_decode_frame(&dec, inbuf + inPos, inLen - inPos, pcm, &info);
        inPos += info.frame_bytes;
        if (samples <= 0) {
            if (info.frame_bytes <= 0) {
                // Sin frame completo: o es el final del archivo, o basura sin
                // sincronia. Con el buffer lleno, avanzar 1 byte para no atascarse.
                if (inLen - inPos >= 4096) { inPos++; continue; }
                rc = 0; break;
            }
            continue;  // frame invalido saltado por minimp3
        }
        if (info.channels > 2) { rc = -1; break; }
        trackOk = true;
        if (!UT_WritePCM(pcm, samples, info.channels, info.hz)) { rc = -1; break; }
    }
    fclose(f);
    if (rc == 1) return 1;
    return trackOk ? 0 : -1;
}

static int UT_DecodeWAV(const char* path) {
    drwav wav;
    if (!drwav_init_file(&wav, path, nullptr)) return -1;
    if (wav.channels > 2) { drwav_uninit(&wav); Log("UT: WAV con %d canales (solo mono/estereo): %s", (int)wav.channels, UT_Basename(path)); return -1; }
    UT_ResampleReset();
    int16_t buf[1024 * 2];
    int rc = 0;
    while (!ut_stopThread) {
        if (ut_skipReq != 0) { rc = 1; break; }
        if (ut_decPaused) { struct timespec ts = {0, 20 * 1000 * 1000}; nanosleep(&ts, nullptr); continue; }
        drwav_uint64 frames = drwav_read_pcm_frames_s16(&wav, 1024, buf);
        if (frames == 0) break;  // EOF
        if (!UT_WritePCM(buf, (int)frames, (int)wav.channels, (int)wav.sampleRate)) { rc = -1; break; }
    }
    drwav_uninit(&wav);
    return rc;
}

static int UT_DecodeOGG(const char* path) {
    int error = 0;
    stb_vorbis* v = stb_vorbis_open_filename(path, &error, nullptr);
    if (!v) return -1;
    stb_vorbis_info info = stb_vorbis_get_info(v);
    UT_ResampleReset();
    int16_t buf[1024 * 2];
    int rc = 0;
    while (!ut_stopThread) {
        if (ut_skipReq != 0) { rc = 1; break; }
        if (ut_decPaused) { struct timespec ts = {0, 20 * 1000 * 1000}; nanosleep(&ts, nullptr); continue; }
        // Se piden 2 canales: stb_vorbis convierte (mono -> duplicado).
        int frames = stb_vorbis_get_samples_short_interleaved(v, 2, buf, 1024 * 2);
        if (frames <= 0) break;  // EOF
        if (!UT_WritePCM(buf, frames, 2, info.sample_rate)) { rc = -1; break; }
    }
    stb_vorbis_close(v);
    return rc;
}

static int UT_DecodeFLAC(const char* path) {
    drflac* f = drflac_open_file(path, nullptr);
    if (!f) return -1;
    if (f->channels > 2) { drflac_close(f); Log("UT: FLAC con %d canales (solo mono/estereo): %s", (int)f->channels, UT_Basename(path)); return -1; }
    UT_ResampleReset();
    int16_t buf[1024 * 2];
    int rc = 0;
    while (!ut_stopThread) {
        if (ut_skipReq != 0) { rc = 1; break; }
        if (ut_decPaused) { struct timespec ts = {0, 20 * 1000 * 1000}; nanosleep(&ts, nullptr); continue; }
        drflac_uint64 frames = drflac_read_pcm_frames_s16(f, 1024, buf);
        if (frames == 0) break;  // EOF
        if (!UT_WritePCM(buf, (int)frames, (int)f->channels, (int)f->sampleRate)) { rc = -1; break; }
    }
    drflac_close(f);
    return rc;
}

// ---- M4A/AAC: BASS solo para decodificar (opcional) ----
// BASS es una libreria de audio (un4seen). BassMod (net.rusjj.basslib) ya
// carga libbass.so y la inicia; aqui solo la usamos para DECODIFICAR M4A
// (BASS_STREAM_DECODE): BASS saca el PCM con el codec del sistema
// (MediaCodec, Android 5+) y nuestro OpenSL lo reproduce como los demas
// formatos. Si BassMod no esta instalado, libbass.so no existe y los M4A
// se saltan sin romper nada (MP3/WAV/OGG/FLAC no dependen de BASS).
// BASS es gratis para mods no comerciales. No hay interfaz IBASS en BassMod,
// por eso se abre libbass.so directo con dlopen/dlsym.
struct BASSChannelInfo { uint32_t freq, chans, reserved[16]; };  // solo importan freq y chans (los 2 primeros)
typedef int (*BASS_Init_t)(int, uint32_t, uint32_t, void*, const void*);
typedef uint32_t (*BASS_StreamCreateFile_t)(int, const void*, uint64_t, uint64_t, uint32_t);
typedef uint32_t (*BASS_ChannelGetData_t)(uint32_t, void*, uint32_t);
typedef int (*BASS_ChannelGetInfo_t)(uint32_t, BASSChannelInfo*);
typedef int (*BASS_StreamFree_t)(uint32_t);
typedef int (*BASS_ErrorGetCode_t)();
static void* ut_bassLib = nullptr;
static BASS_Init_t ut_BASS_Init = nullptr;
static BASS_StreamCreateFile_t ut_BASS_StreamCreateFile = nullptr;
static BASS_ChannelGetData_t ut_BASS_ChannelGetData = nullptr;
static BASS_ChannelGetInfo_t ut_BASS_ChannelGetInfo = nullptr;
static BASS_StreamFree_t ut_BASS_StreamFree = nullptr;
static BASS_ErrorGetCode_t ut_BASS_ErrorGetCode = nullptr;
static bool ut_bassTried = false, ut_bassOk = false;
static bool UT_BassLoad() {
    if (ut_bassTried) return ut_bassOk;
    ut_bassTried = true;
    ut_bassLib = dlopen("libbass.so", RTLD_NOW | RTLD_LOCAL);
    if (!ut_bassLib) { Log("UT: BASS no disponible (sin libbass.so: sin M4A)"); return false; }
    ut_BASS_Init = (BASS_Init_t)dlsym(ut_bassLib, "BASS_Init");
    ut_BASS_StreamCreateFile = (BASS_StreamCreateFile_t)dlsym(ut_bassLib, "BASS_StreamCreateFile");
    ut_BASS_ChannelGetData = (BASS_ChannelGetData_t)dlsym(ut_bassLib, "BASS_ChannelGetData");
    ut_BASS_ChannelGetInfo = (BASS_ChannelGetInfo_t)dlsym(ut_bassLib, "BASS_ChannelGetInfo");
    ut_BASS_StreamFree = (BASS_StreamFree_t)dlsym(ut_bassLib, "BASS_StreamFree");
    ut_BASS_ErrorGetCode = (BASS_ErrorGetCode_t)dlsym(ut_bassLib, "BASS_ErrorGetCode");
    if (!ut_BASS_Init || !ut_BASS_StreamCreateFile || !ut_BASS_ChannelGetData || !ut_BASS_ChannelGetInfo || !ut_BASS_StreamFree || !ut_BASS_ErrorGetCode) {
        Log("UT: BASS incompleto (faltan simbolos: sin M4A)");
        return false;
    }
    // Inicializar. BassMod ya lo hizo, asi que normalmente devuelve
    // BASS_ERROR_ALREADY (14), que es exito, no fallo.
    if (!ut_BASS_Init(-1, 44100, 0, nullptr, nullptr)) {
        int e = ut_BASS_ErrorGetCode();
        if (e != 14) { Log("UT: BASS_Init fallo (error %d: sin M4A)", e); return false; }
    }
    ut_bassOk = true;
    Log("UT: BASS listo (M4A disponible)");
    return true;
}
static int UT_DecodeM4A(const char* path) {
    if (!UT_BassLoad()) return -1;
    uint32_t stream = ut_BASS_StreamCreateFile(0, path, 0, 0, 0x200000);  // BASS_STREAM_DECODE
    if (!stream) { Log("UT: BASS no pudo abrir %s (error %d)", UT_Basename(path), ut_BASS_ErrorGetCode()); return -1; }
    BASSChannelInfo info = {};
    if (!ut_BASS_ChannelGetInfo(stream, &info) || info.chans == 0 || info.chans > 2) {
        ut_BASS_StreamFree(stream);
        Log("UT: M4A con %d canales (solo mono/estereo): %s", (int)info.chans, UT_Basename(path));
        return -1;
    }
    UT_ResampleReset();
    int16_t buf[1024 * 2];
    int rc = 0;
    while (!ut_stopThread) {
        if (ut_skipReq != 0) { rc = 1; break; }
        if (ut_decPaused) { struct timespec ts = {0, 20 * 1000 * 1000}; nanosleep(&ts, nullptr); continue; }
        uint32_t got = ut_BASS_ChannelGetData(stream, buf, sizeof buf);
        if (got == 0xFFFFFFFFu) { rc = -1; break; }  // error
        if (got == 0) break;  // fin de la pista
        int frames = (int)got / (2 * (int)info.chans);
        if (frames <= 0) break;
        if (!UT_WritePCM(buf, frames, (int)info.chans, (int)info.freq)) { rc = -1; break; }
    }
    ut_BASS_StreamFree(stream);
    return rc;
}
// Decodifica una pista completa. 0 = terminada, 1 = saltada, -1 = fallo.
static int UT_DecodeTrack(const char* path) {
    snprintf(ut_curTrack, sizeof ut_curTrack, "%s", UT_Basename(path));
    switch (UT_AudioExt(path)) {
        case UT_FMT_MP3: return UT_DecodeMP3(path);
        case UT_FMT_WAV: return UT_DecodeWAV(path);
        case UT_FMT_OGG: return UT_DecodeOGG(path);
        case UT_FMT_FLAC: return UT_DecodeFLAC(path);
        case UT_FMT_M4A: return UT_DecodeM4A(path);
        default: return -1;
    }
}

// ---- hilo decodificador ----
static void* UT_DecoderThread(void* /*arg*/) {
    setpriority(PRIO_PROCESS, (id_t)syscall(SYS_gettid), -16);  // v1.9.3 (Claude): prioridad de hilo de audio (si el sistema la niega, se ignora)
    int consecutiveFails = 0;
    while (!ut_stopThread) {
        if (ut_decPaused) { struct timespec ts = {0, 20 * 1000 * 1000}; nanosleep(&ts, nullptr); continue; }
        // Saltos pendientes (la rueda los pide con UT_NextTrack/UT_PrevTrack).
        if (ut_skipReq > 0) {
            ut_skipReq--;
            ut_ring.discard();  // tirar lo que quedaba de la pista anterior
        } else if (ut_skipReq < 0) {
            ut_skipReq++;
            // Volver una pista: UT_NextPath avanza, asi que retroceder 2.
            if (!ut_order.empty()) {
                ut_orderPos -= 2;
                if (ut_orderPos < 0) ut_orderPos += (int)ut_order.size();
            }
            ut_ring.discard();
        }
        std::string path = UT_NextPath();
        if (path.empty()) { ut_wantStop = true; break; }
        int rc = UT_DecodeTrack(path.c_str());
        if (ut_stopThread) break;
        if (rc == 1) {
            Log("UT: pista saltada: %s", ut_curTrack);
            continue;
        }
        if (rc == 0) {
            consecutiveFails = 0;
            Log("UT: pista terminada: %s", ut_curTrack);
        } else {
            if (++consecutiveFails >= (int)ut_tracks.size()) { ut_wantStop = true; break; }
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
    if (!UT_ScanMusic()) { Log("UT: sin musica (mp3/wav/ogg/flac/m4a) en las carpetas (se revisaron las 10)"); return false; }
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
    ut_pausedByExit = false;  // v1.7: al parar de verdad no queda pausa pendiente
    Log("UT: parado (%s; underruns=%d)", reason ? reason : "?", ut_underruns);
}

// Llamado cada frame desde Swap_hook (corre siempre, aunque DrawFrame retorne antes).
static void UT_Frame() {
    // v1.8: marca de tiempo del ultimo cuadro (el callback de OpenSL la usa
    // para pausar si el juego se va a fondo / pantalla de inicio).
    ut_lastFrameAt = std::chrono::steady_clock::now();
    if (!ut_active) return;
    if (ut_wantStop) { UT_Stop("sin pistas decodificables"); return; }
    // v1.7: al salir del vehiculo se PAUSA (no se para ni se sigue oyendo a
    // pie): la musica se queda en silencio y se reanuda al subir, en la
    // misma emisora (12) y en el mismo punto, sin reiniciar ni cortar.
    // (La 1.6 la dejaba sonando a pie y al volver el juego ponia Radio Off.)
    if (!g_inVehicle && !ut_pausedByExit) {
        ut_pausedByExit = true;
        Log("UT: pausado al salir del vehiculo (se reanuda al subir)");
    } else if (g_inVehicle && ut_pausedByExit) {
        ut_pausedByExit = false;
        ut_resumeAt = std::chrono::steady_clock::now();  // v1.8: gracia de 2 s (el juego pide Radio Off DESPUES de esto)
        Log("UT: reanudado al subir al vehiculo");
    }
    bool paused = g_paused || (ut_loadingActive && *ut_loadingActive) || ut_pausedByExit;
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
// v1.9.3 (Claude): StopRadio (se declara aqui para usarlo tambien en el hook de StartRadio)
// StopRadio: _ZN20CAERadioTrackManager9StopRadioEP21tVehicleAudioSettingsh
// (this, tVehicleAudioSettings*, unsigned char); SAUserTracks lo llama (0, 0).
typedef void (*StopRadioTrackFn)(void*, void*, unsigned char);
static StopRadioTrackFn ut_stopRadio = nullptr;
static void UT_StartRadio_hook(void* trackMgr, signed char station, signed char a2, float a3, unsigned char a4) {
    if (!ut_origStartRadio) return;
    if (station == 12) {
        if (UT_VehicleBlocked()) {
            Log("UT: vehiculo sin radio (modelo %d): 12 -> Radio Off", UT_BlockedModel());
            g_current = 13;
            ut_origStartRadio(trackMgr, 13, a2, a3, a4);
            return;
        }
        // v1.7: si ya suena (al volver a subir al auto el juego vuelve a
        // pedir la 12), NO se reinicia: UT_Start es no-op y se reanuda.
        // v1.8: se marca la reanudacion (gracia de 2 s contra el Radio Off).
        if (ut_active && ut_pausedByExit) { ut_pausedByExit = false; ut_resumeAt = std::chrono::steady_clock::now(); Log("UT: reanudado al subir al vehiculo (hook 12)"); }
        bool utWasActive = ut_active;
        UT_Start();
        g_current = 12;
        // v1.9.3 (Claude): si se viene de otra emisora (K-Rose, etc.), cortarla con
        // StopRadio ANTES de poner la nativa en 13, igual que ya hace el hook
        // de RetuneRadio. El log de la 1.9.2 mostro que por la rueda se pasaba
        // de la 2 a la 13 sin StopRadio (la anterior podia seguir por debajo).
        if (!utWasActive && ut_active && ut_stopRadio) {
            ut_stopRadio(trackMgr, nullptr, 0);
            Log("UT: StopRadio antes de la nativa 13 (venia de otra emisora)");
        }
        // v1.9.2: REVERTIDO a 13. La 1.9.0/1.9.1 probo que la nativa en 12 SI
        // suena: la emisora anterior por debajo (doble audio) + estatica,
        // aunque el diagnostico diga "nativa 12". La 12 no es silencio para
        // el juego (no existe para el). Solo Radio Off (13) la silencia de
        // verdad (build #9). El menu de pausa marcara Radio Off: es el precio
        // de que no suene doble. La estatica 0x22 se sigue bloqueando aparte.
        Log("UT: 12 -> nativa 13 (Radio Off; la musica la pone el reproductor)");
        ut_origStartRadio(trackMgr, 13, a2, a3, a4);
        return;
    }
    // v1.7: si se pauso al bajar del auto y el juego pide otra emisora al
    // volver a subir (pone Radio Off solo), NO se para el reproductor: se
    // reanuda en la 12. A pie no se puede cambiar de emisora (no hay boton
    // ni rueda), asi que esta llamada solo puede ser del juego al entrar.
    // v1.8: el juego pide Radio Off DESPUES de que UT_Frame ya reanudo (el
    // log de la 1.7.0 lo probo: "reanudado" y luego "parado (cambio de
    // estacion)"), asi que la gracia es de 2 s tras la reanudacion, no solo
    // mientras ut_pausedByExit. Un cambio del usuario (rueda/boton) SI para
    // (ut_userStationCmd lo marca en DoStartRadio).
    if (ut_active && !ut_userStationCmd &&
        (ut_pausedByExit || std::chrono::steady_clock::now() - ut_resumeAt < std::chrono::seconds(2))) {
        ut_pausedByExit = false;
        g_current = 12;
        Log("UT: se queda en la 12 al subir al vehiculo (el juego pedia %d)", (int)station);
        ut_origStartRadio(trackMgr, 13, a2, a3, a4);  // v1.9.2: nativa a 13 (silencio), no a 12
        return;
    }
    if (ut_active) UT_Stop("cambio de estacion");
    g_current = station;
    ut_origStartRadio(trackMgr, station, a2, a3, a4);
}

// ---- hook de RetuneRadio (como SAUserTracks de Alex) ----
// El juego cambia de emisora con RetuneRadio (no con StartRadio) cuando se
// usa la radio original (sus propios controles, el menu de pausa). Sin este
// hook, la estatica del cambio se quedaba sonando junto a la musica. En la
// 12: StopRadio original (corta la emisora anterior, sin estatica) y luego
// StartRadio original con 13 (Radio Off, silencio; la 12 SI suena, v1.9.2).
// En otra emisora: se para el reproductor y se deja pasar el RetuneRadio
// original.
static RetuneFn ut_origRetuneRadio = nullptr;

// v1.9.5 (Daniel): la radio del juego tambien puede cambiar por
// CAudioEngine::RetuneRadio (no solo por CAERadioTrackManager::RetuneRadio).
// El log de la 1.9.4 mostro el hook del TrackManager instalado pero nunca
// disparo en un cambio a la 12: el doble audio por la radio del juego viene
// por esta otra via. Misma logica que el hook del TrackManager, pero `self`
// es el AudioEngine: StopRadio/StartRadio se llaman sobre g_trackManager.
// Devuelve la emisora (signed char); si en realidad devuelve void, devolver
// la emisora es inocuo (el que llama la ignora o la usa como emisora).
typedef signed char (*AudioRetuneFn)(void*, signed char);
static AudioRetuneFn ut_origAudioRetune = nullptr;
static signed char UT_AudioRetune_hook(void* self, signed char station) {
    (void)self;
    // v1.9.6: al reanudar/cargar la partida el juego puede llamar a RetuneRadio
    // antes de estar listo; en ese momento no se toca el reproductor, solo se
    // deja pasar la llamada original (evita el cierre al reanudar de la 1.9.5).
    if (ut_loadingActive && *ut_loadingActive) {
        if (ut_origAudioRetune) return ut_origAudioRetune(self, station);
        return station;
    }
    if (station == 12) {
        if (UT_VehicleBlocked()) {
            g_current = 13;
            if (ut_origStartRadio && g_trackManager) ut_origStartRadio(g_trackManager, 13, 0, 1.0f, 0);
            return 13;
        }
        if (ut_active && ut_pausedByExit) { ut_pausedByExit = false; ut_resumeAt = std::chrono::steady_clock::now(); Log("UT: reanudado al subir al vehiculo (hook AudioRetune 12)"); }
        UT_Start();
        g_current = 12;
        if (ut_stopRadio && g_trackManager) ut_stopRadio(g_trackManager, nullptr, 0);
        Log("UT: 12 por AudioEngine::RetuneRadio (StopRadio + StartRadio 13)");
        if (ut_origStartRadio && g_trackManager) ut_origStartRadio(g_trackManager, 13, 0, 1.0f, 0);
        return 12;
    }
    if (ut_active && !ut_userStationCmd &&
        (ut_pausedByExit || std::chrono::steady_clock::now() - ut_resumeAt < std::chrono::seconds(2))) {
        ut_pausedByExit = false;
        g_current = 12;
        Log("UT: se queda en la 12 al subir al vehiculo (AudioRetune pedia %d)", (int)station);
        if (ut_stopRadio && g_trackManager) ut_stopRadio(g_trackManager, nullptr, 0);
        if (ut_origStartRadio && g_trackManager) ut_origStartRadio(g_trackManager, 13, 0, 1.0f, 0);
        return 12;
    }
    if (ut_active) UT_Stop("cambio de estacion");
    g_current = station;
    if (ut_stopRadio && g_trackManager) ut_stopRadio(g_trackManager, nullptr, 0);  // v1.9.4: cortar la nativa al salir de la 12
    if (ut_origAudioRetune) return ut_origAudioRetune(self, station);
    return station;
}

// v1.9.6 (Daniel): la radio del juego tambien puede cambiar por
// CAudioEngine::StartRadio (no solo por RetuneRadio). Ninguno de los dos
// hooks de RetuneRadio disparo en el log de la 1.9.5, asi que esta es la via
// que queda. Misma logica que el hook de StartRadio del TrackManager.
// StartRadioAudioFn: void (self, signed char, signed char).
typedef void (*AudioStartFn)(void*, signed char, signed char);
static AudioStartFn ut_origAudioStart = nullptr;
static void UT_AudioStart_hook(void* self, signed char station, signed char a2) {
    // v1.9.6: al reanudar/cargar la partida el juego puede llamar a StartRadio
    // antes de estar listo; en ese momento no se toca el reproductor, solo se
    // deja pasar la llamada original (evita el cierre al reanudar de la 1.9.5).
    if (ut_loadingActive && *ut_loadingActive) {
        if (ut_origAudioStart) ut_origAudioStart(self, station, a2);
        return;
    }
    if (station == 12) {
        if (UT_VehicleBlocked()) {
            Log("UT: vehiculo sin radio (modelo %d): 12 -> Radio Off", UT_BlockedModel());
            g_current = 13;
            if (ut_origStartRadio && g_trackManager) ut_origStartRadio(g_trackManager, 13, 0, 1.0f, 0);
            return;
        }
        if (ut_active && ut_pausedByExit) { ut_pausedByExit = false; ut_resumeAt = std::chrono::steady_clock::now(); Log("UT: reanudado al subir al vehiculo (hook AudioStart 12)"); }
        bool utWasActive = ut_active;
        UT_Start();
        g_current = 12;
        if (!utWasActive && ut_active && ut_stopRadio && g_trackManager) {
            ut_stopRadio(g_trackManager, nullptr, 0);
            Log("UT: StopRadio antes de la nativa 13 (venia de otra emisora)");
        }
        Log("UT: 12 por AudioEngine::StartRadio (StopRadio + StartRadio 13)");
        if (ut_origStartRadio && g_trackManager) ut_origStartRadio(g_trackManager, 13, 0, 1.0f, 0);
        return;
    }
    if (ut_active && !ut_userStationCmd &&
        (ut_pausedByExit || std::chrono::steady_clock::now() - ut_resumeAt < std::chrono::seconds(2))) {
        ut_pausedByExit = false;
        g_current = 12;
        Log("UT: se queda en la 12 al subir al vehiculo (AudioStart pedia %d)", (int)station);
        if (ut_stopRadio && g_trackManager) ut_stopRadio(g_trackManager, nullptr, 0);
        if (ut_origStartRadio && g_trackManager) ut_origStartRadio(g_trackManager, 13, 0, 1.0f, 0);
        return;
    }
    if (ut_active) UT_Stop("cambio de estacion");
    g_current = station;
    if (ut_origAudioStart) ut_origAudioStart(self, station, a2);
}

static signed char UT_RetuneRadio_hook(void* trackMgr, signed char station) {
    if (!ut_origRetuneRadio) return 0;
    if (station == 12) {
        if (UT_VehicleBlocked()) {
            g_current = 13;
            return ut_origRetuneRadio(trackMgr, 13);
        }
        if (ut_active && ut_pausedByExit) { ut_pausedByExit = false; ut_resumeAt = std::chrono::steady_clock::now(); Log("UT: reanudado al subir al vehiculo (hook RetuneRadio 12)"); }
        UT_Start();
        g_current = 12;
        if (ut_stopRadio) ut_stopRadio(trackMgr, nullptr, 0);  // corta la anterior (sin estatica)
        Log("UT: 12 por RetuneRadio (StopRadio + StartRadio 13)");
        if (ut_origStartRadio) ut_origStartRadio(trackMgr, 13, 0, 1.0f, 0);  // v1.9.2: nativa a 13 (silencio)
        return 12;
    }
    // v1.8: gracia de 2 s tras subir al auto (el juego pide Radio Off solo).
    if (ut_active && !ut_userStationCmd &&
        (ut_pausedByExit || std::chrono::steady_clock::now() - ut_resumeAt < std::chrono::seconds(2))) {
        ut_pausedByExit = false;
        g_current = 12;
        Log("UT: se queda en la 12 al subir al vehiculo (RetuneRadio pedia %d)", (int)station);
        if (ut_stopRadio) ut_stopRadio(trackMgr, nullptr, 0);
        if (ut_origStartRadio) ut_origStartRadio(trackMgr, 13, 0, 1.0f, 0);  // v1.9.2: nativa a 13 (silencio)
        return 12;
    }
    if (ut_active) UT_Stop("cambio de estacion");
    g_current = station;
    // v1.9.4 (Daniel): al salir de la 12 por la radio del juego, cortar la
    // nativa con StopRadio antes del RetuneRadio original. El doble audio y
    // la estatica aparecen al cambiar desde la radio original (no desde la
    // rueda) y se quitan al salir/entrar del auto: la nativa quedaba sonando.
    // Igual que el StopRadio antes de la 13 en el hook de StartRadio (1.9.3).
    if (ut_stopRadio) ut_stopRadio(trackMgr, nullptr, 0);
    return ut_origRetuneRadio(trackMgr, station);
}

// ---- bloquear la estatica de cambio de emisora (como SAUserTracks) ----
// SAUserTracks bloquea el evento de audio 0x22 (estatica de sintonia) mientras
// el mixtape esta activo: "blocking native retune static (0x22) while mixtape
// active". Sin esto, la estatica suena JUNTO a la musica y se oye como doble
// audio, sobre todo al cambiar con la radio original del juego. Por eso la
// 1.9.0 sonaba doble aunque la nativa estaba en 12: NO era la emisora
// anterior, era la estatica 0x22. Se engancha ReportFrontendAudioEvent
// (_ZN12CAudioEngine24ReportFrontendAudioEventEiff) y se salta el evento
// 0x22 mientras suena el reproductor; los demas eventos pasan igual.
typedef void (*ReportAudioEventFn)(void*, int, float, float);
static ReportAudioEventFn ut_origReportAudioEvent = nullptr;
static void UT_ReportAudioEvent_hook(void* engine, int eventId, float a, float b) {
    if (eventId == 0x22 && ut_active) return;  // estatica de sintonia: no suena con User Tracks
    if (ut_origReportAudioEvent) ut_origReportAudioEvent(engine, eventId, a, b);
}

// ---- ocultar los botones del juego con la rueda abierta (v1.9) ----
// Como el Void Radio Wheel: el mundo se desenfoca y el HUD no se ve. Se
// engancha CTouchInterface::Draw y se salta cuando la rueda, el panel o la
// lista estan abiertos. El hook recibe `self` (this) y lo pasa tal cual al
// original, asi vale tanto si Draw es estatico como si es de instancia.
// Si el simbolo no resuelve, no se hace nada (sin riesgo de cierre).
typedef void (*TouchDrawFn)(void*);
static TouchDrawFn ut_origTouchDraw = nullptr;
static void UT_TouchDraw_hook(void* self) {
    if (g_open || g_settings || g_trackList) return;  // no dibujar botones del juego
    if (ut_origTouchDraw) ut_origTouchDraw(self);
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
// ---- v1.9.3 (Claude): sonda de simbolos + ocultar CHud::Draw ----
#include <elf.h>

struct SymDumpCtx { const char* lib; uintptr_t base; const ElfW(Phdr)* ph; int phnum; };
static int SymDumpCb(struct dl_phdr_info* i, size_t, void* d) {
    SymDumpCtx* x = (SymDumpCtx*)d;
    if (i->dlpi_name && strstr(i->dlpi_name, x->lib)) { x->base = i->dlpi_addr; x->ph = i->dlpi_phdr; x->phnum = i->dlpi_phnum; return 1; }
    return 0;
}
// Escribe en outPath los simbolos de .dynsym que contengan alguna palabra de
// needles. Devuelve cuantos escribio (<0 = error). Es solo lectura: no hookea
// nada. Con la lista se engancha el dibujo de los botones y se deja visible
// solo el claxon.
static int DumpSymbols(const char* lib, const char* const* needles, int nNeedles, const char* outPath) {
    SymDumpCtx c = {lib, 0, nullptr, 0};
    dl_iterate_phdr(SymDumpCb, &c);
    if (!c.ph) return -1;
    const ElfW(Dyn)* dyn = nullptr;
    for (int k = 0; k < c.phnum; k++) if (c.ph[k].p_type == PT_DYNAMIC) { dyn = (const ElfW(Dyn)*)(c.base + c.ph[k].p_vaddr); break; }
    if (!dyn) return -2;
    auto fix = [&](uintptr_t v) -> uintptr_t { return v < c.base ? c.base + v : v; };  // bionic: desplazamientos; glibc: ya reubicadas
    const ElfW(Sym)* symtab = nullptr; const char* strtab = nullptr;
    const uint32_t* gnuHash = nullptr; const uint32_t* sysvHash = nullptr;
    for (const ElfW(Dyn)* p = dyn; p->d_tag != DT_NULL; p++) {
        switch (p->d_tag) {
            case DT_SYMTAB: symtab = (const ElfW(Sym)*)fix(p->d_un.d_ptr); break;
            case DT_STRTAB: strtab = (const char*)fix(p->d_un.d_ptr); break;
            case DT_GNU_HASH: gnuHash = (const uint32_t*)fix(p->d_un.d_ptr); break;
            case DT_HASH: sysvHash = (const uint32_t*)fix(p->d_un.d_ptr); break;
        }
    }
    if (!symtab || !strtab) return -3;
    uint32_t count = 0;
    if (sysvHash) count = sysvHash[1];
    else if (gnuHash) {
        uint32_t nb = gnuHash[0], symoff = gnuHash[1], bloom = gnuHash[2];
        const uint64_t* bl = (const uint64_t*)(gnuHash + 4);
        const uint32_t* buckets = (const uint32_t*)(bl + bloom);
        const uint32_t* chain = buckets + nb;
        uint32_t mx = 0;
        for (uint32_t b = 0; b < nb; b++) if (buckets[b] > mx) mx = buckets[b];
        if (mx < symoff) count = symoff;
        else { while (!(chain[mx - symoff] & 1)) mx++; count = mx + 1; }
    } else return -4;
    FILE* f = fopen(outPath, "w");
    if (!f) return -5;
    int hits = 0;
    for (uint32_t s = 0; s < count; s++) {
        const ElfW(Sym)& sy = symtab[s];
        if (sy.st_shndx == SHN_UNDEF || sy.st_name == 0) continue;
        const char* nm = strtab + sy.st_name;
        for (int n = 0; n < nNeedles; n++) if (strstr(nm, needles[n])) { fprintf(f, "%s\n", nm); hits++; break; }
    }
    fclose(f);
    return hits;
}

// CHud::Draw (radar, vida, dinero, etc.): se oculta con la rueda/panel/lista
// abiertos. El hook recibe `self` y lo pasa tal cual al original, asi vale
// tanto si Draw es estatico como si es de instancia (igual que el de botones).
typedef void (*HudDrawFn)(void*);
static HudDrawFn ut_origHudDraw = nullptr;
static void UT_HudDraw_hook(void* self) {
    if (g_open || g_settings || g_trackList) return;  // rueda/panel/lista abiertos: sin HUD
    if (ut_origHudDraw) ut_origHudDraw(self);
}

// v1.9.4 (AdvancedSettings de Daniel): los elementos del HUD son widgets
// CWidget (CWidgetPlayerInfo: vida, chaleco, arma, buscado, dinero;
// CWidgetRadar: el radar). AdvancedSettings los referencia por nombre, asi
// que existen en 2.10. Se ocultan con la rueda/panel/lista abiertos. Son
// metodos de instancia: `self` se pasa al original.
typedef void (*WidgetDrawFn)(void*);
static WidgetDrawFn ut_origPlayerInfoDraw = nullptr;
static WidgetDrawFn ut_origRadarUpdate = nullptr;
static void UT_PlayerInfoDraw_hook(void* self) {
    if (g_open || g_settings || g_trackList) return;
    if (ut_origPlayerInfoDraw) ut_origPlayerInfoDraw(self);
}
static void UT_RadarUpdate_hook(void* self) {
    if (g_open || g_settings || g_trackList) return;
    if (ut_origRadarUpdate) ut_origRadarUpdate(self);
}

// v1.9.5 (VHudRadioSyms.txt de Daniel): CTouchInterface::DrawAll(bool) SI
// existe en 2.10 (los 5 nombres que probe en la 1.9.1 eran los equivocados).
// Dibuja TODOS los botones tactiles. Se ocultan con la rueda/panel/lista
// abiertos. El claxon tambien se oculta mientras la rueda esta abierta, pero
// el doble-toque para abrir/cerrar sigue funcionando por POSICION (g_evs),
// no por el dibujo, y el boton flotante queda visible como control principal.
typedef void (*TouchDrawAllFn)(void*, bool);
static TouchDrawAllFn ut_origTouchDrawAll = nullptr;
static void UT_TouchDrawAll_hook(void* self, bool b) {
    if (g_open || g_settings || g_trackList) return;  // rueda/panel/lista abiertos: sin botones
    if (ut_origTouchDrawAll) ut_origTouchDrawAll(self, b);
}

static void UT_InstallHooks() {
    // v1.9.3 (Claude): si libSAUserTrack64.so esta cargado, el reproductor
    // propio NO se activa (dos reproductores + dos hooks de StartRadio =
    // doble audio y estatica). Quita ese .so de la carpeta mods.
    if (UT_LibBase("libSAUserTrack") != 0) {
        Log("UT: !! libSAUserTrack64.so esta cargado: el reproductor propio NO se activa (sonaria doble y con estatica). Quita ese .so de la carpeta mods.");
        return;
    }
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
        // v1.9: StopRadio (para cortar la emisora anterior sin estatica) y
        // RetuneRadio (el juego cambia con el cuando se usa su radio
        // original). Como SAUserTracks.
        ut_stopRadio = (StopRadioTrackFn)aml->GetSym(h, "_ZN20CAERadioTrackManager9StopRadioEP21tVehicleAudioSettingsh");
        Log("UT: StopRadio %s", ut_stopRadio ? "ok" : "no resolvio (la estatica puede quedarse)");
        if (g_retuneTrack) {
            aml->Hook((void*)g_retuneTrack, (void*)UT_RetuneRadio_hook, (void**)&ut_origRetuneRadio);
            Log("UT: hook RetuneRadio %s", ut_origRetuneRadio ? "ok" : "no se pudo instalar (la estatica puede quedarse)");
        } else {
            Log("UT: RetuneRadio no resolvio (la estatica puede quedarse)");
        }
        // v1.9.5: la radio del juego tambien puede cambiar por
        // CAudioEngine::RetuneRadio. Se engancha con la misma logica.
        uintptr_t ar = aml->GetSym(h, "_ZN12CAudioEngine11RetuneRadioEa");
        if (ar) {
            aml->Hook((void*)ar, (void*)UT_AudioRetune_hook, (void**)&ut_origAudioRetune);
            Log("UT: hook CAudioEngine::RetuneRadio %s", ut_origAudioRetune ? "ok" : "no se pudo instalar");
        } else {
            Log("UT: CAudioEngine::RetuneRadio no resolvio");
        }
        // v1.9.6: la radio del juego tambien puede cambiar por
        // CAudioEngine::StartRadio. Se engancha con la misma logica.
        uintptr_t as = aml->GetSym(h, "_ZN12CAudioEngine10StartRadioEaa");
        if (as) {
            aml->Hook((void*)as, (void*)UT_AudioStart_hook, (void**)&ut_origAudioStart);
            Log("UT: hook CAudioEngine::StartRadio %s", ut_origAudioStart ? "ok" : "no se pudo instalar");
        } else {
            Log("UT: CAudioEngine::StartRadio no resolvio");
        }
        // v1.9.5: CTouchInterface::DrawAll(bool) SI existe (VHudRadioSyms.txt
        // de Daniel): dibuja TODOS los botones tactiles. Los 5 nombres de la
        // sonda (1.9.1) eran los equivocados; la sonda se mantiene por si
        // acaso, pero DrawAll es el que se engancha.
        uintptr_t da = aml->GetSym(h, "_ZN15CTouchInterface7DrawAllEb");
        if (da) {
            aml->Hook((void*)da, (void*)UT_TouchDrawAll_hook, (void**)&ut_origTouchDrawAll);
            Log("UT: hook CTouchInterface::DrawAll %s", ut_origTouchDrawAll ? "ok (botones ocultos con la rueda)" : "no se pudo instalar");
        } else {
            Log("UT: CTouchInterface::DrawAll no resolvio (los botones no se ocultan)");
        }
        // v1.9: ocultar los botones del juego con la rueda abierta.
        // CTouchInterface::Draw no existe en 2.10; se sondean otros nombres
        // para encontrar el que dibuja los botones (se registra en el log).
        static const char* kTouchDrawNames[] = {
            "_ZN15CTouchInterface4DrawEv",
            "_ZN15CTouchInterface6RenderEv",
            "_ZN15CTouchInterface12DrawWidgetsEv",
            "_ZN15CTouchInterface11DrawButtonsEv",
            "_ZN15CTouchInterface10DrawWidgetEv",
        };
        for (int i = 0; i < 5; i++) {
            uintptr_t tn = aml->GetSym(h, kTouchDrawNames[i]);
            Log("UT: sonda botones: %s %s", kTouchDrawNames[i], tn ? "EXISTE" : "no");
            if (tn && !ut_origTouchDraw) {
                aml->Hook((void*)tn, (void*)UT_TouchDraw_hook, (void**)&ut_origTouchDraw);
                Log("UT: hook botones %s", ut_origTouchDraw ? "ok (botones ocultos con la rueda)" : "no se pudo instalar");
            }
        }
        // v1.9.1: bloquear la estatica de sintonia (0x22) con User Tracks.
        uintptr_t ra = aml->GetSym(h, "_ZN12CAudioEngine24ReportFrontendAudioEventEiff");
        if (ra) {
            aml->Hook((void*)ra, (void*)UT_ReportAudioEvent_hook, (void**)&ut_origReportAudioEvent);
            Log("UT: hook ReportFrontendAudioEvent %s", ut_origReportAudioEvent ? "ok (sin estatica 0x22 con User Tracks)" : "no se pudo instalar (puede quedar estatica)");
        } else {
            Log("UT: ReportFrontendAudioEvent no resolvio (puede quedar estatica)");
        }
        uintptr_t dn = aml->GetSym(h, "_ZN20CAERadioTrackManager23DisplayRadioStationNameEv");  // GetSym devuelve uintptr_t, no void*
        if (dn) {
            aml->Hook((void*)dn, (void*)UT_DisplayName_hook, (void**)&ut_origDisplayName);
            Log("UT: hook DisplayRadioStationName %s", ut_origDisplayName ? "ok" : "no se pudo instalar (se vera Radio Off en la 12)");
        } else {
            Log("UT: DisplayRadioStationName no resolvio (se vera Radio Off en la 12)");
        }
        // v1.9.3 (Claude): ocultar el HUD (CHud::Draw) con la rueda abierta.
        uintptr_t hd = aml->GetSym(h, "_ZN4CHud4DrawEv");
        if (hd) {
            aml->Hook((void*)hd, (void*)UT_HudDraw_hook, (void**)&ut_origHudDraw);
            Log("UT: hook CHud::Draw %s", ut_origHudDraw ? "ok (HUD oculto con la rueda)" : "no se pudo instalar");
        } else {
            Log("UT: CHud::Draw no resolvio (el HUD no se oculta)");
        }
        // v1.9.4 (AdvancedSettings): ocultar los widgets del HUD
        // (CWidgetPlayerInfo: vida/chaleco/arma/buscado/dinero; CWidgetRadar).
        uintptr_t pi = aml->GetSym(h, "_ZN17CWidgetPlayerInfo4DrawEv");
        if (pi) {
            aml->Hook((void*)pi, (void*)UT_PlayerInfoDraw_hook, (void**)&ut_origPlayerInfoDraw);
            Log("UT: hook CWidgetPlayerInfo::Draw %s", ut_origPlayerInfoDraw ? "ok (info del jugador oculta con la rueda)" : "no se pudo instalar");
        } else {
            Log("UT: CWidgetPlayerInfo::Draw no resolvio");
        }
        uintptr_t ru = aml->GetSym(h, "_ZN12CWidgetRadar6UpdateEv");
        if (ru) {
            aml->Hook((void*)ru, (void*)UT_RadarUpdate_hook, (void**)&ut_origRadarUpdate);
            Log("UT: hook CWidgetRadar::Update %s", ut_origRadarUpdate ? "ok (radar oculto con la rueda)" : "no se pudo instalar");
        } else {
            Log("UT: CWidgetRadar::Update no resolvio");
        }
        // v1.9.3 (Claude): volcado de simbolos para enganchar el dibujo de los
        // botones y dejar visible solo el claxon.
        {
            static const char* nd[] = {"Widget", "Touch", "Hud", "Radar"};
            std::string out = g_gameDataDir + "/VHudRadioSyms.txt";
            int n = DumpSymbols("libGTASA.so", nd, 4, out.c_str());
            Log("UT: volcado de simbolos: %d -> %s", n, out.c_str());
        }
    }
}
