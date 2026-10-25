// ===========================================================================
//  Roky - boitier mini TV LED
//
//  PIECE choisit ce qui est rendu (openscad -D 'PIECE="coque"') :
//    "assemblage"  la TV montee, carte et haut-parleur en fantome
//    "eclate"      vue eclatee pour l'ordre de montage
//    "coque"       a imprimer debout, dessous sur le plateau (PLA noir)
//    "dalle"       a plat, face avant en haut (PLA noir, logo en PLA blanc)
//    "dos"         a imprimer face exterieure sur le plateau
//    "pied_gauche" / "pied_droit"  couches sur le flanc, sans support
// ===========================================================================
include <parametres.scad>

PIECE = "assemblage";

// ------------------------------------------------------------ primitives
// Pave dans le repere de la TV, arrondi vu de face (aretes paralleles a Y).
module pave(x0, x1, z0, z1, y0, y1, r = 0) {
    if (r <= 0) {
        translate([x0, y0, z0]) cube([x1 - x0, y1 - y0, z1 - z0]);
    } else {
        hull() for (x = [x0 + r, x1 - r], z = [z0 + r, z1 - r])
            translate([x, y0, z]) rotate([-90, 0, 0]) cylinder(r = r, h = y1 - y0);
    }
}

// Cylindre d'axe Y.
module tige(x, z, y0, y1, d) {
    translate([x, y0, z]) rotate([-90, 0, 0]) cylinder(d = d, h = y1 - y0);
}

// Texte grave dans une face orientee vers +Y (le dos, lu de derriere).
module grave_arriere(t, taille, x, z, y) {
    translate([x, y - GRAVURE, z]) rotate([90, 0, 180])
        linear_extrude(GRAVURE + 0.1)
            text(t, size = taille, halign = "center", valign = "center",
                 font = "Liberation Sans:style=Bold");
}

// ------------------------------------------------------------ reperes
USB_XC      = CARTE_X0 + USB_X;
POUSSOIR_ZC = CARTE_Z0 + POUSSOIR_Z;
GLISSE_ZC   = CARTE_Z0 + GLISSE_Z;
MIC_XC      = CARTE_X0 + MIC_X;
MIC_ZC      = CARTE_Z0 + MIC_Z;
Y_DOS_INT   = BOSSE_P;                  // face interieure du dos
Y_DOS_EXT   = BOSSE_P + PAROI;

// Vis du dos : sur deux pattes exterieures, comme au dos d'une vraie TV.
// A l'interieur, aucun coin n'est libre : la carte touche le flanc gauche
// (ses boutons y sortent) et le haut-parleur occupe le bas. Cote gauche, la
// patte et son chanfrein passent entre les fentes des deux boutons.
VIS_Z = POUSSOIR_ZC + 4.5/2 + 0.5 + VIS_PATTE + 3;
VIS = [[-(BOSSE_W/2 + VIS_PATTE/2), VIS_Z], [BOSSE_W/2 + VIS_PATTE/2, VIS_Z]];
assert(VIS_Z + 3 + 0.5 <= GLISSE_ZC - 7/2,
       "la patte de vis gauche mord sur la fente de l'interrupteur");

