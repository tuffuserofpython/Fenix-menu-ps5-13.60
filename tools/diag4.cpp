// Diagnostico v4 - descoberta de pid pelas APIs que o PS5 realmente expoe.
//
// v3 provou que sysctl KERN_PROC nao existe neste kernel (ENOENT ate para o
// proprio processo). Entao trocamos de API: o PS5 expoe sceKernelGetAppInfo,
// sceKernelGetProcessName e sceSystemServiceGetAppIdOfRunningBigApp.
//
// So leitura. Nao anexa a nada, nao escreve nada em nenhum jogo.
#include "notify.h"
#include <stdio.h>
#include <stdarg.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>

extern "C" {
int sceKernelGetProcessName(pid_t pid, char* out);
int sceKernelGetAppInfo(pid_t pid, void* info);
int sceSystemServiceGetAppIdOfRunningBigApp(int* appId);
int sceSystemServiceGetAppTitleId(int appId, char* titleId, size_t size);
int sceKernelGetProcessType(pid_t pid, int* type);
}

namespace gtav {

static FILE* g_log = nullptr;
static void L(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
static void L(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    if (g_log) { vfprintf(g_log, fmt, ap); fputc('\n', g_log); fflush(g_log); }
    va_end(ap);
    va_start(ap, fmt); vprintf(fmt, ap); putchar('\n'); va_end(ap);
}

// Extrai trechos ASCII imprimiveis de um buffer, para achar o title id sem
// conhecer o layout da struct.
static void LogAscii(const char* tag, const uint8_t* b, size_t n) {
    char out[256]; size_t o = 0; size_t runStart = 0; int run = 0;
    for (size_t i = 0; i <= n; ++i) {
        int printable = (i < n) && b[i] >= 0x20 && b[i] < 0x7F;
        if (printable) { if (!run) runStart = i; ++run; }
        else {
            if (run >= 4 && o < sizeof(out) - 40) {
                o += (size_t)snprintf(out + o, sizeof(out) - o, "[%zu]\"%.*s\" ",
                                      runStart, run > 24 ? 24 : run, (const char*)b + runStart);
            }
            run = 0;
        }
    }
    if (o) L("   %s ascii: %s", tag, out);
}

int diag4_main() {
    g_log = fopen("/data/gtav_diag.log", "w");
    L("=== GTAV diag v4 (APIs do PS5) ===");

    // 1) qual "big app" esta rodando
    int appId = 0;
    errno = 0;
    int r = sceSystemServiceGetAppIdOfRunningBigApp(&appId);
    L("GetAppIdOfRunningBigApp: r=0x%x appId=0x%x errno=%d", r, appId, errno);
    if (r == 0 && appId) {
        char tid[64] = {};
        int r2 = sceSystemServiceGetAppTitleId(appId, tid, sizeof(tid));
        L("GetAppTitleId: r=0x%x titleId=\"%s\"", r2, tid);
    }

    // 2) varredura de pids com as APIs do kernel do PS5
    L("--- varredura de pid 1..3000 ---");
    int found = 0; pid_t gtaPid = -1;
    for (pid_t pid = 1; pid < 3000; ++pid) {
        char name[64] = {};
        int rn = sceKernelGetProcessName(pid, name);
        if (rn != 0 || !name[0]) continue;
        ++found;

        uint8_t info[512] = {};
        int ri = sceKernelGetAppInfo(pid, info);
        int ptype = -1; sceKernelGetProcessType(pid, &ptype);

        L("pid %4d name=\"%s\" appinfo_r=0x%x type=%d", (int)pid, name, ri, ptype);
        if (ri == 0) {
            LogAscii("appinfo", info, sizeof(info));
            bool hit = false;
            for (size_t k = 0; k + 9 <= sizeof(info) && !hit; ++k)
                if (memcmp(info + k, "PPSA03420", 9) == 0) hit = true;
            if (hit) {
                gtaPid = pid;
                L("   >>> GTA V (PPSA03420) neste pid");
            }
        }
    }
    L("--- processos com nome: %d ---", found);

    char msg[200];
    if (gtaPid > 0) snprintf(msg, sizeof(msg), "GTA V encontrado: pid %d", (int)gtaPid);
    else            snprintf(msg, sizeof(msg), "diag v4: %d processos listados, GTA nao identificado", found);
    Notify(msg);

    L("=== fim ===");
    if (g_log) fclose(g_log);
    return gtaPid > 0 ? 0 : 1;
}

} // namespace gtav

int main() { return gtav::diag4_main(); }
