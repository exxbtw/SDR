#include <SoapySDR/Device.h>
#include <SoapySDR/Formats.h>
#include <stdio.h> //printf
#include <stdlib.h> //free
#include <stdint.h>
#include <math.h> //cos, M_PI

// Запуск: ./main [uri]
// uri берется из iio_info -s, по умолчанию usb:1.3.5
int main(int argc, char **argv)
{
    const char *uri = (argc > 1) ? argv[1] : "usb:1.3.5";

    // Создание сущности устройства
    // args can be user defined or from the enumeration result
    SoapySDRKwargs args = {};
    SoapySDRKwargs_set(&args, "driver", "plutosdr");
    if (1) {
        SoapySDRKwargs_set(&args, "uri", uri); // ПРОВЕРЬ через iio_info -s
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

    // Настраиваем устройства RX\TX
    if (SoapySDRDevice_setSampleRate(sdr, SOAPY_SDR_RX, 0, 1e6) != 0)
    {
        printf("setSampleRate rx fail: %s\n", SoapySDRDevice_lastError());
    }
    if (SoapySDRDevice_setFrequency(sdr, SOAPY_SDR_RX, 0, 800e6, NULL) != 0)
    {
        printf("setFrequency rx fail: %s\n", SoapySDRDevice_lastError());
    }
    if (SoapySDRDevice_setSampleRate(sdr, SOAPY_SDR_TX, 0, 1e6) != 0)
    {
        printf("setSampleRate tx fail: %s\n", SoapySDRDevice_lastError());
    }
    if (SoapySDRDevice_setFrequency(sdr, SOAPY_SDR_TX, 0, 800e6, NULL) != 0)
    {
        printf("setFrequency tx fail: %s\n", SoapySDRDevice_lastError());
    }
    printf("SoapySDRDevice_getFrequency tx: %lf\n", SoapySDRDevice_getFrequency(sdr, SOAPY_SDR_TX, 0));

    // Настройка каналов и стримов
    size_t channels[] = {0} ; // {0} or {0, 1}
    // TODO: - с вариантом {0, 1} необходимо разробраться, не работают 2 канала

    size_t channel_count = sizeof(channels) / sizeof(channels[0]);
    SoapySDRStream *rxStream = SoapySDRDevice_setupStream(sdr, SOAPY_SDR_RX, SOAPY_SDR_CS16, channels, channel_count, NULL);
    if (rxStream == NULL)
    {
        printf("setupStream rx fail: %s\n", SoapySDRDevice_lastError());
        SoapySDRDevice_unmake(sdr);
        return EXIT_FAILURE;
    }
    if(SoapySDRDevice_setGain(sdr, SOAPY_SDR_RX, channels[0], 30.0) !=0 ){
        printf("setGain rx fail: %s\n", SoapySDRDevice_lastError());
    }
    if(SoapySDRDevice_setGain(sdr, SOAPY_SDR_TX, channels[0], -40.0) !=0 ){
        printf("setGain tx fail: %s\n", SoapySDRDevice_lastError());
    }
    SoapySDRStream *txStream = SoapySDRDevice_setupStream(sdr, SOAPY_SDR_TX, SOAPY_SDR_CS16, channels, channel_count, NULL);
    if (txStream == NULL)
    {
        printf("setupStream tx fail: %s\n", SoapySDRDevice_lastError());
        SoapySDRDevice_unmake(sdr);
        return EXIT_FAILURE;
    }

    // Получение MTU (Maximum Transmission Unit), в нашем случае - размер буферов.
    size_t rx_mtu = SoapySDRDevice_getStreamMTU(sdr, rxStream);
    size_t tx_mtu = SoapySDRDevice_getStreamMTU(sdr, txStream);
    printf("MTU - TX: %zu, RX: %zu\n", tx_mtu, rx_mtu);


    int16_t *tx_buff = (int16_t *)malloc(sizeof(int16_t) * 2 * tx_mtu);
    int16_t *rx_buffer = (int16_t *)malloc(sizeof(int16_t) * 2 * rx_mtu);

    //заполнение tx_buff значениями сэмплов первые 16 бит - I, вторые 16 бит - Q.
    double baseline = 300.0;
    double amplitude = 1800.0;

    for (size_t i = 0; i < tx_mtu; i++)
    {
        double t = (double)i / (double)(tx_mtu - 1);
        double smooth = 0.5 * (1.0 - cos(2.0 * M_PI * t));

        int16_t sample = (int16_t)(baseline + amplitude * smooth);

        tx_buff[2 * i] = sample; // I
        tx_buff[2 * i + 1] = sample; // Q
    }

    //prepare fixed bytes in transmit buffer
    //we transmit a pattern of FFFF FFFF [TS_0]00 [TS_1]00 [TS_2]00 [TS_3]00 [TS_4]00 [TS_5]00 [TS_6]00 [TS_7]00 FFFF FFFF
    //that is a flag (FFFF FFFF) followed by the 64 bit timestamp, split into 8 bytes and packed into the lsb of each of the DAC words.
    //DAC samples are left aligned 12-bits, so each byte is left shifted into place
    for(size_t i = 0; i < 2; i++)
    {
        tx_buff[0 + i] = 0xffff;
        // 8 x timestamp words
        tx_buff[10 + i] = 0xffff;
    }

    //activate streams
    SoapySDRDevice_activateStream(sdr, rxStream, 0, 0, 0); //start streaming
    SoapySDRDevice_activateStream(sdr, txStream, 0, 0, 0); //start streaming

    //here goes
    printf("Start test...\n");
    const long  timeoutUs = 400000; // arbitrarily chosen (взяли из srsRAN)

    long long last_time = 0;

    // Создаем файл для записи сэмплов с rx_buff
    FILE *file = fopen("txdata.pcm", "wb");
    if (file == NULL)
    {
        printf("Cannot open txdata.pcm\n");
    }

    // Количество итерация
    size_t iteration_count = 10;
    // Начинается работа с получением и отправкой сэмплов
    for (size_t buffers_read = 0; buffers_read < iteration_count; buffers_read++)
    {
        void *rx_buffs[] = {rx_buffer};
        int flags;        // flags set by receive operation
        long long timeNs; //timestamp for receive buffer

        int sr = SoapySDRDevice_readStream(sdr, rxStream, rx_buffs, rx_mtu, &flags, &timeNs, timeoutUs);
        if (sr < 0)
        {
            // Skip read on error (likely timeout)
            continue;
        }

        // Dump info
        printf("Buffer: %zu - Samples: %i, Flags: %i, Time: %lli, TimeDiff: %lli\n", buffers_read, sr, flags, timeNs, timeNs - last_time);
        if (file) fwrite(rx_buffer, sizeof(int16_t) * 2, sr, file);
        last_time = timeNs;

        // Переменная для времени отправки сэмплов относительно текущего приема
        long long tx_time = timeNs + (4 * 1000 * 1000); // на 4 [мс] в будущее

        // Добавляем время, когда нужно передать блок tx_buff, через N -наносекунд
        for(size_t i = 0; i < 8; i++)
        {
            // Extract byte from tx time
            uint8_t tx_time_byte = (tx_time >> (i * 8)) & 0xff;

            // Add byte to buffer
            tx_buff[2 + i] = tx_time_byte << 4;
        }

        // Send buffer
        void *tx_buffs[] = {tx_buff};
        if( (buffers_read == 2) ){
            printf("buffers_read: %zu\n", buffers_read);
            flags = SOAPY_SDR_HAS_TIME;
            int st = SoapySDRDevice_writeStream(sdr, txStream, (const void * const*)tx_buffs, tx_mtu, &flags, tx_time, timeoutUs);
            if ((size_t)st != tx_mtu)
            {
                printf("TX Failed: %i\n", st);
            }
        }

    }
    if (file) fclose(file);

    //stop streaming
    SoapySDRDevice_deactivateStream(sdr, rxStream, 0, 0);
    SoapySDRDevice_deactivateStream(sdr, txStream, 0, 0);

    //shutdown the stream
    SoapySDRDevice_closeStream(sdr, rxStream);
    SoapySDRDevice_closeStream(sdr, txStream);

    //cleanup device handle
    SoapySDRDevice_unmake(sdr);

    // Process each rx buffer, looking for transmitted timestamp
    for (size_t j = 0; j < channel_count; j++)
    {
        printf("Checking channel %zu\n", j);
        //check_channel(sample_count, channel_count, j, tx_timestamps, rx_timestamps, (uint16_t**)rx_buff, rx_mtu);
    }

    free(tx_buff);
    free(rx_buffer);

    //all done
    printf("test complete!\n");

    return EXIT_SUCCESS;
}
