#include "face_display.h"
#include "settings.h"
#include "lvgl_theme.h"
#include "application.h"
#include "board.h"
#include "assets/lang_config.h"

#include <esp_log.h>
#include <esp_random.h>
#include <esp_timer.h>
#include <esp_mac.h>
#include <esp_heap_caps.h>
#include <esp_wifi.h>
#include <esp_netif.h>
#include <esp_app_desc.h>
#include <esp_idf_version.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <cstring>
#include <cstdio>
#include <ctime>
#include <algorithm>

#define TAG "FaceDisplay"

namespace {
constexpr int kTopBar     = 16;
constexpr int kTickMs     = 40;   // 25 Hz
constexpr int kEyeHalfGap = 25;   // demi-ecart entre les centres des yeux

// ------------------------------------------------- console « mainframe »
constexpr int kConCols     = 16;  // 128 px / 8 px par glyphe
constexpr int kConRows     = 8;   //  64 px / 8 px de hauteur de ligne
constexpr int kConGlyph    = 8;
constexpr int kConScroll   = kConRows - 1;  // la derniere ligne est l'invite
constexpr int kIplPace     = 10;  // une ligne toutes les 0,4 s au demarrage
constexpr int kConPace     = 18;  // une ligne toutes les 0,72 s en veille
constexpr int kReadyHold   = 30;  // 1,2 s d'affichage apres READY
constexpr int kSleepAfter  = 25 * 25;        // 25 s : les yeux se ferment
constexpr int kConsoleAfter = kSleepAfter + 100;  // +4 s : la console prend le relais

// Polarite des pixels. esp_lvgl_port 2.9 eteint un pixel quand la couleur est
// claire et l'allume quand elle est sombre : le noir ALLUME, le blanc ETEINT.
// Kira, sur SH1106, avait la convention inverse ; tout le dessin passe par ces
// deux fonctions pour ne plus dependre de ce detail.
lv_color_t Allume() { return lv_color_black(); }
lv_color_t Eteint() { return lv_color_white(); }

int RandRange(int lo, int hi) {
    return lo + (int)(esp_random() % (uint32_t)(hi - lo + 1));
}

// Onde triangulaire dans [-amp, +amp], de periode donnee en ticks.
int Triangle(int phase, int period, int amp) {
    if (period <= 0 || amp == 0) return 0;
    int p = phase % period;
    int half = period / 2;
    int v = (p < half) ? p : (period - p);          // 0 .. half
    return (v * 2 * amp) / std::max(1, half) - amp; // -amp .. +amp
}
// Valeurs reelles de la machine. La console affiche l'etat du systeme et
// non un faux vidage hexadecimal : au bout d'une semaine, le faux se voit.
uint32_t HeapLibre()    { return (uint32_t)heap_caps_get_free_size(MALLOC_CAP_INTERNAL); }
// Pas de PSRAM sur l'ESP32-C3 : le plus petit tas libre jamais atteint est
// la mesure utile, c'est elle qui dit si l'on frole la panne de memoire.
uint32_t HeapMini()     { return (uint32_t)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL); }

// Niveau de batterie, "---" tant que la carte n'a rien mesure.
void Batterie(char* out, size_t n) {
    int level = 0;
    bool charging = false, discharging = false;
    if (Board::GetInstance().GetBatteryLevel(level, charging, discharging)) {
        snprintf(out, n, "%d%%", level);
    } else {
        strncpy(out, "---", n);
    }
}

bool WifiInfo(int* rssi, char* ssid, size_t n) {
    wifi_ap_record_t ap;
    if (esp_wifi_sta_get_ap_info(&ap) != ESP_OK) return false;
    if (rssi) *rssi = ap.rssi;
    if (ssid && n) { strncpy(ssid, (const char*)ap.ssid, n - 1); ssid[n - 1] = '\0'; }
    return true;
}

// « ETIQ  : valeur », deux-points toujours en colonne 7. C'est cette
// colonne alignee qui fait lire un bloc comme une console de systeme
// plutot que comme une liste de mesures.
void LigneVal(char* out, size_t n, const char* etiq, const char* val) {
    snprintf(out, n, "%-5s : %.8s", etiq, val);
}

// Adresse IP. Trop longue pour la colonne alignee : elle prend une ligne
// entiere, avec un « = » — c'est exactement la forme de l'en-tete d'un
// ecran de logon z/OS, ou l'IP est separee du bloc aligne du dessous.
bool IpAddr(char* out, size_t n) {
    esp_netif_t* nif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    esp_netif_ip_info_t ip{};
    if (!nif || esp_netif_get_ip_info(nif, &ip) != ESP_OK || ip.ip.addr == 0) return false;
    snprintf(out, n, IPSTR, IP2STR(&ip.ip));
    return true;
}

// L'horloge n'a de sens qu'une fois le serveur joint. Avant la
// synchronisation, l'annee lue est absurde : on l'avoue plutot que
// d'afficher une date fausse.
bool Horodate(char* out, size_t n, bool date) {
    time_t t = time(nullptr); struct tm lt{};
    localtime_r(&t, &lt);
    if (lt.tm_year + 1900 < 2024) { strncpy(out, date ? "--/--/--" : "--:--:--", n); return false; }
    if (date) snprintf(out, n, "%02d/%02d/%02d", lt.tm_mday, lt.tm_mon + 1, (lt.tm_year + 1900) % 100);
    else      snprintf(out, n, "%02d:%02d:%02d", lt.tm_hour, lt.tm_min, lt.tm_sec);
    return true;
}

