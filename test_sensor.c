#include <stdio.h>
#include <pigpio.h>

int main() {
    if (gpioInitialise() < 0) {
        printf("Pigpio baslatilamadi!\n");
        return 1;
    }

    // GPIO 17'yi (Fiziksel Pin 11) giriş olarak ayarla
    gpioSetMode(17, PI_INPUT);
    
    // ÇOK ÖNEMLİ: Pini yazılımsal olarak 0'a (GND) çekerek dalgalanmayı önle
    gpioSetPullUpDown(17, PI_PUD_DOWN); 

    printf("Sensor Testi Basladi! (Durdurmak icin CTRL+C)\n");
    printf("Lutfen 3.3V kablosunu Fiziksel Pin 11'e degdirip cekin...\n");

    while(1) {
        // Pindeki voltajı saniyede 2 kez oku ve ekrana yazdır
        int durum = gpioRead(17);
        printf("GPIO 17 Durumu: %d\n", durum);
        time_sleep(0.5); 
    }

    gpioTerminate();
    return 0;
}
