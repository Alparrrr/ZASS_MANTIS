/* GVN_TEST_MASKE_ILK testi: "ne olursa olsun haberlesme hatasi" ekrani.
 * Acilis degeri 0x40 iken kural 7 ekrani BMS ne derse desin ilk Task'tan itibaren
 * gosterilmeli; gercek olay sayilmamali, flash'a kayit yazilmamali.
 * derle: zig cc -DGVN_TEST_MASKE_ILK=0x40u -I fake_hal test_zorla.c -o t_zorla.exe */
#define main test_main
#include "test_guvenlik.c"
#undef main

int main(void)
{
    const uint8_t ilk = gvn_test_maske;              /* reset_all() sifirlamadan ONCE oku */
    int g9 = 0, g10 = 0;

    printf("zorla testi (GVN_TEST_MASKE_ILK=0x%02X)\n", (unsigned)GVN_TEST_MASKE_ILK);
    BASLIK("acilis degeri");
    CHECK(ilk == 0x40u, "gvn_test_maske acilista 0x40 olmali: 0x%02X", ilk);

    BASLIK("CAN YOK: 0,2 sn'de haberlesme hatasi (5 sn beklemeden)");
    reset_all(0); gvn_test_maske = ilk;
    B.st1 = B.cv = B.ct = B.at = B.sw = 0;
    sim_ms(200);
    CHECK(VP_POPUP == 9 || VP_POPUP == 10, "pop-up kural 7 karesi (9/10) olmali: %d", VP_POPUP);
    CHECK(VP_SERIT == GVN_SERIT_VERIYOK, "serit 'BMS verisi alinamiyor' (6) olmali: %d", VP_SERIT);
    CHECK(mini_hepsi(GVN_MINI_GIZLI), "veri yokken rakamlar gizli kalmali");
    CHECK(Guvenlik_Aktif_Maske() == 0 && log_say(0, 0) == 0, "gercek olay yok, olay kaydi yok: maske=%02X kayit=%d",
          Guvenlik_Aktif_Maske(), log_say(0, 0));     /* (Init'in yazdigi flash basligi olay degil) */
    for (int i = 0; i < 20; i++) { sim_ms(100); if (VP_POPUP == 9) g9 = 1; if (VP_POPUP == 10) g10 = 1; }
    CHECK(g9 && g10, "pop-up yanip sonmeli (9 ve 10 ikisi de gorulmeli)");

    BASLIK("BMS SAGLIKLI: yine haberlesme hatasi, altta canli bilgi");
    reset_all(0); gvn_test_maske = ilk;              /* reset_all BMS'i saglikli kurar */
    sim_s(3);
    CHECK(VP_POPUP == 9 || VP_POPUP == 10, "BMS saglikliyken de pop-up 9/10: %d", VP_POPUP);
    CHECK(VP_SERIT == GVN_SERIT_BILGI && mini_hepsi(GVN_MINI_ACIK), "serit BILGI + rakamlar gorunur: serit=%d", VP_SERIT);
    CHECK(Guvenlik_Aktif_Maske() == 0 && log_say(0, 0) == 0, "zorla gosterim olay sayilmaz, kayit yok");
    CHECK(Guvenlik_Buzzer_Aktif(), "buzzer kural 7 deseninde olmali");

    BASLIK("maske 0 -> normale doner");
    gvn_test_maske = 0; sim_s(1);
    CHECK(VP_POPUP == 0 && VP_SERIT == GVN_SERIT_NORMAL && mini_hepsi(GVN_MINI_GIZLI),
          "normal ekran: popup=%d serit=%d", VP_POPUP, VP_SERIT);
    CHECK(!Guvenlik_Buzzer_Aktif() && g_buz_a == 0, "buzzer susmali");
    CHECK(g_bad_frames == 0 && g_fl_viol == 0 && g_buz_mismatch == 0, "yan hatalar");

    printf("\n%s: %d kontrol, %d basarisiz\n", g_fail ? "BASARISIZ" : "TUM TESTLER GECTI", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
