#include <SoapySDR/Device.h>
#include <SoapySDR/Formats.h>
#include <stdio.h> //printf
#include <stdlib.h> //free
#include <stdint.h>
#include <string.h> //strncpy
#include <unistd.h> //sleep

typedef struct {
    int mode; // 0 - Tx, 1 - Rx
    char uri[128];
    double tx_freq;
    double rx_freq;
    double rate;
    double tx_gain;
    double rx_gain;
} AppConfig;

int main(int argc, char **argv)
{
    // ./main 0 - плата "Tx"
    // ./main 1 - плата "Rx"
    if (argc < 2)
    {
        printf("Необходимо указать аргумент: 1 - Rx, 0 - Tx\n");
        return 0;
    }

    AppConfig Config;
    if (atoi(argv[1]) == 0)
    {
        Config.mode    = 0;
        Config.rx_freq = 750e6;
        Config.tx_freq = 800e6;
    }
    else
    {
        Config.mode    = 1;
        Config.rx_freq = 800e6;
        Config.tx_freq = 750e6;
    }
    Config.rate    = 1e6;
    Config.tx_gain = -40.0;
    Config.rx_gain = 30.0;
    strncpy(Config.uri, (argc >= 3) ? argv[2] : "usb:0.1.5", sizeof(Config.uri) - 1);
    Config.uri[sizeof(Config.uri) - 1] = '\0';

    printf("Режим: %s, uri: %s, RX %.1f МГц, TX %.1f МГц\n",
           Config.mode == 0 ? "Tx" : "Rx", Config.uri, Config.rx_freq / 1e6, Config.tx_freq / 1e6);

    SoapySDRKwargs args = {};
    SoapySDRKwargs_set(&args, "driver", "plutosdr");
    if (1) {
        SoapySDRKwargs_set(&args, "uri", Config.uri);
    } else {
        SoapySDRKwargs_set(&args, "uri", "ip:192.168.2.1");
    }
    SoapySDRKwargs_set(&args, "direct", "1");
    SoapySDRKwargs_set(&args, "timestamp_every", "1920");
    SoapySDRKwargs_set(&args, "loopback", "0");
    SoapySDRDevice *sdr = SoapySDRDevice_make(&args);
    SoapySDRKwargs_clear(&args);

    if (sdr == NULL)
    {
        printf("SoapySDRDevice_make fail: %s\n", SoapySDRDevice_lastError());
        return EXIT_FAILURE;
    }

    sleep(1);

    if (SoapySDRDevice_setSampleRate(sdr, SOAPY_SDR_RX, 0, Config.rate) != 0)
    {
        printf("setSampleRate rx fail: %s\n", SoapySDRDevice_lastError());
    }
    if (SoapySDRDevice_setFrequency(sdr, SOAPY_SDR_RX, 0, Config.rx_freq, NULL) != 0)
    {
        printf("setFrequency rx fail: %s\n", SoapySDRDevice_lastError());
    }
    if (SoapySDRDevice_setSampleRate(sdr, SOAPY_SDR_TX, 0, Config.rate) != 0)
    {
        printf("setSampleRate tx fail: %s\n", SoapySDRDevice_lastError());
    }
    if (SoapySDRDevice_setFrequency(sdr, SOAPY_SDR_TX, 0, Config.tx_freq, NULL) != 0)
    {
        printf("setFrequency tx fail: %s\n", SoapySDRDevice_lastError());
    }
    printf("SoapySDRDevice_getFrequency tx: %lf\n", SoapySDRDevice_getFrequency(sdr, SOAPY_SDR_TX, 0));

    size_t channels[] = {0};
    size_t channel_count = sizeof(channels) / sizeof(channels[0]);

    SoapySDRStream *rxStream = SoapySDRDevice_setupStream(sdr, SOAPY_SDR_RX, SOAPY_SDR_CS16, channels, channel_count, NULL);
    if (rxStream == NULL)
    {
        printf("setupStream rx fail: %s\n", SoapySDRDevice_lastError());
        SoapySDRDevice_unmake(sdr);
        return EXIT_FAILURE;
    }
    if (SoapySDRDevice_setGain(sdr, SOAPY_SDR_RX, channels[0], Config.rx_gain) != 0) {
        printf("setGain rx fail: %s\n", SoapySDRDevice_lastError());
    }
    if (SoapySDRDevice_setGain(sdr, SOAPY_SDR_TX, channels[0], Config.tx_gain) != 0) {
        printf("setGain tx fail: %s\n", SoapySDRDevice_lastError());
    }
    SoapySDRStream *txStream = SoapySDRDevice_setupStream(sdr, SOAPY_SDR_TX, SOAPY_SDR_CS16, channels, channel_count, NULL);
    if (txStream == NULL)
    {
        printf("setupStream tx fail: %s\n", SoapySDRDevice_lastError());
        SoapySDRDevice_unmake(sdr);
        return EXIT_FAILURE;
    }

    size_t rx_mtu = SoapySDRDevice_getStreamMTU(sdr, rxStream);
    size_t tx_mtu = SoapySDRDevice_getStreamMTU(sdr, txStream);
    printf("MTU - TX: %zu, RX: %zu\n", tx_mtu, rx_mtu);

    int16_t *tx_buff   = (int16_t *)malloc(sizeof(int16_t) * 2 * tx_mtu);
    int16_t *rx_buffer = (int16_t *)malloc(sizeof(int16_t) * 2 * rx_mtu);

    for (size_t i = 0; i < 2 * tx_mtu; i += 2)
    {
        tx_buff[i]     = 2000;
        tx_buff[i + 1] = 2000;
    }

    for (size_t i = 0; i < 2; i++)
    {
        tx_buff[0 + i]  = 0xffff;
        tx_buff[10 + i] = 0xffff;
    }

    SoapySDRDevice_activateStream(sdr, rxStream, 0, 0, 0);
    SoapySDRDevice_activateStream(sdr, txStream, 0, 0, 0);

    printf("Start test...\n");
    const long timeoutUs = 400000;
    long long last_time = 0;

    const char *filename_Rx = "Rxdata.pcm";
    const char *filename_Tx = "Txdata.pcm";
    const char *current_file = (Config.mode == 0) ? filename_Tx : filename_Rx;
    FILE *file = fopen(current_file, "wb");
    if (file == NULL)
    {
        printf("Cannot open %s\n", current_file);
    }

    size_t iteration_count = 10;
    for (size_t buffers_read = 0; buffers_read < iteration_count; buffers_read++)
    {
        void *rx_buffs[] = {rx_buffer};
        int flags = 0;
        long long timeNs = 0;

        int sr = SoapySDRDevice_readStream(sdr, rxStream, rx_buffs, rx_mtu, &flags, &timeNs, timeoutUs);
        if (sr < 0)
        {
            continue;
        }

        printf("Buffer: %zu - Samples: %i, Flags: %i, Time: %lli, TimeDiff: %lli\n",
               buffers_read, sr, flags, timeNs, timeNs - last_time);
        if (file) fwrite(rx_buffer, sizeof(int16_t) * 2, sr, file);
        last_time = timeNs;

        long long tx_time = timeNs + (4 * 1000 * 1000); 

        for (size_t i = 0; i < 8; i++)
        {
            uint8_t tx_time_byte = (tx_time >> (i * 8)) & 0xff;
            tx_buff[2 + i] = tx_time_byte << 4;
        }

        void *tx_buffs[] = {tx_buff};
        if (buffers_read % 2 == 0 && buffers_read > 0)
        {
            printf("buffers_read: %zu\n", buffers_read);
            flags = SOAPY_SDR_HAS_TIME;
            int st = SoapySDRDevice_writeStream(sdr, txStream, (const void *const *)tx_buffs, tx_mtu, &flags, tx_time, timeoutUs);
            if ((size_t)st != tx_mtu)
            {
                printf("TX Failed: %i\n", st);
            }
        }
    }
    if (file) fclose(file);

    SoapySDRDevice_deactivateStream(sdr, rxStream, 0, 0);
    SoapySDRDevice_deactivateStream(sdr, txStream, 0, 0);

    SoapySDRDevice_closeStream(sdr, rxStream);
    SoapySDRDevice_closeStream(sdr, txStream);

    SoapySDRDevice_unmake(sdr);

    printf("test complete!\n");

    free(tx_buff);
    free(rx_buffer);

    return EXIT_SUCCESS;
}