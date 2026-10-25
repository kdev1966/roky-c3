#ifndef _ROKY_FACE_DISPLAY_H_
#define _ROKY_FACE_DISPLAY_H_

#include "display/oled_display.h"
#include "device_state.h"
#include <driver/temperature_sensor.h>

// Visage anime plein ecran pour OLED 128x64 monochrome.
//
// Porte depuis Kira (XIAO ESP32-S3) sans la radio ni la PSRAM, absentes
// de l'ESP32-C3. Reprend le modele du projet Kora : la FORME de base suit l'ETAT de
// l'appareil (repos, ecoute, parole, connexion), et l'EMOTION vient se
// superposer temporairement avant de s'estomper. Un tick a 25 Hz
// interpole en continu, ce qui donne des transitions douces.
//
// Derive d'OledDisplay uniquement pour heriter de son constructeur
// (port LVGL, panneau, theme, polices). SetupUI() est entierement reecrit.
class FaceDisplay : public OledDisplay {
public:
    using OledDisplay::OledDisplay;
    ~FaceDisplay();

    virtual void SetupUI() override;
    virtual void SetEmotion(const char* emotion) override;
    virtual void SetChatMessage(const char* role, const char* content) override;

private:
    // Quatre ecrans, tous dans le theme TV de l'atelier : l'allumage ne
    // passe qu'une fois ; au repos, menu de service et « NO SIGNAL »
    // alternent ; le reste du temps, le visage.
    enum class Screen { Boot, Face, Service, NoSignal };

    // Forme des yeux a un instant donne.
    enum class Shape { Rect, ArcSmile, ArcFrown };

    struct Look {
        int   w, h, radius;
        Shape shape;
        int   gaze_y;      // decalage vertical
        int   breathe;     // amplitude de respiration, en pixels (0 = aucune)
        int   period;      // periode de respiration, en ticks
        int   slant;       // 0 aucun, +1 colere, -1 tristesse
        bool  idle_gaze;   // le regard vagabonde-t-il ?
        int   shake_h, shake_v;
    };

    lv_obj_t* bar_      = nullptr;   // barre d'etat : masquee hors du visage
    lv_obj_t* face_ = nullptr;
    lv_obj_t* eye_[2]   = {nullptr, nullptr};
    lv_obj_t* arc_[2]   = {nullptr, nullptr};
    lv_obj_t* slant_[2] = {nullptr, nullptr};
    lv_obj_t* overlay_  = nullptr;   // mention textuelle courte : ha, <3, ?, !
    lv_obj_t* zzz_[3]   = {nullptr, nullptr, nullptr};  // Z dessines, taille libre
    lv_point_precise_t zzz_pts_[3][4]{};
    lv_point_precise_t slant_pts_[2][2]{};

    // ---------------------------------------------- allumage de la TV
    lv_obj_t* boot_      = nullptr;
    lv_obj_t* boot_fill_ = nullptr;   // barre de chargement, sa partie pleine

    // ---------------------------------------------- menu de service
    lv_obj_t* service_     = nullptr;
    lv_obj_t* svc_label_   = nullptr;
    char      svc_text_[7 * 17 + 1]{};

    // ---------------------------------------------- « NO SIGNAL »
    lv_obj_t* nosig_     = nullptr;   // fond noir plein ecran
    lv_obj_t* nosig_box_ = nullptr;   // le cadre qui rebondit
    int nosig_x_ = 10, nosig_y_ = 12, nosig_dx_ = 1, nosig_dy_ = 1;

    Screen screen_    = Screen::Boot;
    bool   boot_done_ = false;
    int    veille_ticks_ = 0;   // duree passee dans les ecrans de repos

    void ShowScreen(Screen s);
    void VeilleTick(DeviceState state);
    void BootTick();
    void ServiceRender();
    void NoSignalTick();

    // Capteur de temperature interne de l'ESP32-C3. Il donne la temperature
    // de la PUCE, pas celle de l'air : ce qui compte est l'ecart entre
    // boitier ouvert et ferme, et la derive au fil des heures.
    temperature_sensor_handle_t tsens_ = nullptr;

    lv_timer_t* tick_ = nullptr;

    Look  look_{};             // cible courante
    bool  mood_active_ = false;
    int   mood_ttl_    = 0;    // ticks restants avant retour a l'etat seul
    Look  mood_{};
    bool  wink_ = false;

    int cur_w_ = 0, cur_h_ = 0, cur_r_ = 0;
    int cur_gaze_x_ = 0, cur_gaze_y_ = 0;
    int gaze_tx_ = 0, gaze_ty_ = 0;
    Shape cur_shape_ = Shape::Rect;

    // Mention ponctuelle affichee en haut a droite du visage.
    enum class Overlay { None, Sleep, Think, Alert, Badge };
    Overlay overlay_kind_ = Overlay::None;
    int overlay_step_ = 0;
    const char* mood_badge_ = "";   // mention courte liee a l'emotion
    int idle_ticks_   = 0;   // duree sans interaction, en ticks

    int phase_ = 0;
    int blink_countdown_ = 0, blink_phase_ = 0;
    int idle_countdown_  = 0;

    void Layout(int extra_h);
    void UpdateOverlay(DeviceState state);
    void OnTick();
    static void TickCb(lv_timer_t* t);
    static Look  LookForState(DeviceState s);
    static bool  LookForEmotion(const char* emotion, Look* out, int* ttl_ticks,
                                const char** badge);
    static int   Approach(int cur, int tgt, int step);
};

#endif // _ROKY_FACE_DISPLAY_H_
