/* ============================================================================
 * PC TESTLERI ICIN ORTAK: sahte RTC (takvim + yedek kaydi) ve BELLEK ICI sahte FatFs.
 * test_kayit.c ve ornek_senaryo.c kullanir. Once stm32g0xx_hal.h ve ff_gen_drv.h
 * (fake_hal / fake_sd) include edilmis olmali.
 *   sahte_sd_disa_aktar("klasor") : sahte kartin icerigini gercek klasore yazar.
 * ========================================================================== */
#ifndef SAHTE_SD_H
#define SAHTE_SD_H
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <direct.h>
#define SAHTE_MKDIR(p) _mkdir(p)
#else
#include <sys/stat.h>
#define SAHTE_MKDIR(p) mkdir((p), 0755)
#endif

/* ---------------------------------------------------------------- sahte RTC */
static struct { uint16_t y; uint8_t a, g, s, d, sn, hg; } R = { 2026, 6, 19, 15, 10, 0, 5 };
static uint32_t g_bkp[8];
RCC_TypeDef fake_rcc;
static int      g_rtc_set_n;
static uint8_t rtc_aygunu(uint16_t y, uint8_t a)
{
    static const uint8_t G[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    return (a == 2 && ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0)) ? 29 : G[a - 1];
}
static void rtc_saniye(void)
{
    if (++R.sn < 60) return; R.sn = 0;
    if (++R.d < 60) return;  R.d = 0;
    if (++R.s < 24) return;  R.s = 0; R.hg = (uint8_t)(R.hg % 7 + 1);
    if (++R.g <= rtc_aygunu(R.y, R.a)) return; R.g = 1;
    if (++R.a <= 12) return; R.a = 1; R.y++;
}
HAL_StatusTypeDef HAL_RTC_GetTime(RTC_HandleTypeDef *h, RTC_TimeTypeDef *t, uint32_t f)
{ (void)h; (void)f; t->Hours = R.s; t->Minutes = R.d; t->Seconds = R.sn; return HAL_OK; }
HAL_StatusTypeDef HAL_RTC_GetDate(RTC_HandleTypeDef *h, RTC_DateTypeDef *d, uint32_t f)
{ (void)h; (void)f; d->Year = (uint8_t)(R.y - 2000); d->Month = R.a; d->Date = R.g; d->WeekDay = R.hg; return HAL_OK; }
HAL_StatusTypeDef HAL_RTC_SetTime(RTC_HandleTypeDef *h, RTC_TimeTypeDef *t, uint32_t f)
{ (void)h; (void)f; R.s = t->Hours; R.d = t->Minutes; R.sn = t->Seconds; g_rtc_set_n++; return HAL_OK; }
HAL_StatusTypeDef HAL_RTC_SetDate(RTC_HandleTypeDef *h, RTC_DateTypeDef *d, uint32_t f)
{ (void)h; (void)f; R.y = (uint16_t)(2000 + d->Year); R.a = d->Month; R.g = d->Date; R.hg = d->WeekDay; return HAL_OK; }
uint32_t HAL_RTCEx_BKUPRead(RTC_HandleTypeDef *h, uint32_t r) { (void)h; return g_bkp[r & 7u]; }
void     HAL_RTCEx_BKUPWrite(RTC_HandleTypeDef *h, uint32_t r, uint32_t v) { (void)h; g_bkp[r & 7u] = v; }

/* ---------------------------------------------------------------- sahte FatFs (bellek ici) */
#include "app_fatfs.h"
Disk_drvTypeDef disk;
#define FMAX 32
#define FBOY 600000u
typedef struct { char ad[64]; int var; uint32_t boy; char *veri; } fd_t;
static fd_t g_f[FMAX];
static char g_dir[32][40];
static int  g_dir_n, g_kart, g_bagli, g_mount_n, g_sync_n, g_yaz_hata, g_acik, g_acik_max, g_init_ihlal, g_olay_ac_hata;

