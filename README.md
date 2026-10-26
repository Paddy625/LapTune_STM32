# LapTune_STM32
LapTune code development

IMU **KAmodLSM6DS3** (zastępuje LIS3DH): [połączenia, akcelerometr i żyroskop](docs/IMU_LSM6DS3.md).

Obsługa wyświetlacza **ST7735S 160×128**: [schemat połączeń, kod i uruchomienie](docs/TFT_ST7735S.md).

GPS **u-blox NEO-6M / GY-NEO6MV2**: [połączenia UART, pozycja, wysokość MSL i data/czas UTC](docs/GPS_NEO6M.md).

Logger **microSD na SPI1 wspólnym z TFT**: [połączenia, format TXT i zatrzymanie przez B1](docs/SD_LOGGING.md).

IMU parameters:
	- Maximum acceleration +/- 16g
	- Accelerometer and gyroscope ODR 104Hz; FIFO used for acquisition and SD logging
	- Acceleration sensitivity 0.488mg/LSB
	- Angular velocity range +/- 2000 degrees/s, sensitivity 0.070 degrees/s/LSB
	- TFT shows AX/GX, AY/GY, AZ/GZ with MIN/NOW/MAX; GPS view unchanged
