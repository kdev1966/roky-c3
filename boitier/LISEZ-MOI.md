# Boîtier de Roky — mini TV LED

Une TV LED de 89 × 50 mm, clin d'œil à l'atelier de réparation TV LED et
plasma de son propriétaire : façade noire avec logo blanc, écran derrière un
insert fumé, bosse arrière pour l'électronique, son par le dessous, deux
pieds en V.

Modèle OpenSCAD paramétrique : toutes les cotes vivent dans
`parametres.scad`. `./construire.sh` régénère les pièces (`stl/`) et les vues
(`apercus/`).

## Pièces

| Fichier | Matière | Orientation | Remarque |
|---|---|---|---|
| `stl/coque.stl` | PLA noir | debout, dessous sur le plateau | le plafond de la bosse ponte sur 15 mm |
| `stl/dalle.stl` | PLA noir **puis blanc** | à plat, face avant vers le HAUT | changement de filament, voir plus bas |
| `stl/dos.stl` | PLA noir | face extérieure sur le plateau | entretoises et nervures vers le haut |
| `stl/pied_gauche.stl`, `stl/pied_droit.stl` | PLA noir | couchés sur le flanc | sans support |
| insert d'écran | plexiglas fumé 1 mm, ou verre de protection d'écran | — | découpé à **31 × 21 mm** |

Buse 0,4, couches 0,2 mm, parois de 4 périmètres. Aucun support nécessaire.

### Logo blanc : changement de filament

La dalle s'imprime face avant vers le haut. Le logo « ROKY » et le voyant de
veille sont en relief de 0,4 mm au-dessus de la face (qui est à 1,6 mm).
Dans le trancheur, insérer un **changement de filament (M600) à 1,6 mm**,
soit au début de la 9ᵉ couche en 0,2 mm : passer au **PLA blanc**. Seules
les deux dernières couches, celles du relief, sortent blanches. Le lissage
(« ironing ») de la face supérieure donne un aspect de façade plus net.

### Le PLA et la chaleur

Le PLA se ramollit vers 55-60 °C. La carte chauffe peu, mais pas de Roky au
soleil derrière une vitre ni dans une voiture.

## Quincaillerie

- 2 vis M2 × 6 autotaraudeuses, tête fraisée (fermeture du dos)
- colle cyanoacrylate ou adhésif double face fin (dalle dans la coque,
  insert dans sa feuillure)
- l'insert fumé de 31 × 21 × 1 mm

## Montage

1. **Insert** : le coller dans la feuillure de la fenêtre, à fleur de la
   façade.
2. **Dalle** : la coller par l'avant dans son logement, à fleur, logo en
   bas. Le petit trou du micro est sous la fenêtre, à droite.
3. **Haut-parleur** : grille vers le BAS, pattes enfilées sur les deux pions
   du plancher.
4. **Carte** : écran vers l'avant, USB-C en haut. Elle entre par le dos ;
   l'USB-C glisse dans la fente du dessus, les deux boutons dans les fentes
   du flanc gauche. Le verre de l'écran vient contre la dalle.
5. **Dos** : il se pose sur l'arrière de la bosse, centré par sa jupe. Ses
   deux pions entrent dans les trous de fixation de la carte, sa nervure
   haute plaque la carte, son doigt bas tient le haut-parleur. Deux vis M2
   dans les pattes des flancs, comme au dos d'une vraie TV.
6. **Pieds** : tenons dans les mortaises sous la dalle, jambes vers
   l'extérieur.

## Cotes à confirmer avant impression

Estimée sur photo, marquée `A MESURER` dans `parametres.scad` :

- `BOUTON_H` : hauteur du corps des deux boutons sur le circuit (4 mm
  supposés). Les fentes du flanc gauche sont larges, l'erreur est tolérée.

Mesurées : écran à 6 mm du bord gauche, 5 du droit, 13 du haut et 8 du bas
(centre à 19 ; 16).

Repère des mesures : carte vue de face, USB-C en haut, x depuis le bord
gauche, z depuis le bord **bas**.

## Garde-fous

`parametres.scad` vérifie que la carte tient sous la paroi du haut et
au-dessus du haut-parleur. Une mesure qui ne passe plus arrête le rendu avec
un message au lieu de produire une pièce fausse.
