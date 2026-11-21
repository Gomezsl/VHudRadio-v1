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

// ---- carpetas de musica (solo GTAVRadioMusic; se construyen en tiempo de
// ejecucion con el paquete detectado, g_pkgName / g_gameFilesDir) ----
// v1.4: la carpeta GTAVRadioMusic es la propia del mod (se crea sola).
// v1.9.14 (Daniel): la carpeta "music" vieja YA NO se usa (ni como
// respaldo): solo GTAVRadioMusic. Y las rutas usan el paquete real del
// juego (Rehan: com.rockstargames.gtasa2/gtasa23), no el fijo
// com.rockstargames.gtasa.
static const int UT_NUM_MUSIC_DIRS = 6;
static char UT_MUSIC_DIRS[6][512];
static void UT_BuildMusicDirs() {
    static bool done = false;
    if (done) return;
    done = true;
    snprintf(UT_MUSIC_DIRS[0], sizeof UT_MUSIC_DIRS[0], "%s/GTAVRadioMusic", g_gameFilesDir.c_str());
    snprintf(UT_MUSIC_DIRS[1], sizeof UT_MUSIC_DIRS[1], "/sdcard/Android_unprotected/data/%s/files/GTAVRadioMusic", g_pkgName.c_str());
    snprintf(UT_MUSIC_DIRS[2], sizeof UT_MUSIC_DIRS[2], "/storage/emulated/0/Android_unprotected/data/%s/files/GTAVRadioMusic", g_pkgName.c_str());
    snprintf(UT_MUSIC_DIRS[3], sizeof UT_MUSIC_DIRS[3], "/sdcard/Android/data/%s/files/GTAVRadioMusic", g_pkgName.c_str());
    snprintf(UT_MUSIC_DIRS[4], sizeof UT_MUSIC_DIRS[4], "/storage/emulated/0/Android/data/%s/files/GTAVRadioMusic", g_pkgName.c_str());
    snprintf(UT_MUSIC_DIRS[5], sizeof UT_MUSIC_DIRS[5], "/storage/emulated/0/GTAVRadioMusic");
}
// Crea la carpeta GTAVRadioMusic (la primera que se pueda) para que el
// usuario tenga donde poner la musica sin crearla a mano. Solo lo intenta
// una vez por sesion; si ya existe alguna, no hace nada.
static void UT_EnsureRadioMusicFolder() {
    static bool done = false;
    if (done) return;
    done = true;
    UT_BuildMusicDirs();
    for (int i = 0; i < UT_NUM_MUSIC_DIRS; i++) {
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
static bool ut_playbackDisabled = false;  // v1.9.11: PlayTrack lo pone si a2 == -1 (puerta de StartTrackPlayback)
static bool ut_inAudioMenu = false;  // v1.9.12: la pantalla de seleccion de radio (opciones de sonido) esta construida
static bool ut_menuPreview = false;  // v1.9.12: preview de Mixtape en el menu (suena aunque el juego este en pausa)
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

// Escanea las 6 carpetas GTAVRadioMusic en orden; la primera con al menos un archivo de
// audio gana. Rellena ut_tracks (alfabetico por basename) y el orden de
// reproduccion. Devuelve true si hay al menos una pista.
static bool UT_ScanMusic() {
    UT_EnsureRadioMusicFolder();  // v1.4: crear GTAVRadioMusic si no existe
    ut_tracks.clear();
    ut_order.clear();
    ut_orderPos = 0;
    char dirUsed[512] = "";
    for (int d = 0; d < UT_NUM_MUSIC_DIRS; d++) {
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
// v1.8: si el juego lleva un tiempo sin dibujar un cuadro (se fue a la
// pantalla de inicio / fondo), se pausa el reproductor. Al volver,
// UT_Frame lo reanuda. Sin esto la musica seguia sonando en la pantalla
// de inicio. v1.9.13: el umbral baja de 3 s a 1 s (Rehan: con 3 s sonaba
// 4-5 s al ir a inicio; con 1 s se corta casi al momento; si el juego se
// traba 1 s en partida, UT_Frame lo reanuda al siguiente cuadro).
// No depende de ut_menuPreview (la 1.9.12 no lo toco): el callback es un
// hilo de OpenSL independiente de los cuadros del juego.
static void UT_PauseDevice(bool pause);  // v1.8: declaracion adelantada (se define mas abajo)
static void UT_BufferCallback(SLBufferQueueItf bq, void* /*ctx*/) {
    // v1.9.15 (Rehan): parar casi al instante al ir a inicio, como las
    // emisoras nativas (Radio Los Santos). Antes solo el hueco sin cuadros
    // lo detectaba: 1 s de umbral = 1-2 s sonando en total. Ahora:
    // (1) si el juego pone su pausa (m_UserPause/m_CodePause, que tambien
    // pone al ir a segundo plano) se pausa en el siguiente buffer (~20 ms);
    // (2) el hueco sin cuadros baja de 1 s a 0.5 s como respaldo (por si el
    // juego no pone su pausa al ir a inicio). El preview del menu
    // (ut_menuPreview) no se pausa por la pausa del juego, como en
    // UT_Frame; pero el hueco sin cuadros SI lo pausa (ir a inicio desde el
    // menu tambien debe cortar la musica).
    if (ut_active) {
        bool gamePaused = (g_userPause && *g_userPause) || (g_codePause && *g_codePause);
        bool noFrames = ut_lastFrameAt.time_since_epoch().count() > 0 &&
                        std::chrono::steady_clock::now() - ut_lastFrameAt > std::chrono::milliseconds(500);
        if ((gamePaused && !ut_menuPreview) || noFrames) UT_PauseDevice(true);
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
    ut_playbackDisabled = false;  // v1.9.11 (Alex): al entrar a Mixtape se limpia (la nativa 12 se puede suprimir)
    SoundInit();
    if (!g_slEngine || !g_slEngineItf || !g_slMix) { Log("UT: sin motor OpenSL: no hay reproductor"); return false; }
    if (!UT_ScanMusic()) { Log("UT: sin musica (mp3/wav/ogg/flac/m4a) en GTAVRadioMusic (se revisaron las 6)"); return false; }
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
    ut_menuPreview = false;  // v1.9.12: al parar se acabo el preview
    Log("UT: parado (%s; underruns=%d)", reason ? reason : "?", ut_underruns);
}

// Llamado cada frame desde Swap_hook (corre siempre, aunque DrawFrame retorne antes).
// ---- v1.9.24: DIAGNOSTICO DE EMISORA (solo LEE memoria; no cambia nada del juego) ----
// El log de la 1.9.23 mostro que al entrar y salir de opciones de sonido SIN
// tocar nada, la emisora real paso de CSR (8) a Radio X (7): el juego vuelve a
// aplicar una emisora guardada que la rueda no actualiza, y AudioEngine+0x149
// (lo que se sincronizaba) NO es esa variable. Aqui se vuelcan los objetos del
// juego a VHudRadioDump.txt (solo las lineas con algun byte distinto de cero) al
// abrir y cerrar el dial y cada vez que la emisora cambia dentro o justo despues
// del menu. Con eso se ve que byte guarda la emisora del menu.
static void* ut_menuManager = nullptr;
static void* ut_mobileSettings = nullptr;  // v1.9.27: MobileSettings::settings (su +0x240 es lo que reaplica el dial)
static int ut_diagPipe[2] = {-1, -1};
static int ut_diagWatch = 0;                    // cuadros que se vigila la emisora tras cerrar el dial
static std::chrono::steady_clock::time_point ut_lastRetuneAt = std::chrono::steady_clock::time_point();  // v1.9.25: ultimo RetuneRadio (tap en el dial)
static std::chrono::steady_clock::time_point ut_dialOpenedAt = std::chrono::steady_clock::time_point();  // v1.9.26: cuando se abrio el dial (la reaplicacion es inmediata)
static int ut_diagRedirects = 0;             // v1.9.25: redirecciones hechas en este dial (tope 3)
static int ut_diagLastA = -99, ut_diagLastT = -99;
static int ut_diagDumps = 0;                    // tope: el archivo no crece sin limite
// Lectura segura (si la direccion no es valida devuelve menos bytes en vez de cerrar el juego).
static size_t UT_SafeRead(const void* src, unsigned char* dst, size_t n) {
    if (ut_diagPipe[0] < 0 && pipe(ut_diagPipe) != 0) { ut_diagPipe[0] = ut_diagPipe[1] = -1; return 0; }
    size_t total = 0, chunk = 2048;
    while (total < n) {
        size_t want = n - total; if (want > chunk) want = chunk;
        ssize_t w = write(ut_diagPipe[1], (const unsigned char*)src + total, want);
        if (w <= 0) {                       // direccion invalida: reintentar con trozos mas chicos
            if (chunk <= 1) break;
            chunk /= 2; continue;
        }
        size_t got = 0;
        while (got < (size_t)w) { ssize_t r = read(ut_diagPipe[0], dst + total + got, (size_t)w - got); if (r <= 0) return total + got; got += (size_t)r; }
        total += (size_t)w;
    }
    return total;
}
static void UT_DiagObj(FILE* f, const char* name, const void* obj, size_t n) {
    if (!obj) { fprintf(f, "  %s: NULL\n", name); return; }
    static unsigned char buf[0x1000];
    if (n > sizeof(buf)) n = sizeof(buf);
    size_t got = UT_SafeRead(obj, buf, n);
    fprintf(f, "  %s @%p leidos=0x%zx\n", name, obj, got);
    for (size_t off = 0; off < got; off += 16) {
        size_t m = (got - off < 16) ? got - off : 16; bool any = false;
        for (size_t i = 0; i < m; i++) if (buf[off + i]) { any = true; break; }
        if (!any) continue;
        fprintf(f, "  %s +%03zx:", name, off);
        for (size_t i = 0; i < m; i++) fprintf(f, " %02x", buf[off + i]);
        fprintf(f, "\n");
    }
}
static void UT_DumpAll(const char* tag, const void* radioSel) {
    if (ut_diagDumps >= 16) return;
    std::string path = g_gameDataDir + "/VHudRadioDump.txt";
    FILE* f = fopen(path.c_str(), "a");
    if (!f) return;
    ut_diagDumps++;
    int a = (g_getStationAudio && g_audioEngine) ? (int)g_getStationAudio(g_audioEngine) : -1;
    int t = (g_getStationTrack && g_trackManager) ? (int)g_getStationTrack(g_trackManager) : -1;
    int ae = g_audioEngine ? ((const unsigned char*)g_audioEngine)[0x149] : -1;
    int tm1 = g_trackManager ? ((const unsigned char*)g_trackManager)[0xAD] : -1;
    int tm2 = g_trackManager ? ((const unsigned char*)g_trackManager)[0xE9] : -1;
    int m240 = ut_mobileSettings ? *(const int*)((const char*)ut_mobileSettings + 0x240) : -1;
    int m244 = ut_mobileSettings ? *(const int*)((const char*)ut_mobileSettings + 0x244) : -1;
    fprintf(f, "== #%d %s | audio=%d track=%d rueda=%d activo=%d | AE+0x149=%d TM+0xAD=%d TM+0xE9=%d MS+0x240=%d MS+0x244=%d\n",
            ut_diagDumps, tag, a, t, g_current, ut_active ? 1 : 0, ae, tm1, tm2, m240, m244);
    if (radioSel) UT_DiagObj(f, "RadioSelection", radioSel, 0x300);
    UT_DiagObj(f, "TrackManager", g_trackManager, 0x400);
    UT_DiagObj(f, "AudioEngine", g_audioEngine, 0x800);
    UT_DiagObj(f, "MenuManager", ut_menuManager, 0x1000);
    fclose(f);
    Log("UT: [diag] volcado #%d (%s) -> VHudRadioDump.txt", ut_diagDumps, tag);
}
// Cada cuadro: dentro del dial o hasta ~90 cuadros despues de cerrarlo, avisa
// y vuelca si la emisora que dice el juego cambia (aqui se ve cuando se reaplica la guardada).
static void UT_DiagTick() {
    if (ut_diagWatch <= 0 && !ut_inAudioMenu) { ut_diagLastA = ut_diagLastT = -99; return; }
    // v1.9.29: mantener EMISORA sincronizada mientras el dial esta abierto.
    // El juego recarga MS+0x240 desde el archivo de ajustes mientras se dibuja
    // el menu (la 1.9.28 lo escribe al cambiar de emisora y al abrir el dial,
    // pero la recarga lo revierte despues y EMISORA queda en la guardada).
    // Escribirlo cada cuadro lo mantiene en la emisora de la rueda; un tap en
    // el dial actualiza g_current via el hook, asi que el siguiente cuadro
    // sigue al tap.
    if (ut_inAudioMenu && g_current >= 1 && g_current <= 13 && ut_mobileSettings) {
        int* msCur = (int*)((char*)ut_mobileSettings + 0x240);
        int* msDef = (int*)((char*)ut_mobileSettings + 0x244);
        if (*msCur != g_current) *msCur = g_current;
        if (*msDef != g_current) *msDef = g_current;
    }
    int a = (g_getStationAudio && g_audioEngine) ? (int)g_getStationAudio(g_audioEngine) : -1;
    int t = (g_getStationTrack && g_trackManager) ? (int)g_getStationTrack(g_trackManager) : -1;
    bool redirected = false;
    if (ut_diagLastA != -99 && (a != ut_diagLastA || t != ut_diagLastT)) {
        Log("UT: [diag] la emisora del juego cambio %d/%d -> %d/%d (dial abierto=%d, cuadros vigilados restantes=%d)",
            ut_diagLastA, ut_diagLastT, a, t, ut_inAudioMenu ? 1 : 0, ut_diagWatch);
        UT_DumpAll("cambio_de_emisora", nullptr);
        // v1.9.26: el juego reaplica la emisora guardada del menu al abrir el
        // dial (ni StartRadio del TrackManager ni del AudioEngine la actualizan:
        // probado con modoCambio=0 y =2). La reaplicacion es INMEDIATA al abrir
        // el dial; un tap humano tarda mas de 1 s (ver el dial y tocar). Por eso
        // la ventana es 1 s desde que se abrio el dial, no desde el ultimo
        // RetuneRadio (la 1.9.25 fallo: el propio abrir del dial dispara un
        // RetuneRadio y el guarda de 500 ms bloqueo la redireccion).
        // Si la nueva emisora no es la de la rueda, devolverla a la de la rueda
        // (max 3 por dial).
        if (ut_inAudioMenu && a >= 1 && a <= 13 && g_current >= 1 && g_current <= 13 &&
            a != g_current && ut_diagRedirects < 3) {
            auto now = std::chrono::steady_clock::now();
            if (now - ut_dialOpenedAt < std::chrono::milliseconds(1000)) {
                ut_diagRedirects++;
                Log("UT: [diag] reaplicacion del menu (%d -> %d): devolviendo a %d (rueda)", ut_diagLastA, a, g_current);
                if (g_current == 12) {
                    UT_Start();
                    if (ut_origStartRadio && g_trackManager) ut_origStartRadio(g_trackManager, 12, 0, 1.0f, 0);
                } else {
                    if (ut_active) UT_Stop("cambio de estacion");
                    if (ut_origStartRadio && g_trackManager) ut_origStartRadio(g_trackManager, (signed char)g_current, 0, 1.0f, 0);
                }
                redirected = true;
            }
        }
    }
    if (redirected) { ut_diagLastA = g_current; ut_diagLastT = g_current; }
    else { ut_diagLastA = a; ut_diagLastT = t; }
    if (ut_diagWatch > 0) ut_diagWatch--;
}

static void UT_Frame() {
    UT_DiagTick();  // v1.9.24
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
    bool paused = (g_paused && !ut_menuPreview) || (ut_loadingActive && *ut_loadingActive) || (ut_pausedByExit && !ut_menuPreview);  // v1.9.13: el preview tambien salta la pausa por salir del vehiculo (en el menu de inicio no hay vehiculo)
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
        ut_menuPreview = ut_inAudioMenu;  // v1.9.12: si se eligio en el menu, preview (suena en pausa)
        g_current = 12;
        // v1.9.3 (Claude): si se viene de otra emisora (K-Rose, etc.), cortarla con
        // StopRadio ANTES de poner la nativa en 13, igual que ya hace el hook
        // de RetuneRadio. El log de la 1.9.2 mostro que por la rueda se pasaba
        // de la 2 a la 13 sin StopRadio (la anterior podia seguir por debajo).
        if (!utWasActive && ut_active && ut_stopRadio) {
            ut_stopRadio(trackMgr, nullptr, 0);
            Log("UT: StopRadio antes de la nativa 12 (venia de otra emisora)");
        }
        // v1.9.7 (Daniel): VUELTA a la nativa 12 (Mixtape). La 1.9.2 la revirtio
        // a 13 porque la 1.9.0/1.9.1 sonaba doble, pero entonces el hook de
        // StartRadio NO llamaba a StopRadio antes de orig(12): la anterior
        // quedaba sonando por debajo. Desde la 1.9.3 StopRadio SI se llama
        // antes en todos los hooks, asi que la nativa 12 + StopRadio es una
        // prueba nueva. Daniel sospecha que sin Mixtape el juego falla y por
        // eso sigue el doble audio por la radio del juego. Si vuelve el doble
        // audio, se revierte a 13 (build #9 probo que 13 silencia de verdad).
        Log("UT: 12 -> nativa 12 (Mixtape; la musica la pone el reproductor)");
        ut_origStartRadio(trackMgr, 12, a2, a3, a4);
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
        ut_origStartRadio(trackMgr, 12, a2, a3, a4);  // v1.9.7: nativa a 12 (Mixtape) con StopRadio
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
// v1.9.28: redirige la reaplicacion del menu a la emisora de la rueda.
// AudioScreen::C2Ev llama StartRadio/RetuneRadio con la emisora guardada
// (MS+0x240, que se revierte al valor del archivo de ajustes) justo al abrir
// el dial. Un tap humano en el dial tarda mas de 1 s, asi que la ventana de
// 1 s desde que se abrio el dial distingue la reaplicacion del tap.
static signed char UT_RedirectReapply(signed char station) {
    if (ut_inAudioMenu && g_current >= 1 && g_current <= 13 && station != g_current) {
        auto now = std::chrono::steady_clock::now();
        if (now - ut_dialOpenedAt < std::chrono::milliseconds(1000)) {
            Log("UT: reaplicacion del menu (%d): redirigiendo a %d (rueda)", (int)station, g_current);
            return (signed char)g_current;
        }
    }
    return station;
}

static signed char UT_AudioRetune_hook(void* self, signed char station) {
    (void)self;
    ut_lastRetuneAt = std::chrono::steady_clock::now();  // v1.9.25: tap en el dial (RetuneRadio del AudioEngine)
    // v1.9.6: al reanudar/cargar la partida el juego puede llamar a RetuneRadio
    // antes de estar listo; en ese momento no se toca el reproductor, solo se
    // deja pasar la llamada original (evita el cierre al reanudar de la 1.9.5).
    if (ut_loadingActive && *ut_loadingActive) {
        if (ut_origAudioRetune) return ut_origAudioRetune(self, station);
        return station;
    }
    station = UT_RedirectReapply(station);  // v1.9.28: reaplicacion del menu -> rueda
    if (station == 12) {
        if (UT_VehicleBlocked()) {
            g_current = 13;
            if (ut_origStartRadio && g_trackManager) ut_origStartRadio(g_trackManager, 13, 0, 1.0f, 0);
            return 13;
        }
        if (ut_active && ut_pausedByExit) { ut_pausedByExit = false; ut_resumeAt = std::chrono::steady_clock::now(); Log("UT: reanudado al subir al vehiculo (hook AudioRetune 12)"); }
        UT_Start();
        ut_menuPreview = ut_inAudioMenu;  // v1.9.12: si se eligio en el menu, preview (suena en pausa)
        g_current = 12;
        if (ut_stopRadio && g_trackManager) ut_stopRadio(g_trackManager, nullptr, 0);
        Log("UT: 12 por AudioEngine::RetuneRadio (StopRadio + StartRadio 12)");
        if (ut_origStartRadio && g_trackManager) ut_origStartRadio(g_trackManager, 12, 0, 1.0f, 0);
        return 12;
    }
    if (ut_active && !ut_userStationCmd &&
        (ut_pausedByExit || std::chrono::steady_clock::now() - ut_resumeAt < std::chrono::seconds(2))) {
        ut_pausedByExit = false;
        g_current = 12;
        Log("UT: se queda en la 12 al subir al vehiculo (AudioRetune pedia %d)", (int)station);
        if (ut_stopRadio && g_trackManager) ut_stopRadio(g_trackManager, nullptr, 0);
        if (ut_origStartRadio && g_trackManager) ut_origStartRadio(g_trackManager, 12, 0, 1.0f, 0);
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
    station = UT_RedirectReapply(station);  // v1.9.28: reaplicacion del menu -> rueda
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
        ut_menuPreview = ut_inAudioMenu;  // v1.9.12: si se eligio en el menu, preview (suena en pausa)
        g_current = 12;
        if (!utWasActive && ut_active && ut_stopRadio && g_trackManager) {
            ut_stopRadio(g_trackManager, nullptr, 0);
            Log("UT: StopRadio antes de la nativa 12 (venia de otra emisora)");
        }
        Log("UT: 12 por AudioEngine::StartRadio (StopRadio + StartRadio 12)");
        if (ut_origStartRadio && g_trackManager) ut_origStartRadio(g_trackManager, 12, 0, 1.0f, 0);
        return;
    }
    if (ut_active && !ut_userStationCmd &&
        (ut_pausedByExit || std::chrono::steady_clock::now() - ut_resumeAt < std::chrono::seconds(2))) {
        ut_pausedByExit = false;
        g_current = 12;
        Log("UT: se queda en la 12 al subir al vehiculo (AudioStart pedia %d)", (int)station);
        if (ut_stopRadio && g_trackManager) ut_stopRadio(g_trackManager, nullptr, 0);
        if (ut_origStartRadio && g_trackManager) ut_origStartRadio(g_trackManager, 12, 0, 1.0f, 0);
        return;
    }
    if (ut_active) UT_Stop("cambio de estacion");
    g_current = station;
    if (ut_origAudioStart) ut_origAudioStart(self, station, a2);
}

static signed char UT_RetuneRadio_hook(void* trackMgr, signed char station) {
    ut_lastRetuneAt = std::chrono::steady_clock::now();  // v1.9.25: tap en el dial (RetuneRadio del TrackManager)
    if (!ut_origRetuneRadio) return 0;
    station = UT_RedirectReapply(station);  // v1.9.28: reaplicacion del menu -> rueda
    if (station == 12) {
        if (UT_VehicleBlocked()) {
            g_current = 13;
            return ut_origRetuneRadio(trackMgr, 13);
        }
        if (ut_active && ut_pausedByExit) { ut_pausedByExit = false; ut_resumeAt = std::chrono::steady_clock::now(); Log("UT: reanudado al subir al vehiculo (hook RetuneRadio 12)"); }
        UT_Start();
        ut_menuPreview = ut_inAudioMenu;  // v1.9.12: si se eligio en el menu, preview (suena en pausa)
        g_current = 12;
        if (ut_stopRadio) ut_stopRadio(trackMgr, nullptr, 0);  // corta la anterior (sin estatica)
        Log("UT: 12 por RetuneRadio (StopRadio + StartRadio 12)");
        if (ut_origStartRadio) ut_origStartRadio(trackMgr, 12, 0, 1.0f, 0);  // v1.9.7: nativa a 12 (Mixtape)
        return 12;
    }
    // v1.8: gracia de 2 s tras subir al auto (el juego pide Radio Off solo).
    if (ut_active && !ut_userStationCmd &&
        (ut_pausedByExit || std::chrono::steady_clock::now() - ut_resumeAt < std::chrono::seconds(2))) {
        ut_pausedByExit = false;
        g_current = 12;
        Log("UT: se queda en la 12 al subir al vehiculo (RetuneRadio pedia %d)", (int)station);
        if (ut_stopRadio) ut_stopRadio(trackMgr, nullptr, 0);
        if (ut_origStartRadio) ut_origStartRadio(trackMgr, 12, 0, 1.0f, 0);  // v1.9.7: nativa a 12 (Mixtape)
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
// v1.9.11: declaraciones adelantadas (UT_LibBase/UT_Patch32 se definen
// mas abajo; sin esto el compilador falla por orden de declaracion).
static uintptr_t UT_LibBase(const char* libName);
static bool UT_Patch32(uintptr_t addr, uint32_t expected, uint32_t replacement, const char* name);

// v1.9.20 (Daniel): volcado hex para encontrar la variable de EMISORA.
// El log de la 1.9.19 probo que EMISORA NO lee los getters ni mgr+0xAD/0xE9:
// en CSR (8) todo decia 8 (audio=8 track=8 rueda=8) y EMISORA mostraba
// K-DST (3, la ultima elegida EN EL MENU). La emisora del menu vive en otra
// variable (ajustes/frontend). Este volcado la busca: se vuelcan
// RadioSelection, TrackManager, AudioEngine y FrontEndMenuManager al abrir
// el dial; comparando dos aperturas (p. ej. en 8 y en 7) el byte que se
// queda en 3 es la del menu y el que cambia es la real.
// v1.9.22/1.9.23: escribe la emisora real en AudioEngine+0x149 (la
// variable que muestra EMISORA). Solo registra cuando cambia.
static void UT_SyncEmisora(int station) {
    // v1.9.27: RE de libGTASA.so (FrontEndMenuManager-0x35-RE.md). Hay DOS variables:
    // (1) FrontEndMenuManager+0x35 (byte, m_nRadioStation): lista del dial, fila EMISORA,
    //     partida guardada. (2) MobileSettings::settings+0x240 (int): lo que
    //     AudioScreen::C2Ev reaplica con RetuneRadio al abrir opciones de sonido; el tap
    //     en el dial escribe ESTA, no +0x35. Escribir solo una no basta: hay que escribir
    //     las dos. AudioEngine+0x149 NO era nada (valor aleatorio 0..3 de inicializacion).
    if (station < 1 || station > 13) return;
    // v1.9.32: preferir g_menuManager (ya resuelto con ResolveMenuManager en
    // VHudRadio.cpp); ut_menuManager queda como respaldo.
    void* mmObj = g_menuManager ? g_menuManager : ut_menuManager;
    if (mmObj) {
        unsigned char* mm = (unsigned char*)mmObj;
        unsigned char old = mm[0x35];
        if (old != (unsigned char)station) {
            mm[0x35] = (unsigned char)station;
            Log("UT: EMISORA sincronizada %d -> %d (FrontEndMenuManager+0x35)", (int)old, station);
        }
    }
    if (ut_mobileSettings) {
        int* ms = (int*)((char*)ut_mobileSettings + 0x240);
        int old = *ms;
        if (old != station) {
            *ms = station;
            Log("UT: EMISORA sincronizada %d -> %d (MobileSettings+0x240)", old, station);
        }
        // v1.9.28: escribir TAMBIEN el valor por defecto del elemento 14
        // (MS+0x244, elemento+0x14). SetDefault y LoadFromFile restauran +0x10
        // desde +0x14 (o desde el archivo); si +0x244 sigue en 5, la
        // reaplicacion vuelve a 5.
        int* msDef = (int*)((char*)ut_mobileSettings + 0x244);
        if (*msDef != station) {
            int oldDef = *msDef;
            *msDef = station;
            Log("UT: EMISORA por defecto %d -> %d (MobileSettings+0x244)", oldDef, station);
        }
    }
}

static void UT_DumpHex(const char* tag, const void* ptr, size_t n) {
    if (!ptr) { Log("UT: dump %s: NULL", tag); return; }
    const unsigned char* b = (const unsigned char*)ptr;
    for (size_t off = 0; off < n; off += 16) {
        char line[96]; int w = 0;
        w += snprintf(line + w, sizeof(line) - (size_t)w, "UT: dump %s +%02zx:", tag, off);
        for (size_t i = 0; i < 16 && off + i < n; i++)
            w += snprintf(line + w, sizeof(line) - (size_t)w, " %02x", b[off + i]);
        Log("%s", line);
    }
}

// v1.9.11 (Alex 1.8.4, RE2): silenciar la musica nativa de la 12.
// CAEAudioHardware::StartTrackPlayback es lo que EMPIEZA la pista nativa
// (PlayTrack solo la encola). Con User Tracks sonando y las puertas del
// juego despejadas, tragar la llamada = la nativa nunca suena. Si no es
// User Tracks o alguna puerta esta puesta, pasar al original.
typedef void (*StartTrackPlaybackFn)(void*);
static StartTrackPlaybackFn ut_origStartTrackPlayback = nullptr;
static bool UT_StartTrackGateClear() {
    uintptr_t base = UT_LibBase("libGTASA.so");
    if (!base) return false;  // sin base no se puede verificar: no suprimir
    uintptr_t pA = *(volatile uintptr_t*)(base + 0x851b00);
    if (!pA || (*(volatile uint8_t*)pA & 1)) return false;
    uintptr_t pB = *(volatile uintptr_t*)(base + 0x84c998);
    if (!pB || (*(volatile uint8_t*)pB & 1)) return false;
    uintptr_t pC = *(volatile uintptr_t*)(base + 0x84e6d0);
    if (!pC || (*(volatile uint32_t*)pC != 0)) return false;
    return true;
}
static void UT_StartTrackPlayback_hook(void* self) {
    if (ut_active && !ut_playbackDisabled && UT_StartTrackGateClear()) return;  // nativa 12 silenciada: no suena debajo de User Tracks
    if (ut_origStartTrackPlayback) ut_origStartTrackPlayback(self);
}
// v1.9.11: PlayTrack solo marca el flag (a2 == -1 = no hay pista) y pasa al original.
typedef void (*PlayTrackFn)(void*, unsigned int, int, unsigned int, signed char, bool, bool);
static PlayTrackFn ut_origPlayTrack = nullptr;
static void UT_PlayTrack_hook(void* self, unsigned int a1, int a2, unsigned int a3, signed char a4, bool a5, bool a6) {
    ut_playbackDisabled = (a2 == -1);
    if (ut_origPlayTrack) ut_origPlayTrack(self, a1, a2, a3, a4, a5, a6);
}
// v1.9.11 (Alex 1.8.4, RE2): ctor de RadioSelection para el menu Mixtape sin crash.
// El juego dibuja el icono de la emisora 12 desde RadioSelection+0x80; si
// es NULL (el juego no tiene emisora 12), Render crashea. Asignar la
// textura radio_TPLAYER ahi (con refcount+1) y SOLO DESPUES los 2 parches
// del menu. Si GetTexture falla, ni se intentan los parches.
typedef void* (*GetTextureFn)(const char*);
static GetTextureFn ut_getTexture = nullptr;
typedef void (*RadioSelectionCtorFn)(void*);
static RadioSelectionCtorFn ut_origRadioSelectionCtor = nullptr;
static bool ut_menuPatchesDone = false;
// v1.9.35: SettingSelection::Render — la fila EMISORA lee MS+0x240 EN VIVO
// en cada dibujado (RE: ldr w11, [x10, #0x10] en 0x361d38; x10 = settings +
// indice*40; para EMISORA indice 14 -> MS+0x240). El simbolo NO esta en
// .dynsym (solo .symtab), asi que se engancha por direccion (base+0x361c3c).
// Antes de dibujar la fila EMISORA (indice 14 en this+0x10) se escribe
// MS+0x240/MS+0x244 = g_current, asi Render lee la emisora de la rueda.
struct UT_CVector2D { float x, y; };
typedef void (*SettingRenderFn)(void*, void*, UT_CVector2D, UT_CVector2D, float);
static SettingRenderFn ut_origSettingRender = nullptr;
static void UT_SettingRender_hook(void* self, void* screen, UT_CVector2D a, UT_CVector2D b, float c) {
    if (self && g_current >= 1 && g_current <= 13 && ut_mobileSettings) {
        int idx = *(int*)((char*)self + 0x10);
        if (idx == 14) {
            *(int*)((char*)ut_mobileSettings + 0x240) = g_current;
            *(int*)((char*)ut_mobileSettings + 0x244) = g_current;
        }
    }
    if (ut_origSettingRender) ut_origSettingRender(self, screen, a, b, c);
}

// v1.9.32: constructor de AudioScreen — la pantalla de opciones de sonido
// construye los SettingSelection (incl. la fila EMISORA) copiando el valor
// de MS+0x240/mm[0x35]; sincronizar ANTES del ctor original para que la
// fila nazca ya con la emisora de la rueda (la 1.9.31 escribia MS+0x240
// cada cuadro en DrawFrontEnd, pero la fila ya tenia la copia vieja).
typedef void (*AudioScreenCtorFn)(void*);
static AudioScreenCtorFn ut_origAudioScreenCtor = nullptr;
// v1.9.33: validador de punteros para el diagnostico de AudioScreen
// (rangos observados en los volcados: libGTASA 0x70..0x72, heap 0xb4).
static bool UT_IsReadablePtr(uintptr_t p) {
    if (p < 0x100000000ULL || (p & 3) != 0) return false;
    uintptr_t top = (p >> 40) & 0xFF;
    return (top == 0x70 || top == 0x71 || top == 0x72 || top == 0xb4);
}

static void UT_AudioScreenCtor_hook(void* self) {
    if (g_current >= 1 && g_current <= 13) UT_SyncEmisora(g_current);
    if (ut_origAudioScreenCtor) ut_origAudioScreenCtor(self);
    // Post-ctor: por si C2Ev reseteo los valores durante la construccion.
    if (g_current >= 1 && g_current <= 13) UT_SyncEmisora(g_current);
    // Diagnostico + auto-fix (Gemini, via Daniel): la fila EMISORA guarda su
    // indice en un SettingSelection al que apunta AudioScreen. v1.9.33 tenia
    // un error: solo escaneaba si MS+0x240 (stale) != g_current, pero la rueda
    // ya lo sincroniza antes de abrir opciones, asi que el escaneo nunca corrio.
    // Ahora se escanea SIEMPRE que se construye AudioScreen, y el auto-fix
    // escribe solo si el objeto coincide con la firma EMISORA (indice en
    // +0x10 valido y distinto de la rueda, min +0x18 = 1, max +0x1c = 12).
    if (self && g_current >= 1 && g_current <= 13) {
        unsigned char buf[0x100];
        size_t got = UT_SafeRead(self, buf, sizeof(buf));
        uintptr_t* words = (uintptr_t*)buf;
        for (size_t i = 0; i < got / 8; i++) {
            uintptr_t p = words[i];
            if (!UT_IsReadablePtr(p)) continue;
            int vals[8] = {0, 0, 0, 0, 0, 0, 0, 0};
            size_t got2 = UT_SafeRead((const void*)p, (unsigned char*)vals, sizeof(vals));
            if (got2 < sizeof(int)) continue;
            int n = (int)(got2 / sizeof(int));
            Log("UT: AudioScreen+0x%02x -> 0x%lx [%d, %d, %d, %d, %d, %d, %d, %d]",
                (int)(i * 8), (unsigned long)p,
                n > 0 ? vals[0] : -1, n > 1 ? vals[1] : -1, n > 2 ? vals[2] : -1,
                n > 3 ? vals[3] : -1, n > 4 ? vals[4] : -1, n > 5 ? vals[5] : -1,
                n > 6 ? vals[6] : -1, n > 7 ? vals[7] : -1);
            // v1.9.35: SIN auto-fix — el RE de SettingSelection::Render probo
            // que +0x10 del SettingSelection es el INDICE del elemento (14
            // para EMISORA), no la emisora; escribir ahi corromperia la fila.
            // El diagnostico (log de arriba) se conserva, es inofensivo.
        }
    }
}

static void UT_RadioSelectionCtor_hook(void* self) {
    // v1.9.32: sincronizar ANTES del ctor original: el dial lee MS+0x240 al
    // construirse; si se escribe despues, nace con la emisora vieja.
    if (g_current >= 1 && g_current <= 13) UT_SyncEmisora(g_current);
    if (ut_origRadioSelectionCtor) ut_origRadioSelectionCtor(self);  // ctor original primero
    if (!self || !ut_getTexture) return;
    ut_diagRedirects = 0;  // v1.9.25: nuevo dial, contador de redirecciones a cero
    ut_dialOpenedAt = std::chrono::steady_clock::now();  // v1.9.26: marca de apertura del dial
    UT_DumpAll("dial_abierto", self);  // v1.9.24: antes de tocar nada
    void* tex = ut_getTexture("radio_TPLAYER");
    if (!tex) { Log("UT: no se pudo cargar la textura radio_TPLAYER (sin parches del menu)"); return; }
    *(volatile uint32_t*)((uintptr_t)tex + 0x64) += 1;  // refcount+1
    *(volatile void**)((uintptr_t)self + 0x80) = tex;   // RadioSelection+0x80
    Log("UT: radio_TPLAYER asignada en RadioSelection+0x80");
    ut_inAudioMenu = true;  // v1.9.12: el menu de seleccion de radio existe (preview posible)
    if (!ut_menuPatchesDone) {
        ut_menuPatchesDone = true;
        UT_Patch32(UT_LibBase("libGTASA.so") + 0x364628, 0xF1002EFF, 0xF10032FF, "menu 12 huecos A");
        UT_Patch32(UT_LibBase("libGTASA.so") + 0x3646E0, 0x7100303F, 0x7100343F, "menu 12 huecos B");
    }
    // v1.9.17 (Daniel): sincronizar el dial con la emisora real. Al entrar
    // a opciones de sonido estando en otra emisora (p. ej. CSR 8), el dial
    // se quedaba en User Track (12) en vez de CSR. Si el reproductor no
    // esta activo y la emisora real no es la 12 pero el juego cree que es
    // la 12, se resincroniza con StartRadio a la emisora real.
    {
        // v1.9.19: el getter del AudioEngine va atrasado (devuelve la emisora
        // anterior); se lee PRIMERO el del TrackManager, que es el que
        // actualizan los hooks. Se registran ambos para confirmar si difieren.
        int cur = -1, curAudio = -1, curTrack = -1;
        if (g_getStationAudio && g_audioEngine) curAudio = (int)g_getStationAudio(g_audioEngine);
        if (g_getStationTrack && g_trackManager) curTrack = (int)g_getStationTrack(g_trackManager);
        cur = (curTrack >= 1 && curTrack <= 13) ? curTrack : curAudio;
        Log("UT: dial abierto (juego=%d audio=%d track=%d rueda=%d activo=%d)", cur, curAudio, curTrack, g_current, ut_active ? 1 : 0);
        if (!ut_active && g_current >= 1 && g_current <= 13 && g_current != 12 && cur == 12 &&
            ut_origStartRadio && g_trackManager) {
            Log("UT: dial resincronizado a %d (el juego creia que era 12)", g_current);
            ut_origStartRadio(g_trackManager, (signed char)g_current, 0, 1.0f, 0);
        }
        // v1.9.18 (Daniel): el dial mostraba la emisora ANTERIOR elegida en
        // el menu, no la del auto. El dial lee la emisora "preview"
        // (mgr+0xAD) y esa no se actualiza al cambiar con la rueda (la rueda
        // usa StartRadio, que actualiza la emisora actual pero no el
        // preview del dial). Al abrir el dial se sincroniza el preview (y
        // 0xE9) con la emisora real (la del getter), para que el dial
        // resalte la que suena y no la anterior del menu.
        if (g_trackManager && cur >= 1 && cur <= 13) {
            unsigned char* m = (unsigned char*)g_trackManager;
            if (m[0xAD] != (unsigned char)cur || m[0xE9] != (unsigned char)cur) {
                Log("UT: dial preview sincronizado %d -> %d", (int)m[0xAD], cur);
                m[0xAD] = (unsigned char)cur;
                m[0xE9] = (unsigned char)cur;
            }
        }
        // v1.9.22 (Daniel): EMISORA SINCRONIZADA. El volcado de la 1.9.21
        // con 3 aperturas (8, 7, 8) probo que AudioEngine+0x149 es la
        // variable del menu: es el UNICO byte constante = 3 (K-DST, la que
        // mostraba EMISORA) en los 4 objetos volcados, mientras la real
        // cambiaba 8->7->8. No esta en RadioSelection, TrackManager ni
        // FrontEndMenuManager. Al abrir el dial se escribe la emisora real
        // (cur, del TrackManager) ahi, para que EMISORA muestre la que
        // suena y no la ultima elegida en el menu. Se quita el volcado
        // (era solo diagnostico) y se registra la escritura.
        // v1.9.23: tambien se sincroniza al cambiar con la rueda
        // (DoStartRadio llama a UT_SyncEmisora), porque EMISORA se lee al
        // abrir opciones de sonido ANTES de que el ctor del dial corra; si
        // solo se escribe al abrir el dial, la primera vista muestra el
        // valor viejo.
        UT_SyncEmisora(cur);
    }
}

// v1.9.12 (Alex 1.8.4, RE3): destructor de RadioSelection — al cerrar el
// menu se acabo el preview (UT_Frame vuelve a pausar, el juego sigue en pausa).
typedef void (*RadioSelectionDtorFn)(void*);
static RadioSelectionDtorFn ut_origRadioSelectionDtor = nullptr;
static void UT_RadioSelectionDtor_hook(void* self) {
    UT_DumpAll("dial_cierra_antes", self);  // v1.9.24
    ut_inAudioMenu = false;
    ut_menuPreview = false;
    if (ut_origRadioSelectionDtor) ut_origRadioSelectionDtor(self);
    UT_DumpAll("dial_cierra_despues", nullptr);  // v1.9.24: sin leer el dial (ya destruido)
    ut_diagWatch = 90;  // v1.9.24: vigilar ~90 cuadros si el juego reaplica otra emisora
}

// v1.9.12 (Alex 1.8.4, RE3): CKeyGen::GetUppercaseKey — el nombre EMISORA.
// El juego busca la clave "FEA_NON" (Radio Off) para la emisora 12; con
// Mixtape sonando, devolver el hash de "FEA_MP3" (0x9109b7c1, JAMCRC
// verificado) para que muestre el nombre propio del juego para User Tracks
// en lugar de "RADIO OFF". "SAMP_AUDIO_HINT" se redirige a 0x93516cf0
// (valor de Alex, hash no identificado; se copia tal cual).
typedef uint32_t (*GetUppercaseKeyFn)(const char*);
static GetUppercaseKeyFn ut_origGetUppercaseKey = nullptr;
static uint32_t UT_GetUppercaseKey_hook(const char* key) {
    if (key) {
        if (strcmp(key, "FEA_MP3") == 0) return 0x9109b7c1;  // identidad (JAMCRC de FEA_MP3)
        if (ut_active && strcmp(key, "FEA_NON") == 0) return 0x9109b7c1;  // Radio Off -> nombre de User Tracks
        if (strcmp(key, "SAMP_AUDIO_HINT") == 0) return 0x93516cf0;  // hint del menu (valor de Alex)
    }
    if (ut_origGetUppercaseKey) return ut_origGetUppercaseKey(key);
    return 0;
}

static ReportAudioEventFn ut_origReportAudioEvent = nullptr;
static void UT_ReportAudioEvent_hook(void* engine, int eventId, float a, float b) {
    if (eventId == 0x22 && ut_active) return;  // estatica de sintonia: no suena con User Tracks
    if (ut_origReportAudioEvent) ut_origReportAudioEvent(engine, eventId, a, b);
}

// v1.9.9 (Alex 1.8.4, HookOf_FrontendAddAudioEvent 0x74b08): SEGUNDO bloqueo
// de la estatica 0x22. El juego tambien dispara la estatica por
// CAEFrontendAudioEntity::AddAudioEvent (la entidad de audio), no solo por
// CAudioEngine::ReportFrontendAudioEvent; nuestro unico bloqueo no la veia
// y por eso seguia la estatica. Misma firma (void, int, float, float) y
// misma logica: tragar 0x22 si suena el reproductor. El flag de reentrancia
// (ut_audioReentrant) es como el 0xa3c30 de Alex: nuestras propias llamadas
// pasarian; nosotros no disparamos eventos, asi que queda en 0.
static bool ut_audioReentrant = false;
static ReportAudioEventFn ut_origFrontendAddAudio = nullptr;
static void UT_FrontendAddAudioEvent_hook(void* entity, int eventId, float a, float b) {
    if (eventId == 0x22 && ut_active && !ut_audioReentrant) return;  // estatica por la entidad de audio
    if (ut_origFrontendAddAudio) ut_origFrontendAddAudio(entity, eventId, a, b);
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
    // v1.9.9 los anadio (Alex 1.8.4, "12-slot menu patches") pero CRASHEAN
    // el juego al entrar a opciones de sonido: RadioSelection::Render dibuja
    // un hueco 12 cuya textura es NULL (MenuScreen::DrawSprite, SIGSEGV en
    // libGTASA+0x355044, log aml_crashlog_2_7wfq.txt). Alex ADEMAS asigna la
    // textura radio_TPLAYER en RadioSelection+0x80 en el ctor; sin eso el
    // parche solo no sirve. QUITADOS en v1.9.10 hasta enganchar el ctor como
    // Alex. El menu de pausa vuelve a marcar Radio Off en la 12 (como 1.9.2-1.9.6).
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
    if (g_open || g_settings || g_trackList) {  // rueda/panel/lista abiertos: sin HUD
        static bool logged = false;
        if (!logged) { logged = true; Log("UT: CHud::Draw oculto (rueda abierta)"); }  // v1.9.16: diagnostico (el hook dice ok pero Daniel ve el HUD)
        return;
    }
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
    if (g_open || g_settings || g_trackList) {
        static bool logged = false;
        if (!logged) { logged = true; Log("UT: PlayerInfo oculto (rueda abierta)"); }  // v1.9.16: diagnostico
        return;
    }
    if (ut_origPlayerInfoDraw) ut_origPlayerInfoDraw(self);
}
static void UT_RadarUpdate_hook(void* self) {
    if (g_open || g_settings || g_trackList) {
        static bool logged = false;
        if (!logged) { logged = true; Log("UT: Radar oculto (rueda abierta)"); }  // v1.9.16: diagnostico
        return;
    }
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
    if (g_open || g_settings || g_trackList) {  // rueda/panel/lista abiertos: sin botones
        static bool logged = false;
        if (!logged) { logged = true; Log("UT: DrawAll oculto (rueda abierta)"); }  // v1.9.16: diagnostico
        return;
    }
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
        // v1.9.9 (Alex 1.8.4): SEGUNDO bloqueo 0x22 por la entidad de audio.
        uintptr_t fae = aml->GetSym(h, "_ZN22CAEFrontendAudioEntity13AddAudioEventEiff");
        if (fae) {
            aml->Hook((void*)fae, (void*)UT_FrontendAddAudioEvent_hook, (void**)&ut_origFrontendAddAudio);
            Log("UT: hook CAEFrontendAudioEntity::AddAudioEvent %s", ut_origFrontendAddAudio ? "ok (sin estatica 0x22 por la entidad)" : "no se pudo instalar (puede quedar estatica)");
        } else {
            Log("UT: CAEFrontendAudioEntity::AddAudioEvent no resolvio (sin segundo bloqueo 0x22)");
        }
        // v1.9.11 (Alex 1.8.4, RE2): silenciar la musica nativa de la 12.
        // StartTrackPlayback es lo que EMPIEZA la pista nativa; con User
        // Tracks sonando, tragarla = la nativa nunca suena debajo.
        uintptr_t stp = aml->GetSym(h, "_ZN16CAEAudioHardware18StartTrackPlaybackEv");
        if (stp) {
            aml->Hook((void*)stp, (void*)UT_StartTrackPlayback_hook, (void**)&ut_origStartTrackPlayback);
            Log("UT: hook CAEAudioHardware::StartTrackPlayback %s", ut_origStartTrackPlayback ? "ok (nativa 12 silenciada con User Tracks)" : "no se pudo instalar (puede quedar doble audio)");
        } else {
            Log("UT: CAEAudioHardware::StartTrackPlayback no resolvio (puede quedar doble audio)");
        }
        // v1.9.11: PlayTrack solo marca el flag (a2 == -1) y pasa al original.
        uintptr_t pt = aml->GetSym(h, "_ZN16CAEAudioHardware9PlayTrackEjijcbb");
        if (pt) {
            aml->Hook((void*)pt, (void*)UT_PlayTrack_hook, (void**)&ut_origPlayTrack);
            Log("UT: hook CAEAudioHardware::PlayTrack %s", ut_origPlayTrack ? "ok" : "no se pudo instalar");
        } else {
            Log("UT: CAEAudioHardware::PlayTrack no resolvio");
        }
        // v1.9.11 (Alex 1.8.4, RE2): menu Mixtape sin crash — el ctor de
        // RadioSelection asigna radio_TPLAYER y luego los parches del menu.
        {   // v1.9.20: FrontEndMenuManager (el sistema de menus; ahi puede
            // vivir la emisora que muestra EMISORA). Para el volcado.
            // v1.9.21: CORRECCION — la direccion del simbolo ES el objeto
            // (en VHudRadio.cpp sim==obj); en la 1.9.20 se desreferencio
            // (*(void**)mm) y dio NULL porque los primeros bytes del objeto
            // son 0. Ahora se usa la direccion directamente.
            uintptr_t mm = aml->GetSym(h, "FrontEndMenuManager");
            if (!mm) mm = aml->GetSym(h, "_ZN13CMenuManager18FrontEndMenuManagerE");
            if (mm) { ut_menuManager = (void*)mm; Log("UT: FrontEndMenuManager ok (%p)", ut_menuManager); }
            else Log("UT: FrontEndMenuManager no resolvio (sin volcado del menu)");
            // v1.9.27: MobileSettings::settings (RE de libGTASA.so: su +0x240 int es
            // lo que AudioScreen::C2Ev reaplica al abrir opciones de sonido)
            uintptr_t mst = aml->GetSym(h, "_ZN14MobileSettings8settingsE");
            if (mst) { ut_mobileSettings = (void*)mst; Log("UT: MobileSettings::settings ok (%p)", ut_mobileSettings); }
            else Log("UT: MobileSettings::settings no resolvio (sin sincronia de EMISORA)");
        }
        ut_getTexture = (GetTextureFn)aml->GetSym(h, "_ZN22TextureDatabaseRuntime10GetTextureEPKc");
        if (!ut_getTexture) {
            Log("UT: TextureDatabaseRuntime::GetTexture no resolvio (sin menu Mixtape, sin crash)");
        } else {
            uintptr_t rsc = aml->GetSym(h, "_ZN11AudioScreen14RadioSelectionC2Ev");
            if (rsc) {
                aml->Hook((void*)rsc, (void*)UT_RadioSelectionCtor_hook, (void**)&ut_origRadioSelectionCtor);
                Log("UT: hook AudioScreen::RadioSelection %s", ut_origRadioSelectionCtor ? "ok (menu Mixtape con textura)" : "no se pudo instalar (sin menu Mixtape)");
            } else {
                Log("UT: AudioScreen::RadioSelection no resolvio (sin menu Mixtape)");
            }
            // v1.9.12 (Alex 1.8.4, RE3): destructor — al cerrar el menu se acaba el preview.
            uintptr_t rsd = aml->GetSym(h, "_ZN11AudioScreen14RadioSelectionD2Ev");
            if (rsd) {
                aml->Hook((void*)rsd, (void*)UT_RadioSelectionDtor_hook, (void**)&ut_origRadioSelectionDtor);
                Log("UT: hook AudioScreen::RadioSelectionDtor %s", ut_origRadioSelectionDtor ? "ok" : "no se pudo instalar");
            } else {
                Log("UT: AudioScreen::RadioSelectionDtor no resolvio");
            }
            // v1.9.32: constructor de AudioScreen — sincronizar EMISORA ANTES
            // de que C2Ev construya los SettingSelection (la fila EMISORA
            // copia el valor al construirse; escribir despues llega tarde).
            uintptr_t asc = aml->GetSym(h, "_ZN11AudioScreenC2Ev");
            if (asc) {
                aml->Hook((void*)asc, (void*)UT_AudioScreenCtor_hook, (void**)&ut_origAudioScreenCtor);
                Log("UT: hook AudioScreen::AudioScreen %s", ut_origAudioScreenCtor ? "ok (EMISORA nace sincronizada)" : "no se pudo instalar");
            } else {
                Log("UT: AudioScreen::AudioScreen no resolvio (EMISORA puede nacer vieja)");
            }
            // v1.9.12 (Alex 1.8.4, RE3): nombre EMISORA — FEA_NON -> FEA_MP3 con Mixtape sonando.
            uintptr_t kg = aml->GetSym(h, "_ZN7CKeyGen15GetUppercaseKeyEPKc");
            if (kg) {
                aml->Hook((void*)kg, (void*)UT_GetUppercaseKey_hook, (void**)&ut_origGetUppercaseKey);
                Log("UT: hook CKeyGen::GetUppercaseKey %s", ut_origGetUppercaseKey ? "ok (EMISORA con nombre de User Tracks)" : "no se pudo instalar (EMISORA dira Radio Off)");
            } else {
                Log("UT: CKeyGen::GetUppercaseKey no resolvio (EMISORA dira Radio Off)");
            }
        }
        uintptr_t dn = aml->GetSym(h, "_ZN20CAERadioTrackManager23DisplayRadioStationNameEv");  // GetSym devuelve uintptr_t, no void*
        if (dn) {
            aml->Hook((void*)dn, (void*)UT_DisplayName_hook, (void**)&ut_origDisplayName);
            Log("UT: hook DisplayRadioStationName %s", ut_origDisplayName ? "ok" : "no se pudo instalar (se vera Radio Off en la 12)");
        } else {
            Log("UT: DisplayRadioStationName no resolvio (se vera Radio Off en la 12)");
        }
        // v1.9.35: SettingSelection::Render — la fila EMISORA lee MS+0x240 en
        // vivo en cada dibujado. El simbolo NO esta en .dynsym, asi que se
        // engancha por direccion (libGTASA+0x361c3c, confirmado por RE).
        {
            uintptr_t sr = UT_LibBase("libGTASA.so") + 0x361c3c;
            if (sr != 0x361c3c) {
                aml->Hook((void*)sr, (void*)UT_SettingRender_hook, (void**)&ut_origSettingRender);
                Log("UT: hook SettingSelection::Render %s", ut_origSettingRender ? "ok (EMISORA leida en vivo)" : "no se pudo instalar");
            } else {
                Log("UT: SettingSelection::Render sin base libGTASA (sin fix de EMISORA)");
            }
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
