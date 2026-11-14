// The updater: holding WARP while switching the module on starts it (hw::startUpdater). It opens an open Wi-Fi
// network "Tonwerk-Modul", serves a page at http://192.168.4.1 that takes a whole image (the same .bin flashed at 0x0
// over USB), writes the instrument into the partition "factory" (and the data an image carries into "tables"), checks
// it and starts it. For Tonwerk Granular the page also reads the sources from "tables" and writes back a set with the
// user's own sounds (dsp/GrainLayout.h). The radio is on only here; the instruments never start it.
//
// LEDs: blue blinking, waiting for an image; cyan, receiving; green, done (restarts into the instrument); red, the
// image was refused (the page says why) or the instrument's slot holds nothing that starts. WARP pressed while
// waiting goes back to the instrument without an update.

#include "hw/Hardware.h"
#include "hw/Pins.h"
#include "dsp/GrainLayout.h"
#include "hw/UpdateImage.h"

#include "driver/gpio.h"
#include "esp_event.h"
#include "esp_flash.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdio>
#include <cstring>

namespace
{
const char* kTag = "updater";
constexpr char kNetwork[] = "Tonwerk-Modul";
/** Flash is erased a 64 KB block at a time, just ahead of the bytes that arrive. */
constexpr uint32_t kBlock = 0x10000;
/** Nobody uploads for this long: back to the instrument. */
constexpr int64_t kIdleMicros = 15LL * 60 * 1000000;
/** Transmit power in quarter dBm. The computer stands next to the module, and the ESP32's full 20 dBm draws current
 *  peaks of a few hundred mA that the instruments (which never use the radio) never ask of the module's 5 V and the
 *  DevKit's regulator; a dip there restarts the updater and the network drops. 11 dBm reaches across a room. */
constexpr int8_t kTxPower = 44;
/** After a restart by a voltage dip: 2 dBm, still enough for a computer a metre away. */
constexpr int8_t kTxPowerLow = 8;

/** Why this start happened; a restart by a voltage dip, a crash or a watchdog is said on the page. */
esp_reset_reason_t startReason = ESP_RST_UNKNOWN;
enum class Phase { waiting, receiving, done, failed };
std::atomic<Phase> phase { Phase::waiting };
std::atomic<int64_t> lastActivity { 0 };

const esp_partition_t* factory = nullptr;
const esp_partition_t* tables = nullptr;

const char kPage[] = R"page(<!doctype html>
<html lang="de"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Tonwerk Modul</title>
<style>
body{font-family:system-ui,sans-serif;background:#0b0b10;color:#e8e8e8;max-width:34rem;margin:2rem auto;padding:0 1rem}
h1{color:#fcee0a;font-size:1.6rem}button{background:#fcee0a;color:#0b0b10;border:0;padding:.7rem 1.4rem;font-size:1rem}
input{margin:1rem 0}progress{width:100%;height:1.2rem}p.en{color:#9a9aa8}
h2{color:#00f0ff;font-size:1.2rem;margin-top:2.5rem}canvas{width:100%;height:6rem}td{padding:.2rem .4rem}td input{margin:0}
</style></head><body>
<h1>Tonwerk Modul</h1>
<p>Image wählen, dieselbe .bin wie für USB (z.&nbsp;B. tonwerk-modul.bin, tonwerk-granular.bin), und hochladen. Danach startet das Modul neu.</p>
<p class="en">Choose an image, the same .bin as for USB, and upload it. The module restarts afterwards.</p>
%NOTE%<input type="file" id="file" accept=".bin"><br><button id="go">Hochladen / Upload</button>
<p><progress id="bar" max="1" value="0"></progress></p><p id="state"></p>
<script>
const $=id=>document.getElementById(id);
$('go').onclick=()=>{
  const f=$('file').files[0];
  if(!f){$('state').textContent='Keine Datei gewählt. / No file chosen.';return}
  const x=new XMLHttpRequest();
  x.open('POST','/update');
  x.upload.onprogress=e=>{if(e.lengthComputable)$('bar').value=e.loaded/e.total};
  x.onload=()=>{$('state').textContent=x.responseText};
  x.onerror=()=>{$('state').textContent='Verbindung abgebrochen. / Connection lost.'};
  $('state').textContent='Lädt hoch und schreibt … / Uploading and writing …';
  x.send(f);
};
</script>
<h2>Eigene Klänge für Tonwerk Granular / Own sounds</h2>
<p>Ersetzt Quellen von Tonwerk Granular durch eigene Aufnahmen (WAV, MP3, M4A …), zusammen bis 36&nbsp;s; für eine längere erst andere Quellen kürzen (Zahl neben dem Namen). Das Modul spielt sie bei 0&nbsp;V in ihrer Tonhöhe (C4). Die Werksklänge kommen mit tonwerk-granular.bin (oben) zurück.</p>
<p class="en">Replaces Tonwerk Granular's sources with recordings of your own, up to 36 s together; shorten others first for a longer one. The factory sounds come back with tonwerk-granular.bin (above).</p>
<button id="load">Quellen vom Modul lesen / Read sources</button>
<p><progress id="lbar" max="1" value="0"></progress></p><p id="lstate"></p>
<div id="editor" hidden>
<table id="slots"></table>
<p id="budget"></p>
<p>Datei für die gewählte Quelle / File for the chosen source:<br><input type="file" id="audio" accept="audio/*,.wav,.mp3,.m4a,.aif,.aiff,.flac,.ogg"></p>
<div id="cut" hidden>
<canvas id="wave" width="1000" height="120"></canvas>
<p>Start <input type="range" id="start" min="0" max="1" step="0.001" value="0"> <span id="startS"></span><br>
Länge / length <input type="number" id="len" min="0.5" step="0.1" value="4"> s (max <span id="maxS"></span> s)<br>
<label><input type="checkbox" id="norm" checked> Pegel angleichen / normalise</label></p>
<button id="listen">Anhören / Listen</button> <button id="take">Übernehmen / Use</button>
</div>
<p><button id="write">Aufs Modul schreiben / Write to the module</button></p>
<p><progress id="wbar" max="1" value="0"></progress></p><p id="wstate"></p>
</div>
<script>
const RATE=48000,COUNT=9,BUDGET=9*192000,MIN=24000,HEAD=4096,MAGIC=0x52475754;
const FACTORY=['Chor','Glas','Streicher','Zupfen','Orgel','Wind','Metall','Sinus','Rhythmus'];
let slots=null,chosen=0,clip=null,ctx=null;
const sec=n=>(n/RATE).toFixed(1);
const used=except=>slots.reduce((a,s,i)=>a+(i===except?0:s.data.length),0);
function audio(){if(!ctx)ctx=new (window.AudioContext||window.webkitAudioContext)();return ctx}
function show(){
  const t=$('slots');t.innerHTML='';
  slots.forEach((s,i)=>{
    const r=t.insertRow();
    r.innerHTML='<td><input type="radio" name="slot"'+(i===chosen?' checked':'')+'></td><td>'+(i+1)+'</td><td></td><td></td>';
    r.cells[2].textContent=s.name;
    const n=document.createElement('input');n.type='number';n.min='0.5';n.step='0.1';n.style.width='4.5rem';
    n.value=sec(s.data.length);
    n.onchange=()=>{const k=Math.max(MIN,Math.min(s.data.length,Math.round(+n.value*RATE)));s.data=s.data.slice(0,k);show()};
    r.cells[3].append(n,' s');
    r.cells[0].firstChild.onchange=()=>{chosen=i;limits()};
  });
  $('budget').textContent='Belegt / used: '+sec(used(-1))+' von / of 36 s';
  limits();
}
function limits(){
  if(!clip)return;
  const room=BUDGET-used(chosen);
  const from=Math.round(+$('start').value*clip.length);
  const max=Math.min(room,clip.length-from);
  $('maxS').textContent=sec(max);
  $('startS').textContent=sec(from)+' s';
  $('len').max=(max/RATE).toFixed(1);
  if(+$('len').value>max/RATE)$('len').value=(max/RATE).toFixed(1);
  draw(from,Math.round(+$('len').value*RATE));
}
function draw(from,n){
  const c=$('wave'),g=c.getContext('2d'),w=c.width,h=c.height,per=clip.length/w;
  g.fillStyle='#16161e';g.fillRect(0,0,w,h);
  g.fillStyle='rgba(252,238,10,.18)';g.fillRect(from/per,0,n/per,h);
  g.fillStyle='#00f0ff';
  for(let x=0;x<w;x++){let m=0;const a=Math.floor(x*per),b=Math.min(clip.length,Math.floor((x+1)*per));
    for(let i=a;i<b;i++)m=Math.max(m,Math.abs(clip[i]));g.fillRect(x,h/2-m*h/2,1,Math.max(1,m*h))}
}
function selection(){
  const from=Math.round(+$('start').value*clip.length);
  const n=Math.max(MIN,Math.min(Math.round(+$('len').value*RATE),clip.length-from,BUDGET-used(chosen)));
  const out=clip.slice(from,from+n);
  let peak=0;for(const v of out)peak=Math.max(peak,Math.abs(v));
  const gain=$('norm').checked&&peak>0?0.9/peak:1,fade=240;
  for(let i=0;i<out.length;i++){let f=1;if(i<fade)f=i/fade;else if(i>=out.length-fade)f=(out.length-1-i)/fade;out[i]*=gain*f}
  return out;
}
$('load').onclick=()=>{
  const x=new XMLHttpRequest();x.open('GET','/sources');x.responseType='arraybuffer';
  x.onprogress=e=>{const t=+x.getResponseHeader('X-Total');if(t)$('lbar').value=e.loaded/t};
  x.onload=()=>{
    if(x.status!==200){$('lstate').textContent=new TextDecoder().decode(x.response);return}
    const b=x.response,v=new DataView(b);
    if(v.getUint32(0,true)!==MAGIC){$('lstate').textContent='Unbekannte Daten. / Unknown data.';return}
    const version=v.getUint32(4,true);let at=HEAD;slots=[];
    for(let i=0;i<COUNT;i++){
      const n=version===1?192000:v.getUint32(12+4*i,true);
      let name=FACTORY[i];
      if(version===2){const raw=new Uint8Array(b,48+32*i,32);const z=raw.indexOf(0);name=new TextDecoder().decode(raw.slice(0,z<0?32:z))||name}
      slots.push({name,data:new Int16Array(b.slice(at,at+2*n))});at+=2*n;
    }
    $('lstate').textContent='Gelesen. Quelle wählen, Datei wählen, Ausschnitt übernehmen, dann schreiben. / Read.';
    $('editor').hidden=false;show();
  };
  x.onerror=()=>{$('lstate').textContent='Verbindung abgebrochen. / Connection lost.'};
  $('lstate').textContent='Liest … / Reading …';x.send();
};
$('audio').onchange=async()=>{
  const f=$('audio').files[0];if(!f)return;
  $('wstate').textContent='Dekodiert … / Decoding …';
  try{
    const d=await audio().decodeAudioData(await f.arrayBuffer());
    const off=new OfflineAudioContext(1,Math.ceil(d.duration*RATE),RATE);
    const src=off.createBufferSource();src.buffer=d;src.connect(off.destination);src.start();
    clip=(await off.startRendering()).getChannelData(0);
    clip.fileName=f.name.replace(/\.[^.]*$/,'');
    $('cut').hidden=false;$('start').value=0;$('len').value=Math.min(4,clip.length/RATE).toFixed(1);
    $('wstate').textContent='';limits();
  }catch(e){$('wstate').textContent='Die Datei lässt sich nicht lesen. / Cannot decode the file.'}
};
$('start').oninput=limits;$('len').oninput=limits;
$('wave').onclick=e=>{const r=$('wave').getBoundingClientRect();$('start').value=(e.clientX-r.left)/r.width;limits()};
$('listen').onclick=()=>{
  const s=selection(),a=audio(),b=a.createBuffer(1,s.length,RATE);b.copyToChannel(s,0);
  const p=a.createBufferSource();p.buffer=b;p.connect(a.destination);p.start();
};
$('take').onclick=()=>{
  const s=selection(),d=new Int16Array(s.length);
  for(let i=0;i<s.length;i++)d[i]=Math.max(-32767,Math.min(32767,Math.round(s[i]*32767)));
  slots[chosen]={name:clip.fileName,data:d};show();
};
$('write').onclick=()=>{
  const total=used(-1),block=new Uint8Array(HEAD+2*total).fill(0xff,0,HEAD),v=new DataView(block.buffer);
  v.setUint32(0,MAGIC,true);v.setUint32(4,2,true);v.setUint32(8,2*total,true);
  let at=HEAD;const enc=new TextEncoder();
  slots.forEach((s,i)=>{
    v.setUint32(12+4*i,s.data.length,true);
    const name=new Uint8Array(32);name.set(enc.encode(s.name).slice(0,31));block.set(name,48+32*i);
    block.set(new Uint8Array(s.data.buffer,s.data.byteOffset,2*s.data.length),at);at+=2*s.data.length;
  });
  const x=new XMLHttpRequest();x.open('POST','/sources');
  x.upload.onprogress=e=>{if(e.lengthComputable)$('wbar').value=e.loaded/e.total};
  x.onload=()=>{$('wstate').textContent=x.responseText};
  x.onerror=()=>{$('wstate').textContent='Verbindung abgebrochen. / Connection lost.'};
  $('wstate').textContent='Schreibt … / Writing …';x.send(block);
};
</script>
</body></html>)page";

/** Writes a whole image as it streams in, by twm::update::spanAt. */
class Writer
{
public:
    Writer()
    {
        layout.app = { (uint32_t) factory->address, (uint32_t) factory->size };
        layout.tables = { (uint32_t) tables->address, (uint32_t) tables->size };
    }

    /** Takes the next `length` bytes of the image; false (with `error` set) when the image must be refused. */
    bool write(const uint8_t* data, uint32_t length)
    {
        while (length > 0)
        {
            const auto span = twm::update::spanAt(layout, position, length);
            if (! put(span, data))
                return false;
            data += span.length;
            length -= span.length;
            position += span.length;
        }
        return true;
    }

    /** After the last byte: the instrument must be whole and start, then it becomes the one that boots. */
    bool finish()
    {
        if (! tableChecked || appBytes == 0)
            return fail("Die Datei ist kein ganzes Image (zu kurz). / Not a whole image (too short).");
        // Verifies the image (header, segments, checksum) before it erases the boot data, so the bootloader takes the
        // factory slot again.
        const esp_err_t result = esp_ota_set_boot_partition(factory);
        if (result != ESP_OK)
        {
            ESP_LOGE(kTag, "the new instrument does not verify: %s", esp_err_to_name(result));
            return fail("Das Image ist beschädigt, bitte noch einmal hochladen. / The image is damaged, please upload it "
                        "again.");
        }
        return true;
    }

    const char* error = "";

private:
    bool put(const twm::update::Span& span, const uint8_t* data)
    {
        using twm::update::Target;
        switch (span.target)
        {
            case Target::skip: return true;
            case Target::partitionTable:
                std::memcpy(table + span.offset, data, span.length);
                if (span.offset + span.length == layout.partitionTable.size)
                    return checkTable();
                return true;
            case Target::app:
                if (! tableChecked)
                    return fail("Die Datei ist kein ganzes Image. / Not a whole image.");
                // An app starts with the image magic 0xE9.
                if (span.offset == 0 && data[0] != 0xe9)
                    return fail("An 0x10000 steht kein Programm. / No app at 0x10000.");
                appBytes += span.length;
                return store(factory, appErased, span, data);
            case Target::tables:
                if (! tableChecked)
                    return fail("Die Datei ist kein ganzes Image. / Not a whole image.");
                return store(tables, tablesErased, span, data);
        }
        return true;
    }

    bool checkTable()
    {
        uint8_t own[sizeof table];
        if (esp_flash_read(nullptr, own, layout.partitionTable.start, sizeof own) != ESP_OK)
            return fail("Die Partitionstabelle des Moduls ist nicht lesbar. / Cannot read the module's partition table.");
        // An image for another layout would land in the wrong places; that one goes over USB.
        if (std::memcmp(own, table, sizeof own) != 0)
            return fail("Das Image hat eine andere Aufteilung des Flashs. Bitte einmal per USB flashen. / The image "
                        "has another flash layout; flash it over USB once.");
        tableChecked = true;
        return true;
    }

    bool store(const esp_partition_t* part, uint32_t& erased, const twm::update::Span& span, const uint8_t* data)
    {
        const uint32_t end = span.offset + span.length;
        if (end > erased)
        {
            const uint32_t to = std::min<uint32_t>((uint32_t) part->size, (end + kBlock - 1) / kBlock * kBlock);
            if (esp_partition_erase_range(part, erased, to - erased) != ESP_OK)
                return fail("Löschen im Flash fehlgeschlagen. / Erasing the flash failed.");
            erased = to;
        }
        if (esp_partition_write(part, span.offset, data, span.length) != ESP_OK)
            return fail("Schreiben in den Flash fehlgeschlagen. / Writing the flash failed.");
        return true;
    }

    bool fail(const char* why)
    {
        error = why;
        return false;
    }

    twm::update::Layout layout;
    uint32_t position = 0;
    uint8_t table[0xc00] {};
    bool tableChecked = false;
    uint32_t appBytes = 0;
    uint32_t appErased = 0, tablesErased = 0;
};

/** What the page says about the last restart, empty after a plain power-on. */
const char* restartNote()
{
    switch (startReason)
    {
        case ESP_RST_BROWNOUT:
            return "<p style=\"color:#ff5050\">Das Modul ist wegen eines Spannungseinbruchs neu gestartet (das WLAN "
                   "zieht mehr Strom als das Instrument); es sendet jetzt mit wenig Leistung. Bleibt die Verbindung "
                   "nicht, das Modul zum Update über USB versorgen. / The module restarted after a voltage dip; it now "
                   "transmits at low power. If the connection still drops, power it over USB for the update.</p>";
        case ESP_RST_PANIC:
        case ESP_RST_INT_WDT:
        case ESP_RST_TASK_WDT:
        case ESP_RST_WDT:
            return "<p style=\"color:#ff5050\">Der Updater ist abgestürzt und neu gestartet. / The updater crashed "
                   "and restarted.</p>";
        default: return "";
    }
}

esp_err_t sendPage(httpd_req_t* req)
{
    lastActivity = esp_timer_get_time();
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    const char* note = std::strstr(kPage, "%NOTE%");
    httpd_resp_send_chunk(req, kPage, note - kPage);
    httpd_resp_send_chunk(req, restartNote(), HTTPD_RESP_USE_STRLEN);
    httpd_resp_send_chunk(req, note + 6, HTTPD_RESP_USE_STRLEN);
    return httpd_resp_send_chunk(req, nullptr, 0);
}

esp_err_t refuse(httpd_req_t* req, const char* why)
{
    ESP_LOGE(kTag, "%s", why);
    phase = Phase::failed;
    httpd_resp_set_status(req, "400 Bad Request");
    httpd_resp_set_type(req, "text/plain; charset=utf-8");
    httpd_resp_send(req, why, HTTPD_RESP_USE_STRLEN);
    // The rest of the upload is not read: closing the connection ends it.
    return ESP_FAIL;
}

esp_err_t receiveImage(httpd_req_t* req)
{
    lastActivity = esp_timer_get_time();
    if (req->content_len == 0)
        return refuse(req, "Leere Datei. / Empty file.");
    phase = Phase::receiving;
    ESP_LOGI(kTag, "receiving %u bytes", (unsigned) req->content_len);

    static uint8_t buffer[4096];
    Writer writer;
    std::size_t remaining = req->content_len;
    int timeouts = 0;
    while (remaining > 0)
    {
        const int got = httpd_req_recv(req, (char*) buffer, std::min(remaining, sizeof buffer));
        if (got == HTTPD_SOCK_ERR_TIMEOUT && ++timeouts < 5)
            continue;
        if (got <= 0)
            return refuse(req, "Verbindung abgebrochen, bitte noch einmal. / Connection lost, please try again.");
        timeouts = 0;
        lastActivity = esp_timer_get_time();
        if (! writer.write(buffer, (uint32_t) got))
            return refuse(req, writer.error);
        remaining -= (std::size_t) got;
    }
    if (! writer.finish())
        return refuse(req, writer.error);

    ESP_LOGI(kTag, "image written; restarting into it");
    phase = Phase::done;
    const char done[] = "Fertig, das Modul startet neu. / Done, the module restarts.";
    httpd_resp_set_type(req, "text/plain; charset=utf-8");
    return httpd_resp_send(req, done, HTTPD_RESP_USE_STRLEN);
}

namespace layout = twm::grain::layout;

/** The header sector of the partition "tables", or false when it holds no Tonwerk Granular sources. */
bool currentSources(uint8_t* sector, layout::Header& header)
{
    if (esp_partition_read(tables, 0, sector, layout::kHeaderBytes) != ESP_OK)
        return false;
    header = layout::readHeader(sector);
    std::array<uint32_t, layout::kCount> lengths {};
    return layout::lengthsOf(header, lengths) && layout::kHeaderBytes + header.bytes <= tables->size;
}

const char kNotGranular[] = "Auf dem Modul liegen keine Klänge von Tonwerk Granular: erst tonwerk-granular.bin "
                            "hochladen. / No Tonwerk Granular sources on the module: upload tonwerk-granular.bin first.";

/** The sources as they lie in the flash (header sector, then the samples), for the page to edit. */
esp_err_t sendSources(httpd_req_t* req)
{
    lastActivity = esp_timer_get_time();
    static uint8_t buffer[layout::kHeaderBytes];
    layout::Header header;
    if (! currentSources(buffer, header))
    {
        httpd_resp_set_status(req, "404 Not Found");
        httpd_resp_set_type(req, "text/plain; charset=utf-8");
        return httpd_resp_send(req, kNotGranular, HTTPD_RESP_USE_STRLEN);
    }
    const uint32_t total = layout::kHeaderBytes + header.bytes;
    char length[16];
    std::snprintf(length, sizeof length, "%u", (unsigned) total);
    httpd_resp_set_type(req, "application/octet-stream");
    // Chunked, so the page learns the size from here for its progress bar.
    httpd_resp_set_hdr(req, "X-Total", length);
    for (uint32_t at = 0; at < total; at += sizeof buffer)
    {
        const uint32_t n = std::min<uint32_t>(sizeof buffer, total - at);
        if (esp_partition_read(tables, at, buffer, n) != ESP_OK || httpd_resp_send_chunk(req, (const char*) buffer, n) != ESP_OK)
            return ESP_FAIL;
        lastActivity = esp_timer_get_time();
    }
    return httpd_resp_send_chunk(req, nullptr, 0);
}

/** A whole new set of sources from the page: checked by its header, written behind it, the header written last so a
 *  write cut short leaves no sources (the instrument then blinks red until the next upload) rather than wrong ones. */
esp_err_t receiveSources(httpd_req_t* req)
{
    lastActivity = esp_timer_get_time();
    static uint8_t sector[layout::kHeaderBytes];
    static uint8_t buffer[4096];
    layout::Header current;
    if (! currentSources(buffer, current))
        return refuse(req, kNotGranular);
    std::size_t remaining = req->content_len;
    if (remaining < layout::kHeaderBytes)
        return refuse(req, "Zu wenig Daten. / Too little data.");
    phase = Phase::receiving;

    int timeouts = 0;
    auto read = [&](uint8_t* into, std::size_t want) -> int {
        for (;;)
        {
            const int got = httpd_req_recv(req, (char*) into, want);
            if (got == HTTPD_SOCK_ERR_TIMEOUT && ++timeouts < 5)
                continue;
            timeouts = 0;
            lastActivity = esp_timer_get_time();
            return got;
        }
    };
    for (std::size_t have = 0; have < layout::kHeaderBytes;)
    {
        const int got = read(sector + have, layout::kHeaderBytes - have);
        if (got <= 0)
            return refuse(req, "Verbindung abgebrochen, bitte noch einmal. / Connection lost, please try again.");
        have += (std::size_t) got;
    }
    remaining -= layout::kHeaderBytes;
    const layout::Header header = layout::readHeader(sector);
    std::array<uint32_t, layout::kCount> lengths {};
    if (! layout::lengthsOf(header, lengths) || header.bytes != remaining
        || layout::kHeaderBytes + header.bytes > tables->size)
        return refuse(req, "Die Klänge passen nicht (zusammen höchstens 36 s, jeder mindestens 0,5 s). / The sounds "
                           "do not fit (36 s together at most, 0.5 s each at least).");

    // The old header goes first, then the flash is erased a block at a time just ahead of the bytes, as for an image,
    // so no single erase holds the upload back for long.
    uint32_t erased = layout::kHeaderBytes;
    if (esp_partition_erase_range(tables, 0, erased) != ESP_OK)
        return refuse(req, "Löschen im Flash fehlgeschlagen. / Erasing the flash failed.");
    uint32_t at = layout::kHeaderBytes;
    while (remaining > 0)
    {
        const int got = read(buffer, std::min(remaining, sizeof buffer));
        if (got <= 0)
            return refuse(req, "Verbindung abgebrochen, bitte noch einmal. / Connection lost, please try again.");
        const uint32_t end = at + (uint32_t) got;
        if (end > erased)
        {
            const uint32_t to = std::min<uint32_t>((uint32_t) tables->size, (end + kBlock - 1) / kBlock * kBlock);
            if (esp_partition_erase_range(tables, erased, to - erased) != ESP_OK)
                return refuse(req, "Löschen im Flash fehlgeschlagen. / Erasing the flash failed.");
            erased = to;
        }
        if (esp_partition_write(tables, at, buffer, (std::size_t) got) != ESP_OK)
            return refuse(req, "Schreiben in den Flash fehlgeschlagen. / Writing the flash failed.");
        at += (uint32_t) got;
        remaining -= (std::size_t) got;
    }
    if (esp_partition_write(tables, 0, sector, sizeof sector) != ESP_OK)
        return refuse(req, "Schreiben in den Flash fehlgeschlagen. / Writing the flash failed.");
    if (esp_ota_set_boot_partition(factory) != ESP_OK)
        return refuse(req, "Kein Instrument auf dem Modul: tonwerk-granular.bin hochladen. / No instrument on the "
                           "module: upload tonwerk-granular.bin.");

    ESP_LOGI(kTag, "own sources written (%u bytes); restarting into the instrument", (unsigned) header.bytes);
    phase = Phase::done;
    const char done[] = "Fertig, das Modul startet mit den neuen Klängen. / Done, the module restarts with the new "
                        "sounds.";
    httpd_resp_set_type(req, "text/plain; charset=utf-8");
    return httpd_resp_send(req, done, HTTPD_RESP_USE_STRLEN);
}

void startNetwork()
{
    esp_err_t result = nvs_flash_init();
    if (result == ESP_ERR_NVS_NO_FREE_PAGES || result == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        result = nvs_flash_init();
    }
    ESP_ERROR_CHECK(result);
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_ap();
    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init));
    wifi_config_t config {};
    std::memcpy(config.ap.ssid, kNetwork, sizeof kNetwork - 1);
    config.ap.ssid_len = sizeof kNetwork - 1;
    config.ap.channel = 6;
    config.ap.max_connection = 2;
    // Open: the network exists only while someone stands at the module holding WARP, and only takes an image that
    // verifies.
    config.ap.authmode = WIFI_AUTH_OPEN;
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &config));
    ESP_ERROR_CHECK(esp_wifi_start());
    const int8_t power = startReason == ESP_RST_BROWNOUT ? kTxPowerLow : kTxPower;
    ESP_ERROR_CHECK(esp_wifi_set_max_tx_power(power));

