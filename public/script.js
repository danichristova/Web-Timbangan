import { initializeApp } from "https://www.gstatic.com/firebasejs/10.7.0/firebase-app.js";
import { getDatabase, ref, onValue, set } from "https://www.gstatic.com/firebasejs/10.7.0/firebase-database.js";

const firebaseConfig = {
  apiKey: "AIzaSyAUH7n1tvEFw_ns46Wv6as_sldSPG_EQyE",
  authDomain: "timbangan-test-1.firebaseapp.com",
  databaseURL: "https://timbangan-test-1-default-rtdb.asia-southeast1.firebasedatabase.app",
  projectId: "timbangan-test-1",
  storageBucket: "timbangan-test-1.appspot.com",
  messagingSenderId: "921983998416",
  appId: "1:921983998416:web:5c97f8951311d0dbc081d5"
};

const app = initializeApp(firebaseConfig);
const db = getDatabase(app);

// ================= OFFLINE DETECTION =================
let lastWeight = 0;
let lastWeightChangeTime = Date.now();
let offlineCheckInterval = null;

// Inisialisasi berat per botol dari localStorage
let bottleWeight = parseFloat(localStorage.getItem("bottleWeight")) || 0;
document.getElementById("bottleWeight").value = bottleWeight;

// Ambil data realtime
const scaleRef = ref(db, "scale");

onValue(scaleRef, (snapshot) => {
  const data = snapshot.val();

  if (!data) return;

  const weight = parseFloat(data.weight);
  
  // Deteksi perubahan weight untuk offline detection
  if (weight !== lastWeight) {
    lastWeight = weight;
    lastWeightChangeTime = Date.now();
  }

  document.getElementById("weight").innerText = Math.round(weight) + " gram";
  
  // Hitung jumlah botol
  calculateBottles(weight);
  
  let status = data.status || "-";
  
  // Cek offline: jika tidak ada perubahan weight dalam 10 detik
  const timeSinceLastChange = Date.now() - lastWeightChangeTime;
  if (timeSinceLastChange > 70000 && !status.includes("Booting") && !status.includes("Offline")) {
    status = "Offline";
  }
  
  document.getElementById("status").innerText = status;
  document.getElementById("log").innerText = data.log;
  document.getElementById("ip").innerText = data.ip || "-";

  // Disable buttons berdasarkan status
  updateButtonState(status);
});

// Update button state berdasarkan status
window.updateButtonState = function (status) {
  const tareBtn = document.querySelector('button:contains("Tare")') || 
                  Array.from(document.querySelectorAll('button')).find(btn => btn.innerText === "Tare");
  const kalibrasiBtn = document.querySelector('button:contains("Kalibrasi")') || 
                       Array.from(document.querySelectorAll('button')).find(btn => btn.innerText === "Kalibrasi");
  const knownInput = document.getElementById("known");

  if (status.includes("Booting")) {
    // Disable semua saat booting
    if (tareBtn) tareBtn.disabled = true;
    if (kalibrasiBtn) kalibrasiBtn.disabled = true;
    if (knownInput) knownInput.disabled = true;
  } else if (status.includes("Offline")) {
    // Disable semua saat offline
    if (tareBtn) tareBtn.disabled = true;
    if (kalibrasiBtn) kalibrasiBtn.disabled = true;
    if (knownInput) knownInput.disabled = true;
  } else if (status.includes("Tare Stabilizing")) {
    // Disable semua saat tare stabilizing
    if (tareBtn) tareBtn.disabled = true;
    if (kalibrasiBtn) kalibrasiBtn.disabled = true;
    if (knownInput) knownInput.disabled = true;
  } else if (status.includes("Siap Tare")) {
    // Enable tare, disable kalibrasi
    if (tareBtn) tareBtn.disabled = false;
    if (kalibrasiBtn) kalibrasiBtn.disabled = true;
    if (knownInput) knownInput.disabled = true;
  } else if (status.includes("Siap Kalibrasi")) {
    // Enable kalibrasi, disable tare
    if (tareBtn) tareBtn.disabled = true;
    if (kalibrasiBtn) kalibrasiBtn.disabled = false;
    if (knownInput) knownInput.disabled = false;
  } else if (status.includes("Kalibrasi")) {
    // Disable semua saat kalibrasi loading
    if (tareBtn) tareBtn.disabled = true;
    if (kalibrasiBtn) kalibrasiBtn.disabled = true;
    if (knownInput) knownInput.disabled = true;
  } else if (status.includes("Stabil")) {
    // Enable semua
    if (tareBtn) tareBtn.disabled = false;
    if (kalibrasiBtn) kalibrasiBtn.disabled = false;
    if (knownInput) knownInput.disabled = false;
  }
}

// Fungsi menghitung jumlah botol
window.calculateBottles = function (totalWeight) {
  if (bottleWeight <= 0) {
    document.getElementById("bottleCount").innerText = "Setting berat botol terlebih dahulu";
    return;
  }
  
  const count = Math.round(totalWeight / bottleWeight);
  document.getElementById("bottleCount").innerText = count + " botol";
  document.getElementById("bottleCount").title = 
    `Total: ${totalWeight}g ÷ ${bottleWeight}g = ${(totalWeight / bottleWeight).toFixed(2)}`;
}

// Tare dari web
window.tare = function () {
  set(ref(db, "command/tare"), 1);
  set(ref(db, "scale/log"), "TARE dari WEB");
}

// Kalibrasi dari web
window.kalibrasi = function () {

  let berat = document.getElementById("known").value;

  if (berat == "") {
    alert("Masukkan berat dulu");
    return;
  }

  set(ref(db, "command/kalibrasi"), parseFloat(berat));
  set(ref(db, "scale/log"), "Kalibrasi dari WEB: " + berat + " gram");
}

// Simpan berat per botol
window.saveBottleWeight = function () {
  const weight = document.getElementById("bottleWeight").value;
  
  if (weight == "" || weight <= 0) {
    alert("Masukkan berat per botol yang valid (> 0)");
    return;
  }
  
  bottleWeight = parseFloat(weight);
  localStorage.setItem("bottleWeight", bottleWeight);
  alert("Berat per botol tersimpan: " + bottleWeight + " gram");
  
  // Recalculate bottles dengan data terakhir
  const currentWeight = parseFloat(document.getElementById("weight").innerText) || 0;
  if (currentWeight > 0) {
    calculateBottles(currentWeight);
  }
}