#include <SoapySDR/Device.h>  // Инициализация устройства
#include <SoapySDR/Formats.h>  // Типы данных, используемых для записи сэмплов
#include <complex.h>
#include <stdint.h>
#include <stdio.h>   //printf
#include <stdlib.h>  //free
#include <string.h>  //strcmp
#include <math.h>    // M_PI
#include <signal.h>  // Ctrl+C

// Передатчик
#define SPS 10 // сэмплов на символ
#define TX_AMPLITUDE 2000 // амплитуда символа, максимум 2047 (12 бит ЦАП)
#define TX_HEADER_SAMPLES 6 // первые 6 комплексных сэмплов занимает флаг + timestamp

typedef enum { TX_BPSK, TX_QPSK } tx_mod_t;

static const int16_t h[SPS] = {1, 1, 1, 1, 1, 1, 1, 1, 1, 1};

// Генерация случайных битов
static void generate_bits(uint8_t *bits, size_t num_bits)
{
    for (size_t i = 0; i < num_bits; i++)
        bits[i] = (uint8_t)(rand() % 2);
}

// Преобразование битов в BPSK-символы
static void map_bpsk(const uint8_t *bits, int16_t *buffer, size_t num_symbols)
{
    for (size_t i = 0; i < num_symbols; i++)
    {
        buffer[2 * i] = bits[i] ? -TX_AMPLITUDE : TX_AMPLITUDE;
        buffer[2 * i + 1] = 0;
    }
}

// Преобразование битов в QPSK-символы
static void map_qpsk(const uint8_t *bits, int16_t *buffer, size_t num_symbols)
{
    for (size_t i = 0; i < num_symbols; i++)
    {
        buffer[2 * i] = bits[2 * i] ? -TX_AMPLITUDE : TX_AMPLITUDE;
        buffer[2 * i + 1] = bits[2 * i + 1] ? -TX_AMPLITUDE : TX_AMPLITUDE;
    }
}

// Увеличение частоты дискретизации вставкой нулей
static void upsample_zeros(const int16_t *in_symbols, size_t num_symbols,
                           int16_t *out_buffer, size_t sps)
{
    for (size_t i = 0; i < num_symbols; i++)
    {
        size_t pos = i * sps * 2;
        out_buffer[pos] = in_symbols[2 * i];
        out_buffer[pos + 1] = in_symbols[2 * i + 1];
        for (size_t j = 1; j < sps; j++)
        {
            out_buffer[pos + 2 * j] = 0;
            out_buffer[pos + 2 * j + 1] = 0;
        }
    }
}

// Фильтрация комплексного сигнала сверткой
static void convolution(const int16_t *in_upsampled, int16_t *out_shaped,
                        size_t total_samples)
{
    for (size_t i = 0; i < total_samples; i++)
    {
        int32_t sum_i = 0, sum_q = 0;
        for (size_t j = 0; j < SPS && j <= i; j++)
        {
            sum_i += in_upsampled[2 * (i - j)] * h[j];
            sum_q += in_upsampled[2 * (i - j) + 1] * h[j];
        }
        out_shaped[2 * i] = (int16_t)sum_i;
        out_shaped[2 * i + 1] = (int16_t)sum_q;
    }
}

// Заполняет tx_buff (2*tx_mtu int16) сэмплами, начиная с 6-го сэмпла.
// Заголовок (флаг + время) пишется в run().
static void tx_build_buffer(int16_t *tx_buff, size_t tx_mtu, tx_mod_t mod)
{
    size_t num_symbols = (tx_mtu - TX_HEADER_SAMPLES) / SPS;
    size_t total = num_symbols * SPS;
    size_t bps = (mod == TX_QPSK) ? 2 : 1;  // бит на символ

    uint8_t *bits = (uint8_t *)malloc(num_symbols * bps);
    int16_t *symbols = (int16_t *)malloc(num_symbols * 2 * sizeof(int16_t));
    int16_t *up = (int16_t *)malloc(total * 2 * sizeof(int16_t));
    int16_t *shaped  = (int16_t *)malloc(total * 2 * sizeof(int16_t));

    generate_bits(bits, num_symbols * bps);
    if (mod == TX_QPSK) map_qpsk(bits, symbols, num_symbols);
    else map_bpsk(bits, symbols, num_symbols);

    upsample_zeros(symbols, num_symbols, up, SPS);
    convolution(up, shaped, total);

    // всё после заголовка обнуляем, потом кладём сэмплы со сдвигом на 4 бита (ЦАП 12 бит, left-aligned)
    int16_t *dst = tx_buff + 2 * TX_HEADER_SAMPLES;
    memset(dst, 0, 2 * (tx_mtu - TX_HEADER_SAMPLES) * sizeof(int16_t));
    for (size_t i = 0; i < 2 * total; i++)
        dst[i] = shaped[i] * 16;

    free(bits); free(symbols); free(up); free(shaped);
}

