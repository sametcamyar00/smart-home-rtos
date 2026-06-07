from flask import Flask, Response, jsonify
import cv2
import time
import os
import numpy as np
from picamera2 import Picamera2
import psutil

app = Flask(__name__)
STATE_FILE = "/tmp/cam_state.txt"

# --- YAPAY ZEKA MODELİNİ YÜKLEME ---
xml_path = 'haarcascade_frontalface_default.xml'
yuz_algilayici = cv2.CascadeClassifier(xml_path)

if yuz_algilayici.empty():
    print("!!! DIKKAT: XML dosyasi bozuk veya bulunamadi !!!")
else:
    print("Yapay Zeka Modeli basariyla yüklendi.")

def get_state():
    try:
        if os.path.exists(STATE_FILE):
            with open(STATE_FILE, "r") as f:
                return int(f.read().strip())
    except:
        pass
    return 0

# --- SİSTEM PARAMETRELERİNİ OKUMA FONKSİYONU ---
def get_pi_temp():
    try:
        # Raspberry Pi'nin donanımsal sicaklik sensorunu okur
        with open("/sys/class/thermal/thermal_zone0/temp", "r") as f:
            return round(int(f.read()) / 1000.0, 1)
    except:
        return 0.0

@app.route('/stats')
def stats():
    # Arayuzdeki grafiklerin cektigi canli JSON verisi
    return jsonify({
        'cpu': psutil.cpu_percent(interval=None),
        'ram': psutil.virtual_memory().percent,
        'temp': get_pi_temp()
    })

def generate_frames():
    picam2 = None
    while True:
        state = get_state()
        if state == 1:
            if picam2 is None:
                print("Kamera donanimi baslatiliyor...")
                try:
                    picam2 = Picamera2()
                    config = picam2.create_preview_configuration(main={"size": (320, 240)})
                    picam2.configure(config)
                    picam2.start()
                except Exception as e:
                    time.sleep(1)
                    continue
            
            try:
                frame = picam2.capture_array()
                frame_bgr = cv2.cvtColor(frame, cv2.COLOR_RGB2BGR)
                
                if not yuz_algilayici.empty():
                    try:
                        gray = cv2.cvtColor(frame_bgr, cv2.COLOR_BGR2GRAY)
                        yuzler = yuz_algilayici.detectMultiScale(gray, scaleFactor=1.2, minNeighbors=4, minSize=(30, 30))
                        for (x, y, w, h) in yuzler:
                            cv2.rectangle(frame_bgr, (x, y), (x+w, y+h), (0, 255, 0), 2)
                            cv2.putText(frame_bgr, "Insan", (x, y-10), cv2.FONT_HERSHEY_SIMPLEX, 0.5, (0, 255, 0), 2)
                    except:
                        pass
                
                ret, buffer = cv2.imencode('.jpg', frame_bgr)
                frame_bytes = buffer.tobytes()
                yield (b'--frame\r\n'
                       b'Content-Type: image/jpeg\r\n\r\n' + frame_bytes + b'\r\n')
                       
            except Exception as e:
                time.sleep(0.1)
                
        else:
            if picam2 is not None:
                picam2.stop()
                picam2.close()
                picam2 = None
            
            black_frame = np.zeros((240, 320, 3), dtype=np.uint8)
            cv2.putText(black_frame, "KAMERA KAPALI", (45, 110), cv2.FONT_HERSHEY_SIMPLEX, 0.9, (0, 0, 255), 2)
            cv2.putText(black_frame, "Hareket Bekleniyor...", (65, 150), cv2.FONT_HERSHEY_SIMPLEX, 0.6, (255, 255, 255), 1)
            
            ret, buffer = cv2.imencode('.jpg', black_frame)
            frame_bytes = buffer.tobytes()
            yield (b'--frame\r\n'
                   b'Content-Type: image/jpeg\r\n\r\n' + frame_bytes + b'\r\n')
            time.sleep(0.2)

@app.route('/video_feed')
def video_feed():
    return Response(generate_frames(), mimetype='multipart/x-mixed-replace; boundary=frame')

