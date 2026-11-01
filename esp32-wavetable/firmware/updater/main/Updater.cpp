// The updater: holding WARP while switching the module on starts it (hw::startUpdater). It opens an open Wi-Fi
// network "Tonwerk-Modul", serves a page at http://192.168.4.1 that takes a whole image (the same .bin flashed at 0x0
// over USB), writes the instrument into the partition "factory" (and the data an image carries into "tables"), checks
// it and starts it. The radio is on only here; the instruments never start it.
//
// LEDs: blue blinking, waiting for an image; cyan, receiving; green, done (restarts into the instrument); red, the
// image was refused (the page says why) or the instrument's slot holds nothing that starts. WARP pressed while
// waiting goes back to the instrument without an update.

#include "hw/Hardware.h"
#include "hw/Pins.h"
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
#include <atomic>
#include <cstring>

namespace
{
const char* kTag = "updater";
constexpr char kNetwork[] = "Tonwerk-Modul";
/** Flash is erased a 64 KB block at a time, just ahead of the bytes that arrive. */
constexpr uint32_t kBlock = 0x10000;
/** Nobody uploads for this long: back to the instrument. */
constexpr int64_t kIdleMicros = 15LL * 60 * 1000000;

enum class Phase { waiting, receiving, done, failed };
std::atomic<Phase> phase { Phase::waiting };
std::atomic<int64_t> lastActivity { 0 };

const esp_partition_t* factory = nullptr;
const esp_partition_t* tables = nullptr;

const char kPage[] = R"(<!doctype html>
<html lang="de"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Tonwerk Modul</title>
<style>
body{font-family:system-ui,sans-serif;background:#0b0b10;color:#e8e8e8;max-width:34rem;margin:2rem auto;padding:0 1rem}
h1{color:#fcee0a;font-size:1.6rem}button{background:#fcee0a;color:#0b0b10;border:0;padding:.7rem 1.4rem;font-size:1rem}
input{margin:1rem 0}progress{width:100%;height:1.2rem}p.en{color:#9a9aa8}
</style></head><body>
<h1>Tonwerk Modul</h1>
<p>Image wählen, dieselbe .bin wie für USB (z.&nbsp;B. tonwerk-modul.bin, tonwerk-granular.bin), und hochladen. Danach startet das Modul neu.</p>
<p class="en">Choose an image, the same .bin as for USB, and upload it. The module restarts afterwards.</p>
<input type="file" id="file" accept=".bin"><br><button id="go">Hochladen / Upload</button>
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
</script></body></html>)";

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

esp_err_t sendPage(httpd_req_t* req)
{
    lastActivity = esp_timer_get_time();
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return httpd_resp_send(req, kPage, HTTPD_RESP_USE_STRLEN);
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
