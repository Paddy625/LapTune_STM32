# Jednorazowe przywrócenie GPS — 2.1.5

Ta wersja wykonuje przy pierwszym uruchomieniu **jedną próbę** przywrócenia
konfiguracji NEO-6 i zimnego startu. To test hipotezy o pozostawionym stanie
odbiornika. Nie jest potwierdzoną na sprzęcie naprawą odbioru satelitów.
ACC, żyroskop, TFT i zapis SD pracują podczas wyszukiwania pozycji.

## Wgranie i próba

1. Pobierz `main`, wykonaj Clean/Build i wgraj program zwykłym sposobem.
   Nagłówek TFT musi pokazać **LAPTUNE 2.1.5**.
2. Przy pierwszym uruchomieniu pojawi się na krótko `GPS RESTORE 1/3`,
   `2/3`, `3/3`, następnie zwykły status GPS. Poszczególne etapy mogą
   być za krótkie, aby zauważyć każdy z nich.
3. Pozostaw urządzenie zasilone przez 15 minut. Po zimnym starcie
   odbiornik musi ponownie uzyskać dane nawigacyjne; 15 minut to okno
   testowe, nie gwarantowany czas fixa. Firmware nie przerywa wyszukiwania.
4. Zatrzymaj zapis B1, zaczekaj na `SD SAVED` i zachowaj `SESSION.json`
   oraz ostatni TXT. Dane pozwolą sprawdzić wykonane etapy i odbiór RF.

## Dokładna sekwencja

- Rozpoznanie UART przez odpowiedź CFG-PRT przy 9600 lub 38400 baud.
- **UBX-CFG-CFG (06/09)**, payload 13 B:
  `FF FF 00 00 00 00 00 00 FF FF 00 00 17`.
  `clearMask=loadMask=0x0000FFFF`, `saveMask=0`.
  Przywrócenie domyślnej konfiguracji i jej załadowanie; wybrane nośniki
  konfiguracji to BBR/Flash/EEPROM/SPI Flash, o ile odbiornik je obsługuje
  (`deviceMask=0x17`). Nie zapisujemy bieżącej konfiguracji loggera.
- Nieblokujące oczekiwanie 1,5 s na zakończenie operacji; ponowne rozpoznanie
  UART, ponieważ przywrócenie ustawień może zmienić baudrate.
- **UBX-CFG-RST (06/04)**, payload `FF FF 01 00`:
  `navBbrMask=0xFFFF` oznacza zimny start, `resetMode=1` kontrolowany
  reset programowy odbiornika. To nie jest wcześniejszy gorący restart
  `00 00 02 00`, który zachowywał dane nawigacyjne.
- Nieblokujące oczekiwanie 1,5 s, ponowne rozpoznanie UART i normalna
  konfiguracja GGA/RMC 5 Hz / 38400, sprawdzana przez ACK i odczyt ustawień.
  Po pierwszym fixie uruchamiane są epoki UBX oraz konfiguracja PPS na PA0.

Brak ACK po zmianie portu/resetowaniu nie powoduje ponownego wysłania
polecenia. Jawny NAK, błąd UART lub brak odpowiedzi podczas rozpoznawania
kończą próbę błędem; zwykła obsługa GPS może dalej odzyskać komunikację,
ale nie powtarza czyszczenia ani zimnego startu.

Formaty poleceń porównano z publicznymi implementacjami
[SparkFun — factoryDefault/factoryReset/hardReset](https://github.com/sparkfun/SparkFun_u-blox_GNSS_Arduino_Library/blob/main/src/SparkFun_u-blox_GNSS_Arduino_Library.cpp)
i [pyubx2 — CFG-RST](https://github.com/semuconsulting/pyubx2/blob/master/src/pyubx2/ubxtypes_set.py).
Zastosowano reset kontrolowany (`1`), nie sprzętowy (`0`).

## Dlaczego nie powtarza się po odłączeniu zasilania

Rezerwacja próby jest zapisywana **przed uruchomieniem zegara akwizycji**
w istniejącym dzienniku startów Nucleo (ostatnie dwa zarezerwowane sektory
Flash po 1 KB). Dotychczas nieużywany półwyraz rekordu, offset 12, otrzymuje
`0x4752`. Znacznik zatwierdzenia całego rekordu nadal zapisywany jest ostatni.
Przy kolejnych startach i rotacji stron znacznik jest przenoszony dalej.
Numeracja sesji i ochrona istniejących plików pozostają zachowane.

Znacznik oznacza **zarezerwowaną próbę, nie potwierdzony sukces GPS**.
Awaria zasilania po zatwierdzeniu rezerwacji, lecz przed wysłaniem poleceń,
lub odłączony GPS mogą spowodować pominięcie operacji. Automatyczne
ponawianie zostało celowo wykluczone, aby nie tworzyć pętli zimnych startów.
Nie kasuj całego MCU przy zwykłym ponownym wgrywaniu: mass erase usuwa
również znacznik i ponownie uzbroi procedurę. Stare firmware nie przenosi
znacznika przy rotacji dziennika; długotrwały downgrade może go usunąć.

## SESSION.json

`gps_restore_state` dotyczy bieżącego uruchomienia:

| Wartość | Znaczenie |
| --- | --- |
| 0 | Nie zlecono próby podczas tego startu; zwykle rezerwacja była już zużyta |
| 1 | Oczekiwanie na rozpoznanie GPS |
| 2 | Rozpoczęto wysyłanie przywrócenia ustawień |
| 3 | Rozpoczęto wysyłanie zimnego startu |
| 4 | Po obu poleceniach ponownie zweryfikowano konfigurację GGA/RMC i 5 Hz |
| 5 | Próba zakończona błędem; kod w `gps_restore_error` |

Kod błędu: 1=brak odpowiedzi, 2=NAK, 3=niezgodny odczyt, 4=UART.
`gps_restore_ack` jest maską odebranych ACK: 1=CFG-CFG, 2=CFG-RST.
Zero może być prawidłowe przy zmianie portu/resetowaniu. Stan 4 nie dowodzi
przywrócenia każdego ustawienia fabrycznego, wyczyszczenia pamięci nawigacyjnej
ani uzyskania fixa — końcowe 5 Hz ma odczyt potwierdzający, sam cold start
nie ma niezależnego potwierdzenia wykonania.

`gps_recovery_attempts` od 2.1.5 liczy wyłącznie próbę wysłania tego
jednorazowego cold startu (0 lub 1), a `gps_recovery_us` jej czas w skali MCU.
Nie jest licznikiem automatycznych restartów po NO FIX.
Format pomiarów TXT i `format_version=4` pozostają bez zmian.

## Weryfikacja

Testy hosta obejmują migrację starego dziennika, rotację stron, przerwanie
każdego zapisu Flash, restart po rezerwacji, obie prędkości UART, zmianę
baudrate po przywróceniu ustawień, brak ACK, NAK, błąd nadawania, odłączony
odbiornik, przepełnienie licznika ms i 15 minut NO FIX bez ponawiania.
Testy protokołu nie zastępują próby odbioru satelitów na fizycznym module.

Pełne testy hosta przeszły z ASan/UBSan i `-Werror`; kompilacje ARM Debug
i Release również. Statyczny RAM w obu konfiguracjach: 17136 B; pozostaje
3344 B na stos/stertę, przy rezerwacjach 2560/512 B (272 B ponad rezerwacje).
Nie zwiększono buforów danych. To wynik linkera, nie pomiar szczytowego
zużycia stosu na płytce; bieżący pomiar zapisuje `stack_free_min_bytes`.
