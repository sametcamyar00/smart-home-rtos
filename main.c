#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <sched.h>
#include <unistd.h>
#include <pigpio.h>

// --- PIN TANIMLAMALARI ---
#define PIR_PIN 17
#define SERVO_PIN 18
#define BUZZER_PIN 27

// Yeni Eklenen LED Pinleri
#define YELLOW_LED_PIN 5
#define GREEN_LED_PIN 6
#define RED_LED_PIN 13

// --- SPI, MUTEX VE FSM (DURUM MAKİNESİ) DEĞİŞKENLERİ ---
int spi_handle;
pthread_mutex_t shared_resource_mutex;

// FSM Bayrakları (Flags)
volatile int state_waiting_card = 0;
volatile int state_alarm = 0;
volatile int timeout_counter = 0;

/* -------------------------------------------------------------------------
   KAMERA KONTROL FONKSİYONU (IPC)
   ------------------------------------------------------------------------- */
void set_camera_state(int state) {
    FILE *f = fopen("/tmp/cam_state.txt", "w");
    if (f != NULL) {
        fprintf(f, "%d", state);
        fclose(f);
    }
}

/* -------------------------------------------------------------------------
   RFID (RC522) GARANTİLİ HABERLEŞME FONKSİYONLARI
   ------------------------------------------------------------------------- */
void write_spi(int handle, char reg, char val) {
    char data[2];
    data[0] = (reg << 1) & 0x7E;
    data[1] = val;
    spiXfer(handle, data, data, 2);
}

char read_spi(int handle, char reg) {
    char tx[2];
    char rx[2];
    tx[0] = ((reg << 1) & 0x7E) | 0x80;
    tx[1] = 0x00;
    spiXfer(handle, tx, rx, 2);
    return rx[1];
}

void rc522_init(int handle) {
    write_spi(handle, 0x01, 0x0F); // Soft Reset
    time_sleep(0.05);
    
    write_spi(handle, 0x2A, 0x8D); // TModeReg
    write_spi(handle, 0x2B, 0x3E); // TPrescalerReg
    write_spi(handle, 0x2D, 0x1E); // TReloadRegL
    write_spi(handle, 0x2C, 0x00); // TReloadRegH
    write_spi(handle, 0x15, 0x40); // TxASKReg
    write_spi(handle, 0x11, 0x3D); // ModeReg
    
    // Anteni Aç
    char tx_ctrl = read_spi(handle, 0x14); 
    if ((tx_ctrl & 0x03) != 0x03) {
        write_spi(handle, 0x14, tx_ctrl | 0x03);
    }
}

int is_card_present(int handle) {
    write_spi(handle, 0x01, 0x00); 
    write_spi(handle, 0x04, 0x7F); 
    write_spi(handle, 0x0A, 0x80); 
    
    write_spi(handle, 0x09, 0x26); 
    write_spi(handle, 0x01, 0x0C); 
    
    write_spi(handle, 0x0D, 0x87); // StartSend = 1 ve TxLastBits = 7
    
    time_sleep(0.025); 
    
    char fifo_level = read_spi(handle, 0x0A) & 0x7F; 
    
    if (fifo_level >= 2) { 
        return 1; 
    }
    return 0; 
}

/* -------------------------------------------------------------------------
   YÜKSEK ÖNCELİKLİ GÖREV (PIR Sensörü & Timer & Alarm Yöneticisi)
   ------------------------------------------------------------------------- */
