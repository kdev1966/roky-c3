#include "face_display.h"
#include "lvgl_theme.h"
#include "application.h"
#include "board.h"
#include "assets/lang_config.h"

// Coordonnees de l'atelier : atelier.h est hors de git (depot public).
#if __has_include("atelier.h")
#include "atelier.h"
#else
#include "atelier.h.exemple"
#endif

#include <esp_log.h>
#include <esp_random.h>
#include <esp_timer.h>
#include <esp_wifi.h>
#include <esp_netif.h>
#include <esp_app_desc.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <cstring>
#include <cstdio>
#include <algorithm>

#define TAG "FaceDisplay"

namespace {
constexpr int kTopBar     = 16;
constexpr int kTickMs     = 40;   // 25 Hz
constexpr int kEyeHalfGap = 25;   // demi-ecart entre les centres des yeux

// ------------------------------------------------- ecrans de repos, theme TV
constexpr int kCols        = 16;  // 128 px / 8 px par glyphe unscii_8
constexpr int kSleepAfter  = 25 * 25;          // 25 s : les yeux se ferment
constexpr int kVeilleAfter = kSleepAfter + 100; // +4 s : les ecrans de repos
constexpr int kServiceTicks  = 15 * 25;   // menu de service : 15 s
constexpr int kNoSignalTicks = 45 * 25;   // puis « NO SIGNAL » : 45 s
constexpr int kNoSignalPas   = 2;         // un pixel tous les 2 ticks : 12,5 px/s
constexpr int kNoSigW = 80, kNoSigH = 24;  // le cadre qui rebondit
constexpr int kBootBarW = 84;             // barre de chargement de l'allumage

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
// Valeurs reelles de la machine. Le menu de service affiche l'etat du
// systeme et non un decor : un reparateur verrait le faux en une semaine.
bool WifiInfo(int* rssi) {
    wifi_ap_record_t ap;
    if (esp_wifi_sta_get_ap_info(&ap) != ESP_OK) return false;
    if (rssi) *rssi = ap.rssi;
    return true;
}

// « ETIQ  : valeur », deux-points toujours en colonne 7, comme les menus
// de service des TV : une etiquette courte, une valeur alignee.
void LigneVal(char* out, size_t n, const char* etiq, const char* val) {
    snprintf(out, n, "%-5s : %.8s", etiq, val);
}

bool IpAddr(char* out, size_t n) {
    esp_netif_t* nif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    esp_netif_ip_info_t ip{};
    if (!nif || esp_netif_get_ip_info(nif, &ip) != ESP_OK || ip.ip.addr == 0) return false;
    snprintf(out, n, IPSTR, IP2STR(&ip.ip));
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

// Objet LVGL nu : sans bordure, marge, defilement ni mise en page.
lv_obj_t* Bloc(lv_obj_t* parent, int w, int h, lv_color_t fond) {
    lv_obj_t* o = lv_obj_create(parent);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_radius(o, 0, 0);
    lv_obj_set_style_border_width(o, 0, 0);
    lv_obj_set_style_pad_all(o, 0, 0);
    lv_obj_set_style_bg_color(o, fond, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_scrollbar_mode(o, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_layout(o, LV_LAYOUT_NONE, 0);
    return o;
}

// Texte en police unscii, compilee dans le binaire : contrairement a la
// police du theme, les assets ne la liberent jamais.
lv_obj_t* Texte(lv_obj_t* parent, const lv_font_t* f, lv_color_t c, const char* t) {
    lv_obj_t* l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, f, 0);
    lv_obj_set_style_text_color(l, c, 0);
    lv_label_set_text(l, t);
    return l;
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

    // ---------------------------------------------- allumage de la TV
    // Le logo, une barre de chargement et l'atelier : ce que montre une TV
    // LED pendant les secondes ou elle demarre.
    boot_ = Bloc(screen, LV_HOR_RES, LV_VER_RES, Eteint());
    lv_obj_align(boot_, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_align(Texte(boot_, &lv_font_unscii_16, Allume(), "ROKY"), LV_ALIGN_TOP_MID, 0, 6);
    lv_obj_t* cadre = Bloc(boot_, kBootBarW, 7, Eteint());
    lv_obj_set_style_border_width(cadre, 1, 0);
    lv_obj_set_style_border_color(cadre, Allume(), 0);
    lv_obj_align(cadre, LV_ALIGN_TOP_MID, 0, 30);
    boot_fill_ = Bloc(cadre, 0, 3, Allume());
    lv_obj_align(boot_fill_, LV_ALIGN_LEFT_MID, 2, 0);
    lv_obj_align(Texte(boot_, &lv_font_unscii_8, Allume(), ATELIER_NOM), LV_ALIGN_TOP_MID, 0, 48);

    // ---------------------------------------------- menu de service
    // Le menu cache que les reparateurs ouvrent a la telecommande : un
    // bandeau inverse, puis des valeurs alignees. 8 lignes de 8 px.
    service_ = Bloc(screen, LV_HOR_RES, LV_VER_RES, Eteint());
    lv_obj_align(service_, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_t* titre = Bloc(service_, LV_HOR_RES, 8, Allume());
    lv_obj_align(titre, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_align(Texte(titre, &lv_font_unscii_8, Eteint(), "SERVICE MODE"), LV_ALIGN_CENTER, 0, 0);
    svc_label_ = Texte(service_, &lv_font_unscii_8, Allume(), "");
    // unscii_8 declare 9 px de hauteur de ligne pour un glyphe de 8 : la
    // rangee de plus sert aux jambages, que nos capitales n'ont pas. On la
    // reprend pour loger 7 lignes sous le bandeau.
    lv_obj_set_style_text_line_space(svc_label_, -1, 0);
    lv_obj_set_width(svc_label_, LV_HOR_RES);
    lv_label_set_long_mode(svc_label_, LV_LABEL_LONG_CLIP);
    lv_obj_align(svc_label_, LV_ALIGN_TOP_LEFT, 0, 8);

    // ---------------------------------------------- « NO SIGNAL »
    // L'ecran de toute TV sans source. Le cadre se deplace sans cesse : un
    // OLED qui garde la meme image finit par la marquer, et le proprietaire
    // le sait mieux que personne.
    nosig_ = Bloc(screen, LV_HOR_RES, LV_VER_RES, Eteint());
    lv_obj_align(nosig_, LV_ALIGN_TOP_LEFT, 0, 0);
    nosig_box_ = Bloc(nosig_, kNoSigW, kNoSigH, Eteint());
    lv_obj_set_style_border_width(nosig_box_, 1, 0);
    lv_obj_set_style_border_color(nosig_box_, Allume(), 0);
    lv_obj_align(Texte(nosig_box_, &lv_font_unscii_8, Allume(), "NO SIGNAL"), LV_ALIGN_TOP_MID, 0, 3);
    lv_obj_align(Texte(nosig_box_, &lv_font_unscii_8, Allume(), "HDMI 1"), LV_ALIGN_TOP_MID, 0, 13);
    lv_obj_set_pos(nosig_box_, nosig_x_, nosig_y_);

    // Au demarrage : l'allumage seul, visage et barre caches.
    lv_obj_add_flag(face_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(bar_,  LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(service_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(nosig_, LV_OBJ_FLAG_HIDDEN);

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

// ------------------------------------------------------- ecrans TV

void FaceDisplay::ShowScreen(Screen s) {
    if (s == screen_) return;
    screen_ = s;
    auto montre = [](lv_obj_t* o, bool oui) {
        if (o == nullptr) return;
        oui ? lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN) : lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
    };
    montre(boot_,    s == Screen::Boot);
    montre(face_,    s == Screen::Face);
    montre(bar_,     s == Screen::Face);
    montre(service_, s == Screen::Service);
    montre(nosig_,   s == Screen::NoSignal);
    if (s == Screen::Service) ServiceRender();
}

// La barre avance a vue pendant le demarrage et s'arrete a 90 % : la TV
// n'annonce jamais qu'elle est prete avant de l'etre.
void FaceDisplay::BootTick() {
    int plein = kBootBarW - 4;
    int w = std::min(phase_ * 2, plein * 9 / 10);
    lv_obj_set_width(boot_fill_, w);
}

void FaceDisplay::ServiceRender() {
    char v[24], l[7][kCols + 2];
    uint32_t up = (uint32_t)(esp_timer_get_time() / 1000000);

    LigneVal(l[0], sizeof(l[0]), "MODEL", "ROKY-C3");
    LigneVal(l[1], sizeof(l[1]), "SW", esp_app_get_description()->version);
    snprintf(v, sizeof(v), "%uH%02uM", (unsigned)(up / 3600), (unsigned)((up / 60) % 60));
    LigneVal(l[2], sizeof(l[2]), "RUN", v);
    TempPuce(tsens_, v, sizeof(v));
    LigneVal(l[3], sizeof(l[3]), "TEMP", v);
    // Le signal et l'adresse se partagent une ligne, 4 s chacun.
    if ((veille_ticks_ / 100) % 2 == 0) {
        int r = 0;
        if (WifiInfo(&r)) snprintf(v, sizeof(v), "%dDBM", r);
        else              strcpy(v, "---");
        LigneVal(l[4], sizeof(l[4]), "WIFI", v);
    } else {
        if (!IpAddr(v, sizeof(v))) strcpy(v, "---");
        snprintf(l[4], sizeof(l[4]), "IP=%.13s", v);
    }
    snprintf(l[5], sizeof(l[5]), "%.16s", ATELIER_NOM);
    snprintf(l[6], sizeof(l[6]), "TEL : %.10s", ATELIER_TEL);

    char* p = svc_text_;
    for (int i = 0; i < 7; i++) {
        size_t n = strlen(l[i]);
        memcpy(p, l[i], n); p += n;
        if (i < 6) *p++ = '\n';
    }
    *p = '\0';
    lv_label_set_text(svc_label_, svc_text_);
}

// Rebond a la maniere des vieux economiseurs d'ecran de DVD.
void FaceDisplay::NoSignalTick() {
    if (phase_ % kNoSignalPas != 0) return;
    nosig_x_ += nosig_dx_;
    nosig_y_ += nosig_dy_;
    if (nosig_x_ <= 0 || nosig_x_ >= LV_HOR_RES - kNoSigW) nosig_dx_ = -nosig_dx_;
    if (nosig_y_ <= 0 || nosig_y_ >= LV_VER_RES - kNoSigH) nosig_dy_ = -nosig_dy_;
    lv_obj_set_pos(nosig_box_, nosig_x_, nosig_y_);
}

void FaceDisplay::VeilleTick(DeviceState state) {
    if (boot_ == nullptr) return;

    // Ces etats s'adressent a l'utilisateur — code d'activation, consignes
    // wifi, message d'erreur — et passent par la barre d'etat du visage.
    // Aucun ecran de repos ne doit les recouvrir.
    if (state == kDeviceStateWifiConfiguring || state == kDeviceStateActivating ||
        state == kDeviceStateUpgrading       || state == kDeviceStateFatalError) {
        boot_done_ = true;
        ShowScreen(Screen::Face);
        return;
    }

    if (!boot_done_) {
        if (state == kDeviceStateStarting || state == kDeviceStateUnknown) {
            ShowScreen(Screen::Boot);
            BootTick();
            return;
        }
        boot_done_ = true;
    }

    if (state != kDeviceStateIdle || idle_ticks_ <= kVeilleAfter) {
        veille_ticks_ = 0;
        ShowScreen(Screen::Face);
        return;
    }

    // Au repos : menu de service, puis « NO SIGNAL », en boucle.
    int t = veille_ticks_++ % (kServiceTicks + kNoSignalTicks);
    if (t < kServiceTicks) {
        ShowScreen(Screen::Service);
        if (t % 25 == 0) ServiceRender();   // une fois par seconde
    } else {
        ShowScreen(Screen::NoSignal);
        NoSignalTick();
    }
}

void FaceDisplay::OnTick() {
    if (eye_[0] == nullptr) return;
    DisplayLockGuard lock(this);
    phase_++;

    DeviceState state = Application::GetInstance().GetDeviceState();

    // Assoupissement : apres une longue inactivite, les yeux se ferment
    // a demi et un « z z z » apparait. Les ecrans TV prennent ensuite le relais.
    if (state == kDeviceStateIdle && !mood_active_) idle_ticks_++;
    else idle_ticks_ = 0;

    // Ecrans TV : allumage au demarrage, menu de service et « NO SIGNAL »
    // apres la ceremonie de sommeil. Tant qu'ils sont affiches, le visage
    // n'a pas a etre anime.
    VeilleTick(state);
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
