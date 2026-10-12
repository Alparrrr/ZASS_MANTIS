/* PC testi icin SAHTE FatFs arayuzu (Task_SDCard.c'nin kullandigi kadari).
 * Gercegi: FATFS/App/app_fatfs.h -> ff.h (R0.12c). Davranis test_kayit.c'de. */
#ifndef FAKE_APP_FATFS_H
#define FAKE_APP_FATFS_H
#include <stdint.h>

typedef unsigned int UINT;
typedef uint8_t      BYTE;
typedef uint32_t     DWORD;

typedef enum {
    FR_OK = 0, FR_DISK_ERR, FR_INT_ERR, FR_NOT_READY, FR_NO_FILE, FR_NO_PATH,
    FR_INVALID_NAME, FR_DENIED, FR_EXIST, FR_TOO_MANY_OPEN_FILES = 18
} FRESULT;

typedef struct { int dummy; } FATFS;
typedef struct { int idx; uint32_t ptr; BYTE mod; int acik; } FIL;

#define FA_READ         0x01
#define FA_WRITE        0x02
#define FA_OPEN_APPEND  0x30

FRESULT f_mount(FATFS *fs, const char *path, BYTE opt);
FRESULT f_open(FIL *fp, const char *path, BYTE mode);
FRESULT f_close(FIL *fp);
FRESULT f_read(FIL *fp, void *buff, UINT btr, UINT *br);
FRESULT f_write(FIL *fp, const void *buff, UINT btw, UINT *bw);
FRESULT f_sync(FIL *fp);
FRESULT f_mkdir(const char *path);
FRESULT f_unlink(const char *path);
FRESULT f_rename(const char *eski, const char *yeni);
DWORD   fake_f_size(FIL *fp);
#define f_size(fp) fake_f_size(fp)

#endif