static int f_bul(const char *ad) { for (int i = 0; i < FMAX; i++) if (g_f[i].var && !strcmp(g_f[i].ad, ad)) return i; return -1; }
static int dir_var(const char *d) { for (int i = 0; i < g_dir_n; i++) if (!strcmp(g_dir[i], d)) return 1; return 0; }
static int ust_var(const char *yol)
{
    const char *s = strrchr(yol, '/');
    char d[40];
    if (!s) return 1;
    memcpy(d, yol, (size_t)(s - yol)); d[s - yol] = '\0';
    return dir_var(d);
}
FRESULT f_mount(FATFS *fs, const char *p, BYTE opt)
{
    (void)p; (void)opt;
    if (fs == NULL) { g_bagli = 0; g_acik = 0; return FR_OK; }   /* FatFs: f_mount(NULL) kilitleri temizler */
    g_mount_n++;
    if (disk.is_initialized[0]) g_init_ihlal++;      /* ST diskio: kart bir daha baslatilmazdi */
    disk.is_initialized[0] = 1;
    if (!g_kart) return FR_NOT_READY;
    g_bagli = 1;
    return FR_OK;
}
FRESULT f_open(FIL *fp, const char *path, BYTE mode)
{
    int i;
    if (!g_bagli || !g_kart) return FR_NOT_READY;
    if (g_olay_ac_hata && strstr(path, "OLAY")) return FR_DISK_ERR;
    if (!ust_var(path)) return FR_NO_PATH;
    if (g_acik >= 2) return FR_TOO_MANY_OPEN_FILES;   /* _FS_LOCK 2 */
    i = f_bul(path);
    if (i < 0) {
        if (!(mode & FA_WRITE)) return FR_NO_FILE;
        for (i = 0; i < FMAX && g_f[i].var; i++) {}
        if (i == FMAX) return FR_DENIED;
        memset(&g_f[i], 0, sizeof(g_f[i]));
        g_f[i].var = 1; snprintf(g_f[i].ad, sizeof(g_f[i].ad), "%s", path);
        g_f[i].veri = (char *)calloc(1, FBOY + 1);
    }
    fp->idx = i; fp->mod = mode; fp->acik = 1;
    fp->ptr = ((mode & FA_OPEN_APPEND) == FA_OPEN_APPEND) ? g_f[i].boy : 0u;
    if (++g_acik > g_acik_max) g_acik_max = g_acik;
    return FR_OK;
}
FRESULT f_close(FIL *fp) { if (!fp->acik) return FR_INT_ERR; fp->acik = 0; g_acik--; return g_kart ? FR_OK : FR_DISK_ERR; }
FRESULT f_read(FIL *fp, void *b, UINT n, UINT *br)
{
    fd_t *f = &g_f[fp->idx];
    UINT k = (fp->ptr + n > f->boy) ? (UINT)(f->boy - fp->ptr) : n;
    memcpy(b, f->veri + fp->ptr, k); fp->ptr += k; *br = k;
    return FR_OK;
}
FRESULT f_write(FIL *fp, const void *b, UINT n, UINT *bw)
{
    fd_t *f;
    *bw = 0;
    if (!fp->acik || !g_kart || g_yaz_hata) return FR_DISK_ERR;
    f = &g_f[fp->idx];
    if (f->boy + n > FBOY) return FR_DENIED;
    memcpy(f->veri + f->boy, b, n); f->boy += n; f->veri[f->boy] = '\0'; fp->ptr = f->boy; *bw = n;
    return FR_OK;
}
FRESULT f_sync(FIL *fp) { (void)fp; if (!g_kart) return FR_DISK_ERR; g_sync_n++; return FR_OK; }
FRESULT f_mkdir(const char *p)
{
    if (!g_bagli) return FR_NOT_READY;
    if (dir_var(p)) return FR_EXIST;
    if (!ust_var(p)) return FR_NO_PATH;
    snprintf(g_dir[g_dir_n++], sizeof(g_dir[0]), "%s", p);
    return FR_OK;
}
FRESULT f_unlink(const char *p) { int i = f_bul(p); if (i < 0) return FR_NO_FILE; g_f[i].var = 0; free(g_f[i].veri); return FR_OK; }
FRESULT f_rename(const char *a, const char *b)
{
    int i = f_bul(a);
    if (i < 0) return FR_NO_FILE;
    if (f_bul(b) >= 0) return FR_EXIST;
    snprintf(g_f[i].ad, sizeof(g_f[i].ad), "%s", b);
    return FR_OK;
}
DWORD fake_f_size(FIL *fp) { return g_f[fp->idx].boy; }
/* soketin CD ucu (PA3): kart takiliyken GND (aktif dusuk). g_cd_kopuk = CD teli bagli degil (pull-up -> 1) */
GPIO_TypeDef fake_gpioa, fake_gpiob;
static int g_cd_kopuk, g_cd_init, g_cd_okuma;
void HAL_GPIO_Init(GPIO_TypeDef *port, GPIO_InitTypeDef *g) { (void)port; if (g->Pin == GPIO_PIN_3 && g->Pull == GPIO_PULLUP) g_cd_init++; }
GPIO_PinState HAL_GPIO_ReadPin(GPIO_TypeDef *port, uint16_t pin)
{
    (void)port; (void)pin; g_cd_okuma++;
    return (g_kart && !g_cd_kopuk) ? GPIO_PIN_RESET : GPIO_PIN_SET;
}
static void dosya_koy(const char *ad, const char *icerik)
{
    FIL f; UINT bw; int k = g_kart, b = g_bagli;
    g_kart = 1; g_bagli = 1;
    f_open(&f, ad, FA_OPEN_APPEND | FA_WRITE); f_write(&f, icerik, (UINT)strlen(icerik), &bw); f_close(&f);
    g_kart = k; g_bagli = b;
}
static const char *icerik(const char *ad) { int i = f_bul(ad); return (i < 0) ? NULL : g_f[i].veri; }
static int satir_say(const char *ad, const char *onek)
{
    const char *p = icerik(ad); int n = 0; size_t L = strlen(onek);
    if (!p) return -1;
    while (*p) { if (!strncmp(p, onek, L)) n++; p = strchr(p, '\n'); if (!p) break; p++; }
    return n;
}
static const char *son_satir(const char *ad)
{
    static char s[512];
    const char *p = icerik(ad), *bas;
    size_t n;
    if (!p || !*p) return "";
    n = strlen(p); if (p[n - 1] == '\n') n--;
    bas = p + n; while (bas > p && bas[-1] != '\n') bas--;
    memcpy(s, bas, (size_t)(p + n - bas)); s[p + n - bas] = '\0';
    return s;
}

/* sahte kartin klasor ve dosyalarini gercek diske yazar (orn. Excel'de acmak icin) */
static int sahte_sd_disa_aktar(const char *kok)
{
    char yol[256];
    int n = 0;
    SAHTE_MKDIR(kok);
    for (int i = 0; i < g_dir_n; i++) { snprintf(yol, sizeof(yol), "%s/%s", kok, g_dir[i]); SAHTE_MKDIR(yol); }
    for (int i = 0; i < FMAX; i++) {
        FILE *f;
        if (!g_f[i].var) continue;
        snprintf(yol, sizeof(yol), "%s/%s", kok, g_f[i].ad);
        f = fopen(yol, "wb");
        if (!f) continue;
        fwrite(g_f[i].veri, 1, g_f[i].boy, f); fclose(f); n++;
    }
    return n;
}

#endif /* SAHTE_SD_H */
