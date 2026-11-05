# Odczyt konfiguracji GPS przez logger — 2.1.7

Podłącz przy wyłączonym zasilaniu **PB10 / TX USART3 Nucleo → RX GPS**.
Zostaw **TX GPS → PB11 / RX USART3** oraz wspólną masę. Bez przewodu PB10 → RX
odbiornik nadal może nadawać NMEA, ale nie otrzyma zapytań i raport pokaże TIMEOUT.
Nie potrzeba u-center ani adaptera. UART pozostaje 9600 baud.

Wgraj `main` przez zwykły Clean/Build; TFT musi pokazać LAPTUNE 2.1.7.
Z kartą SD poczekaj około 2 minut, następnie B1 i SD SAVED. Prześlij
**GPS_CFG.TXT i SESSION.json z tego samego folderu sesji**. Nie trzeba czekać
na uzyskanie pozycji. Pliki pomiarowe LOGnnnn.TXT nadal powstają normalnie.

## Co jest wysyłane

Wyłącznie 21 zapytań UBX (poll):

- MON-VER; CFG-PRT dla UART1; CFG-RATE; CFG-NAV5; CFG-RXM; CFG-TP; CFG-ANT.
- CFG-MSG dla GGA, GLL, GSA, GSV, RMC, VTG, NAV-POSLLH, NAV-VELNED,
  NAV-SOL, NAV-TIMEUTC i TIM-TP.
- NAV-STATUS, NAV-SVINFO, MON-HW.

Nadajnik korzysta z zamkniętej listy zapytań z payloadem 0, 1 lub 2 bajty.
W kodzie GPS nie ma CFG-CFG, CFG-RST ani setterów CFG-PRT/RATE/MSG/TP.
Stara rezerwacja przywrócenia w STM32 nie uruchamia niczego w tej wersji.
Nie zmieniamy modelu nawigacji, częstotliwości pomiarów, ustawień PPS ani
pamięci konfiguracji modułu. Poll nie włącza cyklicznego nadawania wiadomości.

Pierwsze zapytanie jest wysyłane po 3 s. Każde ma do dwóch prób i 1,5 s
oczekiwania na odpowiedź. Pomiędzy zapisanymi wynikami jest co najmniej 250 ms.
Po zapisaniu ostatniego wyniku nie wysyłamy kolejnych zapytań do restartu STM32.
Brak karty nie blokuje IMU ani odbioru GPS: pierwszy wynik czeka w RAM na zapis,
a kolejne zapytania są wstrzymane. Po włożeniu karty seria jest kontynuowana.
Odczyt jest migawką z czasu zapytań, nie ciągłą diagnostyką przez całą sesję.

## Format raportu

Każdy rekord ma BEGIN z numerem, czasem w ms od startu TIM2, nazwą zapytania,
liczbą prób i wynikiem; następnie surową ramkę UBX w HEX, dekodowanie i END.
BEGIN bez odpowiadającego END jest niekompletnym zapisem. Ponowienie po błędzie
karty może dopisać ten sam numer ponownie — pełny późniejszy rekord jest ważny.
Po ostatnim rekordzie pojawia się COMPLETE. Oznacza ukończenie prób, nie
sukces wszystkich odczytów ani poprawny fix.

- **OK**: pasująca odpowiedź z poprawną sumą i akceptowaną długością.
- **TIMEOUT**: brak pasującej odpowiedzi w dwóch próbach. Nie oznacza ustawienia
  równego zero. Przyczyną może być odłączony TX, wyłączony protokół UBX,
  inny baudrate lub nieobsługiwana wiadomość; samo milczenie ich nie rozróżnia.
- **NAK**: odbiornik jawnie odrzucił zapytanie.
- **MALFORMED**: zgodna suma, ale nieprawidłowa długość/struktura odpowiedzi.
- **TRUNCATED**: odpowiedź większa niż 400 bajtów; raport zawiera pierwsze 400,
  oryginalną długość i otrzymane bajty sumy, z oznaczeniem pominiętej części.
  Nie należy traktować jej jako kompletnej ramki do ponownego wysłania.
- **TX_ERROR**: HAL nie rozpoczął transmisji albo nie zakończył jej w 100 ms.

Odpowiedzi o błędnej sumie są odrzucane; liczniki sum i UART pozostają w SESSION.
Niepasujące odpowiedzi nie kończą bieżącego zapytania. ACK nie zastępuje odczytu.
400 bajtów mieści pełne NAV-SVINFO z 32 kanałami (392 bajty); wyjątkowo długie
rozszerzenia MON-VER mogą być oznaczone TRUNCATED. Dodatkowe pola zachowujemy
w HEX, nawet jeżeli skrócone dekodowanie tekstowe ich nie wypisuje.

CFG-ANT opisuje konfigurację obsługi anteny; bieżący stan diagnostyczny
pochodzi z MON-HW i zależy od możliwości modułu. PPS nie jest włączany:
CFG-TP tylko odczytuje to, co odbiornik ma ustawione.

## Współpraca z loggerem i pamięć

Parser przechowuje jedną odpowiedź i zamraża ją do udanego zamknięcia raportu.
Bufor nadawania jest statyczny i pozostaje ważny do zakończenia transmisji IT.
GPS_Process i przerwania nigdy nie wywołują FatFs. Raport zapisuje główna
pętla poza wywołaniami zapisu pomiarów; podczas operacji SD UART jest nadal
opróżniany. Zapis diagnostyki czeka, gdy surowy bufor IMU ma ponad 100 rekordów.
Błędy otwarcia, zapisu lub zamknięcia raportu zachowują wynik i uruchamiają
ponowną próbę zapisu po 2 s. Długi przestój karty nadal podlega ograniczeniom
istniejących buforów IMU/SD, a ich liczniki strat pozostają aktywne.

ARM Debug: statyczny RAM 17152 B, wolne 3328 B; rezerwacja stosu 2560 B,
minimum sterty 512 B, dodatkowy margines 256 B. Bez zwiększania buforów IMU/SD.
To budżet linkera; rzeczywiste maksimum stosu sprawdza stack_free_min_bytes.
Poprzednia sesja miała tylko 92 B niewykorzystanej części rezerwacji stosu,
więc raport używa małych fragmentów tekstu, nie dużego bufora na stosie.

Testy hosta sprawdzają dozwolone ramki TX, brak resetów i setterów, brak
odpowiedzi/NAK, nieprawidłowe długości i sumy, 32 kanały SVINFO, zamrożenie
odpowiedzi podczas zapisu SD, błędy transmisji/zapisu i przepełnienie czasu ms.
Wynik testów programowych nie potwierdza ustawień fizycznego odbiornika.