// ===========================================================================
//  Coque : dalle fine + bosse arriere, d'une seule piece
// ===========================================================================
module coque() {
    difference() {
        union() {
            difference() {
                union() {
                    pave(-TV_W/2, TV_W/2, 0, TV_H, 0, PANNEAU_P, TV_R);
                    pave(-BOSSE_W/2, BOSSE_W/2, 0, TV_H, 0, BOSSE_P);
                }
                // logement de la dalle (toute la face avant), collee a fleur
                pave(-(TV_W/2 - BEZ), TV_W/2 - BEZ, BEZ, TV_H - BEZ,
                     -1, DALLE_E, 0.4);
                // interieur de la bosse, ouvert sur la dalle et vers l'arriere
                pave(-BOSSE_INT, BOSSE_INT, PAROI, TV_H - PAROI,
                     DALLE_E - 0.01, BOSSE_P + 1);
            }
            // pattes des vis, chanfreinees a 45 degres dessous : la coque
            // s'imprime debout, une avancee plate demanderait un support
            for (s = [-1, 1]) hull() {
                translate([s > 0 ? BOSSE_W/2 - 0.01 : -BOSSE_W/2 - VIS_PATTE,
                           Y_DOS_INT - VIS_PROF, VIS_Z - 3])
                    cube([VIS_PATTE + 0.01, VIS_PROF, 6]);
                translate([s * BOSSE_W/2 - 0.01, Y_DOS_INT - VIS_PROF, VIS_Z - 3 - VIS_PATTE])
                    cube([0.02, VIS_PROF, 6 + VIS_PATTE]);
            }
            // pions du haut-parleur, dans ses pattes
            for (s = [-1, 1])
                translate([s * HP_ENTRAXE/2, HP_Y0 + HP_P/2, PAROI - 0.01])
                    cylinder(d = HP_TROU_D - 0.1, h = 2.5);   // PLA : pion plein, ajuste
        }
        // trous des vis (M2 autotaraudeuse)
        for (v = VIS) tige(v[0], v[1], Y_DOS_INT - VIS_PROF + 1, BOSSE_P + 1, 1.7);

        // fentes du haut-parleur, sous la TV
        for (x = [-HP_L/2 + 4 : 2.6 : HP_L/2 - 4])
            translate([x - 0.65, HP_Y0 + 1.5, -1])
                cube([1.3, HP_P - 3, PAROI + 1.02]);

        // USB-C dans la paroi du haut : fente ouverte vers l'arriere pour
        // que la carte entre par le dos, connecteur compris
        translate([USB_XC - USB_W/2 - 0.5, Y_PCB_AV - USB_H - 0.5, TV_H - PAROI - 0.1])
            cube([USB_W + 1, BOSSE_P, PAROI + 1]);

        // boutons du flanc gauche, memes fentes ouvertes vers l'arriere
        for (b = [[POUSSOIR_ZC, 4.5], [GLISSE_ZC, 7.0]])
            translate([-BOSSE_W/2 - 1, Y_PCB_AV - BOUTON_H - 0.4, b[0] - b[1]/2])
                cube([PAROI + 1.1, BOSSE_P, b[1]]);

        // mortaises des pieds
        for (s = [-1, 1])
            translate([s * PIED_X - TENON_L/2 - JEU, PANNEAU_P/2 - TENON_P/2 - JEU, -1])
                cube([TENON_L + 2*JEU, TENON_P + 2*JEU, TENON_H + 1.3]);
    }
}

// ===========================================================================
//  Dalle : PETG fume, l'OLED apparait au travers comme une image TV
// ===========================================================================
module dalle() {
    // Imprimee face avant vers le HAUT. Changer pour le filament BLANC a la
    // hauteur DALLE_E : seuls le logo et le voyant, en relief, sortent blancs.
    difference() {
        pave(-(TV_W/2 - BEZ) + DALLE_JEU, TV_W/2 - BEZ - DALLE_JEU,
             BEZ + DALLE_JEU, TV_H - BEZ - DALLE_JEU, 0, DALLE_E, 0.4);
        // fenetre devant la zone allumee de l'OLED
        pave(-FENETRE_W/2, FENETRE_W/2, OLED_CZ - FENETRE_H/2, OLED_CZ + FENETRE_H/2,
             -1, DALLE_E + 1, 0.5);
        // feuillure de l'insert fume, colle a fleur de la face avant
        pave(-INSERT_W/2 - INSERT_JEU, INSERT_W/2 + INSERT_JEU,
             OLED_CZ - INSERT_H/2 - INSERT_JEU, OLED_CZ + INSERT_H/2 + INSERT_JEU,
             -1, INSERT_E, 0.8);
        // rainure qui dessine le bas de l'ecran, au-dessus du bandeau
        translate([-(TV_W/2 - BEZ), -0.01, BEZ_BAS - LIGNE/2])
            cube([TV_W - 2*BEZ, LIGNE + 0.01, LIGNE]);
        // le micro est derriere la dalle : sans ce trou, Roky est sourd
        tige(MIC_XC, MIC_ZC, -1, DALLE_E + 1, 1.2);
    }
    logo();
}