    httpd_config_t server = HTTPD_DEFAULT_CONFIG();
    // Room for the writer and its two copies of the partition table.
    server.stack_size = 16384;
    // Erasing a block holds the next read back for a moment.
    server.recv_wait_timeout = 30;
    httpd_handle_t handle = nullptr;
    ESP_ERROR_CHECK(httpd_start(&handle, &server));
    httpd_uri_t page {};
    page.uri = "/";
    page.method = HTTP_GET;
    page.handler = sendPage;
    ESP_ERROR_CHECK(httpd_register_uri_handler(handle, &page));
    httpd_uri_t update {};
    update.uri = "/update";
    update.method = HTTP_POST;
    update.handler = receiveImage;
    ESP_ERROR_CHECK(httpd_register_uri_handler(handle, &update));
    httpd_uri_t readSources {};
    readSources.uri = "/sources";
    readSources.method = HTTP_GET;
    readSources.handler = sendSources;
    ESP_ERROR_CHECK(httpd_register_uri_handler(handle, &readSources));
    httpd_uri_t writeSources {};
    writeSources.uri = "/sources";
    writeSources.method = HTTP_POST;
    writeSources.handler = receiveSources;
    ESP_ERROR_CHECK(httpd_register_uri_handler(handle, &writeSources));
    ESP_LOGI(kTag, "network \"%s\" open, page at http://192.168.4.1", kNetwork);
}

