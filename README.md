# VHud Radio Wheel v1.9.11-beta

Rueda de radio estilo GTA V para GTA SA Android 2.10 (AML .so), **con reproductor de User Tracks integrado**.

Esta es la línea v1: el reproductor propio dentro, para no depender del mod SAUserTracks, **con TODAS las funciones que llevaba la 2.59** (interruptores, blur, bloquear juego, modoCambio). La línea 2.x (rueda sola, con SAUserTracks externo) llegó a la 2.59.0-beta; esta v1 trae sus mismas funciones más el reproductor integrado.

## Qué hace

- Rueda de radio táctil de 13 segmentos (11 emisoras + User Tracks + Radio Off), como en GTA V.
- Botón flotante: 1 toque cambia a la siguiente emisora, 2 toques abren la rueda. La rueda también se abre con doble toque al claxon.
- Reproductor de User Tracks integrado (estación 12): busca la música en la carpeta **GTAVRadioMusic** (se crea sola si no existe) o, si no hay nada ahí, en las carpetas de siempre (music), y la reproduce con más buffer de audio que el mod externo, para que no se trabe en teléfonos con poca CPU. Al salir del auto la música se pausa y al subir se reanuda en la misma emisora y en el mismo punto (sin reiniciar ni cortar; el juego ya no la pone en Radio Off al entrar). También se pausa si el juego se va a la pantalla de inicio. Formatos: **MP3, WAV, OGG Vorbis, FLAC y M4A/AAC** (el M4A usa BASS, que viene con BassMod; si no lo tienes instalado, los M4A se saltan sin romper nada).
- **Siguiente/anterior canción:** con la rueda abierta en la 12, las flechas a los lados del nombre de la canción cambian de pista (la rueda sigue abierta). En la 12, la ventana del botón flotante lleva las flechas DENTRO (a los lados del nombre de la canción) para cambiar de canción sin abrir la rueda y sin tocar el cambio de emisora. El tamaño del botón (icono + ventana) se ajusta en el panel (Botón flotante > Tamaño del botón) y su posición se puede bloquear ahí mismo.
- **Nombre de la canción:** aparece en el centro de la rueda (debajo del género) y en la ventana del botón flotante (en lugar del género) cuando suena la 12. Si es largo, se desplaza solo (marquesina) en los dos sitios; las flechas están en posición fija para que no se junten al texto.
- **Lista de canciones:** en la rueda, con la 12, el botón "Lista de canciones" abre la lista de todas las pistas (como en GTA V: cada canción con su división, título + artista debajo, fila actual resaltada y pastilla amarilla PLAYING); tocar una la pone a sonar. El botón y el menú se traducen según el idioma del panel.
- **Programa de cada emisora:** en el centro de la rueda, debajo del género, sale el programa o locutor que se está transmitiendo (por ejemplo, The Dusty Show en K-DST, WCTR News en WCTR).
- **Sin retraso:** la rueda y el botón ya no se quedan en la emisora anterior justo después de cambiar (la lectura del juego va una estación por detrás; ahora se ignora 2 s tras el cambio).
- Panel de personalización: colores, tamaño, opacidad, cámara lenta al abrir la rueda, iconos a color, y los interruptores de la 2.59: mostrar género, ocultar ventana de emisora, fondo de color de los iconos, animación al abrir, blur de fondo gaussiano (con barra), bloquear el juego con la rueda abierta, y modoCambio (forma de llamar a StartRadio: 0 normal, 1 aplazado 3 cuadros, 2 por CAudioEngine).
- Los iconos van INCRUSTADOS en el .so (no hace falta ninguna carpeta): los 28 PNG (14 normales + 14 a color) están dentro del .so en base64 y se cargan de memoria al arrancar. La carpeta `VHudRadio/radio/` (y `VHudRadio/radio_color/` para los de color) es opcional: solo sirve para reemplazar un icono por uno propio. Si el PNG está en la carpeta, manda el de la carpeta; si no, se usa el incrustado. El log dice de dónde salió cada icono ("archivo" o "en el .so").
- **Blur gaussiano de verdad:** el desenfoque del fondo ahora es un blur gaussiano (shader separable, como el de Void Radio Wheel Expanded): al 100% ya no se ven los cuadros/pixeles, sale suave. Antes era solo bajar la imagen, que al 100% se pixeleaba.
- **Sin estática al cambiar con la radio original:** el juego cambia de emisora con RetuneRadio (no con StartRadio) cuando se usa su radio original; ahora ese cambio también se engancha (como SAUserTracks de Alex: StopRadio para cortar la anterior y luego StartRadio con 12), así la estática del cambio ya no se queda sonando junto a la música.
- **Mixtape en el menú de pausa (v1.9.7–v1.9.11):** la emisora nativa se queda en la 12. Desde la v1.9.11, el constructor de RadioSelection asigna la textura radio_TPLAYER (con refcount+1) y luego aplica los 2 parches del menú de 12 huecos, como Alex (SAUserTracks 1.8.4) — sin la textura el juego se cerraba al entrar a opciones de sonido (1.9.9).
- **HUD oculto con la rueda (v1.9.3–v1.9.4):** el radar, la vida y el dinero se ocultan mientras la rueda, el panel o la lista están abiertos (CHud::Draw + CWidgetPlayerInfo::Draw + CWidgetRadar::Update, como hace AdvancedSettings).
- **Botones táctiles ocultos con la rueda (v1.9.5):** CTouchInterface::DrawAll dibuja todos los botones del juego; se ocultan mientras la rueda, el panel o la lista están abiertos. El claxon también se oculta en ese momento, pero el doble-toque para abrir/cerrar la rueda sigue funcionando por la posición, y el botón flotante queda visible.

- **Lista de canciones deslizable (v1.9.6):** la lista se desplaza con el dedo (arrastre vertical con inercia, como el panel de ajustes) cuando hay más canciones de las que caben. Un toque salta a la canción; un arrastre solo desplaza. **El primer toque tras abrir la lista no cambia la canción (v1.9.7):** el toque que abre la lista ya no se cuela como un clic en una fila. **Las filas ya no traspasan el encabezado (v1.9.8):** se dibujan con la lista recortada, así que al desplazar no se ven por encima del título.
- **Doble audio (v1.9.3–v1.9.11):** al cambiar a User Tracks desde otra emisora, la anterior se corta con StopRadio antes de poner la nativa en 12 (Mixtape) — rueda, radio del juego por RetuneRadio del TrackManager, por RetuneRadio del AudioEngine y por StartRadio del AudioEngine. La estática de resintonía (0x22) se bloquea en DOS sitios, como Alex: CAudioEngine::ReportFrontendAudioEvent y CAEFrontendAudioEntity::AddAudioEvent. Desde la v1.9.11, la música propia del juego en la 12 se silencia en CAEAudioHardware::StartTrackPlayback (con User Tracks sonando y las puertas del juego despejadas, la pista nativa nunca empieza), también como Alex. StopRadio nunca se llama después de poner la nativa en 12 (la 1.9.8 lo hacía y reseteaba la emisora a 13).
- **Sin estática (bloqueo 0x22):** la estática de sintonía del juego (evento 0x22) se bloquea mientras suena User Tracks, como hace SAUserTracks de Alex. Es lo que se oía como "doble audio" junto a la música, sobre todo al cambiar con la radio original.
- **Botones del juego ocultos con la rueda abierta:** mientras la rueda, el panel o la lista de canciones están abiertos, los botones del HUD del juego no se dibujan (como el Void Radio Wheel); al cerrar la rueda vuelven. Si el juego no deja enganchar esa función, simplemente no se ocultan (sin riesgo).

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
