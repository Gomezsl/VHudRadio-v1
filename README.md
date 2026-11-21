# VHud Radio Wheel v1.3.0-beta

Rueda de radio estilo GTA V para GTA SA Android 2.10 (AML .so), **con reproductor de User Tracks integrado**.

Esta es la línea v1: el reproductor propio dentro, para no depender del mod SAUserTracks, **con TODAS las funciones que llevaba la 2.59** (interruptores, blur, bloquear juego, modoCambio). La línea 2.x (rueda sola, con SAUserTracks externo) llegó a la 2.59.0-beta; esta v1 trae sus mismas funciones más el reproductor integrado.

## Qué hace

- Rueda de radio táctil de 13 segmentos (11 emisoras + User Tracks + Radio Off), como en GTA V.
- Botón flotante: 1 toque cambia a la siguiente emisora, 2 toques abren la rueda. La rueda también se abre con doble toque al claxon.
- Reproductor de User Tracks integrado (estación 12): busca la música en las carpetas de siempre y la reproduce con más buffer de audio que el mod externo, para que no se trabe en teléfonos con poca CPU. Formatos: **MP3, WAV, OGG Vorbis, FLAC y M4A/AAC** (el M4A usa BASS, que viene con BassMod; si no lo tienes instalado, los M4A se saltan sin romper nada).
- **Siguiente/anterior canción:** con la rueda abierta en la 12, las flechas a los lados del nombre de la canción cambian de pista (la rueda sigue abierta). También puedes tocar la etiqueta del botón flotante (el nombre de la canción) para pasar a la siguiente sin abrir la rueda.
- **Nombre de la canción:** aparece en el centro de la rueda (debajo del género) y en la ventana del botón flotante (en lugar del género) cuando suena la 12. Si es largo, se desplaza solo (marquesina) en los dos sitios; las flechas están en posición fija para que no se junten al texto.
- **Sin retraso:** la rueda y el botón ya no se quedan en la emisora anterior justo después de cambiar (la lectura del juego va una estación por detrás; ahora se ignora 2 s tras el cambio).
- Panel de personalización: colores, tamaño, opacidad, cámara lenta al abrir la rueda, iconos a color, y los interruptores de la 2.59: mostrar género, ocultar ventana de emisora, fondo de color de los iconos, animación al abrir, blur de fondo (con barra, ahora más fuerte: 8x/16x/32x), bloquear el juego con la rueda abierta, y modoCambio (forma de llamar a StartRadio: 0 normal, 1 aplazado 3 cuadros, 2 por CAudioEngine).
- Los iconos van INCRUSTADOS en el .so (no hace falta ninguna carpeta): los 28 PNG (14 normales + 14 a color) están dentro del .so en base64 y se cargan de memoria al arrancar. La carpeta `VHudRadio/radio/` (y `VHudRadio/radio_color/` para los de color) es opcional: solo sirve para reemplazar un icono por uno propio. Si el PNG está en la carpeta, manda el de la carpeta; si no, se usa el incrustado. El log dice de dónde salió cada icono ("archivo" o "en el .so").
- En vehículos sin radio (bicis y otros de la lista de bloqueo) la rueda no se abre.

## Requisitos

- GTA San Andreas 2.10 de 64 bits (Android).
- AML 1.4.1 o superior.
- La música de User Tracks va en las carpetas de siempre (las mismas que usaba SAUserTracks).

## Instalación

1. Copia `libVHudRadio64.so` a la carpeta de mods de AML.
2. No necesitas ningún otro archivo ni el mod SAUserTracks: el reproductor ya viene dentro. Si lo tenías instalado, quita su `libSAUserTrack64.so`: con los dos a la vez la música sonaría duplicada.
3. Los ajustes se guardan en `data/configs/VHudRadio.ini` y el registro en `VHudRadioLog.txt` (en la carpeta data del juego).

## Créditos

- Rueda e iconos basados en V-HUD de Armando Gennari (MIT). Licencia original incluida en los paquetes de distribución.
- VHudRadio por Hunter09.

## Compilación

GitHub Actions compila el proyecto con el NDK (`Android.mk` / `Application.mk`) y publica el artefacto `VHudRadio` con el `.so` para arm64-v8a.
