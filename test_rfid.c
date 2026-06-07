#include <stdio.h>
#include <stdlib.h>
#include <pigpio.h>

int main() {
    if (gpioInitialise() < 0) {
        printf("Pigpio baslatilamadi!\n");
        return 1;
    }

    // SPI Hattını Aç: Kanal 0, Hız 1MHz (1000000), Bayraklar 0
    int spi_handle = spiOpen(0, 1000000, 0);
    if (spi_handle < 0) {
        printf("SPI Hatti Acilamadi! Kod: %d\n", spi_handle);
        gpioTerminate();
        return 1;
    }

    printf("RFID (RC522) Baglanti Testi Basladi...\n");
    printf("Modulun versiyon kaydi (VersionReg) okunuyor...\n\n");

    // RC522 kütüphane kuralı: Versiyon adresi 0x37'dir.
    // SPI okuma modunda adres biti sola kaydırılır ve 0x80 ile OR'lanır.
    char tx_buffer[2] = {0xEE, 0x00}; 
    char rx_buffer[2] = {0x00, 0x00};

    // SPI üzerinden veriyi gönder ve cevabı al (2 byte transfer)
    spiXfer(spi_handle, tx_buffer, rx_buffer, 2);

    unsigned char version = rx_buffer[1];

    printf("RFID Modulunden Donen Ham Deger (Hex): 0x%02X\n", version);

    // Donen degere gore teshis koyuyoruz
    if (version == 0x91 || version == 0x92) {
        printf("[BAŞARILI] RC522 başarıyla algılandı! Sinyal tıkır tıkır çalışıyor.\n");
    } else if (version == 0x88) {
        printf("[BAŞARILI] Çin malı klon RC522 algılandı ama iletişim kurulabiliyor.\n");
    } else if (version == 0x00 || version == 0xFF) {
        printf("[HATA] Iletisim kurulamadi! Kabloları (özellikle MISO, MOSI, SCK) kontrol edin.\n");
    } else {
        printf("[UYARI] Beklenmeyen bir deger dondu (0x%02X). Kablolarda temassızlık olabilir.\n", version);
    }

    spiClose(spi_handle);
    gpioTerminate();
    return 0;
}
