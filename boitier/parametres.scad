// ===========================================================================
//  Boitier de Roky - mini TV LED pour la carte WHA103 (ESP32-C3)
//  SOURCE UNIQUE DE VERITE : lue par roky_tv.scad
//
//  Repere : origine au milieu du bord BAS de la face avant de la TV.
//    X  largeur      negatif = gauche, vue de face
//    Y  profondeur   0 = face avant, positif = vers l'arriere
//    Z  hauteur      0 = dessous de la TV (hors pieds)
//  Unites : millimetres.
//
//  Cotes de la carte : relevees au pied a coulisse, carte vue de face,
//  USB-C en haut, x depuis le bord GAUCHE, z depuis le bord BAS.
//  « A MESURER » = estime sur photo, a confirmer avant impression.
// ===========================================================================

$fn = 48;

// ------------------------------------------------------------ impression
JEU         = 0.25;   // jeu d'emboitement general
PAROI       = 1.6;    // 4 perimetres en buse 0,4

// ------------------------------------------------------------ carte WHA103
CARTE       = 37.0;   // carree
CARTE_E     = 9.0;    // dos du circuit -> dessus du verre de l'ecran
PCB_E       = 1.6;

OLED_W      = 25.0;   // zone allumee
OLED_H      = 15.0;
OLED_X      = 19.0;   // centre de la zone allumee : 6 du bord gauche, 5 du droit
OLED_Z      = 16.0;   // centre de la zone allumee : 8 du bord bas, 13 du haut

USB_X       = 18.0;   // centre du connecteur, bord haut
USB_SAILLIE = 2.0;
USB_W       = 9.0;    // receptacle USB-C standard
USB_H       = 3.3;    // epaisseur du receptacle, posee sur la face avant du PCB

POUSSOIR_Z  = 8.0;    // bord gauche, centre ; c'est le bouton BOOT (interrompre Roky)
GLISSE_Z    = 27.0;   // bord gauche, centre ; interrupteur a glissiere
BOUTON_SAILLIE = 1.0;
BOUTON_H    = 4.0;    // A MESURER : hauteur du corps des boutons sur le PCB

MIC_X       = 33.0;   // micro MEMS, face avant
MIC_Z       = 2.0;

JST_X       = 6.0;    // connecteur batterie, bord haut, ne depasse pas

TROUS_X     = [2.0, 28.0];   // trous de fixation, centres
TROUS_Z     = 2.0;
TROU_D      = 2.0;

// ------------------------------------------------------------ haut-parleur
HP_L        = 35.0;   // pattes comprises
HP_P        = 14.0;
HP_E        = 7.0;
HP_ENTRAXE  = 30.0;
HP_TROU_D   = 2.0;
// Couche, la grille vers le BAS : le son sort sous la TV, comme sur une vraie.

// ------------------------------------------------------------ la TV
TV_W        = 89.0;   // 16:9
TV_H        = 50.0;
TV_R        = 1.2;    // arrondi des aretes de la dalle
BEZ         = 1.6;    // bordure laterale et haute
BEZ_BAS     = 5.5;    // limite basse de l'ecran : dessous, le bandeau du logo
PANNEAU_P   = 5.0;    // epaisseur de la partie fine
// Dalle en PLA NOIR opaque : toute la face avant, bandeau du logo compris.
// L'image passe par une fenetre fermee par un insert fume colle a fleur
// (plexiglas de 1 mm ou verre de protection d'ecran de telephone).
DALLE_E     = 1.6;
DALLE_JEU   = 0.15;
FENETRE_W   = 27.0;   // zone allumee (26 x 16 d'apres le croquis) + 0,5 par cote
FENETRE_H   = 17.0;
INSERT_W    = FENETRE_W + 4;   // l'insert deborde de 2 mm : il se colle sur la levre
INSERT_H    = FENETRE_H + 4;
INSERT_E    = 1.0;
INSERT_JEU  = 0.2;
LIGNE       = 0.3;    // rainure qui separe l'ecran du bandeau, largeur et profondeur

// Centre de l'OLED dans la TV : au centre de la dalle visible, qui va de
// BEZ_BAS a TV_H - BEZ.
OLED_CZ     = (BEZ_BAS + TV_H - BEZ) / 2;

LOGO        = "ROKY";
LOGO_T      = 2.4;    // hauteur des lettres
RELIEF      = 0.4;    // logo et voyant en relief : changement de filament -> BLANC
GRAVURE     = 0.4;

// ------------------------------------------------------------ pieds en V
PIED_X      = TV_W/2 - 11;   // position des pieds depuis le centre
PIED_H      = 8.0;           // garde au sol : le son sort par dessous
PIED_AV     = 10.0;          // patte avant
PIED_AR     = 20.0;          // patte arriere, plus longue : la bosse pese derriere
TENON_L     = 8.0;
TENON_P     = 2.4;
TENON_H     = 3.6;

// ===========================================================================
//  Cotes derivees : ne pas modifier a la main
// ===========================================================================
CARTE_X0    = -OLED_X;                 // bord gauche de la carte
CARTE_X1    = CARTE_X0 + CARTE;
CARTE_Z0    = OLED_CZ - OLED_Z;        // bord bas de la carte
CARTE_Z1    = CARTE_Z0 + CARTE;
Y_VERRE     = DALLE_E;                 // le verre touche le dos de la dalle
Y_PCB_DOS   = Y_VERRE + CARTE_E;
Y_PCB_AV    = Y_PCB_DOS - PCB_E;

HP_Y0       = DALLE_E + JEU;           // juste derriere la dalle
HP_Z0       = PAROI;                   // pose sur le plancher, grille en bas

BOSSE_INT   = max(-CARTE_X0, CARTE_X1, HP_L/2) + JEU;
BOSSE_W     = 2*(BOSSE_INT + PAROI);
BOSSE_PINT  = max(Y_PCB_DOS + 3, HP_Y0 + HP_P + JEU);   // profondeur interieure
BOSSE_P     = BOSSE_PINT;          // la bosse est ouverte derriere : le dos s'y pose
VIS_PATTE   = 5.0;                 // pattes de vis sur les flancs de la bosse
VIS_PROF    = 7.0;

// Garde-fous : un echo plutot qu'une piece fausse en silence.
assert(CARTE_Z1 + JEU <= TV_H - PAROI,
       "la carte depasse sous la paroi du haut : baisser OLED_CZ ou monter TV_H");
assert(CARTE_Z0 - JEU >= HP_Z0 + HP_E,
       "la carte touche le haut-parleur : monter OLED_CZ ou TV_H");
assert(BOSSE_W < TV_W - 20, "la bosse arriere est plus large que la TV");
