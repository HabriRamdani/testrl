/*
 * Smart Energy Dashboard Final Version v2.1 (UTF-8 Fixed)
 * ESP32 + PZEM004Tv30 + WebServer
 * By: teman & ChatGPT
 */

#include <WiFi.h>
#include <WebServer.h>
#include <PZEM004Tv30.h>
#include <EEPROM.h>

#define PZEM_RX_PIN 16
#define PZEM_TX_PIN 17
#define RELAY_PIN 2
#define EEPROM_SIZE 32
#define ADDR_TRIP 0

PZEM004Tv30 pzem(Serial2, PZEM_RX_PIN, PZEM_TX_PIN);
WebServer server(80);

// --- WiFi Setting ---
const char* ssid = "LEKMELU GAWE YOMELU BAYAR";
const char* password = "cahrantau";

// --- Variabel Sensor dan Sistem ---
float voltage = 0, current = 0, power = 0, energy = 0;
float avgPower = 0, cost = 0, TRIP_THRESHOLD = 1800.0;
bool relayStatus = true;
float lastEnergy = 0;

unsigned long lastTripTime = 0;
unsigned long lastDataSave = 0;
const unsigned long DEBOUNCE_TIME = 3000;
const unsigned long LIMIT_SAVE_DELAY = 5000; // 5 detik
const unsigned long HOUR_INTERVAL = 3600000; // 1 jam

// --- Log per jam ---
struct HourData {
  float voltage;
  float current;
  float power;
  float energy;
  float cost;
  String time;
};

#define MAX_HOURS 10
HourData hourLog[MAX_HOURS];
int logIndex = 0;