// Temperature de la puce. Renvoie false si le capteur n'a pas pu etre
// installe : on affiche alors "---" plutot qu'un zero trompeur.
bool TempPuce(temperature_sensor_handle_t h, char* out, size_t n) {
    float c = 0.0f;
    if (h == nullptr || temperature_sensor_get_celsius(h, &c) != ESP_OK) {
        strncpy(out, "---", n);
        return false;
    }
    snprintf(out, n, "%.1fC", c);
    return true;
}

// Le serveur auquel elle se raccorde, tel qu'il est enregistre en NVS.
// On n'en garde que la premiere etiquette du nom d'hote : « kora » plutot
// que « kora.kdevserver.ovh », qui deborderait des seize colonnes.
void ServeurCourt(char* out, size_t n) {
    std::string url;
    {
        Settings reglages("wifi", false);
        url = reglages.GetString("ota_url");
    }
    if (url.empty()) {           // aucune adresse : le serveur compile
        strncpy(out, "XIAOZHI", n - 1);
        out[n - 1] = '\0';
        return;
    }
    size_t d = url.find("://");
    d = (d == std::string::npos) ? 0 : d + 3;
    size_t f = url.find_first_of("./:", d);
    if (f == std::string::npos) f = url.size();
    std::string h = url.substr(d, f - d);
    if (h.empty()) h = "?";
    for (auto& c : h) c = (char)toupper((unsigned char)c);
    strncpy(out, h.c_str(), n - 1);
    out[n - 1] = '\0';
}

void MacCourt(char* out, size_t n) {
    uint8_t m[6] = {0};
    esp_read_mac(m, ESP_MAC_WIFI_STA);
    snprintf(out, n, "%02X%02X%02X", m[3], m[4], m[5]);
}
} // namespace

int FaceDisplay::Approach(int cur, int tgt, int step) {
    if (cur < tgt) return std::min(cur + step, tgt);
    if (cur > tgt) return std::max(cur - step, tgt);
    return cur;
}

// ------------------------------------------------------- formes par etat
// Les yeux sont volontairement plus LARGES que hauts : c'est ce rapport
// qui les rend expressifs plutot que fixes.
//                    w   h   r   forme            gy  br per sl idle sh sv
FaceDisplay::Look FaceDisplay::LookForState(DeviceState s) {
    switch (s) {
        case kDeviceStateListening:   // attentif : yeux plus ouverts
            return {42, 34, 15, Shape::Rect,  0, 2, 38, 0, false, 0, 0};
        case kDeviceStateSpeaking:    // pulsation plus rapide
            return {42, 32, 14, Shape::Rect,  0, 3, 14, 0, false, 0, 0};
        case kDeviceStateConnecting:
        case kDeviceStateActivating:
        case kDeviceStateUpgrading:   // fentes horizontales scintillantes
            return {44,  6,  3, Shape::Rect,  0, 2, 16, 0, false, 0, 0};
        case kDeviceStateWifiConfiguring:
            return {42, 28, 13, Shape::Rect,  0, 2, 50, 0, true,  0, 0};
        case kDeviceStateFatalError:
            return {42, 10,  4, Shape::Rect,  4, 0,  0, 1, false, 3, 0};
        case kDeviceStateIdle:
        default:                      // repos : respiration lente
            return {40, 30, 13, Shape::Rect,  0, 2, 100, 0, true, 0, 0};
    }
}

