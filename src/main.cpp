#include <Arduino.h>
#include <WiFi.h>
#include <Preferences.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <ESPmDNS.h>
#include <LittleFS.h>

// ===== Settings (persisted) =====
struct Settings
{
  bool enableFilling;
  bool enableCapping;

  long pushTime;
  long fillTime;
  long capTime;
  long postPushDelay;
  long postFillDelay;
  long bottlePositioningDelay;

  // Ultrasonic detection thresholds (in microseconds of echo pulse)
  int thresholdBottleLoaded;
  int thresholdCapLoaded;
  int thresholdCapFull;

  // Rolling average window (runtime adjustable)
  int rollingAverageWindow;
};

static Settings settings = {
    /*enableFilling*/ true,
    /*enableCapping*/ false,
    /*pushTime*/ 3000L,
    /*fillTime*/ 32000L,
    /*capTime*/ 2000L,
    /*postPushDelay*/ 3000L,
    /*postFillDelay*/ 1000L,
    /*bottlePositioningDelay*/ 1000L,
    /*thresholdBottleLoaded*/ 200,
    /*thresholdCapLoaded*/ 160,
    /*thresholdCapFull*/ 160,
    /*rollingAverageWindow*/ 5};

const int conveyorPin = 14;
const int capLoaderPin = 27;
const int fillPin = 25;
const int capPin = 33;
const int pushRegisterPin = 32;

// Blue = Trigger, White = Echo
// Used to check if a bottle is loaded
const int triggerPinBottle = 4;
const int echoPinBottle = 2;

// Used to check if the cap loader is full
const int triggerPinCapFull = 23;
const int echoPinCapFull = 22;

// Used to check if the cap loader has a cap available to bottle
const int triggerPinCapLoaded = 18;
const int echoPinCapLoaded = 5;

const int MAX_ROLLING_AVG = 20;  // Absolute upper bound for rolling window
const int maxSensorBuffers = 10; // Maximum number of different sensor buffers supported

// ===== Persistence and Networking =====
Preferences prefsSettings;
Preferences prefsWifi;
AsyncWebServer server(80);
static String g_hostname;

enum MachineState
{
  STATE_STOPPED = 0,
  STATE_PAUSED = 1,
  STATE_RUNNING = 2
};

static volatile MachineState machineState = STATE_PAUSED;

static String _getChipIdSuffix()
{
  uint64_t mac = ESP.getEfuseMac();
  uint16_t suffix = (uint16_t)(mac & 0xFFFF);
  char buf[8];
  snprintf(buf, sizeof(buf), "%04X", suffix);
  return String(buf);
}

static String _getHostname()
{
  if (g_hostname.length() > 0)
  {
    return g_hostname;
  }
  g_hostname = String("bottling-machine-") + _getChipIdSuffix();
  return g_hostname;
}

static void _setupMDNS()
{
  String host = _getHostname();
  MDNS.end();
  bool ok = MDNS.begin(host.c_str());
  Serial.print("Starting mDNS: ");
  Serial.print(host);
  Serial.print(".local => ");
  Serial.println(ok ? "OK" : "FAIL");
  if (ok)
  {
    MDNS.addService("http", "tcp", 80);
  }
}

static void _applySafeOutputs()
{
  digitalWrite(conveyorPin, LOW);
  digitalWrite(capLoaderPin, LOW);
  digitalWrite(fillPin, LOW);
  digitalWrite(capPin, LOW);
  digitalWrite(pushRegisterPin, LOW);
}

static inline bool _isRunning()
{
  return machineState == STATE_RUNNING;
}

static bool _waitWithAbort(uint32_t durationMs)
{
  uint32_t startMs = millis();
  while (millis() - startMs < durationMs)
  {
    if (!_isRunning())
    {
      _applySafeOutputs();
      return false;
    }
    delay(10);
  }
  return true;
}