#define SAMPLE_RATE  1e6
#define RX_GAIN 10.0
#define TX_GAIN -40.0
#define PRINT_EVERY 250 // печать статуса раз в 250 буферов (~0.5 с)
#define NOISE_PEAK 20 // пик приёма ниже = на входе только шум
#define MODULATION TX_BPSK // или TX_QPSK

double freq1 = 821e6;
double freq2 = 766e6;

static volatile sig_atomic_t running = 1;
static void on_sigint(int sig) {
  (void)sig;
  running = 0;
  signal(SIGINT, SIG_DFL); // второй Ctrl+C - выход сразу
}

SoapySDRDevice *make_device(const char *uri) {
  SoapySDRKwargs args = {};
  SoapySDRKwargs_set(&args, "driver","plutosdr");
  SoapySDRKwargs_set(&args, "uri", uri);
  SoapySDRKwargs_set(&args, "direct", "1"); 
  SoapySDRKwargs_set(&args, "timestamp_every","1920");  // Размер буфера + временные метки
  SoapySDRKwargs_set(&args, "loopback", "0"); // Используем антенны или нет
  SoapySDRDevice *sdr = SoapySDRDevice_make(&args); // Инициализация
  SoapySDRKwargs_clear(&args);
  return sdr;
}


int run(const char *uri, bool is_tx) {
  SoapySDRDevice *sdr = make_device(uri);
  if (sdr == NULL) {
    printf("SoapySDRDevice_make fail: %s\n", SoapySDRDevice_lastError());
    return 1;
  }

  double rx_freq = freq1;
  double tx_freq = freq2;
  const char *mode = "rx";
  const char *filename = "rxdata.pcm";

  if (is_tx) {
    rx_freq = freq2;
    tx_freq = freq1;
    mode = "tx";
    filename = "txdata.pcm";
  }
  printf("Режим: %s, устройство: %s\n", mode, uri);
  printf("TX %.0f МГц, RX %.0f МГц, %s, %d отсчетов на символ\n",
         tx_freq / 1e6, rx_freq / 1e6, MODULATION == TX_QPSK ? "QPSK" : "BPSK", SPS);

  SoapySDRDevice_setSampleRate(sdr, SOAPY_SDR_RX, 0, SAMPLE_RATE);
  SoapySDRDevice_setSampleRate(sdr, SOAPY_SDR_TX, 0, SAMPLE_RATE);
  SoapySDRDevice_setFrequency(sdr, SOAPY_SDR_RX, 0, rx_freq, NULL);
  SoapySDRDevice_setFrequency(sdr, SOAPY_SDR_TX, 0, tx_freq, NULL);

  size_t channels[] = {0};
  SoapySDRDevice_setGain(sdr, SOAPY_SDR_RX, *channels, RX_GAIN);
  SoapySDRDevice_setGain(sdr, SOAPY_SDR_TX, *channels, TX_GAIN);
  size_t channel_count = sizeof(channels) / sizeof(channels[0]);

  SoapySDRStream *rxStream = SoapySDRDevice_setupStream(sdr, SOAPY_SDR_RX, SOAPY_SDR_CS16, channels, channel_count, NULL);
  SoapySDRStream *txStream = SoapySDRDevice_setupStream(sdr, SOAPY_SDR_TX, SOAPY_SDR_CS16, channels, channel_count, NULL);

  SoapySDRDevice_activateStream(sdr, rxStream, 0, 0, 0);
  SoapySDRDevice_activateStream(sdr, txStream, 0, 0, 0);

  size_t rx_mtu = SoapySDRDevice_getStreamMTU(sdr, rxStream);
  size_t tx_mtu = SoapySDRDevice_getStreamMTU(sdr, txStream);
  printf("MTU - TX: %zu, RX: %zu\n", tx_mtu, rx_mtu);
  int16_t rx_buffer[2 * rx_mtu];
  int16_t tx_buff[2 * tx_mtu];

  const long timeoutUs = 400000;
  long long last_time = 0;
  FILE *file = fopen(filename, "wb");
  size_t buffers_read = 0;

  // Сэмплы передатчика (первые 6 комплексных сэмплов оставлены под заголовок)
  tx_build_buffer(tx_buff, tx_mtu, MODULATION);

  // Заголовок: флаг (FFFF FFFF) + 8 слов времени + флаг. Флаги не меняются, время - в цикле.
  for (size_t i = 0; i < 2; i++) {
    tx_buff[0 + i] = 0xffff;
    tx_buff[10 + i] = 0xffff;
  }

  signal(SIGINT, on_sigint);

  // Перед стартом выбрасываем мусор из RX
  void *rx_buffs[] = {rx_buffer};
  for (size_t n = 0; n < 128 && running; /* in loop */) {
    int flags;
    long long timeNs;
    int sr = SoapySDRDevice_readStream(sdr, rxStream, rx_buffs, rx_mtu, &flags, &timeNs, timeoutUs);
    if (sr < 0) continue; // таймаут или ошибка
    n++;
  }

  printf("Start...\n");
  int peak = 0; // пик амплитуды приёма за период печати
  std::complex<double> rot = 0; // накопитель для оценки сдвига частоты между платами

  while (running) {
    int flags;
    long long timeNs = last_time;
    int sr = SoapySDRDevice_readStream(sdr, rxStream, rx_buffs, rx_mtu, &flags, &timeNs, timeoutUs);
    if (sr < 0) continue;

    if (file != NULL)
      fwrite(rx_buffer, 2 * sizeof(int16_t), sr, file);

    for (int i = 0; i < 2 * sr; i++)
      if (abs(rx_buffer[i]) > peak)
        peak = abs(rx_buffer[i]);

    // Сдвиг частоты между платами - по повороту фазы между соседними сэмплами
    for (int i = 1; i < sr; i++) {
      std::complex<double> cur (rx_buffer[2 * i], rx_buffer[2 * i + 1]);
      std::complex<double> prev(rx_buffer[2 * (i - 1)], rx_buffer[2 * (i - 1) + 1]);
      rot += cur * std::conj(prev);
    }

    if (buffers_read % PRINT_EVERY == 0) {
      double f_khz = std::arg(rot) * SAMPLE_RATE / (2.0 * M_PI) / 1e3;
      printf("Buffer: %zu - TimeDiff: %lli | Прием: пик %4d из 2047, сдвиг ~%+.2f кГц%s\n",
             buffers_read, timeNs - last_time, peak, f_khz,
             peak < NOISE_PEAK ? "  - ШУМ" : "");
      peak = 0;
      rot = 0;
    }
    last_time = timeNs;

    long long tx_time = last_time + (4 * 1000 * 1000); 


    for(size_t i = 0; i < 8; i++)
    {
        uint8_t tx_time_byte = (tx_time >> (i * 8)) & 0xff;
        tx_buff[2 + i] = tx_time_byte << 4;
    }
    
    // отправляем tx_buff массив
    void *tx_buffs[] = {tx_buff};
    flags = SOAPY_SDR_HAS_TIME;
    int st = SoapySDRDevice_writeStream(sdr, txStream, (const void * const*)tx_buffs, tx_mtu, &flags, tx_time, timeoutUs);
    if ((size_t)st != tx_mtu) {
      printf("TX Failed: %i\n", st);
    }

    buffers_read++;
  }

  printf("\nОстановка... Записан %s\n", filename);
  if (file != NULL) fclose(file);
  SoapySDRDevice_deactivateStream(sdr, rxStream, 0, 0);
  SoapySDRDevice_deactivateStream(sdr, txStream, 0, 0);
  SoapySDRDevice_closeStream(sdr, rxStream);
  SoapySDRDevice_closeStream(sdr, txStream);
  SoapySDRDevice_unmake(sdr);
  return 0;
}

int main(int argc, char *argv[]) {
  if (argc < 2) {
    printf("Запуск: %s <rx|tx> [uri]\n", argv[0]);
    return 1;
  }
  const char *uri="usb:0.1.5";
  if (argc > 2) {uri=argv[2];}  
  if (strcmp(argv[1], "rx") == 0){return run(uri, false);}
  else if (strcmp(argv[1], "tx") == 0) {return run(uri, true);} 
  else {
    printf("Ошибка режима работы\n");
    return 1;
  }
}