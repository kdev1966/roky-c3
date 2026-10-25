# Roky C3 (WHA103)

Carte vendue comme assistant xiaozhi ESP32-C3, sérigraphiée/identifiée `WHA103`
par son firmware d'origine (xiaozhi 1.8.8, SKU `wha103`). Aucune documentation
publique : le brochage ci-dessous a été relevé dans le code machine de ce
firmware, à partir de la sauvegarde complète de la flash.

## Matériel

- ESP32-C3 (QFN32, rév. 0.4), 16 Mo de flash, pas de PSRAM
- Codec audio ES8311 (I2C 0x18), ampli commandé par GPIO11
- Écran OLED SSD1306 128×64 sur le même bus I2C (0x3C), tourné de 180° pour avoir l'USB-C en haut
- Bouton BOOT sur GPIO9
- Mesure de batterie sur GPIO2 (ADC1 canal 2)

## Brochage

| Fonction | GPIO |
|---|---|
| I2C SDA / SCL | 3 / 4 |
| I2S MCLK / BCLK / WS | 10 / 8 / 6 |
| I2S DOUT / DIN | 5 / 7 |
| Ampli (PA) | 11, **actif à l'état bas** |
| Bouton | 9 |
| Batterie (ADC) | 2 |

## Différences avec la Xmini-C3

- Ampli inversé (`pa_inverted = true`).
- GPIO2 mesure la batterie : pas de LED.
- L'eFuse `VDD_SPI_AS_GPIO` est déjà gravé sur la carte : ce firmware ne le
  grave pas (opération irréversible).

## Batterie

Atténuation 12 dB, table brute reprise du firmware d'origine : 2450 = 0 %,
3080 = 100 %. La carte n'a pas de détection de charge exploitable.

## Construire

```bash
source ~/esp/esp-idf/export.sh
python scripts/build.py roky-c3 --name roky-c3 --language fr-FR --wake-word wn9s_alexa
```

La langue et le mot de réveil se passent en paramètres (règle du dépôt,
vérifiée par `scripts/tests`) : sans eux, le build retombe sur les valeurs par
défaut (chinois, « Ni hao Xiaozhi »).

## Mot de réveil

« Alexa » (`wn9s_alexa`). Le C3 n'accepte que les modèles WakeNet9s
pré-entraînés ; esp-sr fournit ce modèle sans option de menu, d'où le symbole
`SR_WN_WN9S_ALEXA` déclaré dans `main/Kconfig.projbuild`.

## Audio UDP

Le serveur xiaozhi.me livre des paquets audio désordonnés, jusqu'à trois places
de retard. `mqtt_protocol.cc` les remet dans l'ordre au lieu de jeter les
retardataires : fenêtre de 8 paquets ou 300 ms, puis le trou est déclaré perdu.
Ce qui attend plus d'une seconde est jeté, pour ne pas mêler deux répliques.

## Écrans, thème TV

Roky est logé dans une mini TV LED : ses écrans parlent le langage d'un
atelier de réparation TV.

- **Allumage** : « ROKY », une barre de chargement, le nom de l'atelier.
- **Visage animé** dès que Roky est prêt, et pendant les conversations.
- **Au repos** (25 s sans échange, les yeux se ferment, puis 4 s) : le
  **menu de service** (`SERVICE MODE` : modèle, version, heures de marche,
  température, wifi ou IP, atelier et téléphone) pendant 15 s, puis
  **« NO SIGNAL »** qui rebondit pendant 45 s, en boucle. Le cadre en
  mouvement évite de marquer l'OLED.

Le nom et le téléphone de l'atelier vivent dans `atelier.h`, **ignoré par
git** car le dépôt est public. Copier `atelier.h.exemple` en `atelier.h` et
le remplir ; sans lui, le build utilise les valeurs de l'exemple.