// Logo et voyant de veille, en relief sur le bandeau : la partie blanche.
module logo() {
    zb = (BEZ + BEZ_BAS) / 2;
    translate([0, 0, zb]) rotate([90, 0, 0])
        linear_extrude(RELIEF)
            text(LOGO, size = LOGO_T, halign = "center", valign = "center",
                 font = "Liberation Sans:style=Bold");
    tige(TV_W/2 - BEZ - 5, zb, -RELIEF, 0.01, 1.2);
}

// ===========================================================================
//  Dos : ferme la bosse, tient la carte et le haut-parleur
// ===========================================================================
module dos() {
    difference() {
        union() {
            // plaque posee sur l'arriere de la bosse, avec ses deux oreilles
            pave(-BOSSE_W/2, BOSSE_W/2, 0, TV_H, Y_DOS_INT, Y_DOS_EXT);
            for (v = VIS) hull() {
                tige(v[0], v[1], Y_DOS_INT, Y_DOS_EXT, 6);
                tige(0, v[1], Y_DOS_INT, Y_DOS_EXT, 6);
            }
            // jupe qui centre le dos dans la bosse : flancs et haut seulement,
            // le bas est pris par le haut-parleur, plaque contre l'ouverture
            difference() {
                pave(-BOSSE_INT + JEU, BOSSE_INT - JEU, HP_Z0 + HP_E + 1.5,
                     TV_H - PAROI - JEU, Y_DOS_INT - 2, Y_DOS_INT + 0.01);
                pave(-BOSSE_INT + JEU + 1.2, BOSSE_INT - JEU - 1.2, 0,
                     TV_H - PAROI - JEU - 1.2, Y_DOS_INT - 3, Y_DOS_INT + 1);
            }
            // entretoises : pion dans le trou, epaulement sur le dos du PCB
            for (x = TROUS_X) {
                tige(CARTE_X0 + x, CARTE_Z0 + TROUS_Z, Y_PCB_DOS, Y_DOS_INT + 0.01, 4.4);
                tige(CARTE_X0 + x, CARTE_Z0 + TROUS_Z, Y_PCB_DOS - PCB_E + 0.2,
                     Y_PCB_DOS, TROU_D - 0.1);
            }
            // nervure qui plaque le haut de la carte contre la dalle
            pave(CARTE_X0 + 4, CARTE_X1 - 4, CARTE_Z1 - 4.6, CARTE_Z1 - 3,
                 Y_PCB_DOS, Y_DOS_INT + 0.01);
            // doigt qui maintient le haut-parleur sur ses pions, sous la carte
            pave(-9, 9, HP_Z0 + HP_E + JEU, min(HP_Z0 + HP_E + JEU + 1.2, CARTE_Z0 - 0.3),
                 HP_Y0 + 3, Y_DOS_INT + 0.01);
        }
        // vis, tete fraisee
        for (v = VIS) {
            tige(v[0], v[1], Y_DOS_INT - 1, Y_DOS_EXT + 1, 2.2);
            translate([v[0], Y_DOS_EXT - 1.2, v[1]]) rotate([-90, 0, 0])
                cylinder(d1 = 2.2, d2 = 4.2, h = 1.21);
        }
        // aerations au-dessus de la carte
        for (x = [-10.5 : 3 : 10.5])
            translate([x - 0.6, Y_DOS_INT - 1, 30]) cube([1.2, PAROI + 2, 11]);
        // gravures du dos : trous VESA et plaque signaletique, pour l'atelier
        for (x = [-10, 10], z = [19, 45])
            translate([x, Y_DOS_EXT - GRAVURE, z]) rotate([-90, 0, 0])
                cylinder(d = 2.6, h = GRAVURE + 0.1);
        grave_arriere("ROKY C3", 2.6, 0, 22, Y_DOS_EXT);
        grave_arriere("5V  USB-C", 1.8, 0, 17.5, Y_DOS_EXT);
    }
}

