/**
 * Weather over WiFi, for Montreal and Vancouver.
 *
 * Ported from ~/dev/esp32-s3, which drives a T-Display-S3-Pro. Same data
 * source, different panel:
 *
 *   S3-Pro      480x222 over 2.33"  = 227 PPI
 *   this board  536x240 over 1.91"  = 307 PPI
 *
 * More pixels on a physically smaller screen, so the layout is *not* a
 * straight copy: at 1.35x the density, reusing the original's pixel sizes
 * would render everything about a quarter smaller to the eye. Text is scaled
 * up where the fonts allow it.
 *
 * They do not always allow it. TFT_eSPI's built-in font 8 is 75 px and is the
 * largest there is, so the temperature cannot grow to the ~101 px that would
 * match the original's physical height. Matching it would mean LVGL or a
 * converted font; 75 px is the honest ceiling of this approach.
 *
 * Composed into an off-screen sprite and pushed as one frame. A full push costs ~63 ms (see docs/hardware.md) which
 * would matter at animation rates and does not here: the server is polled once
 * every five minutes.
 *
 * Reads the observation straight from Environment Canada over HTTPS and reduces
 * it on the device, as ~/dev/esp32-s3 now does. This used to poll the FastAPI
 * service on the Pi, whose whole weather job was that same reduction; going
 * direct takes the Pi out of the path, so the display works on any network with
 * a route to the internet.
 *
 * Three screens side by side: espresso, Montreal, Vancouver. Montreal is on
 * the glass at boot; swipe right for the espresso screen, left for Vancouver.
 * Both cities are fetched on every poll, so the one off the glass is current
 * when it slides in.
 *
 * The espresso screen is a placeholder until the two MAX31865 / PT1000 sensors
 * are wired: boiler and group head show fixed values, 125.0 and 100.0.
 *
 * Needs `include/secrets.h` — copy `secrets.h.example`. 2.4 GHz only; the S3
 * has no 5 GHz radio.
 */
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <LilyGo_AMOLED.h>
#include <TFT_eSPI.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <ctype.h>
#include <math.h>

#include "secrets.h"

LilyGo_Class amoled;
TFT_eSPI tft = TFT_eSPI();

// The app API behind weather.gc.ca's own site -- the document the Pi used to
// fetch on this board's behalf. The coordinates are part of the path, not a
// query parameter. Declared with the rest of the per-screen state in LOCS below.
#define WEATHER_API "https://weather.gc.ca/api/app/v3/en/Location/"

// A public service polled from a desk. Saying what is calling costs nothing.
static const char *USER_AGENT = "esp32-amoled-weather/1.0 (+https://github.com/anselbrandt)";

// WiFiClientSecure allows 120 s for a handshake by default, which would freeze
// the panel for two minutes against a host that accepts and then stalls.
static const uint16_t HTTP_TIMEOUT_MS = 10000;
static const uint32_t HANDSHAKE_TIMEOUT_S = 10;

// Observations only change hourly upstream, and each fetch is now a TLS
// handshake and ~28 KB rather than 60 bytes on the LAN, so this polls every five
// minutes rather than every one.
static const uint32_t POLL_MS = 5 * 60 * 1000;
static const uint32_t WIFI_TIMEOUT_MS = 20 * 1000;

static const uint16_t COL_BG = TFT_BLACK;
static const uint16_t COL_TEXT = TFT_WHITE;
static const uint16_t COL_LABEL = 0x52AA; // muted grey
static const uint16_t COL_VALUE = TFT_CYAN;
static const uint16_t COL_RAIN = 0x3D7F; // cornflower blue
static const uint16_t COL_SNOW = TFT_WHITE;
static const uint16_t COL_ERROR = TFT_RED;

// Test mode: alternates rain (raindrop + RAIN) and snow (snowflake + SNOW)
// every ICON_CYCLE_MS on whichever screen is showing, whatever the weather, so
// both can be looked at without waiting for it. The readings stay real. Set to
// 0 and both follow the real conditions only.
#define ICON_CYCLE 0
static const uint32_t ICON_CYCLE_MS = 5000;