void* pir_sensor_task(void* arg) {
    int son_durum = 0; 
    int yellow_state = 0;

    while(1) {
        int mevcut_durum = gpioRead(PIR_PIN); 
        
        // 1. DURUM: HAREKET ALGILANDI (Sistemi Bekleme Moduna Sok)
        if (mevcut_durum == 1 && son_durum == 0 && state_waiting_card == 0 && state_alarm == 0) {
            
            pthread_mutex_lock(&shared_resource_mutex);
            printf("\n[PIR TASK] Hareket algilandi! 10 Saniye icinde kart okutun...\n");
            
            set_camera_state(1); // Kamera Acildi
            state_waiting_card = 1;
            timeout_counter = 0;
            
            // Kısa bir uyarı sesi
            gpioWrite(BUZZER_PIN, 1); time_sleep(0.1); gpioWrite(BUZZER_PIN, 0);
            pthread_mutex_unlock(&shared_resource_mutex);
        }
        son_durum = mevcut_durum; 
        
        // 2. DURUM: KART BEKLENİYOR (Zamanlayıcı ve Sarı LED)
        if (state_waiting_card == 1) {
            timeout_counter++;
            
            // Sarı LED Yanıp Sönme Efekti (Her 500ms'de bir durum değiştirir)
            if (timeout_counter % 10 == 0) {
                yellow_state = !yellow_state;
                gpioWrite(YELLOW_LED_PIN, yellow_state);
            }
            
            // 3. DURUM: SÜRE DOLDU - ALARM AKTİF (10 Saniye = 200 döngü * 50ms)
            if (timeout_counter >= 200) {
                pthread_mutex_lock(&shared_resource_mutex);
                
                state_waiting_card = 0;
                state_alarm = 1; // Alarmı tetikle
                
                printf("\n[ALARM] Sure doldu! Gecersiz giris denemesi.\n");
                set_camera_state(0); // Kamerayı kapat
                
                // LED'leri ve Buzzer'ı Alarm konumuna getir
                gpioWrite(YELLOW_LED_PIN, 0);
                gpioWrite(RED_LED_PIN, 1); // Kırmızı LED sürekli yanar
                gpioWrite(BUZZER_PIN, 1);  // Buzzer sürekli öter
                
                pthread_mutex_unlock(&shared_resource_mutex);
            }
        }
        
        time_sleep(0.05); // 50ms loop hassasiyeti
    }
    return NULL;
}

/* -------------------------------------------------------------------------
   DÜŞÜK ÖNCELİKLİ GÖREV (RFID Okuyucu & Alarm Kapatıcı)
   ------------------------------------------------------------------------- */
void* rfid_task(void* arg) {
    rc522_init(spi_handle); 
    int kart_okundu_mu = 0;

    while(1) {
        if (is_card_present(spi_handle) == 1 && kart_okundu_mu == 0) {
            kart_okundu_mu = 1;

            printf("\n[RFID TASK] KART ONAYLANDI!\n");
            
            // Sistemi Sıfırla (Alarm veya Bekleme durumundan çıkar)
            state_waiting_card = 0;
            state_alarm = 0;
            set_camera_state(0);

            pthread_mutex_lock(&shared_resource_mutex);
            
            // Önceden kalan tüm uyarıcıları temizle (Alarmı Sustur)
            gpioWrite(RED_LED_PIN, 0);
            gpioWrite(YELLOW_LED_PIN, 0);
            gpioWrite(BUZZER_PIN, 0);
            
            // Yeşil LED'i Yak (Başarılı Giriş)
            gpioWrite(GREEN_LED_PIN, 1);
            
            // Onay Sesi (Çift Bip)
            gpioWrite(BUZZER_PIN, 1); time_sleep(0.1); gpioWrite(BUZZER_PIN, 0); time_sleep(0.1);
            gpioWrite(BUZZER_PIN, 1); time_sleep(0.1); gpioWrite(BUZZER_PIN, 0);
            
            // Kapıyı Aç
            gpioServo(SERVO_PIN, 1500);
            printf("[RFID TASK] Kapi Kilidi Acildi (3 Sn).\n");
            
            // 3 Saniye Bekle (Kapı Açık + Yeşil LED Yanıyor)
            time_sleep(3.0); 
            
            // Kapıyı Kilitle ve Yeşil LED'i Söndür
            gpioServo(SERVO_PIN, 500);
            gpioWrite(GREEN_LED_PIN, 0);
            printf("[RFID TASK] Kapi Kilitlendi.\n\n");

            pthread_mutex_unlock(&shared_resource_mutex);
            
            time_sleep(2.0); 
        } 
        else if (is_card_present(spi_handle) == 0) {
            kart_okundu_mu = 0; 
        }
        time_sleep(0.1); 
    }
    return NULL;
}

