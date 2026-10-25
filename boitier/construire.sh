#!/bin/bash
# Regenere les pieces a imprimer et TOUTES les vues depuis parametres.scad.
# Les vues sont rendues a chaque fois : rendues a la demande, elles finissent
# par deriver en silence derriere le modele (lecon du boitier de Kira).
set -e
cd "$(dirname "$0")"
SCAD=${SCAD:-openscad}
mkdir -p stl apercus

echo "→ pieces"
for p in coque dalle dos pied_gauche pied_droit; do
    "$SCAD" -o "stl/$p.stl" -D "PIECE=\"$p\"" roky_tv.scad 2>&1 \
        | grep -E "WARNING|ERROR|assert" | sed "s/^/   $p /" || true
    printf '   stl/%s.stl\n' "$p"
done

echo "→ vues"
# V <fichier> <largeur,hauteur> <projection> <camera> [options scad...]
V() { "$SCAD" -o "apercus/$1" --imgsize="$2" --projection="$3" --camera="$4" \
      --colorscheme=Tomorrow "${@:5}" roky_tv.scad >/dev/null 2>&1; printf '   apercus/%s\n' "$1"; }

V face.png          900,600 o "0,-300,25,0,0,25"
V trois_quarts.png  900,700 p "-140,-190,110,0,8,22"
V arriere.png       900,700 p "150,220,110,0,8,22"
V cote.png          700,600 o "300,8,22,0,8,22"
V dessous.png       900,600 p "-60,-90,-160,0,8,10"
V eclate.png        900,800 p "-230,-110,140,0,15,15" -D 'PIECE="eclate"'

# Garde-fou : aucune vue ne doit etre anterieure a la coque qu'elle montre.
retard=$(find apercus -name '*.png' ! -newer stl/coque.stl -print)
if [ -n "$retard" ]; then echo "⚠ vues perimees :"; echo "$retard" | sed 's/^/   /'; exit 1; fi
echo "termine."