static int16_t W = 0; // filled from the panel, never hardcoded -- see below
static int16_t H = 0;

struct Weather {
    float temp;
    int humidity;
    bool rain;
    bool snow;
};

// One per weather screen, left to right, after the espresso screen.
struct Location {
    const char *name; // ASCII: font 4 has no accents, so no "Montréal"
    const char *url;
    Weather cached;
    bool haveReading;
    bool stale;
    uint32_t readingAt;
};

static Location LOCS[] = {
    // Observed at Montreal-Trudeau.
    {"MONTREAL", WEATHER_API "45.529,-73.562?type=city"},
    // The API names this point Richmond (displayName), and observes it at
    // Vancouver Int'l -- the city the screen is for.
    {"VANCOUVER", WEATHER_API "49.163,-123.138?type=city"},
};
static const int LOC_COUNT = sizeof(LOCS) / sizeof(LOCS[0]);

// The espresso machine's two surface temperatures, top row first. Fixed
// placeholder values until the sensors are wired; a probe with no reading
// shows "---.-".
struct Probe {
    const char *name;
    float temp;
    bool haveReading;
};

static Probe PROBES[] = {
    {"BOILER", 125.0f, true},
    {"GROUP HEAD", 100.0f, true},
};
// False while PROBES[] holds placeholders: the screen says "no sensors" so the
// fixed values are not mistaken for readings.
static const bool SENSORS_WIRED = false;
static const int PROBE_COUNT = sizeof(PROBES) / sizeof(PROBES[0]);

// Screens, left to right: espresso, then one per location. Swiping left moves
// one screen to the right. Montreal is shown at boot.
static const int ESPRESSO_PAGE = 0;
static const int FIRST_WEATHER_PAGE = 1;
static const int PAGES = FIRST_WEATHER_PAGE + LOC_COUNT;
static const int BOOT_PAGE = FIRST_WEATHER_PAGE; // LOCS[0], Montreal

// One sprite per screen, each a finished frame, plus one more that the slide
// composes into. 257 KB apiece, all in PSRAM (-DBOARD_HAS_PSRAM): 1 MB of 8 MB.
TFT_eSprite page[PAGES] = {TFT_eSprite(&tft), TFT_eSprite(&tft), TFT_eSprite(&tft)};
TFT_eSprite frame = TFT_eSprite(&tft);
static_assert(PAGES == 3, "page[] has one initialiser per screen");

static int current = BOOT_PAGE; // the screen on the glass
static uint32_t nextPoll = 0;

// A swipe is a press that travels at least this far sideways, and further
// sideways than vertically, before it lifts. Measured on release rather than
// followed live: a full-frame push is ~63 ms, too slow to track a finger.
static const int16_t SWIPE_MIN_PX = 60;
// Frames in the slide. At ~63 ms per push this is about half a second.
static const int SLIDE_FRAMES = 8;

static bool ensureWiFi()
{
    if (WiFi.status() == WL_CONNECTED) {
        return true;
    }
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < WIFI_TIMEOUT_MS) {
        delay(250);
    }
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("wifi: connect failed");
        return false;
    }
    Serial.printf("wifi: connected ip=%s rssi=%d\n", WiFi.localIP().toString().c_str(),
                  (int)WiFi.RSSI());
    return true;
}

// Case-insensitive substring test; `needle` must already be lower case.
// Conditions are title-case prose ("Light Rain Showers", "Rain And Snow"), and
// matching the word is sturdier than enumerating Environment Canada's
// vocabulary, which is long and unpublished. The server made the same test.
static bool mentions(const char *haystack, const char *needle)
{
    for (; *haystack; haystack++) {
        size_t i = 0;
        while (needle[i] && tolower((unsigned char)haystack[i]) == needle[i]) {
            i++;
        }
        if (!needle[i]) {
            return true;
        }
    }
    return false;
}