/* -------------------------------------------------------------------------
   ANA İŞLETİM SİSTEMİ KURULUMU
   ------------------------------------------------------------------------- */
int main() {
    if (gpioInitialise() < 0) {
        fprintf(stderr, "Pigpio baslatilamadi!\n");
        return 1;
    }

    // SPI Hattını Aç
    spi_handle = spiOpen(0, 1000000, 0);
    if (spi_handle < 0) {
        fprintf(stderr, "SPI Hatti acilamadi!\n");
        gpioTerminate();
        return 1;
    }

    // --- PIN AYARLARI ---
    gpioSetMode(PIR_PIN, PI_INPUT);
    gpioSetPullUpDown(PIR_PIN, PI_PUD_DOWN); 
    gpioSetMode(SERVO_PIN, PI_OUTPUT);
    gpioSetMode(BUZZER_PIN, PI_OUTPUT);
    
    // Yeni LED'lerin Pin Ayarları
    gpioSetMode(YELLOW_LED_PIN, PI_OUTPUT);
    gpioSetMode(GREEN_LED_PIN, PI_OUTPUT);
    gpioSetMode(RED_LED_PIN, PI_OUTPUT);
    
    // Başlangıçta Tüm Uyarıcıları Kapat
    gpioWrite(BUZZER_PIN, 0);
    gpioWrite(YELLOW_LED_PIN, 0);
    gpioWrite(GREEN_LED_PIN, 0);
    gpioWrite(RED_LED_PIN, 0);
    gpioServo(SERVO_PIN, 500); 

    set_camera_state(0);

    // Mutex (Priority Inheritance)
    pthread_mutexattr_t mutex_attr;
    pthread_mutexattr_init(&mutex_attr);
    pthread_mutexattr_setprotocol(&mutex_attr, PTHREAD_PRIO_INHERIT); 
    pthread_mutex_init(&shared_resource_mutex, &mutex_attr);

    pthread_t thread_pir, thread_rfid;
    pthread_attr_t attr_pir, attr_rfid;
    struct sched_param param_pir, param_rfid;

    // Yüksek Öncelik Ayarı
    pthread_attr_init(&attr_pir);
    pthread_attr_setinheritsched(&attr_pir, PTHREAD_EXPLICIT_SCHED);
    pthread_attr_setschedpolicy(&attr_pir, SCHED_FIFO);
    param_pir.sched_priority = 90;
    pthread_attr_setschedparam(&attr_pir, &param_pir);

    // Düşük Öncelik Ayarı
    pthread_attr_init(&attr_rfid);
    pthread_attr_setinheritsched(&attr_rfid, PTHREAD_EXPLICIT_SCHED);
    pthread_attr_setschedpolicy(&attr_rfid, SCHED_FIFO);
    param_rfid.sched_priority = 70;
    pthread_attr_setschedparam(&attr_rfid, &param_rfid);

    printf("--- Akilli Ev GERCEK ZAMANLI Sistemi Aktif ---\n");
    printf("Sistem hazir. Yaklasma uyarisi icin hareket, giris icin kart bekleniyor.\n\n");

    // Görevleri Başlat
    pthread_create(&thread_pir, &attr_pir, pir_sensor_task, NULL);
    pthread_create(&thread_rfid, &attr_rfid, rfid_task, NULL);

    while(1) {
        time_sleep(1);
    }

    spiClose(spi_handle);
    gpioTerminate();
    return 0;
}