/* INDEX_HTML moved to LittleFS: /data/index.html */
static const char INDEX_HTML[] PROGMEM = R"HTML(
<!doctype html>
<html>
<head>
  <meta charset="utf-8"/>
  <meta name="viewport" content="width=device-width, initial-scale=1"/>
  <title>Bottling Machine</title>
  <style>
    :root{--bg:#0f172a;--card:#111827;--text:#e5e7eb;--muted:#9ca3af;--accent:#22c55e;--accent2:#60a5fa;--warn:#f59e0b;--err:#ef4444}
    *{box-sizing:border-box}
    body{margin:0;background:var(--bg);color:var(--text);font-family:system-ui,-apple-system,Segoe UI,Roboto,Ubuntu,Cantarell,Noto Sans,sans-serif;-webkit-tap-highlight-color:transparent}
    .container{max-width:960px;margin:0 auto;padding:16px}
    h1{font-size:22px;margin:8px 0 16px}
    .grid{display:grid;grid-template-columns:1fr;gap:12px}
    @media(min-width:800px){.grid{grid-template-columns:1fr 1fr}}
    .card{background:var(--card);border:1px solid #1f2937;border-radius:12px;padding:16px}
    .row{display:flex;gap:8px;align-items:center;margin:6px 0}
    label{min-width:220px;color:var(--muted)}
    input[type="text"], input[type="number"]{flex:1;background:#0b1020;color:var(--text);border:1px solid #1f2937;border-radius:8px;padding:12px 12px;font-size:16px;min-height:44px}
    input[type="checkbox"]{transform:scale(1.2)}
    .btn{background:#1f2937;border:1px solid #334155;color:var(--text);padding:12px 14px;border-radius:10px;cursor:pointer;min-height:44px;min-width:44px;touch-action:manipulation}
    .btn.primary{background:var(--accent);border:0;color:#04130a}
    .btn.alt{background:var(--accent2);border:0;color:#06131f}
    .btn.warn{background:var(--warn);border:0;color:#1a1204}
    .btn.err{background:var(--err);border:0}
    .toolbar{display:flex;gap:8px;flex-wrap:wrap}
    .btn.big{padding:16px 20px;font-size:18px}
    .btn.primary.big{box-shadow:0 8px 16px rgba(34,197,94,.25);font-weight:700}
    .btn.huge{padding:22px 30px;font-size:22px}
    .btn.primary.huge{box-shadow:0 10px 20px rgba(34,197,94,.28);font-weight:800}
    .advanced{display:none}
    body.adv .advanced{display:block}
    .slide{overflow:hidden;max-height:0;transition:max-height .25s ease}
    body.adv .slide{max-height:2000px}
    .muted{color:var(--muted);font-size:12px}
    .toast{position:fixed;right:12px;bottom:12px;background:#0b1020;border:1px solid #334155;color:var(--text);padding:10px 12px;border-radius:8px;opacity:0;transform:translateY(8px);transition:all .2s}
    .toast.show{opacity:1;transform:none}
    .kv{display:grid;grid-template-columns:1fr;gap:8px}
    @media(max-width:600px){
      .container{padding:12px}
      label{min-width:0;width:100%}
      .row{flex-direction:column;align-items:stretch}
      input[type="text"], input[type="number"]{width:100%}
      .kv{display:block}
    }
  </style>
  <script>
    const toast=(msg)=>{const t=document.getElementById('toast');t.textContent=msg;t.classList.add('show');setTimeout(()=>t.classList.remove('show'),1800)};
    const $=(id)=>document.getElementById(id);
    const api=async (p,opt)=>{const r=await fetch(p,{headers:{'Content-Type':'application/json'},...opt});return r.json().catch(()=>({}))};
    const debounce=(fn,ms)=>{let to;return (...args)=>{clearTimeout(to);to=setTimeout(()=>fn(...args),ms)}};
    const setDebounced = {};
    const lastSettings = {};
    const attachInputHandlers=()=>{
      const keys=['enableFilling','enableCapping','pushTime','fillTime','capTime','postPushDelay','postFillDelay','bottlePositioningDelay','thresholdBottleLoaded','thresholdCapLoaded','thresholdCapFull','rollingAverageWindow'];
      keys.forEach(k=>{
        const el=$(k); if(!el) return;
        if(!setDebounced[k]) setDebounced[k]=debounce((val)=>setKey(k,val), 300);
        const handler=()=>{const val=(el.type==='checkbox')?el.checked:el.value; setDebounced[k](val)};
        if(el.type==='checkbox'){
          el.addEventListener('change', handler);
        } else {
          el.addEventListener('input', handler, {passive:true});
        }
      });
    };
    const bindTap=(id,handler)=>{
      const el=$(id); if(!el) return;
      let touched=false;
      el.addEventListener('touchstart', e=>{touched=true; handler(e); e.preventDefault();}, {passive:false});
      el.addEventListener('click', e=>{ if(touched){touched=false; return;} handler(e); });
    };
    const load=async()=>{
      const st=await api('/api/status');
      $('status').textContent=`${st.connected? 'Connected':'AP mode'} ${st.ip? '('+st.ip+')':''} · State: ${st.machineState}`;
      const s=await api('/api/settings');
      const map={enableFilling:'enableFilling',enableCapping:'enableCapping',pushTime:'pushTime',fillTime:'fillTime',capTime:'capTime',postPushDelay:'postPushDelay',postFillDelay:'postFillDelay',bottlePositioningDelay:'bottlePositioningDelay',thresholdBottleLoaded:'thresholdBottleLoaded',thresholdCapLoaded:'thresholdCapLoaded',thresholdCapFull:'thresholdCapFull',rollingAverageWindow:'rollingAverageWindow'};
      Object.keys(map).forEach(k=>{const el=$(k); if(!el) return; const val=s[map[k]]; if(el.type==='checkbox'){el.checked=!!val;} else {el.value=val;} lastSettings[k]=(el.type==='checkbox')? !!val : String(val);});
      attachInputHandlers();
    };
    const setKey=async(k,v)=>{
      if(v===undefined||v===null||v===''){return;}
      if(lastSettings[k]===v){return;}
      const payload={}; payload[k]=v;
      let r=await fetch(`/api/settings`,{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(payload)});
      if(!r.ok){
        const form=new URLSearchParams(); form.set('value', String(v));
        r=await fetch(`/api/settings/${k}`,{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:form.toString()});
      }
      if(r.ok){ lastSettings[k]=v; toast('Saved'); } else { toast('Failed'); }
    };
    const saveAll=async()=>{
      const body={
        enableFilling:$('enableFilling').checked,
        enableCapping:$('enableCapping').checked,
        pushTime:+$('pushTime').value,
        fillTime:+$('fillTime').value,
        capTime:+$('capTime').value,
        postPushDelay:+$('postPushDelay').value,
        postFillDelay:+$('postFillDelay').value,
        bottlePositioningDelay:+$('bottlePositioningDelay').value,
        thresholdBottleLoaded:+$('thresholdBottleLoaded').value,
        thresholdCapLoaded:+$('thresholdCapLoaded').value,
        thresholdCapFull:+$('thresholdCapFull').value,
        rollingAverageWindow:+$('rollingAverageWindow').value,
      };
      const r=await api('/api/settings',{method:'POST',body:JSON.stringify(body)});
      toast('Settings saved');
    };
    const wifiConnect=async()=>{
      const r=await api('/api/wifi',{method:'POST',body:JSON.stringify({ssid:$('ssid').value,password:$('password').value})});
      toast(r.connected? 'Wi‑Fi connected':'Wi‑Fi failed');
      load();
    };
    const ctl=async(a)=>{await api('/api/control',{method:'POST',body:JSON.stringify({action:a})});toast(`Action: ${a}`);load();};
    window.addEventListener('DOMContentLoaded',()=>{
      bindTap('startBtn', ()=>ctl('start'));
      bindTap('pauseBtn', ()=>ctl('pause'));
      bindTap('stopBtn', ()=>ctl('stop'));
      const advPref=localStorage.getItem('advOpen');
      if(advPref==='1'){document.body.classList.add('adv');$('advToggle').textContent='Hide Advanced';}
      bindTap('advToggle',()=>{document.body.classList.toggle('adv');const open=document.body.classList.contains('adv');localStorage.setItem('advOpen',open?'1':'0');$('advToggle').textContent=open?'Hide Advanced':'Show Advanced';});
      load();
    });
  </script>
  </head>
  <body>
    <div class="container">
      <h1>Bottling Machine</h1>
      <div class="muted" id="status">Loading…</div>
      <div class="grid" style="margin-top:10px">
        <div class="card">
          <h3>Controls</h3>
          <div class="toolbar">
            <button id="startBtn" type="button" class="btn primary">Start</button>
            <button id="pauseBtn" type="button" class="btn warn">Pause</button>
            <button id="stopBtn" type="button" class="btn err">Stop</button>
          </div>
        </div>
        <div class="card advanced">
          <h3>Wi‑Fi</h3>
          <div class="row"><label>SSID</label><input id="ssid" type="text" placeholder="Network name"></div>
          <div class="row"><label>Password</label><input id="password" type="text" placeholder="Password (optional)"></div>
          <div class="toolbar"><button class="btn alt" onclick="wifiConnect()">Connect</button></div>
          <div class="muted">If connection succeeds, the AP will stop broadcasting.</div>
        </div>
      </div>

      <div class="card" style="margin-top:12px">
        <h3>Settings</h3>
        <div class="toolbar" style="margin:6px 0 10px"><button id="advToggle" class="btn">Show Advanced</button></div>
        <div class="kv">
          <div class="row"><label>Enable Filling</label><input id="enableFilling" type="checkbox"></div>
          <div></div>
          <div class="row"><label>Enable Capping</label><input id="enableCapping" type="checkbox"></div>
          <div></div>

          <div class="row"><label>Push Time (ms)</label><input id="pushTime" type="number" inputmode="numeric" pattern="[0-9]*" min="0" step="1"></div>
          <div class="row"><label>Fill Time (ms)</label><input id="fillTime" type="number" inputmode="numeric" pattern="[0-9]*" min="0" step="1"></div>
          <div class="row"><label>Cap Time (ms)</label><input id="capTime" type="number" inputmode="numeric" pattern="[0-9]*" min="0" step="1"></div>
        </div>
        <div id="advPanel" class="slide advanced">
          <div class="kv">
            <div class="row"><label>Post‑Push Delay (ms)</label><input id="postPushDelay" type="number" inputmode="numeric" pattern="[0-9]*" min="0" step="1"></div>
            <div class="row"><label>Post‑Fill Delay (ms)</label><input id="postFillDelay" type="number" inputmode="numeric" pattern="[0-9]*" min="0" step="1"></div>
            <div class="row"><label>Bottle Positioning Delay (ms)</label><input id="bottlePositioningDelay" type="number" inputmode="numeric" pattern="[0-9]*" min="0" step="1"></div>

            <div class="row"><label>Threshold Bottle Loaded</label><input id="thresholdBottleLoaded" type="number" inputmode="numeric" pattern="[0-9]*" min="0" step="1"></div>
            <div class="row"><label>Threshold Cap Loaded</label><input id="thresholdCapLoaded" type="number" inputmode="numeric" pattern="[0-9]*" min="0" step="1"></div>
            <div class="row"><label>Threshold Cap Full</label><input id="thresholdCapFull" type="number" inputmode="numeric" pattern="[0-9]*" min="0" step="1"></div>
            <div class="row"><label>Rolling Average Window</label><input id="rollingAverageWindow" type="number" inputmode="numeric" pattern="[0-9]*" min="1" max="20" step="1"></div>
          </div>
        </div>
        <div class="toolbar" style="margin-top:16px"><button class="btn primary huge" onclick="saveAll()">Save All</button></div>
      </div>
    </div>
    <div id="toast" class="toast"></div>
  </body>
</html>
)HTML";

static void loadSettings()
{
  prefsSettings.begin("bm", true);
  settings.enableFilling = prefsSettings.getBool("enableFill", settings.enableFilling);
  settings.enableCapping = prefsSettings.getBool("enableCap", settings.enableCapping);
  settings.pushTime = (long)prefsSettings.getInt("pushTime", settings.pushTime);
  settings.fillTime = (long)prefsSettings.getInt("fillTime", settings.fillTime);
  settings.capTime = (long)prefsSettings.getInt("capTime", settings.capTime);
  settings.postPushDelay = (long)prefsSettings.getInt("postPush", settings.postPushDelay);
  settings.postFillDelay = (long)prefsSettings.getInt("postFill", settings.postFillDelay);
  settings.bottlePositioningDelay = (long)prefsSettings.getInt("posDelay", settings.bottlePositioningDelay);
  settings.thresholdBottleLoaded = prefsSettings.getInt("thBottle", settings.thresholdBottleLoaded);
  settings.thresholdCapLoaded = prefsSettings.getInt("thCapLoad", settings.thresholdCapLoaded);
  settings.thresholdCapFull = prefsSettings.getInt("thCapFull", settings.thresholdCapFull);
  settings.rollingAverageWindow = prefsSettings.getInt("rollAvg", settings.rollingAverageWindow);
  prefsSettings.end();

  if (settings.rollingAverageWindow < 1)
  {
    settings.rollingAverageWindow = 1;
  }
  if (settings.rollingAverageWindow > MAX_ROLLING_AVG)
  {
    settings.rollingAverageWindow = MAX_ROLLING_AVG;
  }
}

static void saveSettings()
{
  prefsSettings.begin("bm", false);
  prefsSettings.putBool("enableFill", settings.enableFilling);
  prefsSettings.putBool("enableCap", settings.enableCapping);
  prefsSettings.putInt("pushTime", (int)settings.pushTime);
  prefsSettings.putInt("fillTime", (int)settings.fillTime);
  prefsSettings.putInt("capTime", (int)settings.capTime);
  prefsSettings.putInt("postPush", (int)settings.postPushDelay);
  prefsSettings.putInt("postFill", (int)settings.postFillDelay);
  prefsSettings.putInt("posDelay", (int)settings.bottlePositioningDelay);
  prefsSettings.putInt("thBottle", settings.thresholdBottleLoaded);
  prefsSettings.putInt("thCapLoad", settings.thresholdCapLoaded);
  prefsSettings.putInt("thCapFull", settings.thresholdCapFull);
  prefsSettings.putInt("rollAvg", settings.rollingAverageWindow);
  prefsSettings.end();
}

static bool tryConnectWifi(const String &ssid, const String &password, uint32_t timeoutMs)
{
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid.c_str(), password.c_str());
  Serial.print("Connecting to WiFi SSID: ");
  Serial.println(ssid);
  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - start) < timeoutMs)
  {
    delay(250);
    Serial.print(".");
  }
  Serial.println();
  return WiFi.status() == WL_CONNECTED;
}

static void startAP()
{
  String ssid = String("BottlingMachine-") + _getChipIdSuffix();
  WiFi.mode(WIFI_AP_STA);
  bool ok = WiFi.softAP(ssid.c_str());
  Serial.print("Starting AP: ");
  Serial.print(ssid);
  Serial.print(" => ");
  Serial.println(ok ? "OK" : "FAIL");
  WiFi.softAPsetHostname(_getHostname().c_str());
  _setupMDNS();
}

static void stopAP()
{
  if (WiFi.getMode() & WIFI_AP)
  {
    WiFi.softAPdisconnect(true);
  }
}

static void setupNetworking()
{
  // Load WiFi credentials
  prefsWifi.begin("wifi", true);
  String ssid = prefsWifi.getString("ssid", "");
  String pass = prefsWifi.getString("pass", "");
  prefsWifi.end();

  if (ssid.length() > 0)
  {
    WiFi.setHostname(_getHostname().c_str());
    if (tryConnectWifi(ssid, pass, 15000))
    {
      Serial.print("WiFi connected, IP: ");
      Serial.println(WiFi.localIP());
      stopAP();
      _setupMDNS();
      return;
    }
  }
  startAP();
}

static void sendJson(AsyncWebServerRequest *request, const JsonDocument &doc)
{
  String out;
  serializeJson(doc, out);
  request->send(200, "application/json", out);
}

static void serializeSettings(JsonDocument &doc)
{
  doc["enableFilling"] = settings.enableFilling;
  doc["enableCapping"] = settings.enableCapping;
  doc["pushTime"] = settings.pushTime;
  doc["fillTime"] = settings.fillTime;
  doc["capTime"] = settings.capTime;
  doc["postPushDelay"] = settings.postPushDelay;
  doc["postFillDelay"] = settings.postFillDelay;
  doc["bottlePositioningDelay"] = settings.bottlePositioningDelay;
  doc["thresholdBottleLoaded"] = settings.thresholdBottleLoaded;
  doc["thresholdCapLoaded"] = settings.thresholdCapLoaded;
  doc["thresholdCapFull"] = settings.thresholdCapFull;
  doc["rollingAverageWindow"] = settings.rollingAverageWindow;
}

static String machineStateToString()
{
  switch (machineState)
  {
  case STATE_STOPPED:
    return "stopped";
  case STATE_PAUSED:
    return "paused";
  case STATE_RUNNING:
    return "running";
  }
  return "unknown";
}

static bool parseBool(const String &v)
{
  if (v.equalsIgnoreCase("true") || v == "1" || v.equalsIgnoreCase("on") || v.equalsIgnoreCase("yes"))
    return true;
  return false;
}

static bool updateSettingByName(const String &name, const String &value)
{
  if (name == "enableFilling")
  {
    settings.enableFilling = parseBool(value);
  }
  else if (name == "enableCapping")
  {
    settings.enableCapping = parseBool(value);
  }
  else if (name == "pushTime")
  {
    settings.pushTime = value.toInt();
  }
  else if (name == "fillTime")
  {
    settings.fillTime = value.toInt();
  }
  else if (name == "capTime")
  {
    settings.capTime = value.toInt();
  }
  else if (name == "postPushDelay")
  {
    settings.postPushDelay = value.toInt();
  }
  else if (name == "postFillDelay")
  {
    settings.postFillDelay = value.toInt();
  }
  else if (name == "bottlePositioningDelay")
  {
    settings.bottlePositioningDelay = value.toInt();
  }
  else if (name == "thresholdBottleLoaded")
  {
    settings.thresholdBottleLoaded = value.toInt();
  }
  else if (name == "thresholdCapLoaded")
  {
    settings.thresholdCapLoaded = value.toInt();
  }
  else if (name == "thresholdCapFull")
  {
    settings.thresholdCapFull = value.toInt();
  }
  else if (name == "rollingAverageWindow")
  {
    int v = value.toInt();
    if (v < 1)
      v = 1;
    if (v > MAX_ROLLING_AVG)
      v = MAX_ROLLING_AVG;
    settings.rollingAverageWindow = v;
  }
  else
  {
    return false;
  }
  saveSettings();
  return true;
}

static void setupServer()
{
  DefaultHeaders::Instance().addHeader("Access-Control-Allow-Origin", "*");
  DefaultHeaders::Instance().addHeader("Access-Control-Allow-Methods", "GET,POST,OPTIONS");
  DefaultHeaders::Instance().addHeader("Access-Control-Allow-Headers", "Content-Type");

  server.serveStatic("/", LittleFS, "/").setDefaultFile("index.html");

  // Preflight + dynamic routes (per-key settings)
  server.onNotFound([](AsyncWebServerRequest *request)
                    {
    if (request->method() == HTTP_OPTIONS)
    {
      request->send(200);
      return;
    }
    String url = request->url();
    if (request->method() == HTTP_POST && url.startsWith("/api/settings/") && url.length() > 15)
    {
      String name = url.substring(String("/api/settings/").length());
      String val;
      if (request->hasParam("value", true))
      {
        val = request->getParam("value", true)->value();
      }
      else
      {
        val = request->arg("plain");
      }
      if (val.length() == 0)
      {
        request->send(400, "application/json", "{\"error\":\"Missing value\"}");
        return;
      }
      if (!updateSettingByName(name, val))
      {
        request->send(404, "application/json", "{\"error\":\"Unknown setting\"}");
        return;
      }
      StaticJsonDocument<256> doc;
      doc["ok"] = true;
      doc["name"] = name;
      doc["value"] = val;
      sendJson(request, doc);
      return;
    }
    request->send(404, "application/json", "{\"error\":\"Not found\"}"); });

  server.on("/api/status", HTTP_GET, [](AsyncWebServerRequest *request)
            {
    StaticJsonDocument<256> doc;
    bool connected = WiFi.status() == WL_CONNECTED;
    doc["connected"] = connected;
    doc["ip"] = connected ? WiFi.localIP().toString() : String("");
    doc["ap"] = (WiFi.getMode() & WIFI_AP) ? WiFi.softAPSSID() : String("");
    doc["hostname"] = _getHostname();
    doc["mdns"] = _getHostname() + String(".local");
    doc["machineState"] = machineStateToString();
    sendJson(request, doc); });

  server.on("/api/settings", HTTP_GET, [](AsyncWebServerRequest *request)
            {
    StaticJsonDocument<512> doc;
    serializeSettings(doc);
    sendJson(request, doc); });

  server.on("/api/settings", HTTP_POST, [](AsyncWebServerRequest *request) {}, NULL, [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total)
            {
              if (index == 0)
              {
                request->_tempObject = new String();
              }
              String *body = reinterpret_cast<String *>(request->_tempObject);
              body->concat((const char *)data, len);
              if (index + len == total)
              {
                StaticJsonDocument<512> docIn;
                DeserializationError err = deserializeJson(docIn, *body);
                delete body;
                request->_tempObject = nullptr;
                if (!err)
                {
                  for (JsonPair kv : docIn.as<JsonObject>())
                  {
                    updateSettingByName(String(kv.key().c_str()), kv.value().as<String>());
                  }
                  StaticJsonDocument<512> doc;
                  serializeSettings(doc);
                  sendJson(request, doc);
                }
                else
                {
                  request->send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
                }
              } });

  server.on("/api/settings/", HTTP_POST, [](AsyncWebServerRequest *request)
            { request->send(400, "application/json", "{\"error\":\"Missing setting name\"}"); });

  server.on("/api/settings", HTTP_ANY, [](AsyncWebServerRequest *request)
            { request->send(405); });

  server.on("/api/settings/", HTTP_ANY, [](AsyncWebServerRequest *request)
            { request->send(405); });

  server.on("/api/wifi", HTTP_POST, [](AsyncWebServerRequest *request) {}, NULL, [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total)
            {
              if (index == 0)
              {
                request->_tempObject = new String();
              }
              String *body = reinterpret_cast<String *>(request->_tempObject);
              body->concat((const char *)data, len);
              if (index + len == total)
              {
                StaticJsonDocument<256> docIn;
                DeserializationError err = deserializeJson(docIn, *body);
                String ssid = docIn["ssid"].as<String>();
                String pass = docIn["password"].as<String>();
                delete body;
                request->_tempObject = nullptr;
                bool connected = false;
                String ip = "";
                if (!err && ssid.length() > 0)
                {
                  connected = tryConnectWifi(ssid, pass, 15000);
                  if (connected)
                  {
                    prefsWifi.begin("wifi", false);
                    prefsWifi.putString("ssid", ssid);
                    prefsWifi.putString("pass", pass);
                    prefsWifi.end();
                    stopAP();
                    ip = WiFi.localIP().toString();
                    _setupMDNS();
                  }
                }
                StaticJsonDocument<256> doc;
                doc["connected"] = connected;
                doc["ip"] = ip;
                sendJson(request, doc);
              } });

  server.on("/api/control", HTTP_POST, [](AsyncWebServerRequest *request) {}, NULL, [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total)
            {
              if (index == 0)
              {
                request->_tempObject = new String();
              }
              String *body = reinterpret_cast<String *>(request->_tempObject);
              body->concat((const char *)data, len);
              if (index + len == total)
              {
                StaticJsonDocument<128> docIn;
                DeserializationError err = deserializeJson(docIn, *body);
                delete body;
                request->_tempObject = nullptr;
                if (!err)
                {
                  String action = docIn["action"].as<String>();
                  if (action == "start")
                  {
                    machineState = STATE_RUNNING;
                  }
                  else if (action == "pause")
                  {
                    machineState = STATE_PAUSED;
                    _applySafeOutputs();
                  }
                  else if (action == "stop")
                  {
                    machineState = STATE_STOPPED;
                    _applySafeOutputs();
                  }
                  StaticJsonDocument<128> doc;
                  doc["machineState"] = machineStateToString();
                  sendJson(request, doc);
                }
                else
                {
                  request->send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
                }
              } });

  server.begin();
  Serial.println("HTTP server started");
}

// 🧮 Calculate mean of the last N readings from a circular buffer
float _calculateMean(const float *readings, int bufferSize, int lastN, int endIndex)
{
  if (lastN <= 0)
  {
    return 0;
  }
  float sum = 0;
  for (int i = 0; i < lastN; i++)
  {
    int idx = (endIndex - 1 - i + bufferSize) % bufferSize;
    sum += readings[idx];
  }
  return sum / lastN;
}

void setup()
{
  // Initialize serial communication for debugging
  Serial.begin(115200);

  // Mount LittleFS for serving static assets
  if (!LittleFS.begin(true))
  {
    Serial.println("LittleFS mount failed");
  }

  loadSettings();
  setupNetworking();

  pinMode(conveyorPin, OUTPUT);
  pinMode(capLoaderPin, OUTPUT);
  pinMode(fillPin, OUTPUT);
  pinMode(capPin, OUTPUT);
  pinMode(pushRegisterPin, OUTPUT);

  pinMode(triggerPinBottle, OUTPUT);
  pinMode(echoPinBottle, INPUT);
  pinMode(triggerPinCapFull, OUTPUT);
  pinMode(echoPinCapFull, INPUT);
  pinMode(triggerPinCapLoaded, OUTPUT);
  pinMode(echoPinCapLoaded, INPUT);

  Serial.println("Pin setup complete");

  setupServer();
}

float _getRawUltrasonicSensorReading(int triggerPin, int echoPin)
{
  digitalWrite(triggerPin, HIGH);
  delayMicroseconds(10);
  digitalWrite(triggerPin, LOW);
  return pulseIn(echoPin, HIGH);
}

// 🎯 UNIVERSAL SENSOR BUFFER SYSTEM: Map-like structure for per-pin rolling averages
struct SensorBuffer
{
  float readings[MAX_ROLLING_AVG];
  int readingIndex;
  int totalReadingCount;
};

// 🏛️ SENSOR BUFFER REGISTRY: Static storage for multiple sensor buffers
static SensorBuffer sensorBuffers[maxSensorBuffers]; // Support up to maxSensorBuffers different trigger pins
static int registeredPins[maxSensorBuffers];         // Track which pins are registered
static int bufferCount = 0;                          // Number of registered buffers

// 🔍 BUFFER RECONNAISSANCE: Find or create buffer for specific trigger pin
SensorBuffer *_getSensorBuffer(int triggerPin)
{
  // 🎯 EXISTING BUFFER SEARCH: Check if pin already registered
  for (int i = 0; i < bufferCount; i++)
  {
    if (registeredPins[i] == triggerPin)
    {
      return &sensorBuffers[i];
    }
  }

  // 🚀 NEW BUFFER CREATION: Register new pin if space available
  if (bufferCount < maxSensorBuffers)
  {
    registeredPins[bufferCount] = triggerPin;
    SensorBuffer *newBuffer = &sensorBuffers[bufferCount];
    // 🛡️ BUFFER INITIALIZATION: Zero out new buffer
    for (int i = 0; i < MAX_ROLLING_AVG; i++)
    {
      newBuffer->readings[i] = 0;
    }
    newBuffer->readingIndex = 0;
    newBuffer->totalReadingCount = 0;
    bufferCount++;
    return newBuffer;
  }

  // 💀 BUFFER OVERFLOW PROTECTION: Return first buffer as fallback
  return &sensorBuffers[0];
}

float _getUltrasonicSensorDistance(int triggerPin, int echoPin)
{
  // 🎯 BUFFER ACQUISITION: Get dedicated buffer for this trigger pin
  SensorBuffer *buffer = _getSensorBuffer(triggerPin);

  // 📡 SENSOR RECONNAISSANCE: Get raw distance measurement
  float rawDistance = _getRawUltrasonicSensorReading(triggerPin, echoPin);

  // 💾 TACTICAL DATA STORAGE: Store reading in pin-specific circular buffer
  buffer->readings[buffer->readingIndex] = rawDistance;
  buffer->readingIndex = (buffer->readingIndex + 1) % MAX_ROLLING_AVG;
  buffer->totalReadingCount++;

  // 🎯 INITIALIZATION PROTOCOL: Return default for first settings.rollingAverageWindow readings
  if (buffer->totalReadingCount < settings.rollingAverageWindow)
  {
    return 1000; // 🛡️ BUFFER WARMING: Return safe default until buffer full
  }

  // ⚡ MEAN CALCULATION: Return average of last rollingAverageWindow readings for this specific pin
  int window = settings.rollingAverageWindow;
  if (window < 1)
  {
    window = 1;
  }
  if (window > MAX_ROLLING_AVG)
  {
    window = MAX_ROLLING_AVG;
  }
  float mean = _calculateMean(buffer->readings, MAX_ROLLING_AVG, window, buffer->readingIndex);
  if (mean < 0.01)
  {
    return 1000;
  }
  return mean;
}

float getBottleDistance()
{
  return _getUltrasonicSensorDistance(triggerPinBottle, echoPinBottle);
}

float getCapLoadedDistance()
{
  // 🔧 OPERATION CHECK: Return safe distance when capping is disabled
  if (!settings.enableCapping)
  {
    return 50; // Return distance indicating cap is loaded
  }
  return _getUltrasonicSensorDistance(triggerPinCapLoaded, echoPinCapLoaded);
}
float getCapFullDistance()
{
  // 🔧 OPERATION CHECK: Return safe distance when capping is disabled
  if (!settings.enableCapping)
  {
    return 50; // Return distance indicating cap loader is full
  }
  return _getUltrasonicSensorDistance(triggerPinCapFull, echoPinCapFull);
}

bool isCapLoaded()
{
  // 🔧 OPERATION CHECK: Assume cap is always loaded when capping is disabled
  if (!settings.enableCapping)
  {
    Serial.println("🚫 CAPPING DISABLED: Assuming cap is loaded");
    digitalWrite(capLoaderPin, LOW); // Stop cap loader when capping disabled
    return true;
  }

  const int maxDistance = settings.thresholdCapLoaded;
  float capLoadedDistance = getCapLoadedDistance();
  float capFullDistance = getCapFullDistance();

  bool isCapLoaded = capLoadedDistance < maxDistance;
  bool isCapFull = capFullDistance < settings.thresholdCapFull;

  if (!isCapFull)
  {
    digitalWrite(capLoaderPin, HIGH);
    Serial.println("🏆 CAPPER NOT FULL: Cap loader running");
  }
  else
  {
    digitalWrite(capLoaderPin, LOW);
    Serial.println("🏆 CAPPER FULL: Cap loader stopped");
  }

  if (isCapLoaded)
  {
    Serial.println("🏆 CAP LOADED: Distance = ");
    Serial.println(capLoadedDistance);
    return true;
  }
  else
  {
    Serial.print("🏆 CAP NOT LOADED: Distance = ");
    Serial.println(capLoadedDistance);
    return false;
  }
}

bool isBottleLoaded()
{
  const int maxDistance = settings.thresholdBottleLoaded;
  float distance = getBottleDistance();

  if (distance < maxDistance)
  {
    digitalWrite(conveyorPin, LOW);
    Serial.print("🏆 BOTTLE LOADED: Conveyor stopped, Distance = ");
    Serial.println(distance);
    return true;
  }
  else
  {
    digitalWrite(conveyorPin, HIGH);
    Serial.print("🏆 BOTTLE NOT LOADED: Conveyor running, Distance = ");
    Serial.println(distance);
    return false;
  }
}

void loadBottle()
{
  // ⚔️ CONVEYOR DOMINATION PROTOCOL: Run until bottle is loaded
  Serial.println("🚀 CONVEYOR ACTIVATION: Running until bottle loaded");

  // 🎯 TACTICAL LOOP: Monitor bottle loading status
  while (_isRunning() && !isBottleLoaded())
  {
    // 📡 CONTINUOUS RECONNAISSANCE: Check bottle position
    float currentBottleDistance = getBottleDistance();
    Serial.print("🔍 BOTTLE TRACKING: Distance = ");
    Serial.println(currentBottleDistance);

    // ⚡ BRIEF TACTICAL PAUSE: Allow sensor readings to stabilize
    if (!_waitWithAbort(50))
    {
      Serial.println("⛔ LOAD BOTTLE ABORTED");
      return;
    }
  }

  Serial.println("🏆 BOTTLE LOADED: Conveyor stopped");
}

void capBottle()
{
  // 🔧 OPERATION CHECK: Skip if capping is disabled
  if (!settings.enableCapping)
  {
    Serial.println("🚫 CAPPING DISABLED: Skipping cap sequence");
    return;
  }

  while (_isRunning() && isCapLoaded() == false)
  {
    isBottleLoaded();
    if (!_waitWithAbort(50))
    {
      Serial.println("⛔ CAP BOTTLE ABORTED");
      return;
    }
  }

  // ⚔️ BOTTLE CAP PROTOCOL: Execute 2-second cap sequence
  Serial.println("🚀 BOTTLE CAP ACTIVATION: Initiating cap sequence");

  // 🎯 TACTICAL ENGAGEMENT: Activate cap mechanism
  digitalWrite(capPin, HIGH);
  Serial.println("⚡ CAP MECHANISM: Activated for 2 seconds");

  // ⏱️ TIMED OPERATION: Maintain cap for precise duration
  if (!_waitWithAbort(settings.capTime))
  {
    digitalWrite(capPin, LOW);
    Serial.println("⛔ CAP SEQUENCE ABORTED");
    return;
  }

  // 🛡️ MISSION COMPLETE: Deactivate cap mechanism
  digitalWrite(capPin, LOW);
  Serial.println("🏆 CAP SEQUENCE COMPLETE: Bottle capped successfully");
}

void pushBottle()
{

  // ⚔️ BOTTLE PUSH PROTOCOL: Execute push sequence
  Serial.println("🚀 BOTTLE PUSH ACTIVATION: Initiating push sequence");

  while (_isRunning() && isBottleLoaded() == false)
  {
    isCapLoaded();
    if (!_waitWithAbort(50))
    {
      Serial.println("⛔ PUSH BOTTLE ABORTED");
      return;
    }
  }

  // 🎯 BOTTLE POSITIONING: Keep conveyor running to position bottle properly
  digitalWrite(conveyorPin, HIGH);
  Serial.print("🎯 BOTTLE POSITIONING: Conveyor running for ");
  Serial.print(settings.bottlePositioningDelay / 1000.0);
  Serial.println(" seconds to position bottle");
  if (!_waitWithAbort(settings.bottlePositioningDelay))
  {
    digitalWrite(conveyorPin, LOW);
    Serial.println("⛔ POSITIONING ABORTED");
    return;
  }

  // 🛑 CONVEYOR STOP: Ensure conveyor is stopped during push operation
  digitalWrite(conveyorPin, LOW);
  Serial.println("🛑 CONVEYOR STOPPED: For push operation");

  // 🎯 TACTICAL ENGAGEMENT: Activate push mechanism
  digitalWrite(pushRegisterPin, HIGH);
  Serial.print("⚡ PUSH MECHANISM: Activated for ");
  Serial.print(settings.pushTime / 1000.0);
  Serial.println(" seconds");

  // ⏱️ TIMED OPERATION: Maintain push for precise duration
  if (!_waitWithAbort(settings.pushTime))
  {
    digitalWrite(pushRegisterPin, LOW);
    Serial.println("⛔ PUSH ABORTED");
    return;
  }

  // 🛡️ MISSION COMPLETE: Deactivate push mechanism
  digitalWrite(pushRegisterPin, LOW);
  Serial.println("🏆 PUSH SEQUENCE COMPLETE: Bottle pushed successfully");

  // ⏳ POST-PUSH DELAY: Wait before resuming operations
  Serial.print("⏳ POST-PUSH DELAY: Waiting ");
  Serial.print(settings.postPushDelay / 1000.0);
  Serial.println(" seconds before resuming operations");
  if (!_waitWithAbort(settings.postPushDelay))
  {
    Serial.println("⛔ POST-PUSH DELAY ABORTED");
    return;
  }
  Serial.println("✅ POST-PUSH DELAY COMPLETE: Resuming operations");

  if (_isRunning())
  {
    capBottle();
  }
}

void fillBottle()
{
  // 🔧 OPERATION CHECK: Skip if filling is disabled
  if (!settings.enableFilling)
  {
    Serial.println("🚫 FILLING DISABLED: Skipping fill sequence");
    return;
  }

  // ⚔️ BOTTLE FILL PROTOCOL: Execute 5-second fill sequence
  Serial.println("🚀 BOTTLE FILL ACTIVATION: Initiating fill sequence");

  while (_isRunning() && isBottleLoaded() == false)
  {
    isCapLoaded();
    if (!_waitWithAbort(50))
    {
      Serial.println("⛔ FILL BOTTLE ABORTED");
      return;
    }
  }

  // 🎯 TACTICAL ENGAGEMENT: Activate fill mechanism
  digitalWrite(fillPin, HIGH);
  Serial.print("⚡ FILL MECHANISM: Activated for ");
  Serial.print(settings.fillTime / 1000.0);
  Serial.println(" seconds");

  // ⏱️ TIMED OPERATION: Maintain fill for precise duration
  if (!_waitWithAbort(settings.fillTime))
  {
    digitalWrite(fillPin, LOW);
    Serial.println("⛔ FILL SEQUENCE ABORTED");
    return;
  }

  // 🛡️ MISSION COMPLETE: Deactivate fill mechanism
  digitalWrite(fillPin, LOW);
  Serial.println("🏆 FILL SEQUENCE COMPLETE: Bottle filled successfully");

  // ⏳ POST-FILL DELAY: Wait before next push operation
  Serial.print("⏳ POST-FILL DELAY: Waiting ");
  Serial.print(settings.postFillDelay / 1000.0);
  Serial.println(" seconds before next operation");
  if (!_waitWithAbort(settings.postFillDelay))
  {
    Serial.println("⛔ POST-FILL DELAY ABORTED");
    return;
  }
  Serial.println("✅ POST-FILL DELAY COMPLETE: Ready for next operation");
}

void loop()
{
  if (machineState == STATE_STOPPED)
  {
    delay(100);
    return;
  }
  if (machineState == STATE_PAUSED)
  {
    // Keep safety outputs applied while paused
    _applySafeOutputs();
    delay(100);
    return;
  }

  // STATE_RUNNING
  while ((isBottleLoaded() == false || isCapLoaded() == false) && _isRunning())
  {
    if (!_waitWithAbort(50))
    {
      return;
    }
  }
  if (!_isRunning())
  {
    return;
  }
  pushBottle();
  if (!_isRunning())
    return;
  pushBottle();
  if (!_isRunning())
    return;
  pushBottle();

  if (settings.enableFilling && _isRunning())
  {
    fillBottle();
  }

  if (!_isRunning())
  {
    return;
  }
  pushBottle();

  if (settings.enableFilling && _isRunning())
  {
    fillBottle();
  }
}