// ---------------------------------------------------- surcouche emotion
// Renvoie false si l'emotion ne modifie pas le visage.
bool FaceDisplay::LookForEmotion(const char* emotion, Look* out, int* ttl,
                                 const char** badge) {
    if (emotion == nullptr) return false;

    //                     w   h   r   forme              gy br per sl idle sh sv
    const Look happy     {44, 26, 12, Shape::ArcSmile,   0, 0,  0,  0, false, 0, 0};
    const Look laughing  {44, 24, 12, Shape::ArcSmile,   0, 0,  0,  0, false, 0, 3};
    const Look sad       {44, 26, 12, Shape::ArcFrown,   4, 0,  0,  0, false, 0, 0};
    const Look angry     {42, 16,  5, Shape::Rect,       0, 0,  0, +1, false, 0, 0};
    const Look surprised {36, 40, 17, Shape::Rect,      -2, 0,  0,  0, false, 0, 0};
    const Look confused  {40, 28, 13, Shape::Rect,       0, 0,  0,  0, false, 3, 0};
    const Look sleepy    {42, 12,  5, Shape::Rect,       6, 2, 60,  0, false, 0, 0};
    const Look thinking  {44,  8,  4, Shape::Rect,      -4, 2, 20,  0, false, 0, 0};
    const Look loving    {44, 24, 12, Shape::ArcSmile,   0, 2, 30,  0, false, 0, 0};

    // La mention n'illustre pas ce que les yeux montrent deja (sourire, moue) :
    // elle ajoute ce qu'ils ne peuvent pas dire. Deux caracteres au maximum.
    struct Map { const char* n; const Look* l; int ttl_s; const char* badge; };
    const Map table[] = {
        {"happy",     &happy,     6, ""  },   // l'arc sourire suffit
        {"funny",     &happy,     6, "*" },
        {"delicious", &happy,     6, "*" },
        {"confident", &happy,     6, "*" },
        {"cool",      &happy,     6, "*" },
        {"relaxed",   &happy,     8, "~" },
        {"laughing",  &laughing,  5, "ha"},
        {"silly",     &laughing,  5, "ha"},
        {"sad",       &sad,       7, ""  },   // l'arc inverse suffit
        {"crying",    &sad,       8, ","  },  // une larme
        {"embarrassed",&sad,      5, "~" },
        {"angry",     &angry,     6, "#" },
        {"surprised", &surprised, 4, "!" },
        {"shocked",   &surprised, 4, "!!"},
        {"confused",  &confused,  4, "?" },
        {"sleepy",    &sleepy,   10, ""  },   // anime separement : z z z
        {"thinking",  &thinking,  6, ""  },   // anime separement : . .. ...
        {"winking",   &happy,     3, ";)"},
        {"loving",    &loving,    7, "<3"},
        {"kissy",     &loving,    5, "<3"},
    };
    for (const auto& e : table) {
        if (std::strcmp(emotion, e.n) == 0) {
            *out = *e.l;
            *ttl = e.ttl_s * 1000 / kTickMs;
            *badge = e.badge;
            return true;
        }
    }
    return false;   // neutral et inconnus : l'etat seul pilote
}

FaceDisplay::~FaceDisplay() {
    if (tick_ != nullptr) { lv_timer_del(tick_); tick_ = nullptr; }
}