/** Back to the instrument as it is. Stays here (red) when the factory slot holds nothing that starts. */
void leave()
{
    const esp_err_t result = esp_ota_set_boot_partition(factory);
    if (result == ESP_OK)
        esp_restart();
    ESP_LOGE(kTag, "no instrument to go back to (%s): upload an image", esp_err_to_name(result));
    phase = Phase::failed;
}

bool warpPressed() { return gpio_get_level(pins::kWarpButton) == 0; }
} // namespace

extern "C" void app_main()
{
    startReason = esp_reset_reason();
    ESP_LOGI(kTag, "start, reason %d", (int) startReason);
    hw::startLeds();
    hw::showLeds({ 0, 80, 255 }, { 0, 80, 255 });
    gpio_config_t buttons {};
    buttons.pin_bit_mask = (1ULL << pins::kWarpButton) | (1ULL << pins::kFilterButton);
    buttons.mode = GPIO_MODE_INPUT;
    buttons.pull_up_en = GPIO_PULLUP_ENABLE;
    ESP_ERROR_CHECK(gpio_config(&buttons));

    factory = esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_FACTORY, nullptr);
    tables = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, (esp_partition_subtype_t) 0x40, "tables");
    if (factory == nullptr || tables == nullptr)
    {
        ESP_LOGE(kTag, "partition table without \"factory\" or \"tables\": flash a whole image over USB");
        for (;;)
        {
            hw::showLeds({ 255, 0, 0 }, { 255, 0, 0 });
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }

    startNetwork();
    lastActivity = esp_timer_get_time();

    // WARP may still be down from switching on: a press counts only after it was let go.
    bool released = false;
    int tick = 0;
    int64_t doneAt = 0;
    for (;;)
    {
        vTaskDelay(pdMS_TO_TICKS(50));
        ++tick;
        const Phase now = phase.load();
        const bool on = (tick / 10) % 2 == 0;
        switch (now)
        {
            case Phase::waiting:
                hw::showLeds(on ? twm::Rgb { 0, 80, 255 } : twm::Rgb {}, on ? twm::Rgb {} : twm::Rgb { 0, 80, 255 });
                break;
            case Phase::receiving:
                hw::showLeds({ 0, 240, 255 }, (tick / 2) % 2 ? twm::Rgb { 0, 240, 255 } : twm::Rgb {});
                break;
            case Phase::done: hw::showLeds({ 0, 255, 0 }, { 0, 255, 0 }); break;
            case Phase::failed: hw::showLeds(on ? twm::Rgb { 255, 0, 0 } : twm::Rgb {}, on ? twm::Rgb { 255, 0, 0 } : twm::Rgb {}); break;
        }

        if (now == Phase::done)
        {
            // Time for the page to get its answer.
            if (doneAt == 0)
                doneAt = esp_timer_get_time();
            else if (esp_timer_get_time() - doneAt > 1500000)
                esp_restart();
            continue;
        }
        if (now == Phase::receiving)
            continue;

        if (! warpPressed())
            released = true;
        else if (released)
        {
            released = false;
            leave();
        }
        if (esp_timer_get_time() - lastActivity.load() > kIdleMicros)
        {
            lastActivity = esp_timer_get_time();
            leave();
        }
    }
}
