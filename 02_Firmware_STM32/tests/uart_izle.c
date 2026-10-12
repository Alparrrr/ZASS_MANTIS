/* CAN HIC YOK senaryosunda DWIN'e giden butun cerceveleri zaman damgasiyla dokar.
 * derle: zig cc -I fake_hal uart_izle.c -o uart_izle.exe */
#define main test_main
#include "test_guvenlik.c"
#undef main

static void dok(uint32_t t, const uint8_t *p, uint16_t n)
{
    printf("%6u ms  ", (unsigned)t);
    for (int i = 0; i < n; i++) printf("%02X ", p[i]);
    if (n == 8 && p[3] == 0x82)
        printf(" -> VP 0x%02X%02X = %u", p[4], p[5], (p[6] << 8) | p[7]);
    else if (n == 10 && p[5] == 0x84)
        printf(" -> SAYFA GECISI %u", (p[8] << 8) | p[9]);
    printf("\n");
}

int main(void)
{
    const uint8_t ilk = gvn_test_maske;                          /* GVN_TEST_MASKE_ILK (reset_all sifirlar) */
    reset_all(0);
    gvn_test_maske = ilk;
    B.st1 = B.cv = B.ct = B.at = B.sw = 0;                       /* CAN bagli degil: hic kare gelmiyor */
    g_tx_hook = dok;
    printf("--- CAN YOK, acilistan itibaren 12 sn (test maskesi 0x%02X) ---\n", ilk);
    sim_s(12);
    printf("--- bitti: aktif maske=0x%02X  sayfa komutu=%d ---\n", Guvenlik_Aktif_Maske(), g_page_cmds);
    return 0;
}
