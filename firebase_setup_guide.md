# Panduan Setup Firebase: Smart Garden v1.0

Dokumen ini berisi panduan untuk menyiapkan **Firebase Realtime Database** dan mendeploy dashboard menggunakan **Firebase Hosting**.

---

## 1. Membuat Proyek Firebase

1. Buka [Firebase Console](https://console.firebase.google.com/).
2. Klik **Add project** (Tambah proyek).
3. Masukkan nama proyek (misal: `smart-garden-iot`) lalu klik **Continue**.
4. Anda bisa menonaktifkan Google Analytics untuk proyek sederhana ini, lalu klik **Create project**.

---

## 2. Membuat Realtime Database

1. Pada menu sebelah kiri di Firebase Console, buka **Build** > **Realtime Database**.
2. Klik **Create Database**.
3. Pilih lokasi database terdekat (misal: Singapore `asia-southeast1` untuk koneksi lebih cepat dari Indonesia).
4. Pilih **Start in test mode** (Mode tes) untuk pengembangan awal agar database bisa diakses tanpa autentikasi dahulu, lalu klik **Enable**.

### Atur Database Rules
Pilih tab **Rules** di bagian atas halaman Realtime Database, lalu ganti rules menjadi seperti berikut agar ESP32 & Dashboard dapat membaca dan menulis data secara bebas:

```json
{
  "rules": {
    ".read": true,
    ".write": true
  }
}
```
*Klik **Publish** untuk menyimpan perubahan rules.*

---

## 3. Mengimpor Struktur Data Awal

1. Salin struktur JSON di bawah ini:
```json
{
  "sensor": {
    "temperature": 28.5,
    "humidity": 75,
    "soil": 42
  },
  "control": {
    "mode": "AUTO",
    "pumpCommand": false
  },
  "setting": {
    "soilMin": 30,
    "soilMax": 60
  },
  "status": {
    "pumpState": false,
    "wifiConnected": true,
    "sensorHealthy": true,
    "online": true,
    "lastUpdate": 1718460000
  }
}
```
2. Pada Firebase Console Realtime Database, pilih tab **Data**.
3. Klik ikon titik tiga vertikal di pojok kanan atas area data, lalu pilih **Import JSON**.
4. Unggah berkas JSON yang berisi data di atas, atau klik ikon tambah (+) di root node untuk menginput field secara manual.

---

## 4. Menghubungkan Firebase ke Dashboard Web

1. Di Firebase Console, klik ikon Gigi Roda (Settings) > **Project settings**.
2. Di bagian **Your apps**, klik ikon Web (`</>`) untuk mendaftarkan web app.
3. Masukkan nama aplikasi (misal: `Smart Garden Web`), beri centang pada **Also set up Firebase Hosting**, lalu klik **Register app**.
4. Salin objek `firebaseConfig` yang muncul di layar:
   ```javascript
   const firebaseConfig = {
     apiKey: "...",
     authDomain: "...",
     databaseURL: "...",
     projectId: "...",
     storageBucket: "...",
     messagingSenderId: "...",
     appId: "..."
   };
   ```
5. Buka berkas [index.html](file:///d:/Smart%20Garden/index.html), cari tag `<script>` di bagian bawah (sekitar baris 693), ubah `USE_FIREBASE = true` dan ganti objek `firebaseConfig` dengan milik Anda.
