// VHudRadioProbe.cpp - mini-plugin SOLO para probar simbolos de radio en libGTASA.so 2.10 arm64.
// No dibuja nada, no cambia la radio, no llama a ninguna funcion del juego.
// Al arrancar resuelve cada simbolo candidato con aml->GetSym y escribe
// "ok" o "FALTA" en logcat (tag VHudRadioProbe) y en VHudRadioProbe.txt
// junto al juego. Daniel solo tiene que entrar al juego, salir, y mandar ese txt.
// Basado en el patron SYM de ARCX (arcx_core.h).
#include <mod/config.h>
#include <mod/amlmod.h>
#include <android/log.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cstdio>
#include <cstring>
#include <string>

MYMODCFG(net.hunter09.vhudradioprobe, VHud Radio Probe, 1.0.0, Hunter09)
NEEDGAME(com.rockstargames.gtasa)

static std::string g_logPath;
static void ProbeLog(const char* fmt, ...) {
    char buf[700];
    va_list ap; va_start(ap, fmt); vsnprintf(buf, sizeof buf, fmt, ap); va_end(ap);
    __android_log_print(ANDROID_LOG_INFO, "VHudRadioProbe", "%s", buf);
    if (!g_logPath.empty()) { FILE* f = fopen(g_logPath.c_str(), "a"); if (f) { fprintf(f, "%s\n", buf); fclose(f); } }
}

extern "C" void OnModLoad() {
    static const char* kFilesDirs[] = {
        "/storage/emulated/0/Android_unprotected/data/com.rockstargames.gtasa/files",
        "/storage/emulated/0/Android/data/com.rockstargames.gtasa/files",
        "/sdcard/Android/data/com.rockstargames.gtasa/files",
        "/storage/self/primary/Android/data/com.rockstargames.gtasa/files",
    };
    std::string files;
    for (auto d : kFilesDirs) { struct stat st; if (stat(d, &st) == 0) { files = d; break; } }
    if (!files.empty()) g_logPath = files + "/VHudRadioProbe.txt";
    if (!g_logPath.empty()) { FILE* f = fopen(g_logPath.c_str(), "w"); if (f) { fprintf(f, "# VHudRadioProbe 1.0.0 (pid %d)\n", (int)getpid()); fclose(f); } }

    ProbeLog("== VHud Radio Probe 1.0.0: buscando simbolos de radio en libGTASA.so ==");
    void* h = aml->GetLibHandle("libGTASA.so");
    ProbeLog("libGTASA.so handle: %s", h ? "ok" : "FALTA");
    if (!h) { ProbeLog("RESULTADO: sin libGTASA.so, no se pudo probar nada"); return; }

    // Candidatos LIKELY (derivados de la lista movil + g++, NO verificados en 2.10).
    // Primero a probar en la v1: RetuneRadio de CAudioEngine (1 solo arg).
    static const char* kSyms[] = {
        "_ZN12CAudioEngine11RetuneRadioEa",
        "_ZN20CAERadioTrackManager11RetuneRadioEa",
        "_ZN12CAudioEngine24GetCurrentRadioStationIDEv",
        "_ZN20CAERadioTrackManager24GetCurrentRadioStationIDEv",
        "_ZN12CAudioEngine10StartRadioEaa",
        "_ZN20CAERadioTrackManager10StartRadioEaafh",
        "_ZN12CAudioEngine9StopRadioEP21tVehicleAudioSettingsh",
        "_ZN20CAERadioTrackManager9StopRadioEP21tVehicleAudioSettingsh",
        "_ZN20CAERadioTrackManager21CheckForStationRetuneEv",
        "_ZN12CAudioEngine23DisplayRadioStationNameEv",
        "_ZN20CAERadioTrackManager23DisplayRadioStationNameEv",
        "_ZN12CAudioEngine20IsVehicleRadioActiveEv",
        "_ZN20CAERadioTrackManager20IsVehicleRadioActiveEv",
        "_ZN12CAudioEngine23IsRadioRetuneInProgressEv",
        // Variantes PC que NO deberian estar en movil (control negativo):
        "_ZN12CAudioEngine11RetuneRadioEc",
        "_ZN12CAudioEngine10StartRadioEic",
        "_ZN20CAERadioTrackManager10StartRadioEific",
        // Globales / instancias (planos, y con mangling por si acaso):
        "AERadioTrackManager",
        "AudioEngine",
        "_Z19AERadioTrackManager",
        "_Z11AudioEngine",
        // Control: un simbolo que SI existe en ARCX, para saber que GetSym funciona:
        "_Z13FindPlayerPedi",
        "_ZN4CPed13SetModelIndexEj",
    };
    int ok = 0, total = 0;
    for (auto name : kSyms) {
        void* p = (void*)aml->GetSym(h, name);
        ProbeLog("  %s %s", p ? "ok    " : "FALTA ", name);
        total++; if (p) ok++;
    }
    ProbeLog("RESULTADO: %d/%d simbolos resolvieron. Manda VHudRadioProbe.txt para armar la v1 solo con los que dicen ok.", ok, total);
}