// Reduces the document to the four values on the panel. False means the shape
// was not what was expected; the caller treats that as a failed fetch.
static bool parseObservation(Location &loc, const String &payload)
{
    // ~28 KB of forecasts, alerts, air quality and zone polygons, for three
    // fields. The filter keeps only those, so the parsed document stays a few
    // hundred bytes. The top level is an array, and an array filter is one
    // element applied to every element of the input.
    JsonDocument filter;
    JsonObject wanted = filter[0]["observation"].to<JsonObject>();
    wanted["condition"] = true;
    wanted["humidity"] = true;
    wanted["temperature"]["metric"] = true;
    wanted["temperature"]["metricUnrounded"] = true;

    JsonDocument doc;
    DeserializationError err =
        deserializeJson(doc, payload, DeserializationOption::Filter(filter));
    if (err) {
        Serial.printf("json: %s\n", err.c_str());
        return false;
    }

    JsonObject observation = doc[0]["observation"];
    if (observation.isNull()) {
        Serial.println("json: no observation in response");
        return false;
    }

    // Every value is a string, and an unavailable one is "" rather than absent,
    // so an empty unrounded temperature is a real case: fall back to the
    // rounded one, and only give up if that is empty too.
    const char *temp = observation["temperature"]["metricUnrounded"] | "";
    if (!*temp) {
        temp = observation["temperature"]["metric"] | "";
    }
    if (!*temp) {
        Serial.println("json: no temperature in observation");
        return false;
    }
    const char *condition = observation["condition"] | "";

    Weather &cached = loc.cached;
    cached.temp = atof(temp);
    cached.humidity = observation["humidity"].as<int>(); // a string, e.g. "84"
    // "Rain And Snow" is a real condition; renderPage() shows SNOW when both are set.
    cached.rain = mentions(condition, "rain");
    cached.snow = mentions(condition, "snow");

    Serial.printf("weather: %s %.1fC %d%% \"%s\" rain=%d snow=%d heap=%u psram=%u\n",
                  loc.name, cached.temp, cached.humidity, condition, cached.rain, cached.snow,
                  (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getFreePsram());
    return true;
}

static bool fetchWeather(Location &loc)
{
    if (!ensureWiFi()) {
        loc.stale = true;
        return false;
    }

    // The certificate is not checked. Validating one needs the wall clock and
    // nothing here sets it, so every certificate would read as not yet valid.
    // Pinning the issuer and adding configTime() is the upgrade; this is an
    // unauthenticated read of a public observation, and nothing is sent.
    WiFiClientSecure client;
    client.setInsecure();
    client.setHandshakeTimeout(HANDSHAKE_TIMEOUT_S);
    client.setTimeout(HTTP_TIMEOUT_MS / 1000); // seconds, unlike HTTPClient

    HTTPClient http;
    if (!http.begin(client, loc.url)) {
        Serial.println("http: begin failed");
        loc.stale = true;
        return false;
    }
    http.setConnectTimeout(HTTP_TIMEOUT_MS);
    http.setTimeout(HTTP_TIMEOUT_MS);
    http.setUserAgent(USER_AGENT);

    int code = http.GET();
    if (code != HTTP_CODE_OK) {
        Serial.printf("http: %s GET returned %d\n", loc.name, code);
        http.end();
        loc.stale = true;
        return false;
    }

    // getString(), not getStream(). The response is `Transfer-Encoding:
    // chunked`, and getStream() is the raw socket with the chunk headers still
    // interleaved in the JSON; only getString() (via writeToStream()) strips
    // them. So the ~28 KB body is buffered whole, which lands in PSRAM.
    String payload = http.getString();
    http.end();

    if (payload.isEmpty()) {
        Serial.println("http: empty body");
        loc.stale = true;
        return false;
    }
    if (!parseObservation(loc, payload)) {
        loc.stale = true;
        return false;
    }

    loc.haveReading = true;
    loc.stale = false;
    loc.readingAt = millis();
    return true;
}

// The two precipitation icons, drawn from TFT_eSPI's anti-aliased primitives
// rather than carried as bitmaps -- the same shapes as ~/dev/esp32-s3's
// icons.cpp, drawn into the sprite. A jagged icon beside the smooth 75 px
// digits would read as a glitch.

// A teardrop `h` tall: a round bulb below, tapering to a point at the top. The
// taper starts at the bulb's widest point so the join is an edge, not a notch.
static void drawRaindrop(TFT_eSprite &s, int16_t cx, int16_t cy, int16_t h, uint16_t colour)
{
    const int16_t r = (int16_t)(h * 0.36f);
    const int16_t bulbY = cy + h / 2 - r;
    const int16_t tipY = cy - h / 2;

    s.fillSmoothCircle(cx, bulbY, r, colour, COL_BG);
    s.fillTriangle(cx, tipY, cx - r, bulbY, cx + r, bulbY, colour);
}

// A six-spoke flake of radius `r`. The spokes are three diameters rather than
// six radii: six lines meeting at a point leave a lighter patch in the middle
// where each one's anti-aliasing runs out.
static void drawSnowflake(TFT_eSprite &s, int16_t cx, int16_t cy, int16_t r, uint16_t colour)
{
    const float spokeW = r * 0.16f;
    const float branchW = spokeW * 0.8f;
    // Two pairs of branches per spoke, the outer one shorter -- which is what
    // reads as a snowflake rather than an asterisk.
    static const float BRANCH_AT[2] = {0.50f, 0.78f};
    static const float BRANCH_LEN[2] = {0.30f, 0.20f};

    for (int i = 0; i < 3; i++) {
        const float a = (float)i * (float)M_PI / 3.0f;
        const float dx = cosf(a), dy = sinf(a);
        s.drawWideLine(cx - dx * r, cy - dy * r, cx + dx * r, cy + dy * r, spokeW, colour,
                         COL_BG);
    }
    for (int i = 0; i < 6; i++) {
        const float a = (float)i * (float)M_PI / 3.0f;
        const float dx = cosf(a), dy = sinf(a);
        for (int b = 0; b < 2; b++) {
            const float bx = cx + dx * r * BRANCH_AT[b];
            const float by = cy + dy * r * BRANCH_AT[b];
            const float len = r * BRANCH_LEN[b];
            for (int side = -1; side <= 1; side += 2) {
                const float ba = a + (float)side * (float)M_PI / 4.0f;
                s.drawWideLine(bx, by, bx + cosf(ba) * len, by + sinf(ba) * len, branchW,
                                 colour, COL_BG);
            }
        }
    }
}

// The degree sign is two circles rather than a glyph: TFT_eSPI's built-in fonts
// stop at ASCII 127, so 0xB0 would render as nothing.
static void drawDegree(TFT_eSprite &s, int16_t x, int16_t y, uint16_t colour)
{
    s.drawCircle(x, y, 8, colour);
    s.drawCircle(x, y, 7, colour);
}

// How far "°C" reaches right of a temperature's right edge: 36 px to the C,
// plus the C's 33 px advance in FreeSans 24pt.
static const int16_t CELSIUS_W = 36 + 33;

// "°C" after a font 8 temperature drawn right-aligned at (tempRight, tempY).
// The C is FreeSans 24pt, the Helvetica-like face nearest font 8's Arial
// digits: 34 px to its top. The numbered fonts have nothing between font 4's
// ~19 px capitals and fonts 6/7/8, which have no letters at all -- a font 6 "C"
// draws as a blank. Placed on its baseline so its top sits level with the top
// of the degree ring.
static void drawCelsius(TFT_eSprite &s, int16_t tempRight, int16_t tempY, uint16_t colour)
{
    drawDegree(s, tempRight + 22, tempY + 12, colour);
    s.setFreeFont(&FreeSans24pt7b);
    s.setTextDatum(L_BASELINE);
    s.setTextColor(colour, COL_BG);
    s.drawString("C", tempRight + 36, tempY + 4 + 34);
    s.setFreeFont(nullptr); // back to the numbered fonts
}

static void renderWeather(int idx)
{
    TFT_eSprite &s = page[idx];
    const Location &loc = LOCS[idx - FIRST_WEATHER_PAGE];
    const Weather &cached = loc.cached;
    const bool haveReading = loc.haveReading;
    s.fillSprite(COL_BG);

    // Status line, top right. Says what is wrong when something is, and how old
    // the reading is when nothing is.
    char status[28];
    uint16_t statusColour = COL_LABEL;
    if (loc.stale) {
        snprintf(status, sizeof(status), "%s",
                 WiFi.status() == WL_CONNECTED ? "update failed" : "no wifi");
        statusColour = COL_ERROR;
    } else if (!haveReading) {
        snprintf(status, sizeof(status), "starting...");
    } else {
        int minutes = (int)((millis() - loc.readingAt) / 60000UL);
        if (minutes == 0) {
            snprintf(status, sizeof(status), "updated just now");
        } else {
            snprintf(status, sizeof(status), "updated %dm ago", minutes);
        }
    }
    s.setTextDatum(TR_DATUM);
    s.setTextColor(statusColour, COL_BG);
    s.drawString(status, W - 14, 10, 4);

    s.setTextDatum(TL_DATUM);
    s.setTextColor(COL_LABEL, COL_BG);
    s.drawString(loc.name, 14, 10, 4);

    // Temperature: font 8, 75 px, digits and '.' and '-' only -- which is all it
    // needs. Right-aligned so the degree sign never moves as the value changes
    // width between, say, "9.4" and "-19.1".
    const int16_t tempRight = 330;
    const int16_t tempY = 66;
    char temp[8];
    if (haveReading) {
        snprintf(temp, sizeof(temp), "%.1f", cached.temp);
    } else {
        snprintf(temp, sizeof(temp), "--.-");
    }
    s.setTextDatum(TR_DATUM);
    s.setTextColor(haveReading ? COL_TEXT : COL_LABEL, COL_BG);
    s.drawString(temp, tempRight, tempY, 8);

    drawCelsius(s, tempRight, tempY, COL_TEXT);

    // Rain or snow, just left of the digits. Placed off the rendered width
    // rather than at a fixed x, so it sits the same distance from "9.4" as from
    // "-19.1". Snow wins when both are set: "Rain And Snow" is a real
    // condition, and the flake is the more useful warning.
    bool showSnow = haveReading && cached.snow;
    bool showRain = haveReading && cached.rain && !showSnow;
#if ICON_CYCLE
    showSnow = (millis() / ICON_CYCLE_MS) % 2 == 0;
    showRain = !showSnow;
#endif
    if (showSnow || showRain) {
        const int16_t iconHalfW = 32;
        const int16_t iconGap = 20;
        const int16_t iconCy = tempY + 37; // level with the middle of font 8's digits
        const int16_t iconCx = tempRight - s.textWidth(temp, 8) - iconGap - iconHalfW;
        if (showSnow) {
            drawSnowflake(s, iconCx, iconCy, iconHalfW, COL_SNOW);
        } else {
            drawRaindrop(s, iconCx, iconCy, 64, COL_RAIN);
        }
    }

    // Humidity, bottom left.
    s.setTextDatum(TL_DATUM);
    s.setTextColor(COL_LABEL, COL_BG);
    s.drawString("HUMIDITY", 14, 186, 4);
    char hum[8];
    if (haveReading) {
        snprintf(hum, sizeof(hum), "%d%%", cached.humidity);
    } else {
        snprintf(hum, sizeof(hum), "--%%");
    }
    s.setTextColor(COL_VALUE, COL_BG);
    // Measured off the font rather than hardcoded, so the gap survives a font
    // change; a hardcoded position once overlapped the label.
    s.drawString(hum, 14 + s.textWidth("HUMIDITY", 4) + 24, 186, 4);

    // Precipitation, bottom right. Blank when neither -- an empty slot reads as
    // "no" more clearly than the word "NONE" does at a glance.
    // Same decision as the icon, so the two can never disagree.
    if (showSnow || showRain) {
        s.setTextDatum(TR_DATUM);
        s.setTextColor(showSnow ? COL_SNOW : COL_RAIN, COL_BG);
        s.drawString(showSnow ? "SNOW" : "RAIN", W - 14, 186, 4);
    }
}

// Boiler and group head, one row each: name on the left, temperature in font 8
// on the right. Right-aligned, as on the weather screens, so the unit never
// moves as the value changes width. Sized for three digits and one decimal: a
// boiler runs past 100 C, and "188.8" is 249 px, clear of "GROUP HEAD".
static void renderEspresso()
{
    TFT_eSprite &s = page[ESPRESSO_PAGE];
    s.fillSprite(COL_BG);

    s.setTextDatum(TL_DATUM);
    s.setTextColor(COL_LABEL, COL_BG);
    s.drawString("ESPRESSO", 14, 10, 4);

    if (!SENSORS_WIRED) {
        s.setTextDatum(TR_DATUM);
        s.drawString("no sensors", W - 14, 10, 4);
    }

    const int16_t tempRight = W - 14 - CELSIUS_W;
    for (int i = 0; i < PROBE_COUNT; i++) {
        const Probe &p = PROBES[i];
        const int16_t rowY = 50 + i * 95; // two 75 px rows in the 200 px below the header
        char temp[8];
        if (p.haveReading) {
            snprintf(temp, sizeof(temp), "%.1f", p.temp);
        } else {
            snprintf(temp, sizeof(temp), "---.-");
        }
        const uint16_t colour = p.haveReading ? COL_TEXT : COL_LABEL;

        s.setTextDatum(ML_DATUM);
        s.setTextColor(COL_LABEL, COL_BG);
        s.drawString(p.name, 14, rowY + 37, 4); // level with the middle of the digits

        s.setTextDatum(TR_DATUM);
        s.setTextColor(colour, COL_BG);
        s.drawString(temp, tempRight, rowY, 8);

        drawCelsius(s, tempRight, rowY, colour);
    }
}

// Draws screen `idx` into its own sprite. Nothing reaches the glass until
// show() or slideTo() pushes it.
static void renderPage(int idx)
{
    if (idx == ESPRESSO_PAGE) {
        renderEspresso();
    } else {
        renderWeather(idx);
    }
}

static void show(int idx)
{
    amoled.pushColors(0, 0, W, H, (uint16_t *)page[idx].getPointer());
}

// Slides screen `to` in over the current one: from the right when moving right
// (a swipe to the left), from the left when moving back. Each frame is spliced
// row by row from the two finished pages -- the panel takes one contiguous
// W x H buffer, so a page cannot be pushed at an offset on its own.
static void slideTo(int to)
{
    const int from = current;
    const bool forward = to > from;
    renderPage(from);
    renderPage(to);

    const uint16_t *a = (const uint16_t *)page[from].getPointer();
    const uint16_t *b = (const uint16_t *)page[to].getPointer();
    uint16_t *out = (uint16_t *)frame.getPointer();

    for (int f = 1; f <= SLIDE_FRAMES; f++) {
        // Ease out: fast off the finger, settling into place.
        const float t = (float)f / SLIDE_FRAMES;
        const int16_t off = (int16_t)(W * (1.0f - (1.0f - t) * (1.0f - t)));
        // Leftmost pixels of each row come from `left`, starting at column
        // `skip`; the remaining `off`-wide strip comes from `right`.
        const uint16_t *left = forward ? a : b;
        const uint16_t *right = forward ? b : a;
        const int16_t split = forward ? W - off : off; // columns taken from `left`
        const int16_t skip = W - split;
        for (int16_t y = 0; y < H; y++) {
            const size_t row = (size_t)y * W;
            memcpy(out + row, left + row + skip, split * sizeof(uint16_t));
            memcpy(out + row + split, right + row, (W - split) * sizeof(uint16_t));
        }
        amoled.pushColors(0, 0, W, H, out);
    }
    current = to;
}

// Polls the touch controller and turns a completed horizontal swipe into a
// screen change. Only a press-and-release counts, so a finger resting on the
// glass does nothing.
static void pollTouch()
{
    static bool down = false;
    static int16_t startX, startY, lastX, lastY;

    int16_t x, y;
    if (amoled.getPoint(&x, &y)) {
        if (!down) {
            down = true;
            startX = x;
            startY = y;
        }
        lastX = x;
        lastY = y;
        return;
    }
    if (!down) {
        return;
    }
    down = false;

    const int16_t dx = lastX - startX;
    const int16_t dy = lastY - startY;
    Serial.printf("touch: (%d,%d) -> (%d,%d) dx=%d dy=%d\n", startX, startY, lastX, lastY, dx,
                  dy);
    if (abs(dx) < SWIPE_MIN_PX || abs(dx) <= abs(dy)) {
        return;
    }
    // Finger moving left pulls the screen on the right into view.
    const int to = current + (dx < 0 ? 1 : -1);
    if (to < 0 || to >= PAGES) {
        return;
    }
    slideTo(to);
}


// Each fetch blocks for its handshake and download -- a second or two, during
// which a swipe is not seen. If a weather screen is on the glass it is fetched
// first and shown as soon as it has its reading.
static void fetchAll()
{
    const int first = current >= FIRST_WEATHER_PAGE ? current - FIRST_WEATHER_PAGE : 0;
    for (int n = 0; n < LOC_COUNT; n++) {
        const int i = (first + n) % LOC_COUNT;
        fetchWeather(LOCS[i]);
        if (i + FIRST_WEATHER_PAGE == current) {
            renderPage(current);
            show(current);
        }
    }
}

void setup()
{
    Serial.begin(115200);
    while (!Serial && millis() < 2000) {
        delay(10);
    }

    if (!amoled.beginAMOLED_191_SPI()) {
        while (true) {
            Serial.println("beginAMOLED_191_SPI failed -- wrong board profile?");
            delay(1000);
        }
    }

    // Read the geometry back rather than trusting the datasheet: begin() ends in
    // setRotation(0) and hands back landscape 536x240, not the 240x536 every
    // spec sheet quotes. See docs/hardware.md.
    W = amoled.width();
    H = amoled.height();
    Serial.printf("panel: %dx%d\n", W, H);

    for (int i = 0; i < PAGES; i++) {
        page[i].createSprite(W, H); // 257 KB each -> PSRAM, via -DBOARD_HAS_PSRAM
        page[i].setSwapBytes(true);
    }
    frame.createSprite(W, H);
    bool allocated = frame.getPointer() != nullptr;
    for (int i = 0; i < PAGES; i++) {
        allocated &= page[i].getPointer() != nullptr;
    }
    if (!allocated) {
        while (true) {
            Serial.println("sprite allocation failed -- is PSRAM enabled?");
            delay(1000);
        }
    }

    renderPage(current); // "starting..." while WiFi comes up, so the panel is never blank
    show(current);
    fetchAll();
    nextPoll = millis() + POLL_MS;
}

void loop()
{
    pollTouch();

    if ((int32_t)(millis() - nextPoll) >= 0) {
        nextPoll = millis() + POLL_MS;
        fetchAll();
    }
    // Repaint on a slower beat than the poll so the "updated Nm ago" line ages
    // visibly between fetches.
    static uint32_t nextRender = 0;
    if ((int32_t)(millis() - nextRender) >= 0) {
#if ICON_CYCLE
        nextRender = millis() + ICON_CYCLE_MS; // one frame per icon change
#else
        nextRender = millis() + 10000;
#endif
        renderPage(current);
        show(current);
    }
    // Short, so a quick flick still yields several touch samples.
    delay(10);
}
