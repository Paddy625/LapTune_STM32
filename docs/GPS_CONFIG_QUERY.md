# Konfiguracja GPS i test maski UART — 2.1.8

## Wgranie i zebranie wyników

Połączenia: **PB10/TX USART3 → RX GPS**, **TX GPS → PB11/RX USART3**, wspólna masa.
UART pozostaje 9600 baud. Pobierz main, Clean/Build, wgraj; TFT: LAPTUNE 2.1.8.
Uruchom z kartą SD na zewnątrz na **15–20 minut**. B1, poczekaj na SD SAVED,
prześlij **GPS_CFG.TXT oraz SESSION.json** z tego samego folderu sesji.

Raport ma 150 etapów: 90 dla konfiguracji/testu i 60 dla 20 serii obserwacji.
`CONFIG_READ_COMPLETE` oznacza zakończenie pierwszych 90 etapów, a końcowe
`COMPLETE` zakończenie całości. Nie oznacza to poprawnego fixa ani uzyskania
wszystkich odpowiedzi — sprawdź status każdego odczytu. Przerwanie testu wcześniej
pozostawia użyteczny częściowy raport. Odczyt konfiguracji może potrwać kilka minut,
zwłaszcza gdy funkcje niedostępne w NEO-6M nie odpowiadają na zapytania.

## Jedyna zmiana w odbiorniku

1. MON-VER i `UART1-BEFORE`: odczyt oryginalnych 20 bajtów CFG-PRT.
2. NAV-STATUS/SVINFO/MON-HW przed zmianą, jako wczesny punkt odniesienia.
3. `UART1-MASK-SET`: tylko jeśli prawidłowa odpowiedź dotyczy UART1/9600 baud
   i maski wyjściowej 0x0007. Kopiujemy cały payload, zmieniamy wyłącznie
   bajty 14–15 na `03 00` i wysyłamy CFG-PRT dokładnie raz na start MCU.
4. `UART1-VERIFY`: odczyt i porównanie maski 0x0003 oraz pozostałych 18 bajtów
   z oryginałem. ACK samo nie wystarcza. Zmiana baudrate, trybu portu, maski
   wejściowej czy dowolnego innego bajtu przez odbiornik daje VERIFY_FAILED.

Jeśli maska już wynosi 0x0003, zapis jest pomijany. Inna wartość, brak odpowiedzi,
NAK lub niezgodna długość bazowego odczytu również nie uprawniają do zapisu.
Timeout/błąd nadawania/NAK podczas setteru nie powoduje ponowienia setteru.
Odczyt kontrolny następuje także po braku ACK, gdyż ACK może zaginąć.

Nie wysyłamy CFG-CFG, CFG-NVS, CFG-RST ani innych setterów. Zmiana jest bieżąca,
bez polecenia utrwalenia. Reset/utrata zasilania odbiornika może ją cofnąć;
stan zachowany przez podtrzymanie modułu zależy od jego zasilania. Restart samego
STM32 przy masce już 0x0003 nie powoduje dodatkowego zapisu do GPS.

`mask_change` w każdym rekordzie opisuje stan testu:

- NO_BASELINE — brak poprawnego odczytu bazowego;
- READY — odczytano 0x0007, można wykonać test;
- ALREADY_0003 — maska już poprawna, setter pominięty;
- UNEXPECTED_BASELINE — inna maska, setter pominięty;
- SET_SENT_UNVERIFIED / ACKED_UNVERIFIED — próba zapisu / ACK, jeszcze bez potwierdzenia;
- SET_REJECTED — NAK setteru;
- VERIFIED — odczyt potwierdza 0x0003 i zachowanie pozostałych bajtów;
- VERIFY_FAILED — odczyt wykazał niezgodność.

## Zakres pełnego przeglądu publicznej konfiguracji

Podstawa: **GPS.G6-SW-10018-F**, rozdział 31 i tabele wiadomości. Lista w
`Core/Inc/gps_queries.inc` jest zamknięta — nie skanujemy nieznanych ID komend.

| Grupa | Odczyty |
|---|---|
| Nawigacja | CFG-NAV5, CFG-NAVX5, CFG-DAT, CFG-SBAS, CFG-NMEA |
| Energia | CFG-RXM, CFG-FXN, CFG-PM, CFG-PM2 |
| Antena / zakłócenia | CFG-ANT, próba pustego odczytu CFG-ITFM |
| Czas | CFG-RATE, CFG-TP, CFG-TP5 dla wyjść 0 i 1, CFG-TMODE, CFG-TMODE2 |
| Porty | CFG-PRT: DDC=0, UART1=1, UART2=2, USB=3, SPI=4; CFG-USB |
| Informacje | CFG-INF dla UBX=0 i NMEA=1; CFG-RINV |
| Wariant ADR | CFG-EKF, próba pustego odczytu CFG-ESFGWT |
| Częstotliwości wiadomości | 55 zapytań CFG-MSG: wszystkie identyfikatory NMEA/PUBX i wyjściowe UBX z poniższej listy |

