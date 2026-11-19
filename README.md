# VHud Radio Wheel v1.0.0-beta

Rueda de radio estilo GTA V para GTA SA Android 2.10 (AML .so), **con reproductor de User Tracks integrado**.

Esta es la línea v1: la rueda de la 2.47 con el reproductor propio dentro, para no depender del mod SAUserTracks. La línea 2.x (rueda sola, con SAUserTracks externo) quedó congelada en la 2.47.0-beta.

## Qué hace

- Rueda de radio táctil de 13 segmentos (11 emisoras + User Tracks + Radio Off), como en GTA V.
- Botón flotante: 1 toque cambia a la siguiente emisora, 2 toques abren la rueda. La rueda también se abre con doble toque al claxon.
- Reproductor de User Tracks integrado (estación 12): busca los MP3 en las carpetas de música, los decodifica y los reproduce con más buffer de audio que el mod externo, para que la música no se trabe en teléfonos con poca CPU.
- Panel de personalización: colores, tamaño, opacidad, cámara lenta al abrir la rueda, iconos a color.
- Los iconos van INCRUSTADOS en el .so (no hace falta ninguna carpeta): los 28 PNG (14 normales + 14 a color) están dentro del .so en base64 y se cargan de memoria al arrancar. La carpeta `VHudRadio/radio/` (y `VHudRadio/radio_color/` para los de color) es opcional: solo sirve para reemplazar un icono por uno propio. Si el PNG está en la carpeta, manda el de la carpeta; si no, se usa el incrustado. El log dice de dónde salió cada icono ("archivo" o "en el .so").
- En vehículos sin radio (bicis y otros de la lista de bloqueo) la rueda no se abre.

## Requisitos

- GTA San Andreas 2.10 de 64 bits (Android).
- AML 1.4.1 o superior.
- La música de User Tracks va en las carpetas de siempre (las mismas que usaba SAUserTracks).

## Instalación

1. Copia `libVHudRadio64.so` a la carpeta de mods de AML.
2. No necesitas ningún otro archivo ni el mod SAUserTracks: el reproductor ya viene dentro. Si lo tenías instalado, quita su `libSAUserTrack64.so`: con los dos a la vez la música sonaría duplicada.
3. Los ajustes se guardan en `VHudRadio/VHudRadio.ini` y el registro en `VHudRadio/VHudRadioLog.txt`.

## Créditos

- Rueda e iconos basados en V-HUD de Armando Gennari (MIT). Licencia original incluida en los paquetes de distribución.
- VHudRadio por Hunter09.

## Compilación

GitHub Actions compila el proyecto con el NDK (`Android.mk` / `Application.mk`) y publica el artefacto `VHudRadio` con el `.so` para arm64-v8a.