// ----------------------------------------------------------------
//  HTML DASHBOARD
// ----------------------------------------------------------------
String getPage() {
  String html = R"rawliteral(
  <html>
  <head>
    <meta charset="UTF-8">
    <meta name='viewport' content='width=device-width, initial-scale=1'>
    <title>Smart Energy Dashboard</title>
    <script src='https://cdn.jsdelivr.net/npm/chart.js'></script>
    <style>
      body { font-family: Arial; background:#f0f8ff; margin:0; padding:0; text-align:center; }
      h2 { background:#1e3a8a; color:white; padding:14px; margin:0; font-size:22px; }
      .card { background:white; border-radius:12px; width:90%; margin:18px auto; padding:15px;
              box-shadow:0 2px 6px rgba(0,0,0,0.15); }
      canvas { display:block; margin:auto; width:90%; height:160px !important; }
      button { padding:9px 18px; border:none; border-radius:8px; margin:6px; font-size:15px; cursor:pointer; }
      .on { background:#22c55e; color:white; }
      .off { background:#ef4444; color:white; }
      .save { background:#2563eb; color:white; }
      input[type=range] { width:80%; margin:10px 0; }
      input[type=number] { width:90px; text-align:center; }
      table { border-collapse:collapse; width:95%; margin:10px auto; }
      th, td { border:1px solid #ccc; padding:6px; font-size:14px; }
      th { background:#bfdbfe; color:#1e3a8a; }
      .table-card { overflow-x:auto; }
    </style>
  </head>
  <body>
    <h2>Smart Energy Dashboard</h2>

    <div class='card'>
      <p><b>Tegangan:</b> <span id='v'>0</span> V</p>
      <canvas id='voltChart'></canvas>
    </div>

    <div class='card'>
      <p><b>Arus:</b> <span id='i'>0</span> A</p>
      <canvas id='currChart'></canvas>
    </div>

    <div class='card'>
      <p><b>Daya:</b> <span id='p'>0</span> W</p>
      <canvas id='powerChart'></canvas>
    </div>

    <div class='card'>
      <p><b>Energi Total:</b> <span id='e'>0</span> Wh</p>
      <canvas id='energyChart'></canvas>
    </div>

    <div class='card'>
      <p><b>Biaya Total:</b> Rp <span id='c'>0</span></p>
      <p><b>Biaya Pemakaian Jam Ini:</b> Rp <span id='ch'>0</span></p>
      <canvas id='costChart'></canvas>
    </div>

    <div class='card'>
      <p><b>Status Relay:</b> <span id='r'>ON</span></p>
      <a href='/on'><button class='on'>NYALAKAN</button></a>
      <a href='/off'><button class='off'>MATIKAN</button></a>
    </div>

    <div class='card'>
      <p><b>Batas Daya Proteksi:</b> <span id='limit'>0</span> W</p>
      <input type='range' id='limitSlider' min='0' max='5000' step='10' value='1800' oninput='updateLimit(this.value)'>
      <input type='number' id='limitBox' value='1800' min='0' step='10' oninput='updateLimit(this.value)'>
      <button onclick='saveLimit()' class='save'>Simpan Batas</button>
    </div>

    <div class='card table-card'>
      <h3>Statistik Per Jam (10 Terakhir)</h3>
      <button onclick='downloadCSV()' class='save'>Unduh CSV</button>
      <button onclick='resetLog()' class='off'>Reset Log</button>
      <button onclick='resetEnergy()' class='on'>Reset Energi</button>
      <table>
        <thead><tr><th>Waktu</th><th>V</th><th>I</th><th>P</th><th>Energi</th><th>Biaya</th></tr></thead>
        <tbody id='logTable'></tbody>
      </table>
    </div>

    <script>
      let data={v:[],i:[],p:[],e:[],c:[]},maxPoints=20,charts={};
      const makeChart=(id,color)=>{const ctx=document.getElementById(id).getContext('2d');
        charts[id]=new Chart(ctx,{type:'line',data:{labels:[],datasets:[{borderColor:color,borderWidth:2,fill:true,backgroundColor:color+'22',pointRadius:0}]},
        options:{scales:{x:{display:false},y:{display:true}},plugins:{legend:{display:false}},animation:false}});};
      makeChart('voltChart','#3b82f6'); makeChart('currChart','#22c55e'); makeChart('powerChart','#ef4444'); makeChart('energyChart','#f59e0b'); makeChart('costChart','#8b5cf6');

      async function fetchData(){
        const res=await fetch('/data'); const j=await res.json();
        document.getElementById('v').innerText=j.voltage.toFixed(2);
        document.getElementById('i').innerText=j.current.toFixed(3);
        document.getElementById('p').innerText=j.power.toFixed(2);
        document.getElementById('e').innerText=j.energy.toFixed(3);
        document.getElementById('c').innerText=j.cost.toFixed(2);
        document.getElementById('ch').innerText=j.hourCost.toFixed(2);
        document.getElementById('r').innerText=j.relay?"ON":"OFF";
        document.getElementById('limit').innerText=j.limit.toFixed(0);
        document.getElementById('limitSlider').value=j.limit;
        document.getElementById('limitBox').value=j.limit;
        const m={v:'voltChart',i:'currChart',p:'powerChart',e:'energyChart',c:'costChart'};
        const v={v:j.voltage,i:j.current,p:j.power,e:j.energy,c:j.cost};
        for(let k in m){data[k].push(v[k]); if(data[k].length>maxPoints)data[k].shift();
          charts[m[k]].data.labels=Array(data[k].length).fill(''); charts[m[k]].data.datasets[0].data=data[k]; charts[m[k]].update();}
        let rows=''; j.logs.forEach(l=>{rows+=`<tr><td>${l.time}</td><td>${l.voltage.toFixed(2)}</td><td>${l.current.toFixed(3)}</td><td>${l.power.toFixed(2)}</td><td>${l.energy.toFixed(3)}</td><td>${l.cost.toFixed(2)}</td></tr>`;});
        document.getElementById('logTable').innerHTML=rows;
      }
      function updateLimit(v){document.getElementById('limitBox').value=v;document.getElementById('limitSlider').value=v;document.getElementById('limit').innerText=v;}
      async function saveLimit(){const v=document.getElementById('limitBox').value;await fetch('/setlimit?value='+v);alert('Batas daya disimpan: '+v+' W');}
      async function resetLog(){await fetch('/resetlog');alert('Log telah dihapus');}
      async function resetEnergy(){await fetch('/resetenergy');alert('Energi total direset');}
      function downloadCSV(){window.open('/download');}
      setInterval(fetchData,5000);
    </script>
  </body>
  </html>
  )rawliteral";
  return html;
}

// ----------------------------------------------------------------
//  SETUP
// ----------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  EEPROM.begin(EEPROM_SIZE);
  EEPROM.get(ADDR_TRIP, TRIP_THRESHOLD);
  if(isnan(TRIP_THRESHOLD) || TRIP_THRESHOLD<0) TRIP_THRESHOLD=1800.0;

  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, LOW);

  WiFi.begin(ssid,password);
  Serial.print("Menghubungkan ke WiFi ");
  while(WiFi.status()!=WL_CONNECTED){delay(500);Serial.print(".");}
  Serial.println("\nWiFi Terhubung!");
  Serial.println(WiFi.localIP());

  // --- Web routes ---
  server.on("/", [](){ server.send(200,"text/html",getPage()); });
  server.on("/on", [](){ digitalWrite(RELAY_PIN,LOW); relayStatus=true; server.send(200,"text/html",getPage()); });
  server.on("/off", [](){ digitalWrite(RELAY_PIN,HIGH); relayStatus=false; server.send(200,"text/html",getPage()); });

  server.on("/setlimit", [](){
    if(server.hasArg("value")){
      TRIP_THRESHOLD = server.arg("value").toFloat();
      EEPROM.put(ADDR_TRIP, TRIP_THRESHOLD); EEPROM.commit();
      server.send(200,"text/plain","OK");
    } else server.send(400,"text/plain","No value");
  });

  server.on("/resetlog", [](){ logIndex=0; server.send(200,"text/plain","Reset OK"); });
  server.on("/resetenergy", [](){ energy=0; lastEnergy=0; server.send(200,"text/plain","Energy reset OK"); });

  server.on("/download", [](){
    String csv="Waktu,Volt,Ampere,Watt,Energi,Cost\n";
    for(int i=0;i<logIndex;i++){
      csv+=hourLog[i].time+","+String(hourLog[i].voltage)+","+String(hourLog[i].current)+","+String(hourLog[i].power)+","+String(hourLog[i].energy)+","+String(hourLog[i].cost)+"\n";
    }
    server.send(200,"text/csv",csv);
  });

  server.on("/data", [](){
    cost = energy * 1.5;
    float hourEnergy = energy - lastEnergy;
    float hourCost = hourEnergy * 1.5;
    String json="{";
    json+="\"voltage\":"+String(voltage,2)+",";
    json+="\"current\":"+String(current,3)+",";
    json+="\"power\":"+String(power,2)+",";
    json+="\"energy\":"+String(energy,3)+",";
    json+="\"cost\":"+String(cost,2)+",";
    json+="\"hourCost\":"+String(hourCost,2)+",";
    json+="\"limit\":"+String(TRIP_THRESHOLD,0)+",";
    json += "\"relay\":" + String(relayStatus ? "true" : "false") + ",\"logs\":[";
    for(int i=0;i<logIndex;i++){
      json+="{\"time\":\""+hourLog[i].time+"\",\"voltage\":"+String(hourLog[i].voltage,2)+",\"current\":"+String(hourLog[i].current,3)+",\"power\":"+String(hourLog[i].power,2)+",\"energy\":"+String(hourLog[i].energy,3)+",\"cost\":"+String(hourLog[i].cost,2)+"}";
      if(i<logIndex-1) json+=",";
    }
    json+="]}";
    server.send(200,"application/json",json);
  });

  server.begin();
  Serial.println("Server siap diakses!");
}

// ----------------------------------------------------------------
//  LOOP
// ----------------------------------------------------------------
void loop() {
  server.handleClient();

  voltage=pzem.voltage();
  current=pzem.current();
  power=pzem.power();
  energy=pzem.energy();
  if(isnan(voltage)||isnan(current)||isnan(power)) return;

  avgPower=0.7*avgPower+0.3*power;
  power=avgPower;

  if(power>TRIP_THRESHOLD && relayStatus && millis()-lastTripTime>DEBOUNCE_TIME){
    digitalWrite(RELAY_PIN,HIGH); relayStatus=false; lastTripTime=millis();
    Serial.printf("Proteksi aktif! Melebihi %.2f W\n",TRIP_THRESHOLD);
  }

  if(millis()-lastDataSave>=HOUR_INTERVAL){
    float hourEnergy=energy-lastEnergy;
    HourData d={voltage,current,power,hourEnergy,hourEnergy*1.5,String(logIndex+1)+" Jam"};
    if(logIndex<MAX_HOURS) hourLog[logIndex++]=d;
    else {for(int i=1;i<MAX_HOURS;i++) hourLog[i-1]=hourLog[i]; hourLog[MAX_HOURS-1]=d;}
    lastEnergy=energy; lastDataSave=millis();
  }

  delay(1000);
}
