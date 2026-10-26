# LapTune_STM32
LapTune code development

IMU **KAmodLSM6DS3** (zastępuje LIS3DH): [połączenia, akcelerometr i żyroskop](docs/IMU_LSM6DS3.md).

Obsługa wyświetlacza **ST7735S 160×128**: [schemat połączeń, kod i uruchomienie](docs/TFT_ST7735S.md).

GPS **u-blox NEO-6M / GY-NEO6MV2**: [połączenia UART, pozycja, wysokość MSL i data/czas UTC](docs/GPS_NEO6M.md).

IMU parameters:
	- Maximum acceleration +/- 16g
	- Accelerometer and gyroscope ODR 104Hz; main loop polls approximately 100Hz
	- Acceleration sensitivity 0.488mg/LSB
	- Angular velocity range +/- 2000 degrees/s, sensitivity 0.070 degrees/s/LSB
	- Six axes read; TFT continues to show acceleration and GPS