# --- YENİ DASHBOARD ARAYÜZÜ (HTML + JS) ---
@app.route('/')
def index():
    html = """
    <html>
    <head>
        <title>Akilli Ev RTOS Paneli</title>
        <script src="https://cdn.jsdelivr.net/npm/chart.js"></script>
        <style>
            body { background-color: #1e1e1e; color: white; font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif; margin: 0; padding: 20px; }
            h2 { text-align: center; color: #4CAF50; border-bottom: 2px solid #333; padding-bottom: 10px; }
            .container { display: flex; justify-content: center; gap: 30px; margin-top: 20px; flex-wrap: wrap; }
            .camera-box { background: #2d2d2d; padding: 15px; border-radius: 10px; box-shadow: 0 4px 15px rgba(0,0,0,0.5); }
            .camera-box img { width: 640px; border-radius: 5px; border: 2px solid #555; }
            .stats-box { width: 400px; background: #2d2d2d; padding: 20px; border-radius: 10px; box-shadow: 0 4px 15px rgba(0,0,0,0.5); }
            .metric-cards { display: flex; justify-content: space-between; margin-bottom: 20px; }
            .card { background: #3d3d3d; padding: 15px; border-radius: 8px; width: 30%; text-align: center; font-weight: bold; border-bottom: 4px solid #4CAF50; }
            .card.cpu { border-color: #ff6384; }
            .card.ram { border-color: #36a2eb; }
            .card.temp { border-color: #ffce56; }
            .card span { display: block; font-size: 24px; margin-top: 5px; }
            canvas { background: #3d3d3d; border-radius: 8px; padding: 10px; }
        </style>
    </head>
    <body>
        <h2>Akilli Ev GERCEK ZAMANLI Sistemi (CPS) Dashboard</h2>
        <div class="container">
            <div class="camera-box">
                <h3 style="margin-top:0; text-align:center;">Canli Guvenlik Kamerasi</h3>
                <img src="/video_feed" />
            </div>
            
            <div class="stats-box">
                <h3 style="margin-top:0; text-align:center;">Sistem Performansi</h3>
                <div class="metric-cards">
                    <div class="card cpu">CPU <span id="val-cpu">0%</span></div>
                    <div class="card ram">RAM <span id="val-ram">0%</span></div>
                    <div class="card temp">ISI <span id="val-temp">0°C</span></div>
                </div>
                <canvas id="sysChart" width="400" height="250"></canvas>
            </div>
        </div>

        <script>
            // Grafik Kurulumu (Chart.js)
            const ctx = document.getElementById('sysChart').getContext('2d');
            const sysChart = new Chart(ctx, {
                type: 'line',
                data: {
                    labels: [],
                    datasets: [
                        { label: 'CPU (%)', borderColor: '#ff6384', backgroundColor: 'rgba(255, 99, 132, 0.2)', data: [], fill: true, tension: 0.4 },
                        { label: 'RAM (%)', borderColor: '#36a2eb', backgroundColor: 'rgba(54, 162, 235, 0.2)', data: [], fill: true, tension: 0.4 },
                        { label: 'Sıcaklık (°C)', borderColor: '#ffce56', backgroundColor: 'rgba(255, 206, 86, 0.2)', data: [], fill: true, tension: 0.4 }
                    ]
                },
                options: {
                    responsive: true,
                    scales: {
                        y: { min: 0, max: 100, ticks: { color: '#ccc' } },
                        x: { ticks: { color: '#ccc' } }
                    },
                    plugins: { legend: { labels: { color: 'white' } } }
                }
            });

            // Saniyede bir verileri çek ve grafiği/kutuları güncelle
            setInterval(() => {
                fetch('/stats')
                    .then(response => response.json())
                    .then(data => {
                        // Kutulari guncelle
                        document.getElementById('val-cpu').innerText = data.cpu + '%';
                        document.getElementById('val-ram').innerText = data.ram + '%';
                        document.getElementById('val-temp').innerText = data.temp + '°C';

                        // Zamani al
                        const now = new Date();
                        const timeStr = now.getHours() + ':' + String(now.getMinutes()).padStart(2, '0') + ':' + String(now.getSeconds()).padStart(2, '0');

                        // Grafiğe veri ekle
                        sysChart.data.labels.push(timeStr);
                        sysChart.data.datasets[0].data.push(data.cpu);
                        sysChart.data.datasets[1].data.push(data.ram);
                        sysChart.data.datasets[2].data.push(data.temp);

                        // Ekranin dolmamasi icin 15 saniyeden eski veriyi sil
                        if(sysChart.data.labels.length > 15) {
                            sysChart.data.labels.shift();
                            sysChart.data.datasets.forEach(dataset => dataset.data.shift());
                        }

                        sysChart.update();
                    })
                    .catch(error => console.error('Veri cekme hatasi:', error));
            }, 1000); // 1000ms = 1 Saniye
        </script>
    </body>
    </html>
    """
    return html

if __name__ == '__main__':
    with open(STATE_FILE, "w") as f:
        f.write("0")
    print("Dashboard calisiyor... Tarayicidan port 5000'e baglanin.")
    app.run(host='0.0.0.0', port=5000, debug=False, threaded=True)