CFG-MSG: NMEA GGA/GLL/GSA/GSV/RMC/VTG/GRS/GST/ZDA/GBS/DTM/THS/GPQ/TXT;
PUBX 00/03/04/05/06/40/41; ESF-MEAS/STATUS; MON-HW2/HW/IO/MSGPP/RXBUF/RXR/TXBUF/VER;
NAV-AOPSTATUS/CLOCK/DGPS/DOP/EKFSTATUS/POSECEF/POSLLH/SBAS/SOL/STATUS/SVINFO/TIMEGPS/TIMEUTC/VELECEF/VELNED;
RXM-ALM/EPH/RAW/SFRB/SVSI; TIM-SVIN/TM2/TP/VRFY.
INF ma własny CFG-INF. Część identyfikatorów wyjściowych jest wyłącznie odpytywana
albo dostępna tylko w specjalnych wariantach odbiornika: CFG-MSG może zwrócić NAK.
W odpowiedzi 8-bajtowej zachowujemy wszystkie stawki portów, nie tylko UART1.

CFG-ITFM i CFG-ESFGWT nie mają osobno wyszczególnionej formy Poll Request
w tej edycji dokumentacji. Wysyłamy jedynie pustą próbę odczytu, nigdy payload
konfigurujący. Brak odpowiedzi jest raportowany, a nie zastępowany zerami.

To **cały publiczny zakres konfiguracji opisany w tej edycji protokołu**, nie
zrzut prywatnej pamięci odbiornika. Odpowiedzi NAK/TIMEOUT oznaczają lukę w
odczycie, a nie potwierdzenie ustawień domyślnych. Nie odczytujemy nieudokumentowanych
ustawień fabrycznych, kopii konfiguracji w każdym nośniku pamięci ani całej bazy
efemeryd/almanachu (to dane nawigacyjne, nie ustawienia). Polecenia CFG-CFG,
CFG-NVS i CFG-RST nie mają bezpiecznej funkcji odczytu stanu i nie są wysyłane.

## Obserwacja po zmianie

Po przeglądzie konfiguracji wykonujemy **20 serii** NAV-STATUS, NAV-SVINFO, MON-HW.
Między seriami jest co najmniej 30 sekund; timeouty i zapis SD wydłużają odstępy.
Każdy wynik ma `t_ms` w skali MCU i `cycle=1..20`; `cycle=0` oznacza etap początkowy.
To ponad 10 minut obserwacji po przeglądzie. Nie ma kolejnych zmian konfiguracji
ani restartów podczas oczekiwania na pozycję. Po ostatniej serii nadawanie ustaje.

## Interpretacja i wiarygodność zapisu

BEGIN zawiera numer/łączną liczbę etapów, czas, nazwę, liczbę prób i status.
`tx=` to ostatnia próbowana ramka nadania; przy TX_ERROR nie stanowi dowodu
wysłania wszystkich bajtów. `raw=` to odpowiedź z prawidłową sumą kontrolną.
Odpowiedzi zachowujemy w HEX, także dla pól bez skróconego dekodera tekstowego.
`END` zamyka rekord. Niepełny zapis lub powtórzony numer po błędzie karty trzeba
interpretować jako próbę; używaj późniejszego kompletnego rekordu.

Statusy: OK, TIMEOUT, NAK, MALFORMED, TRUNCATED, TX_ERROR, SKIPPED,
VERIFY_FAILED. SKIPPED dotyczy niewykonanego setteru; powód określa mask_change.
Błędne sumy nie kończą zapytania i zwiększają licznik w SESSION.json.
Odczyty mają maksymalnie dwie próby po 1,5 s. Setter najwyżej jedną próbę.

Bufor odpowiedzi ma nadal 400 bajtów (pełne NAV-SVINFO dla 32 kanałów to 392).
Odpowiedź większa jest jawnie TRUNCATED: oryginalna długość, pierwsze 400 bajtów,
bajty otrzymanej sumy i oznaczenie ucięcia. Taka ramka nie jest kompletna do odtworzenia.
Wszystkie stałe struktury CFG w tabeli mieszczą się w buforze; wyjątkowo długa
odpowiedź MON-VER może nie zmieścić się w całości.

Wynik i ramka TX są zamrożone do udanego zamknięcia raportu. GPS_Process nie
wywołuje FatFs. Główna pętla zapisuje raport poza zapisem pomiarów, przy surowej
kolejce IMU <=100 rekordów; przerwania/DMA i odbiór NMEA pracują dalej.
Błąd SD zachowuje wynik i ponawia zapis po 2 s. Bez SD kolejne etapy czekają,
więc również setter czeka, aż wcześniejsze wyniki znajdą się na karcie.

ARM Debug: statyczny RAM 17200 B, wolne 3280 B; rezerwacja stosu 2560 B,
minimum sterty 512 B, dodatkowy margines 208 B. Bufory IMU/SD nie zwiększyły się.
Monitoruj stack_free_min_bytes i liczniki strat; budżet linkera nie zastępuje
pomiaru stosu na sprzęcie. Testy sprawdzają dokładne bajty jedynego setteru,
pełne porównanie odczytu, utratę ACK, NAK, niezgodny stan początkowy, błędy UART/SD,
niezmienność bufora podczas zapisu, zakres CFG, odstępy 20 serii i koniec nadawania.