// ---------------------------------------------------------------- UI
void FaceDisplay::SetupUI() {
    if (setup_ui_called_) {
        ESP_LOGW(TAG, "SetupUI() appele deux fois, ignore");
        return;
    }
    DisplayLockGuard lock(this);
    Display::SetupUI();

    auto theme = static_cast<LvglTheme*>(current_theme_);
    auto text_font = theme->text_font()->font();
    auto icon_font = theme->icon_font()->font();

    lv_obj_t* screen = lv_screen_active();
    lv_obj_set_style_text_font(screen, text_font, 0);
    lv_obj_set_style_bg_color(screen, Eteint(), 0);
    // Couleur de texte explicite : la barre d'etat (reseau, statut, batterie)
    // herite de l'ecran, et la couleur par defaut du theme est sombre, donc
    // allumee ici, comme le fond si on ne la fixait pas.
    lv_obj_set_style_text_color(screen, Allume(), 0);

    bar_ = lv_obj_create(screen);
    lv_obj_set_size(bar_, LV_HOR_RES, kTopBar);
    lv_obj_align(bar_, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_radius(bar_, 0, 0);
    lv_obj_set_style_border_width(bar_, 0, 0);
    lv_obj_set_style_pad_all(bar_, 0, 0);
    lv_obj_set_style_bg_opa(bar_, LV_OPA_TRANSP, 0);
    lv_obj_set_scrollbar_mode(bar_, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_layout(bar_, LV_LAYOUT_NONE, 0);

    network_label_ = lv_label_create(bar_);
    lv_obj_set_style_text_font(network_label_, icon_font, 0);
    lv_label_set_text(network_label_, "");
    lv_obj_align(network_label_, LV_ALIGN_LEFT_MID, 2, 0);

    mute_label_ = lv_label_create(bar_);
    lv_obj_set_style_text_font(mute_label_, icon_font, 0);
    lv_label_set_text(mute_label_, "");
    lv_obj_align(mute_label_, LV_ALIGN_RIGHT_MID, -18, 0);

    battery_label_ = lv_label_create(bar_);
    lv_obj_set_style_text_font(battery_label_, icon_font, 0);
    lv_label_set_text(battery_label_, "");
    lv_obj_align(battery_label_, LV_ALIGN_RIGHT_MID, -2, 0);

    status_label_ = lv_label_create(bar_);
    lv_obj_set_width(status_label_, LV_HOR_RES - 40);
    lv_label_set_long_mode(status_label_, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_style_text_align(status_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(status_label_, Lang::Strings::INITIALIZING);
    lv_obj_align(status_label_, LV_ALIGN_CENTER, 0, 0);

    notification_label_ = lv_label_create(bar_);
    lv_obj_set_width(notification_label_, LV_HOR_RES);
    lv_obj_set_style_text_align(notification_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(notification_label_, "");
    lv_obj_align(notification_label_, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_flag(notification_label_, LV_OBJ_FLAG_HIDDEN);

    face_ = lv_obj_create(screen);
    lv_obj_set_size(face_, LV_HOR_RES, LV_VER_RES - kTopBar);
    lv_obj_align(face_, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_radius(face_, 0, 0);
    lv_obj_set_style_border_width(face_, 0, 0);
    lv_obj_set_style_pad_all(face_, 0, 0);
    lv_obj_set_style_bg_opa(face_, LV_OPA_TRANSP, 0);
    lv_obj_set_scrollbar_mode(face_, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_layout(face_, LV_LAYOUT_NONE, 0);

    for (int i = 0; i < 2; i++) {
        eye_[i] = lv_obj_create(face_);
        lv_obj_set_style_border_width(eye_[i], 0, 0);
        lv_obj_set_style_pad_all(eye_[i], 0, 0);
        lv_obj_set_style_bg_opa(eye_[i], LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(eye_[i], Allume(), 0);
        lv_obj_set_scrollbar_mode(eye_[i], LV_SCROLLBAR_MODE_OFF);
        lv_obj_set_style_layout(eye_[i], LV_LAYOUT_NONE, 0);

        // Arc : remplace entierement l'oeil pour les emotions ou la forme
        // meme du regard devient l'expression (sourire, moue).
        arc_[i] = lv_arc_create(face_);
        lv_obj_remove_style(arc_[i], nullptr, LV_PART_KNOB);
        lv_obj_remove_flag(arc_[i], LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_arc_width(arc_[i], 6, LV_PART_MAIN);
        lv_obj_set_style_arc_color(arc_[i], Allume(), LV_PART_MAIN);
        lv_obj_set_style_arc_rounded(arc_[i], true, LV_PART_MAIN);
        lv_obj_set_style_arc_opa(arc_[i], LV_OPA_TRANSP, LV_PART_INDICATOR);
        lv_obj_add_flag(arc_[i], LV_OBJ_FLAG_HIDDEN);

        slant_[i] = lv_line_create(face_);
        lv_obj_set_style_line_width(slant_[i], 16, 0);
        lv_obj_set_style_line_color(slant_[i], Eteint(), 0);
        lv_obj_add_flag(slant_[i], LV_OBJ_FLAG_HIDDEN);
    }

    // Mention ponctuelle, en haut a droite du visage. Texte plutot
    // qu'emoji : sur un ecran monochrome de 1,3", un glyphe dessine
    // reste lisible la ou un emoji couleur devient une tache.
    overlay_ = lv_label_create(face_);
    // NE PAS fixer de police de texte ici : les assets remplacent la police
    // de texte du theme au demarrage, ce qui libere l'ancienne. Un pointeur
    // explicite pendrait et LVGL planterait dans lv_font_get_glyph_dsc.
    // On herite de celle de l'ecran, que SetTheme maintient a jour.
    lv_obj_set_style_text_color(overlay_, Allume(), 0);
    lv_label_set_text(overlay_, "");
    lv_obj_align(overlay_, LV_ALIGN_TOP_RIGHT, -3, 0);

    // Les « Z » du sommeil sont dessines, pas ecrits : la seule police de
    // texte liee fait 14 px, trop discret face a des yeux de 34. Une
    // polyligne se dimensionne librement.
    //   dimensions croissantes, disposees en diagonale montante
    static const int kZSize[3] = {7, 10, 14};
    static const int kZdx[3]   = {-34, -20, -3};
    static const int kZdy[3]   = { 22,  11,  0};
    for (int i = 0; i < 3; i++) {
        int z = kZSize[i];
        zzz_pts_[i][0] = {0, 0};
        zzz_pts_[i][1] = {(lv_coord_t)z, 0};
        zzz_pts_[i][2] = {0, (lv_coord_t)z};
        zzz_pts_[i][3] = {(lv_coord_t)z, (lv_coord_t)z};
        zzz_[i] = lv_line_create(face_);
        lv_line_set_points(zzz_[i], zzz_pts_[i], 4);
        lv_obj_set_style_line_width(zzz_[i], 2, 0);
        lv_obj_set_style_line_color(zzz_[i], Allume(), 0);
        lv_obj_set_style_line_rounded(zzz_[i], false, 0);
        lv_obj_align(zzz_[i], LV_ALIGN_TOP_RIGHT, kZdx[i], kZdy[i]);
        lv_obj_add_flag(zzz_[i], LV_OBJ_FLAG_HIDDEN);
    }

    // ---------------------------------------------- console « mainframe »
    // Plein ecran, barre d'etat comprise : une console qui s'arrete a
    // 48 px n'aurait pas l'air d'une console.
    console_ = lv_obj_create(screen);
    lv_obj_set_size(console_, LV_HOR_RES, LV_VER_RES);
    lv_obj_align(console_, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_radius(console_, 0, 0);
    lv_obj_set_style_border_width(console_, 0, 0);
    lv_obj_set_style_pad_all(console_, 0, 0);
    lv_obj_set_style_bg_color(console_, Eteint(), 0);
    lv_obj_set_style_bg_opa(console_, LV_OPA_COVER, 0);
    lv_obj_set_scrollbar_mode(console_, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_layout(console_, LV_LAYOUT_NONE, 0);

    con_label_ = lv_label_create(console_);
    // Ici une police explicite EST permise, contrairement a l'overlay du
    // visage : unscii_8 est compilee dans le binaire et n'appartient pas
    // au theme, donc les assets ne la liberent jamais.
    lv_obj_set_style_text_font(con_label_, &lv_font_unscii_8, 0);
    lv_obj_set_style_text_color(con_label_, Allume(), 0);
    // unscii_8 declare 9 px de hauteur de ligne pour un glyphe de 8 : la
    // rangee supplementaire est reservee aux jambages. Nos lignes n'en ont
    // aucun (capitales, chiffres, ponctuation), donc on la reprend pour
    // loger 8 lignes exactement dans les 64 px.
    lv_obj_set_style_text_line_space(con_label_, -1, 0);
    lv_obj_set_width(con_label_, LV_HOR_RES);
    lv_label_set_long_mode(con_label_, LV_LABEL_LONG_CLIP);
    lv_label_set_text(con_label_, "");
    lv_obj_align(con_label_, LV_ALIGN_TOP_LEFT, 0, 0);

    con_cur_ = lv_obj_create(console_);
    lv_obj_set_size(con_cur_, kConGlyph - 1, kConGlyph - 1);
    lv_obj_set_style_radius(con_cur_, 0, 0);
    lv_obj_set_style_border_width(con_cur_, 0, 0);
    lv_obj_set_style_pad_all(con_cur_, 0, 0);
    lv_obj_set_style_bg_color(con_cur_, Allume(), 0);
    lv_obj_set_style_bg_opa(con_cur_, LV_OPA_COVER, 0);
    lv_obj_align(con_cur_, LV_ALIGN_TOP_LEFT, 5 * kConGlyph, (kConRows - 1) * kConGlyph);

    // Au demarrage on est en IPL : console visible, visage et barre caches.
    lv_obj_add_flag(face_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(bar_,  LV_OBJ_FLAG_HIDDEN);
    ConsoleReset();

    ESP_LOGI(TAG, "console : glyphe %d px, hauteur de ligne %d px",
             (int)lv_font_get_glyph_width(&lv_font_unscii_8, 'M', 'M'),
             (int)lv_font_unscii_8.line_height);

    // Capteur de temperature interne. Le pilote n'accepte que des plages
    // predefinies : -10..80 est la seule qui couvre a la fois une piece
    // fraiche et une puce chaude. Un echec n'est pas fatal, la console
    // affichera "---".
    temperature_sensor_config_t tcfg = TEMPERATURE_SENSOR_CONFIG_DEFAULT(-10, 80);
    esp_err_t terr = temperature_sensor_install(&tcfg, &tsens_);
    if (terr == ESP_OK) terr = temperature_sensor_enable(tsens_);
    if (terr != ESP_OK) {
        ESP_LOGW(TAG, "capteur de temperature indisponible : %s", esp_err_to_name(terr));
        tsens_ = nullptr;
    } else {
        float c = 0.0f;
        temperature_sensor_get_celsius(tsens_, &c);
        ESP_LOGI(TAG, "capteur de temperature : %.1f C au demarrage", c);
    }

    look_ = LookForState(kDeviceStateIdle);
    cur_w_ = look_.w; cur_h_ = look_.h; cur_r_ = look_.radius;
    Layout(0);

    blink_countdown_ = RandRange(60, 110);
    idle_countdown_  = RandRange(50, 100);
    tick_ = lv_timer_create(TickCb, kTickMs, this);

    ESP_LOGI(TAG, "Visage anime initialise (%dx%d)", (int)LV_HOR_RES, (int)LV_VER_RES);
}

// ---------------------------------------------------------------- rendu
void FaceDisplay::Layout(int extra_h) {
    if (eye_[0] == nullptr) return;
    int h = std::max(2, cur_h_ + extra_h);

    for (int i = 0; i < 2; i++) {
        int dx = (i == 0 ? -kEyeHalfGap : kEyeHalfGap) + cur_gaze_x_;
        bool shut = (wink_ && i == 1 && blink_phase_ == 0);

        if (cur_shape_ == Shape::Rect) {
            lv_obj_remove_flag(eye_[i], LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(arc_[i], LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_size(eye_[i], cur_w_, shut ? 4 : h);
            lv_obj_set_style_radius(eye_[i], shut ? 2 : cur_r_, 0);
            lv_obj_align(eye_[i], LV_ALIGN_CENTER, dx, cur_gaze_y_);
        } else {
            // Le rectangle s'efface, l'arc devient l'oeil.
            lv_obj_add_flag(eye_[i], LV_OBJ_FLAG_HIDDEN);
            lv_obj_remove_flag(arc_[i], LV_OBJ_FLAG_HIDDEN);
            bool smile = (cur_shape_ == Shape::ArcSmile);
            lv_obj_set_size(arc_[i], cur_w_, cur_w_);
            lv_arc_set_bg_angles(arc_[i], smile ? 20 : 200, smile ? 160 : 340);
            // On decale le cercle pour que la courbe visible tombe au centre
            lv_obj_align(arc_[i], LV_ALIGN_CENTER, dx,
                         cur_gaze_y_ + (smile ? -cur_w_ / 3 : cur_w_ / 3));
        }

        if (look_.slant != 0 && cur_shape_ == Shape::Rect && blink_phase_ == 0) {
            bool inner_down    = (look_.slant > 0);
            bool inner_is_right = (i == 0);
            bool right_down    = inner_down ? inner_is_right : !inner_is_right;
            slant_pts_[i][0] = {0, (lv_coord_t)(right_down ? 0 : 12)};
            slant_pts_[i][1] = {(lv_coord_t)cur_w_, (lv_coord_t)(right_down ? 12 : 0)};
            lv_line_set_points(slant_[i], slant_pts_[i], 2);
            lv_obj_align(slant_[i], LV_ALIGN_CENTER,
                         dx - cur_w_ / 2, cur_gaze_y_ - h / 2 - 3);
            lv_obj_remove_flag(slant_[i], LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(slant_[i], LV_OBJ_FLAG_HIDDEN);
        }
    }
}

// ---------------------------------------------------- console mainframe

void FaceDisplay::ConsoleRender() {
    strcpy(con_[kConRows - 1], "===>");

    char* p = con_join_;
    for (int i = 0; i < kConRows; i++) {
        size_t n = strlen(con_[i]);
        memcpy(p, con_[i], n); p += n;
        if (i < kConRows - 1) *p++ = '\n';
    }
    *p = '\0';
    if (con_label_) lv_label_set_text(con_label_, con_join_);

    // Le curseur suit la fin de l'invite au lieu de rester en colonne 5.
    if (con_cur_) {
        int col = (int)strlen(con_[kConRows - 1]) + 1;
        if (col > kConCols - 1) col = kConCols - 1;
        lv_obj_align(con_cur_, LV_ALIGN_TOP_LEFT, col * kConGlyph, (kConRows - 1) * kConGlyph);
    }
}

void FaceDisplay::ConsoleReset() {
    memset(con_, 0, sizeof(con_));
    // Invite fixe en bas, comme le « Command ===> » d'un terminal 3270 :
    // c'est elle qui fait la difference entre une console et un journal.
    strcpy(con_[kConRows - 1], "===>");
    ConsoleRender();
    con_wait_ = 1;
}

// Defilement d'un cran entier : les lignes anciennes montent d'une case et
// la nouvelle s'inscrit en bas, comme sur un vrai terminal.
void FaceDisplay::ConsolePush(const char* line) {
    for (int i = 0; i < kConScroll - 1; i++) memcpy(con_[i], con_[i + 1], sizeof(con_[0]));
    strncpy(con_[kConScroll - 1], line, kConCols);
    con_[kConScroll - 1][kConCols] = '\0';
    ConsoleRender();
}

void FaceDisplay::ShowScreen(Screen s) {
    if (s == screen_) return;
    screen_ = s;
    if (s == Screen::Face) {
        lv_obj_add_flag(console_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(face_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(bar_,  LV_OBJ_FLAG_HIDDEN);
    } else {
        if (s == Screen::Console) ConsoleReset();   // elle se remplit sous les yeux
        lv_obj_remove_flag(console_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(face_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(bar_,  LV_OBJ_FLAG_HIDDEN);
    }
}

// Sequence de demarrage : chaque ligne porte une valeur relevee a l'instant
// ou elle s'affiche. Elle s'epuise apres LINK ; la rotation de veille prend
// alors le relais jusqu'a ce que l'appareil soit reellement pret.
bool FaceDisplay::NextIplLine(char* out, size_t n) {
    char v[24] = {0};
    switch (ipl_step_++) {
        case 0: snprintf(out, n, "ROKY MAINFRAME"); return true;
        case 1: snprintf(out, n, "ROK0001I IPL");   return true;
        case 2: LigneVal(out, n, "VER", esp_app_get_description()->version); return true;
        case 3: LigneVal(out, n, "SYS", "ESP32-C3"); return true;
        case 4: LigneVal(out, n, "CPU", "MP=1");     return true;   // mono-coeur
        case 5: TempPuce(tsens_, v, sizeof(v));
                LigneVal(out, n, "TEMP", v);         return true;
        case 6: LigneVal(out, n, "IDF", IDF_VER);    return true;
        case 7: LigneVal(out, n, "LPAR", "ROKY");    return true;
        case 8: LigneVal(out, n, "DEV", "0:003C");   return true;   // OLED en I2C 0x3C
        case 9: snprintf(v, sizeof(v), "%u", (unsigned)HeapLibre());
                LigneVal(out, n, "HEAP", v);  return true;
        case 10: Batterie(v, sizeof(v));
                LigneVal(out, n, "BATT", v);  return true;
        case 11: MacCourt(v, sizeof(v));
                 LigneVal(out, n, "MAC", v);  return true;
        case 12: LigneVal(out, n, "WIFI",
                          WifiInfo(nullptr, v, sizeof(v)) ? v : "SCAN"); return true;
        case 13: { int r = 0;
                   if (WifiInfo(&r, nullptr, 0)) snprintf(v, sizeof(v), "%d", r);
                   else                          strcpy(v, "---");
                   LigneVal(out, n, "RSSI", v); return true; }
        case 14: if (!IpAddr(v, sizeof(v))) strcpy(v, "---");
                 snprintf(out, n, "IP=%.13s", v); return true;
        case 15: ServeurCourt(v, sizeof(v));
                 LigneVal(out, n, "HOST", v);        return true;
        case 16: snprintf(out, n, "ROK0010I LINK"); return true;
        default: return false;
    }
}

void FaceDisplay::NextIdleLine(char* out, size_t n) {
    char v[24] = {0};
    uint32_t up = (uint32_t)(esp_timer_get_time() / 1000000);
    switch (con_step_++ % 13) {
        case 0: snprintf(out, n, "ROK0001I IDLE"); break;
        case 1: snprintf(v, sizeof(v), "%u", (unsigned)HeapLibre());
                LigneVal(out, n, "HEAP", v);  break;
        case 2: snprintf(v, sizeof(v), "%u:%02u:%02u",
                         (unsigned)(up / 3600), (unsigned)((up / 60) % 60),
                         (unsigned)(up % 60));
                LigneVal(out, n, "UP", v);    break;
        case 3: { int r = 0;
                  if (WifiInfo(&r, nullptr, 0)) snprintf(v, sizeof(v), "%d", r);
                  else                          strcpy(v, "---");
                  LigneVal(out, n, "RSSI", v); break; }
        case 4: Horodate(v, sizeof(v), false); LigneVal(out, n, "TIME", v); break;
        case 9:  Horodate(v, sizeof(v), true); LigneVal(out, n, "DATE", v); break;
        case 10: TempPuce(tsens_, v, sizeof(v)); LigneVal(out, n, "TEMP", v); break;
        case 5: snprintf(v, sizeof(v), "%u", (unsigned)HeapMini());
                LigneVal(out, n, "HMIN", v);  break;
        case 6: snprintf(v, sizeof(v), "%u", (unsigned)uxTaskGetNumberOfTasks());
                LigneVal(out, n, "TASKS", v); break;
        case 7: MacCourt(v, sizeof(v));
                LigneVal(out, n, "MAC", v);   break;
        case 8: if (!WifiInfo(nullptr, v, sizeof(v))) strcpy(v, "---");
                snprintf(out, n, "WIFI=%.11s", v); break;
        case 11: Batterie(v, sizeof(v));
                 LigneVal(out, n, "BATT", v); break;
        default: if (!IpAddr(v, sizeof(v))) strcpy(v, "---");
                 snprintf(out, n, "IP=%.13s", v);  break;   // cas 10
    }
}

void FaceDisplay::ConsoleTick(DeviceState state) {
    if (console_ == nullptr) return;

    // Ces etats s'adressent a l'utilisateur — code d'activation, consignes
    // wifi, message d'erreur — et passent par la barre d'etat. La console
    // ne doit jamais les recouvrir.
    if (state == kDeviceStateWifiConfiguring || state == kDeviceStateActivating ||
        state == kDeviceStateUpgrading       || state == kDeviceStateFatalError) {
        ipl_done_ = true;
        ready_hold_ = -1;
        ShowScreen(Screen::Face);
        return;
    }

    Screen want;
    if (!ipl_done_) {
        want = Screen::Ipl;
        bool pret = (state == kDeviceStateIdle || state == kDeviceStateListening ||
                     state == kDeviceStateSpeaking);
        if (pret) {
            if (ready_hold_ < 0) { ConsolePush("ROK0042I READY"); ready_hold_ = kReadyHold; }
            else if (--ready_hold_ <= 0) {
                ipl_done_ = true;
                ready_hold_ = -1;      // sinon le maintien bloquerait a jamais
                want = Screen::Face;   // les lignes de la console de veille
            }
        }
    } else if (state == kDeviceStateIdle && idle_ticks_ > kConsoleAfter) {
        want = Screen::Console;
    } else {
        want = Screen::Face;
    }

    ShowScreen(want);
    if (want == Screen::Face) return;

    // Curseur : clignotement a 1 Hz. Objet distinct du texte, donc seuls
    // quelques pixels sont invalides — le label entier ne se redessine pas.
    if (phase_ % 24 == 0)       lv_obj_remove_flag(con_cur_, LV_OBJ_FLAG_HIDDEN);
    else if (phase_ % 24 == 12) lv_obj_add_flag(con_cur_, LV_OBJ_FLAG_HIDDEN);

    if (ready_hold_ >= 0) return;          // ecran fige apres READY
    if (--con_wait_ > 0) return;

    char line[24];
    if (want == Screen::Ipl && NextIplLine(line, sizeof(line))) con_wait_ = kIplPace;
    else { NextIdleLine(line, sizeof(line)); con_wait_ = kConPace; }
    ConsolePush(line);
}

void FaceDisplay::OnTick() {
    if (eye_[0] == nullptr) return;
    DisplayLockGuard lock(this);
    phase_++;

    DeviceState state = Application::GetInstance().GetDeviceState();

    // Assoupissement : apres une longue inactivite, les yeux se ferment
    // a demi et un « z z z » apparait. La console prend ensuite le relais.
    if (state == kDeviceStateIdle && !mood_active_) idle_ticks_++;
    else idle_ticks_ = 0;

    // Ecran mainframe : IPL au demarrage, console apres la ceremonie de
    // sommeil. Tant qu'il est affiche, le visage n'a pas a etre anime.
    ConsoleTick(state);
    if (screen_ != Screen::Face) return;

    // L'etat pilote la forme de base ; l'emotion la remplace le temps de son TTL.
    Look base = LookForState(state);
    if (idle_ticks_ > kSleepAfter) {
        //   w   h   r   forme         gy br per sl idle sh sv
        base = {42, 12,  5, Shape::Rect, 6, 2, 70, 0, true, 0, 0};
    }

    if (mood_active_ && --mood_ttl_ <= 0) { mood_active_ = false; wink_ = false; }
    look_ = mood_active_ ? mood_ : base;
    UpdateOverlay(state);

    cur_w_ = Approach(cur_w_, look_.w, 2);
    cur_r_ = Approach(cur_r_, look_.radius, 1);
    cur_shape_ = look_.shape;

    // Clignement, prioritaire et seulement sur la forme rectangulaire
    int h_target = look_.h;
    if (cur_shape_ == Shape::Rect) {
        if (blink_phase_ > 0)      { blink_phase_--; h_target = 2; }
        else if (--blink_countdown_ <= 0) {
            blink_phase_ = 3;
            blink_countdown_ = RandRange(60, 110);
        }
    }
    cur_h_ = Approach(cur_h_, h_target, blink_phase_ > 0 ? 12 : 3);

    // Regard vagabond
    if (look_.idle_gaze) {
        if (--idle_countdown_ <= 0) {
            gaze_tx_ = RandRange(-9, 9);
            gaze_ty_ = RandRange(-3, 3) + look_.gaze_y;
            idle_countdown_ = RandRange(50, 110);
        }
    } else {
        gaze_tx_ = 0;
        gaze_ty_ = look_.gaze_y;
    }
    cur_gaze_x_ = Approach(cur_gaze_x_, gaze_tx_, 1);
    cur_gaze_y_ = Approach(cur_gaze_y_, gaze_ty_, 1);

    // Respiration / pulsation : module la hauteur sans modifier la cible
    int breathe = (blink_phase_ > 0) ? 0
                : Triangle(phase_, look_.period, look_.breathe);

    // Tremblements
    int sx = look_.shake_h ? Triangle(phase_, 6, look_.shake_h) : 0;
    int sy = look_.shake_v ? Triangle(phase_, 6, look_.shake_v) : 0;
    cur_gaze_x_ += sx; cur_gaze_y_ += sy;
    Layout(breathe);
    cur_gaze_x_ -= sx; cur_gaze_y_ -= sy;
}

// Choisit et anime la mention ponctuelle.
void FaceDisplay::UpdateOverlay(DeviceState state) {
    if (overlay_ == nullptr) return;

    Overlay kind = Overlay::None;
    if (state == kDeviceStateFatalError) {
        kind = Overlay::Alert;
    } else if (state == kDeviceStateConnecting || state == kDeviceStateActivating ||
               state == kDeviceStateUpgrading ||
               (mood_active_ && mood_.h <= 8)) {          // pose « reflexion »
        kind = Overlay::Think;
    } else if (idle_ticks_ > 25 * 25 || (mood_active_ && mood_.gaze_y >= 6)) {
        kind = Overlay::Sleep;                            // inactivite ou pose « somnolence »
    } else if (mood_active_ && mood_badge_[0] != '\0') {
        kind = Overlay::Badge;
    }

    if (kind != overlay_kind_) { overlay_kind_ = kind; overlay_step_ = 0; }
    if (kind != Overlay::Sleep && zzz_[0] != nullptr) {
        for (int i = 0; i < 3; i++) lv_obj_add_flag(zzz_[i], LV_OBJ_FLAG_HIDDEN);
    }

    switch (kind) {
        case Overlay::None:
            lv_label_set_text(overlay_, "");
            break;
        case Overlay::Alert:
            lv_label_set_text(overlay_, "!");
            break;
        case Overlay::Badge:
            lv_label_set_text(overlay_, mood_badge_);
            lv_obj_align(overlay_, LV_ALIGN_TOP_RIGHT, -3, 0);
            break;
        case Overlay::Think: {                              // . .. ...
            static const char* seq[] = {"", ".", "..", "..."};
            if (phase_ % 8 == 0) overlay_step_ = (overlay_step_ + 1) % 4;
            lv_label_set_text(overlay_, seq[overlay_step_]);
            break;
        }
        case Overlay::Sleep: {          // Z dessines, reveles un a un
            lv_label_set_text(overlay_, "");
            if (phase_ % 18 == 0) overlay_step_ = (overlay_step_ + 1) % 4;
            for (int i = 0; i < 3; i++) {
                (i < overlay_step_) ? lv_obj_remove_flag(zzz_[i], LV_OBJ_FLAG_HIDDEN)
                                    : lv_obj_add_flag(zzz_[i], LV_OBJ_FLAG_HIDDEN);
            }
            break;
        }
    }
}

void FaceDisplay::TickCb(lv_timer_t* t) {
    static_cast<FaceDisplay*>(lv_timer_get_user_data(t))->OnTick();
}

void FaceDisplay::SetEmotion(const char* emotion) {
    DisplayLockGuard lock(this);
    if (eye_[0] == nullptr) return;
    Look m; int ttl = 0; const char* badge = "";
    if (LookForEmotion(emotion, &m, &ttl, &badge)) {
        mood_ = m; mood_ttl_ = ttl; mood_active_ = true; mood_badge_ = badge;
        wink_ = (std::strcmp(emotion, "winking") == 0);
        blink_phase_ = 0;
    } else {
        mood_active_ = false; wink_ = false; mood_badge_ = "";
    }
}

void FaceDisplay::SetChatMessage(const char* role, const char* content) {
    (void)role; (void)content;   // mise en page « visage seul »
}