// ===========================================================================
//  Pied en V : un tenon dans la mortaise sous la dalle fine
// ===========================================================================
module pied() {
    // Jambes alignees sur une face du tenon : couche sur cette face, tout
    // repose sur le plateau, sans support. D'ou un pied gauche et un droit.
    e = 3.2;   // epaisseur des jambes
    x0 = -TENON_L/2;
    translate([x0, PANNEAU_P/2 - TENON_P/2, 0])
        cube([TENON_L, TENON_P, TENON_H]);
    for (bout = [-PIED_AV, PANNEAU_P/2 + PIED_AR]) hull() {
        translate([x0, 0, -2]) cube([e, PANNEAU_P, 2]);
        translate([x0, bout - 1.5, -PIED_H]) cube([e, 3, 1.6]);
    }
}

// ===========================================================================
//  Fantomes de la carte et du haut-parleur, pour les vues
// ===========================================================================
module carte_fantome() {
    color("seagreen", 0.85)
        pave(CARTE_X0, CARTE_X1, CARTE_Z0, CARTE_Z1, Y_PCB_AV, Y_PCB_DOS);
    color("royalblue", 0.9)
        pave(CARTE_X0 + 3, CARTE_X1 - 3, CARTE_Z0 + 3, CARTE_Z1 - 3, Y_VERRE + 1.5, Y_PCB_AV);
    color("white")
        pave(-OLED_W/2, OLED_W/2, OLED_CZ - OLED_H/2, OLED_CZ + OLED_H/2, Y_VERRE, Y_VERRE + 1.5);
    color("silver")
        pave(USB_XC - USB_W/2, USB_XC + USB_W/2, CARTE_Z1 - 7 + USB_SAILLIE, CARTE_Z1 + USB_SAILLIE,
             Y_PCB_AV - USB_H, Y_PCB_AV);
}
module hp_fantome() {
    color("dimgray")
        translate([-HP_L/2, HP_Y0, HP_Z0]) cube([HP_L, HP_P, HP_E]);
}

// ===========================================================================
//  Rendu
// ===========================================================================
// Jambes vers l'exterieur de la TV : le pied droit est le miroir du gauche.
module pieds() {
    translate([-PIED_X, 0, 0]) pied();
    translate([PIED_X, 0, 0]) mirror([1, 0, 0]) pied();
}

if (PIECE == "assemblage") {
    color("#1b1b1b") coque();
    color("#141414") difference() { dalle(); logo(); }
    color("white") logo();
    color("#30303a", 0.6)
        pave(-INSERT_W/2, INSERT_W/2, OLED_CZ - INSERT_H/2, OLED_CZ + INSERT_H/2, 0, INSERT_E);
    color("#2a2a2a") dos();
    color("#1b1b1b") pieds();
    carte_fantome();
    hp_fantome();
} else if (PIECE == "eclate") {
    translate([0, -25, 0]) { color("#141414") difference() { dalle(); logo(); }
                             color("white") logo(); }
    color("#30303a", 0.6) translate([0, -40, 0])
        pave(-INSERT_W/2, INSERT_W/2, OLED_CZ - INSERT_H/2, OLED_CZ + INSERT_H/2, 0, INSERT_E);
    color("#1b1b1b") coque();
    translate([0, 32, 0]) carte_fantome();
    translate([0, 32, 0]) hp_fantome();
    color("#2a2a2a") translate([0, 55, 0]) dos();
    color("#1b1b1b") translate([0, 0, -18]) pieds();
} else if (PIECE == "coque") {
    coque();
} else if (PIECE == "dalle") {
    // dos de la dalle sur le plateau, face avant et relief vers le haut
    translate([0, 0, DALLE_E]) rotate([-90, 0, 0]) translate([0, 0, -TV_H/2]) dalle();
} else if (PIECE == "dos") {
    translate([0, 0, Y_DOS_EXT]) rotate([-90, 0, 0]) translate([0, 0, -TV_H/2]) dos();
} else if (PIECE == "pied_gauche") {
    translate([0, 0, TENON_L/2]) rotate([0, -90, 0]) pied();
} else if (PIECE == "pied_droit") {
    mirror([1, 0, 0]) translate([0, 0, TENON_L/2]) rotate([0, -90, 0]) pied();
}
