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
    // Trois ecrans possibles. L'IPL ne passe qu'une fois, au demarrage ;
    // ensuite on alterne entre le visage et la console de veille.
    enum class Screen { Ipl, Face, Console };

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

    lv_obj_t* bar_      = nullptr;   // barre d'etat : masquee en mode console
    lv_obj_t* face_ = nullptr;
    lv_obj_t* eye_[2]   = {nullptr, nullptr};
    lv_obj_t* arc_[2]   = {nullptr, nullptr};
    lv_obj_t* slant_[2] = {nullptr, nullptr};
    lv_obj_t* overlay_  = nullptr;   // mention textuelle courte : ha, <3, ?, !
    lv_obj_t* zzz_[3]   = {nullptr, nullptr, nullptr};  // Z dessines, taille libre
    lv_point_precise_t zzz_pts_[3][4]{};
    lv_point_precise_t slant_pts_[2][2]{};

    // ---------------------------------------------- console « mainframe »
    // 16 colonnes x 8 lignes en unscii_8 (8x8 px) : l'ecran entier, barre
    // d'etat comprise. Les lignes defilent d'un cran entier, jamais au
    // pixel : une image pleine coute 23 ms d'I2C a 400 kHz, un defilement
    // fluide a 25 Hz mangerait plus de la moitie du bus.
    lv_obj_t* console_   = nullptr;
    lv_obj_t* con_label_ = nullptr;
    lv_obj_t* con_cur_   = nullptr;   // curseur clignotant

    Screen screen_     = Screen::Ipl;
    bool   ipl_done_   = false;
    int    ipl_step_   = 0;
    int    ready_hold_ = -1;   // >= 0 : maintien de l'ecran apres READY
    int    con_wait_   = 1;    // ticks avant la prochaine ligne
    int    con_step_   = 0;    // rotation des lignes de veille

    char con_[8][17]{};            // tampon circulaire des lignes affichees
    char con_join_[8 * 17 + 1]{};  // les memes, jointes par des sauts de ligne

    void ShowScreen(Screen s);
    void ConsoleRender();
    void ConsoleReset();
    void ConsolePush(const char* line);
    void ConsoleTick(DeviceState state);
    bool NextIplLine(char* out, size_t n);    // false quand la sequence est finie
    void NextIdleLine(char* out, size_t n);

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
