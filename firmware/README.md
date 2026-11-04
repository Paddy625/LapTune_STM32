# Gotowy firmware diagnostyczny

[Pobierz LapTune-2.1.1-7402645.zip](LapTune-2.1.1-7402645.zip) dla **NUCLEO-F103RB** i rozpakuj.
Archiwum zawiera HEX, manifest i instrukcję po polsku. Programuj HEX przez
ST-LINK w STM32CubeProgrammer z weryfikacją, po czym uruchom przez RESET.
Użyj kasowania wymaganych stron; końcowe 2 KB flash przechowują licznik sesji.

Obraz **2.1.1 / Release** zbudowano z czystych źródeł commita
`74026455b083e9210e54fce0a91b2de84b7918e3`. Commit dodający archiwum do repozytorium jest osobny;
nie zmienia źródeł tego obrazu. Lokalny build nowszego main może mieć nowszy
SHA, ale powinien zgłaszać 2.1.1 i zawierać pola diagnostyczne odbiornika.

Po około 30 s pracy zatrzymaj zapis B1 i zaczekaj na SD SAVED. W najnowszym
SESSION.json sprawdź firmware_version=2.1.1 oraz obecność gps_receiver_seen,
gps_sats_with_cno i gps_antenna_status. Dopiero ten plik zawiera diagnostykę
sygnału, anteny oraz trybu GPS opisaną w [GPS_DIAGNOSTICS.md](../docs/GPS_DIAGNOSTICS.md).
CubeIDE Run/Debug może ponownie zaprogramować stary ELF z konfiguracji uruchomienia.

Zweryfikowano checksum wszystkich rekordów HEX, zgodność z obrazem BIN,
wektory startowe, zakres pamięci poza dwiema stronami licznika sesji,
wersję/SHA osadzone w obrazie oraz integralność ZIP. Nie podłączono fizycznej
płytki do tej sesji. Archiwum nie zawiera logów użytkownika.

SHA-256 ZIP: `590e0747027f4e31d8a60d1710de38b3b6e300530c384cdeb3bbec8f705a5a93`. Lista kontrolna: [SHA256SUMS](SHA256SUMS).
