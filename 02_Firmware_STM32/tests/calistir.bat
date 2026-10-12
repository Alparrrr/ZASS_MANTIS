@echo off
rem ZASS MANTIS firmware PC testleri - zig cc ile derler (pip install ziglang).
rem Kaynaklar dogrudan ..\ZASS_MANTIS_115\Core altindan derlenir (kopya yok).
rem Dort konfigurasyon: varsayilan / kural 1 = VEYA / MOS sicakligi dahil / acilis ek suresi.
rem Besinci: GVN_TEST_MASKE_ILK=0x40 (haberlesme hatasi ekrani acilistan itibaren zorla).
rem Altinci: bms_veri.c + saat.c + Task_SDCard.c (UART6 paketi, CSV, saat, SD akisi; sahte FatFs).
setlocal
cd /d "%~dp0"
set ZIG=python -m ziglang cc
if exist "%~dp0zigenv\Scripts\python.exe" set ZIG="%~dp0zigenv\Scripts\python.exe" -m ziglang cc
set FLAGS=-std=gnu11 -O1 -g -Wall -Wextra -Wshadow -Wno-unused-function -I fake_hal -I ..\ZASS_MANTIS_115\Core\Inc

%ZIG% %FLAGS% test_guvenlik.c -o t_varsayilan.exe || exit /b 1
.\t_varsayilan.exe || exit /b 1
%ZIG% %FLAGS% -DGVN_R1_VEYA=1 test_guvenlik.c -o t_veya.exe || exit /b 1
.\t_veya.exe || exit /b 1
%ZIG% %FLAGS% -DGVN_MOS_DAHIL=1 test_guvenlik.c -o t_mos.exe || exit /b 1
.\t_mos.exe || exit /b 1
%ZIG% %FLAGS% -DGVN_R7_ILK_EK_MS=8000u test_guvenlik.c -o t_ilk.exe || exit /b 1
.\t_ilk.exe || exit /b 1
%ZIG% %FLAGS% -DGVN_LOG_SADECE_OLAY=0 test_guvenlik.c -o t_ayrintili.exe || exit /b 1
.\t_ayrintili.exe || exit /b 1
%ZIG% %FLAGS% -DGVN_TEST_MASKE_ILK=0x40u test_zorla.c -o t_zorla.exe || exit /b 1
.\t_zorla.exe || exit /b 1
%ZIG% %FLAGS% -Wno-date-time -DSAAT_PC_TEST -I fake_sd -I ..\ZASS_MANTIS_115\Core\Inc test_kayit.c -o t_kayit.exe || exit /b 1
.\t_kayit.exe || exit /b 1
rem Yedinci: ornek senaryo (CAN 25S -> UART 8S); SD dosyalarini ornek_sd\ klasorune yazar
%ZIG% %FLAGS% -Wno-date-time -DSAAT_PC_TEST -I fake_sd -I ..\ZASS_MANTIS_115\Core\Inc ornek_senaryo.c -o t_senaryo.exe || exit /b 1
.\t_senaryo.exe || exit /b 1
echo.
echo tum testler gecti
