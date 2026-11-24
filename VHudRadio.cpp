// VHudRadio.cpp - Rueda de radio estilo GTA V para GTA SA Android 2.10 (AML .so), v1 1.9.23-beta.
// v1 1.2.0-beta: reproductor de User Tracks propio (minimp3 + OpenSL) con
// TODAS las funciones de la 2.59, mas: siguiente/anterior cancion, nombre de
// la cancion en la rueda y en el boton, mas formatos (wav/ogg/flac), y el
// retraso entre radio/rueda/boton corregido. Base: VHudRadio.cpp 2.59.0-beta.
// Repo aparte: Gomezsl/VHudRadio-aml-mod. ARCX no se toca.
// Idea: v-hud de PC leia los iconos de VHud\radio\radio_1..13.png sin importarlos.
// Aqui igual: el plugin lee radio_1.png ... radio_13.png de files del juego (vhud/radio/),
// (+ radio_current.png) al arrancar, con stb_image, y los dibuja con ImGui.
// La rueda es NUEVA (en PC la radio no era rueda, solo un icono 2 s al cambiar):
// 13 segmentos en circulo, se abre con un boton flotante dentro de un vehiculo,
// se arrastra el dedo y al soltar cambia la estacion.
//
// Simbolos de radio: VERIFICADOS por VHudRadioProbe 1.0.0 en el telefono de Daniel
// (VHudRadioProbe.txt, 2026-11-16, 18/23 ok): TODOS los candidatos moviles resolvieron,
// las variantes de PC dieron FALTA (como se esperaba).
// v2.29: ICONOS INCRUSTADOS EN EL .SO (YA NO HACE FALTA LA CARPETA)
// - Los 28 PNG (14 normales + 14 a color) van dentro del .so en base64
//   (icons_embedded.h, generado por script). Al arrancar se cargan de
//   memoria con stb_image (v2.55: solo incrustados, sin carpeta). El log dice
//   de donde salio cada icono ("en el .so"). El .so crece ~0.5 MB.
// v2.28: DOS JUEGOS DE ICONOS (NORMALES Y A COLOR) CON INTERRUPTOR
// - Las emisoras tienen dos versiones: las normales (blancas, de radio/) y
//   las de color (de radio_color/). Un interruptor en el panel ("Iconos de las
//   emisoras a color") cambia entre un juego y otro sin reemplazar archivos.
//   El ajuste se guarda como iconosColor=0/1 en el .ini. Si a un icono a color
//   le falta, se usa el normal de esa emisora. El boton flotante tambien cambia.
// v2.27: YA NO SE CREAN LAS CARPETAS vhud VIEJAS
// - Al arrancar, el plugin creaba tambien las carpetas viejas vhud/
//   (/storage/emulated/0/vhud/ y files/vhud/), un resto de cuando ahi
//   vivian los iconos y horn_pos.txt. Ya no se guarda nada ahi, asi que
//   solo se crea VHudRadio/ (y VHudRadio/radio/). Las carpetas viejas se
//   siguen LEYENDO como respaldo si existen (iconos y horn_pos.txt), pero
//   ya no se crean vacias.
// v2.26: LOG JUNTO AL .INI + CREDITO EN LOS 6 IDIOMAS
// - El log (VHudRadioLog.txt) se creaba en files/ (junto al juego) y el
//   .ini vive en files/VHudRadio/. Ahora el log tambien se crea en
//   files/VHudRadio/, junto al .ini. La carpeta se crea antes de abrir el
//   log (si no, fopen fallaba porque la carpeta aun no existia).
// - El credito "rueda de radio GTAV de Hunter09" era un texto fijo en
//   espanol; ahora es una cuerda mas de la tabla (STR_CREDIT, 18 en total)
//   y cambia con el idioma elegido, como el resto del panel.
// v2.25: LAS BARRAS NO SE REGRESAN AL TOCAR OTRA PARTE DEL PANEL
// - PanelSlider guardaba el valor al pulsar (savedPtr/savedVal) para
//   restaurarlo si el arrastre acababa siendo un desplazamiento del panel.
//   Pero ese valor guardado NUNCA se olvidaba: quedaba apuntando a la
//   ultima barra ajustada, asi que al desplazar el panel despues (tocando
//   otra parte), la barra se restauraba a su valor anterior al ajuste y
//   volvia a su mismo lugar. Ahora al soltar el dedo se olvida el valor
//   guardado (savedPtr = nullptr), asi que solo se restaura durante el
//   mismo arrastre que empezo en esa barra.
// v2.50: EL DOBLE TOQUE AL BOTON MUEVE LAS BARRAS + PANEL MEJOR ORDENADO
// - La 2.49 no basto: con solo el boton visible no se dibuja ImGui y los
//   toques del doble toque se acumulan en la cola de entrada; al abrir el
//   panel se procesan todos en su primer cuadro (MouseClicked en true) y
//   activan barras/botones. Ahora los toques solo llegan a ImGui con la
//   rueda, la vista previa o el panel abiertos; con solo el boton, no se
//   alimentan (la logica del boton/rueda/claxon es del juego y sigue igual).
// - Panel mejor ordenado (como ARCX): mas espacio entre controles, controles
//   mas altos al tacto y dos secciones con titulo (Rueda de radio / Boton
//   flotante).
// v2.51: OPTIMIZACIONES LIGERAS (del chat de Claude, verificadas)
// - Logs de diagnostico apagados: cada Log() hace fopen/fclose sobre
//   Android/data (ruta lenta) y en cada entrada a un vehiculo se escribian
//   el volcado hex + el escaneo ptrCand (mincore por candidato) + rt/rid.
//   Ya cumplieron su funcion: la entidad de audio (0x458/0x1B0) y el modelo
//   en 0x32 se conocen, y el bloqueo usa la lista de modelos, no el escaneo.
//   Se quitan: ptrCand, volcado hex, rt/rid, MenuSnapCheck, la escala leida
//   cada segundo y el log por toque. Los logs importantes quedan (version,
//   simbolos, iconos, cambios de estacion, panel, errores).
// - eglQuerySurface cacheado: antes 2 llamadas por cuadro; ahora una vez y
//   cada 120 cuadros (por si gira la pantalla).
// - glGetUniformLocation cacheado en GLBtnInit: antes 2 por cuadro.
// - imgui_demo.cpp quitado del Android.mk (no se usa).
// - NO se toco la camara lenta (el desarmado probo que SAUserTracks nunca lee
//   la escala de tiempo) ni el guardado de estado GL del boton (la 2.33 puso
//   la pantalla en negro por no guardarlo).
// v2.52: PARCHE DE SONIDO (de Claude, verificado contra el codigo)
// - El reproductor OpenSL de los clics quedaba en PLAYING para siempre con
//   la cola vacia (SoundInit lo ponia en PLAYING y nunca se paraba). Eso
//   deja una pista activa en el mezclador de Android mientras SAUserTracks
//   reproduce la suya, y el motor se creaba en el hilo de render justo al
//   cambiar a User Track (el primer clic). Hipotesis sin medir, pero el
//   codigo del parche es correcto y no quita funciones.
// - Ahora el reproductor esta en PAUSED y solo pasa a PLAYING mientras suena
//   el clic; todas las llamadas OpenSL van a un hilo aparte (SoundWorker).
//   El clic suena igual. Si OpenSL falla, no pasa nada (g_soundOk = false).
// - Ojo: la prueba limpia (5 mods, 2.47) tenia este reproductor y la musica
//   sonaba bien, asi que por si solo no es la causa; solo pesaria con el
//   telefono ya saturado (carga acumulada de los 44 mods). El telefono dira.
// v2.53: FUNCIONES PEDIDAS EN DISCORD (dotexe + Rehan, bien hechas)
// - Ocultar ventana de emisora: interruptor + hook a DisplayRadioStationName.
// - Ocultar genero: interruptor; quita el genero del centro, la etiqueta y
//   el boton GL; el nombre queda centrado.
// - Fondo de color del icono: interruptor + color (muestra nueva, objetivo 3
//   del selector RGB); circulo de color detras de cada icono de la rueda.
// - Animacion al abrir: la rueda crece 0.85->1.0 y se funde en 150 ms.
// - (v2.55: los iconos personalizados de carpeta se quitaron por peticion
//   de Daniel; solo van los incrustados en el .so.)
// - NO se hace el blur (muy caro en el A14) ni sonidos/textos propios ni
//   ocultar botones del juego (arriesgado sin probar).
// v2.54: CORRECCIONES a la 2.53 (prueba de Daniel, log VHudRadioLog_2_rlyz.txt)
// - Recuadro del boton: se oculta ENTERO (fondo+estacion+genero) si el genero
//   esta apagado o si esta activa "Ocultar ventana de emisora" (ImGui y GL).
// - Fondo de icono: los PNG traen negro opaco de fondo; se vuelve transparente
//   el negro casi puro al cargar (LoadPngFromMemory), asi el color se ve.
// - Animacion: el inicio se guarda al abrir (global), no en DrawWheel (que no
//   se llama con solo el boton); 250 ms y desde 0.75 para que siempre se vea.
// - Iconos propios: LoadRadioTextures se llama cada cuadro (antes solo una
//   vez al inicio), asi al apagar/encender vuelven a cargar.
// v2.55: QUITAR iconos personalizados (carpeta) por peticion de Daniel (evitar
// iconos mal posicionados/errores): solo van los incrustados en el .so. El
// fondo de color solo quita el negro cuando esta ACTIVADO (apagado = negro
// normal de siempre); el boton recupera su disco negro detras del icono; los
// interruptores se acomodan en su seccion (rueda / boton).
// v2.56: "Ocultar ventana de emisora" pasa a la seccion Boton flotante (la
// ventana es la etiqueta del boton). Iconos en CIRCULO (AddImageRounded y
// disco en GL) para que no se note el cuadrado. Blur de fondo con barra
// (experimental): copia del framebuffer dibujada 5 veces con desplazamiento,
// antes del fondo oscuro; 0 = apagado (por defecto).
// v2.57: blur DE VERDAD por reduccion en FBO (la 2.56 no se notaba: solo
// 12 px de desplazamiento). Desactivar camara y botones del juego con la
// rueda abierta (interruptor bloquearJuego, por defecto activado).
// v2.58 PRUEBA (sin clics): PlayClick devuelve de inmediato, sin crear la
// salida OpenSL del mod. Prueba de Claude: si con esta build nunca falla al
// cambiar a User Tracks por la rueda (15-20 veces, reiniciando el juego),
// el culpable es la salida de audio del mod; si sigue fallando, el audio
// queda descartado y se prueba la forma de llamar a StartRadio.
// v2.58 FALLO al 2o intento -> audio DESCARTADO. v2.59: 3 modos de cambio
// (INI modoCambio: 0 = actual por CAERadioTrackManager::StartRadio;
// 1 = esperar 3 cuadros con la rueda cerrada antes de llamar a StartRadio;
// 2 = por CAudioEngine::StartRadio). Clics restaurados (el audio no era).
// - Interruptores: solo se activan tocando la pastilla, no toda la fila azul.
// v2.49: EL PANEL DEJA TOCAR LOS BOTONES DEL JUEGO DE DETRAS + BARRAS QUE SE MUEVEN
// - Touch_hook solo observaba: todos los toques llegaban al juego Y a ImGui.
//   Ahora cada toque pertenece al panel (si cae dentro al bajar el dedo) o al
//   juego (si cae fuera): los del panel no llegan al juego (no se pulsa
//   bajarse ni se mueve la camara detras del panel) y los movimientos del juego
//   no llegan a ImGui (no se mueven las barras ni los botones solos). Ademas el
//   primer cuadro del panel arranca con el raton limpio.
// v2.48: BARRAS QUE SE MUEVEN SOLAS AL ABRIR EL PANEL + TEXTURAS AL CARGAR
// - Al reabrir el panel se descarta el estado de arrastre/desplazamiento de
//   la sesion anterior (savedPtr, g_panelScrolling, inercia del scroll): si
//   no, una barra se restauraba a su valor viejo al reabrir y el panel podia
//   desplazarse solo. No se inicia ImGui ni se cargan los iconos mientras
//   CLoadingScreen::m_bActive esta en true (cargando partida).
// v2.24: DESPLAZAR CON EL SELECTOR ABIERTO + BARRA DE TONO QUE NO SE REGRESA
// - La v2.20-v2.23 no permitia desplazar el panel con el selector de color
//   abierto (para no robarle sus arrastres verticales), asi que con el
//   selector abierto no se llegaba a los ajustes de abajo. Ahora se puede
//   desplazar salvo si el arrastre empieza SOBRE el cuadro de saturacion/
//   valor o la barra de tono (esos arrastres son del color, como antes).
// - La barra de tono se regresaba sola: con un color gris/blanco (S=0, como
//   el texto blanco por defecto) el tono no se ve, y al soltar, la
//   sincronizacion RGB->HSV borraba el tono elegido (un gris no tiene tono y
//   volvia a 0/rojo). Ahora la sincronizacion no borra el tono si el color
//   es gris, y al mover la barra de tono con S casi 0 se pone saturado para
//   que el color cambie de verdad.
// - Credito al final del panel: "rueda de radio GTAV de Hunter09" (abajo
//   del todo, centrado y en gris, como el "By Hunter09" de ARCX).
// v2.23: CABECERA CON FONDO OPACO + X ACOMODADA + INTERRUPTOR + REDISENO
// - La v2.22 dibujaba la cabecera fija en primer plano pero SIN fondo: al
//   bajar, el contenido se veia por detras del titulo (se veian las funciones
//   en la parte que no se desplaza). Ahora la cabecera lleva un fondo opaco
//   (el mismo azul del panel, con las esquinas de arriba redondeadas) que
//   tapa el contenido que pasa por debajo.
// - La X era muy grande y colgaba por debajo de la linea del titulo; ahora
//   es mas chica y esta centrada en la linea del titulo.
// - "Bloquear posicion del boton" ya no es un boton con ON/OFF: es un
//   INTERRUPTOR (pastilla azul con circulo, como los de ARCX).
// - Se quita el boton "Cerrar" de abajo (ya esta la X arriba).
// - Rediseno como la imagen de referencia: iconos a la izquierda de cada
//   ajuste (sol, expandir, globo, opacidad), el valor en una pastilla a la
//   derecha, y los botones en rejilla de 2 columnas con icono.
// v2.22: CABECERA FIJA ESTILO ARCX (X PARA CERRAR) + BOTONES REDISENADOS
// - La cabecera ("Personalizar rueda de radio" + subtitulo) ya no se desplaza
//   con el contenido: se dibuja en primer plano (como la cabecera del menu de
//   ARCX) y el contenido se desliza por debajo. Arrastrar la cabecera sigue
//   moviendo el panel.
// - X para cerrar arriba a la derecha (circulo oscuro con anillo azul y X
//   blanca, como ARCX): ya no hay que bajar hasta "Cerrar" para cerrar el
//   panel (el boton "Cerrar" sigue abajo por si acaso).
// - Botones redisenados (Bloquear posicion, Restablecer posicion, Vista
//   previa, Restablecer todo, Cerrar): esquinas redondeadas, brillo superior
//   y borde claro, como las filas de ARCX; ya no son simples rectangulos
//   planos de ImGui.
// v2.21: DESPLAZAMIENTO ESTILO ARCX (SOLO DENTRO DEL PANEL + INERCIA)
// - La v2.20 no comprobaba donde empezaba el arrastre y un arrastre vertical
//   en el juego (fuera del panel) tambien desplazaba el panel. Ahora, como
//   el menu de ARCX (ListView), el desplazamiento solo responde a toques
//   DENTRO del panel (inView al pulsar) y sigue al dedo desde donde se pulso
//   (scroll = scroll al pulsar - dy), con inercia al soltar (la velocidad se
//   multiplica x0.93 por frame, como ARCX). La direccion sigue como en la
//   v2.20: vertical = desplaza, horizontal = deslizador (que guarda su valor
//   al pulsar y lo restaura si el arrastre acaba siendo desplazamiento).
// - Aviso "Desliza para ver mas" (como ARCX, en el idioma elegido) en la
//   parte inferior del panel cuando el contenido no cabe y queda mas abajo;
//   al empezar a desplazar se quita para no tapar los ajustes.
// v2.20: DESPLAZAMIENTO POR DIRECCION + TAMANO POR DEFECTO MEDIANO OTRA VEZ
// - La v2.19 solo desplazaba si el arrastre empezaba en el fondo, pero casi
//   todo el panel son deslizadores ("las barras") y se movian en vez de
//   bajar; solo se podia por la barra de la esquina, que es tediosa. Ahora
//   el desplazamiento es por DIRECCION: si el arrastre es mas vertical que
//   horizontal, desplaza el panel; si es mas horizontal, ajusta el
//   deslizador. El deslizador guarda su valor al pulsar y, si el arrastre
//   acaba siendo desplazamiento, lo restaura (asi no se mueve al empezar a
//   desplazar). Con el selector de color abierto no hay desplazamiento por
//   direccion (sus arrastres son en vertical y son del color).
// - El tamano por defecto vuelve a ser MEDIANO (0.44 de ancho x alto
//   automatico); la v2.19 lo habia puesto grande (0.60 x 0.90) y a Daniel
//   no le gusto. Un .ini con los valores grandes de la v2.19 (0.60 x 0.90,
//   nunca se redimensiono o se restablecio en la v2.19) vuelve al mediano;
//   un tamano ajustado a mano se respeta.
// v2.19: PANEL DESLIZABLE CON EL DEDO + TAMANO POR DEFECTO GRANDE (NO MEDIANO)
// - El panel ahora se puede DESLIZAR con el dedo: arrastrar sobre el fondo
//   del panel desplaza el contenido, asi que cuando el panel es mas chico
//   que los ajustes se llega a los de abajo. No se activa si el arrastre
//   empezo en un control (deslizador, recuadro, boton, selector de color),
//   en el titulo (que mueve el panel) ni en la esquina (que lo redimensiona).
//   (ImGui ya muestra la barra de desplazamiento cuando el contenido no
//   cabe, pero con el tacto no hay rueda del raton para usarla.)
// - El tamano por defecto ya no es mediano (0.44 de ancho x alto automatico)
//   sino GRANDE (0.60 x 0.90) para que se vean todos los ajustes sin
//   desplazar. Un .ini con los valores viejos exactos (0.44 y alto
//   automatico, o sea nunca se redimensiono o se restablecio) toma los
//   nuevos valores; un tamano ajustado a mano se respeta.
// v2.18: LA RUEDA VUELVE A CAMBIAR DE ESTACION + PANEL REDIMENSIONABLE EN ALTO Y ANCHO
// - La v2.17 rompio el cambio de estacion: la rueda detectaba el segmento
//   ("rueda: segmento 11") pero se cerraba "sin cambiar". Causa: en la v2.17
//   el soltar se encolaba SIN su posicion (solo {2}), y la v2.16 encolaba
//   {pos, soltar} - la posicion del soltar era lo que rearmaba g_wheelTouch
//   justo antes de aplicar el cambio. Ademas el bajar (kind-1) leia el
//   segmento con io.MousePos, que llega con un frame de retraso (NewFrame
//   corre DESPUES del bucle de eventos) y casi siempre es (-FLT_MAX) o la
//   posicion del toque anterior, asi que el bajar borraba g_wheelTouch.
//   Arreglo en dos partes: (1) el soltar vuelve a encolar su posicion
//   ({pos, soltar}, como en la v2.16; el soltar sigue llegando siempre, el
//   arreglo del doble toque de la v2.17 se mantiene); (2) el bucle de eventos
//   lleva su propia posicion (g_touchX/g_touchY, actualizada en cada evento
//   de posicion) y el bajar la usa para el segmento, la zona del claxon, el
//   registro de toques y el boton flotante, en vez de io.MousePos.
// - El panel ahora se redimensiona en ALTO y en ANCHO (la v2.17 solo cambiaba
//   el ancho): el grip actualiza panelAncho y panelAlto (fraccion de H, -1 =
//   alto automatico) y se dibuja en primer plano en la esquina de la ventana
//   con arrastre a mano, para que nunca se pierda al hacer scroll (el
//   contenido puede ser mas alto que el panel). "Restablecer todo" devuelve
//   el alto a automatico.
// v2.17: PANEL MOVIBLE Y REDIMENSIONABLE (COMO ARCX) + DOBLE TOQUE FIABLE + COLOR DE FONDO
// - Panel movible y con ancho ajustable, como el menu de ARCX: se arrastra
//   desde el titulo (InvisibleButton ##panelDrag + IsMouseDragging como en
//   arcx_ui.h) y se cambia el ancho desde la esquina inferior derecha
//   (grip ##panelGrip con 3 rayas en diagonal, como en ARCX). La posicion
//   y el ancho se guardan en el .ini (panelX/panelY/panelAncho, fraccion de
//   W; -1 = centrado, ancho por defecto 0.44, entre 0.36 y 0.70). El alto
//   sigue automatico segun el contenido, como hasta ahora. Al empezar a
//   arrastrar con la posicion en -1 se toma la posicion real de la ventana
//   para que el panel no salte a la esquina.
// - Doble toque al boton mas fiable: el log de la v2.16 mostro que los
//   toques se clasificaban como arrastres (el boton se movia en vez de
//   cambiar de estacion) y a veces el doble toque no abria el panel.
//   Tres arreglos: (1) Touch_hook encola SIEMPRE el soltar (antes solo
//   cuando la rueda/panel estaban abiertos, y un toque rapido podia perder
//   su soltar si DrawFrame aun no habia procesado el bajar); (2) la
//   ventana de doble toque pasa de 400 a 600 ms; (3) el umbral de arrastre
//   pasa de 0.012H a 0.02H (el temblor del dedo ya no cancela el toque).
//   Ademas el log "rueda bloqueada" ya no repite en cada toque (maximo una
//   vez cada 2 segundos).
// - Color de fondo de la rueda: el fondo oscuro ya no es solo negro con
//   oscuridad ajustable; ahora tiene su propio color (fondoColor=r,g,b,
//   por defecto 0,0,0) con un tercer recuadro "Fondo" en el panel que abre
//   el mismo selector RGB tactil (objetivo 2 del picker).
// v2.16: SIN RADIO NO HAY BOTON NI PANEL + BOTON MOVIBLE + IDIOMAS + CARPETA VHudRadio
// - En vehiculos sin radio el boton flotante NO se ve y el panel NO se puede
//   abrir (antes el boton se veia y el panel se abria igual; ahora ambos
//   requieren g_hasRadio). El claxon sigue bloqueado como en la v2.15.
// - Boton flotante movible: se arrastra con el dedo (X e Y) a cualquier
//   lugar de la pantalla; la etiqueta cambia de lado si el boton queda a la
//   izquierda. En el panel: tamano (50-150%), opacidad (30-100%), boton de
//   bloqueo (fijo/libre) y restablecer posicion. La posicion se guarda en
//   el .ini al soltar el arrastre. El toque se resuelve al soltar: si el
//   dedo no se movio es un toque (siguiente estacion / doble toque = panel).
//   Los eventos de mover/soltar llegan al arrastre porque Touch_hook tambien
//   los encola mientras g_btnDrag.
// - Idiomas: espanol, ingles, indonesio, portugues (BR), frances y aleman.
//   Todo el panel y los generos se traducen (los nombres de las estaciones
//   no, son nombres propios). Se elige en el panel (Combo) y se guarda
//   como idioma=es/en/id/pt/fr/de en el .ini.
// - Carpeta: las texturas ya no van en vhud/ sino en VHudRadio/ dentro de
//   files del juego (files/VHudRadio/radio/), y el .ini tambien vive ahi
//   (files/VHudRadio/VHudRadio.ini). Se migran solos los .ini viejos de
//   data/configs y files/configs; los iconos se buscan primero en la
//   carpeta nueva y luego en las viejas.
// - Misiones: no hizo falta cambiar nada, la rueda ya funciona en misiones
//   (solo depende de estar en un vehiculo con radio y no estar en pausa).
//   En cinematicas el juego pausa la entrada y la UI se oculta, como antes.
// v2.15: BLOQUEO POR LISTA DE MODELOS (la funcion del juego CIERRA el juego)
// - La v2.14 se CERRO al entrar a una bici (y a un carro de policia):
//   crashlog SIGILL (ILL_ILLOPC) con PC en libGTASA.so+0x4895A4, justo en
//   la PRIMERA instruccion de GetVehicleAudioSettings (X0=509, la bici).
//   El simbolo resuelve (ok) pero NO es codigo llamable en este telefono
//   (apunta a datos o a un stub que atrapa); llamarlo = cerrar el juego.
//   Con el carro 445 la llamada devolvio "sin lectura" sin cerrarse, con
//   la bici 509 se cerro al entrar a la funcion: no se por que una si y
//   la otra no, pero la conclusion es firme: ESA FUNCION NO SE LLAMA.
// - El bloqueo ahora es por LISTA DE MODELOS (la via manual que Daniel
//   cuestiono, pero es la segura: usa solo el modelo en 0x32, verificado
//   por el log v2.13, sin llamar a ninguna funcion del juego). Bicis
//   481/509/510, emergencia 407/416/427/432/433/490/497/523/528/544,
//   patrullas 596-599 + SWAT 601, especiales/RC/trabajo 441/449/457/464/
//   465/485/501/530/531/532/537/538/539/564/568/571/572/583/594.
//   Fail-open igual que antes: modelo fuera de rango -> la rueda ABRE.
// v2.14: BLOQUEO POR MODELO + RADIOTYPE OFICIAL (el log v2.13 lo probo)
// - El log v2.13 (VHudRadioLog_0_poj4.txt) registro PERFECTO: 2 entradas,
//   carro Admiral modelo 445 y bici modelo 509. El modelo esta en 0x32, el
//   UNICO uint16 en 400..611 en los dos volcados (y el unico uint32); los
//   bytes 0x30-0x31 cambian al azar (RandomSeed) y 0x32-0x33 son el modelo,
//   justo el patron CEntity RandomSeed+ModelIndex. Los vtables difieren
//   (CAutomobile vs CBike). Mi 0x2E estaba mal (+12): el desplazamiento real
//   es +16 sobre PC (0x22 -> 0x32); la otra IA tambien fallo (0x26/0x2A).
//   Por eso la v2.13 fue solo diagnostico: bloquear con 0x2E habria sido
//   otra v2.11. La funcion GetVehicleAudioSettings SI resolvio (ok) y la
//   tabla gVehicleAudioSettings NO (FALTA los dos nombres): se usa la
//   funcion, que es la que da el RadioType oficial por modelo (+0x1B).
// - Bloqueo: g_hasRadio = (RadioType == 0), calculado UNA VEZ por vehiculo
//   (al cambiar veh, no cada frame: asi no parpadea como en la v2.11).
//   Fail-open en todo: sin vehiculo, modelo fuera de rango o sin lectura
//   de tabla -> la rueda ABRE. Panel y vista previa siguen disponibles
//   sin radio (como en la v2.11). El log registra modelo/radioType al entrar.
// v2.13: DIAGNOSTICO DE RADIO POR MODELO (SIN BLOQUEO TODAVIA)
// - Daniel paso el chat con otra IA (Google Modo IA). Su idea final es buena:
//   preguntarle al juego por el RadioType OFICIAL del modelo (tabla interna)
//   en vez de una lista manual. Pero sus offsets estaban mal:
//   * m_nModelIndex NO esta en 0x26 (ni en 0x2A, que dijo primero): por los
//     headers plugin-sdk (CPlaceable: vtable 8 + placement 16 + m_matrix 8 =
//     0x20; CEntity: m_pRwObject 8 en 0x20, flags 4 en 0x28, RandomSeed 2 en
//     0x2C) el modelo esta en 0x2E en ARM64 (PC 0x22 + 3 punteros x 4).
//     Su error: no conto que m_pRwObject tambien es puntero y se ensancha.
//   * m_nRadioType dentro de tVehicleAudioSettings NO esta en +0x22: esta en
//     +0x1B (VALIDATE_OFFSET del header). Su struct leeria otro byte.
//   * GetVehicleAudioSettings NO aparece en el header plugin-sdk; lo que SI
//     existe es la tabla global gVehicleAudioSettings[232] (indice =
//     modelId - 400, entrada 0x24 bytes). Se prueban las dos vias por GetSym
//     y el log dice ok/FALTA.
//   * Esa IA dijo haber leido nuestro codigo, pero en su propio chat los
//     archivos salen como "no disponibles": sus numeros de linea son
//     inventados, no de nuestro archivo. Y sus primeras respuestas
//     (CPad/ShockButtonL, IsTouchDown(7,0,0)) ya las probamos y no sirven en
//     este telefono: el claxon ya funciona por el byte 84 del widget 7.
// - Esta version SOLO registra: los 3 candidatos de modelo (0x26/0x2A/0x2E),
//   y el RadioType de la tabla del juego para el modelo 0x2E. NO bloquea
//   nada: el log de una prueba (carro + bici) dira que offset es el modelo
//   real y que RadioType tiene cada modelo; con eso la v2.14 bloquea.
// v2.12: QUITAR EL BLOQUEO POR rid + BUSCAR EL MODELO DEL VEHICULO
// - La v2.11 salio AL REVES: bloqueo dos carros y dejo pasar la bici. El log
//   lo explica: rid(0x9A) NO es estable, en un mismo carro dio 0, 2, 0, 0 en
//   lecturas seguidas (es un byte que cambia, no la radio asignada). Asi que
//   el bloqueo por rid se quita por completo: la rueda vuelve a abrir en
//   todos los vehiculos como en la v2.10. El audio queda solo como
//   diagnostico (registra rt/rid solo cuando cambian).
// - Para bloquear de forma fiable se usara el MODELO del vehiculo (estable:
//   bici 481/509/510, tractor 531, patrulla 596-599). Esta version vuelca los
//   primeros 64 bytes del vehiculo al entrar, para encontrar m_nModelIndex.
// v2.11: BLOQUEO DE LA RUEDA EN VEHICULOS SIN RADIO
// - La v2.10 confirmo el discriminador: en una bici rid(0x9A)=0 (sin radio)
//   y en un carro rid=2; rt(0x9B)=0 en los dos, asi que el que distingue es
//   rid. Al entrar se asume sin radio hasta que la lectura lo confirme; si
//   en 120 frames no aparece el puntero se asume que si tiene (no dejar la
//   rueda bloqueada por un fallo de lectura). La rueda no se abre (ni por
//   claxon ni por zona) y el boton flotante no cambia de estacion cuando el
//   vehiculo no tiene radio. El panel de personalizacion sigue disponible.
// v2.10: LECTURA PERSISTENTE DE LA ENTIDAD DE AUDIO
// - La v2.9 SI registro todo y NO se cerro: 3 entradas, escaneo completo.
//   El candidato 0x458 es la entidad de audio (estable por vehiculo, zona
//   de ajustes en +0x90..0xAF, byte 0x9B = 0 = radio civil en un carro).
//   Pero en la entrada 1 (otro carro) el puntero 0x458 no aparecia todavia:
//   puede no estar puesto en el primer frame al entrar. Ahora se reintenta
//   cada frame hasta que aparece, y se registra RadioType (0x9B) y radioID
//   (0x9A). Se prueban 0x458 y 0x1B0 (los dos apuntaron al mismo objeto).
//   Aun no bloquea nada: falta una bici para confirmar que rt=-1 o 3.
// v2.9: ESCANEO SEGURO (mincore) - LA v2.8 CERRO EL JUEGO
// - La v2.8 se cerro (SIGSEGV en libVHudRadio64.so) al entrar a un auto de
//   policia: el escaneo leyo un valor (0x23F800000) que "parecia" un puntero
//   (cae en el rango 2^32..2^48) pero no esta mapeado, y al leerlo el juego
//   se cerro. Ahora cada puntero se valida con mincore (IsMapped) ANTES de
//   leerlo, y tambien la ventana que se vuelca. Rango mas amplio (0x100..
//   0x500) y hasta 20 candidatos. Aun no bloquea nada.
// - CEntity::GetModelIndex NO existe como simbolo en 2.10 (FALTA en las dos
//   formas): se quito; el modelo se sabe por el orden (carro primero, bici
//   despues) hasta encontrar la entidad de audio.
// v2.8: BUSCAR LA ENTIDAD DE AUDIO (ESCANEO DE CVehicle) + MODELO DEL VEHICULO
// - La v2.7 no encontro el puntero de audio en 0x138/0x140/0x148 (todo ceros
//   en 0x120..0x15F, y un flotante pequeno en 0x120): el offset del telefono
//   es otro. Ahora se escanea CVehicle (0x100..0x380) buscando punteros a
//   objetos reales (puntero creible + vtable creible) y se vuelca la zona de
//   ajustes de cada uno (0x70..0xAF); el que tenga el byte 0 en un carro y -1
//   en una bici es la entidad de audio (RadioType). Aun no bloquea nada.
// - La ralentizacion de la v2.7 SI se mantiene: el log mostro
//   "escala leida=0.30 activa=1", o sea que el juego no borra los valores.
// - Nuevo: CEntity::GetModelIndex para registrar el modelo del vehiculo en
//   el log (481 BMX, 509 bici, 510 bici de montana, 531 tractor, 596-599
//   patrullas...) y saber si es carro o bici sin preguntar.
// v2.7: RALENTIZACION DE VERDAD (CADA FRAME) + RADIOTYPE POR PUNTERO
// - La v2.6 decia "ralentizacion: activada" en el log pero NO se veia nada:
//   solo escribia los valores al abrir/cerrar y el juego los borraba en el
//   siguiente frame (como el opcode 015D de CLEO, la velocidad se pone en un
//   bucle, no una vez). Ahora se escriben EN CADA FRAME mientras la rueda
//   esta abierta (bandera + escala del slow-motion + escala de tiempo), y al
//   cerrar se restauran los valores que habia antes. El log lee la escala
//   cada segundo para ver si el juego la esta borrando.
// - RadioType: la v2.6 leia veh+0x1D3 directo y salio todo ceros (el radioID
//   debia ser 5): el audio NO esta dentro de CVehicle, es un PUNTERO en
//   CVehicle (0x138 en PC) a CAEVehicleAudioEntity. Ahora se lee ese puntero
//   (probando 0x138/0x140/0x148) y se vuelca la zona de ajustes de la entidad.
// - La pausa YA funciona (el log de la v2.6 la detecto por m_UserPause=1).
// v2.6: PANEL PROPIO AZUL + 2 COLORES + RALENTIZACION + PAUSA DE VERDAD + RADIOTYPE
// - La pausa SEGUIA sin esconder la UI en la v2.5: ahora se detecta por varias
//   senales a la vez (byte 0x5C de CMenuManager, CTimer::m_UserPause /
//   m_CodePause, y si CMenuManager::DrawFrontEnd se llamo en este frame), y el
//   log registra que senal disparo y que bytes del objeto CMenuManager cambian
//   (para encontrar el byte real de pausa en este telefono).
// - El panel ya no es la ventana de ImGui por defecto: es un panel propio azul
//   casi oscuro, sin barra de titulo, con deslizadores, recuadros y botones
//   dibujados a mano (como ARCX, no como ImGui).
// - El color del anillo ya no tiene el panel RGB siempre abierto: se toca un
//   recuadro pequeno para abrirlo, y hay un segundo recuadro para el color del
//   texto del centro de la rueda (nuevo ajuste texto=r,g,b en el .ini).
// - Al abrir la rueda el juego va lento (ralentizacion estilo GTA V, con el
//   slow-motion propio del juego: CTimer::bSlowMotionActive /
//   ms_fSlowMotionScale, o ms_fTimeScale si no resuelven); al cerrarla vuelve
//   a velocidad normal.
// - RadioType (del texto de la imagen): se lee el byte candidato
//   (CVehicle 0x138 + audio 0x80 + radio 0x1B = 0x1D3) y se registra en el
//   log al entrar a un vehiculo, con una ventana de bytes y la estacion
//   actual, para confirmar el offset del telefono antes de bloquear la rueda
//   en bicis/tractores/patrullas (en la v2.6 aun no bloquea nada).
// v2.5: PANEL ESTILO ARCX + PANEL RGB TACTIL
// - El panel ahora usa el diseno de ARCX (3.1.35-3.1.40): violeta ARCX
//   (0.45, 0.30, 0.95), fondo oscuro tintado, titulo centrado y borde en
//   degradado azul -> morado alrededor de toda la ventana.
// - Los 8 colores predeterminados y el ColorEdit3 se quitaron: ahora hay un
//   panel RGB tactil como el selector de ARCX 3.1.38 - cuadro grande de
//   saturacion/valor + barra vertical de tono, se arrastra con el dedo, con
//   muestra del color y valores R G B + hexadecimal. El color se guarda al
//   soltar (y al cerrar), como los deslizadores.
// - El anillo sigue siendo el color que elija el usuario (por defecto cian);
//   el violeta es solo el diseno del panel.
// v2.4: PANEL FUNCIONAL + PAUSA DE VERDAD
// - El panel NO funcionaba: Touch_hook solo pasaba los eventos de soltar/mover
//   a ImGui cuando la rueda estaba abierta (g_open || g_cap, y g_cap nunca se
//   pone en true). Con el panel abierto y la rueda cerrada, ImGui recibia el
//   toque de bajar pero nunca el de soltar: los botones no se disparaban, los
//   deslizadores no se movian y "Cerrar" no funcionaba. Ahora tambien se pasan
//   cuando el panel o la vista previa estan abiertos.
// - La pausa no escondia la UI: FrontEndMenuManager es una REFERENCIA (puntero
//   al objeto), no el objeto; hay que desreferenciarla. Ademas, si el claxon
//   se pulsa mientras el byte de menu dice 1, el offset 0x5C no es el menu en
//   este telefono y la deteccion se desactiva sola (con mensaje en el log).
// v2.3: APAGAR ARRIBA DEL ANILLO + PANEL USABLE + SIN UI EN PAUSA
// - Apagar NO va en el centro: vuelve al anillo, arriba (donde Daniel lo marco
//   con el circulo en la captura de la v2.1); el centro vuelve a ser el nombre
//   y el genero de la estacion. El anillo vuelve a tener 13 segmentos.
// - Panel redisenado para usarse al tacto: letra grande (32 px), etiquetas
//   ENCIMA de los deslizadores, colores rapidos (8 botones grandes) + color
//   propio, botones a todo lo ancho y ventana mas estrecha.
// - La rueda, el boton y el panel ya no se ven en el menu de pausa del juego
//   (se lee CMenuManager::m_bMenuActive, en 0x5C segun plugin-sdk).
// v2.2: APAGAR AL CENTRO + BOTON CON ETIQUETA + SONIDO + PANEL DE PERSONALIZACION
// - La estacion 13 (Radio Off) ya no va en el anillo: va AL CENTRO de la rueda
//   (tocarla apaga la radio); el anillo queda con 12 estaciones.
// - Boton flotante mas abajo (0.86H) y con etiqueta: nombre de la estacion y su
//   genero a la izquierda del icono. Un toque = siguiente estacion; dos toques
//   = panel de personalizacion (la rueda ya solo se abre con el claxon).
// - Sonido: clic corto al abrir la rueda y al seleccionar una estacion, generado
//   con OpenSL ES (sin depender del juego).
// - Panel de personalizacion: oscuridad del fondo, tamano de la rueda, color del
//   anillo de seleccion, vista previa (solo se ve, no cambia la estacion) y
//   restablecer todo. Se guarda en data/configs/VHudRadio.ini.
// v2.1: LOGOS CON AIRE + MEJOR LETRA + BOTON CON OTRA FUNCION + ICONOS EN FILES
// - Los logos se veian muy juntos: anillo mas grande (0.83R) e iconos mas chicos
//   (0.175R) para que no se toquen.
// - Letra del centro: Roboto Condensed negrita de /system/fonts (la mas parecida
//   a la de GTA V que trae el telefono); si no esta, Roboto Bold o la de ImGui.
// - Boton flotante: un toque = siguiente estacion; dos toques = abrir la rueda.
// - Iconos: ahora se buscan primero en files del juego
//   (Android/data/com.rockstargames.gtasa/files/VHudRadio/radio/) y si no estan ahi,
//   en files/vhud/radio/ o en /storage/emulated/0/vhud/radio/ como antes.
// v2.0: DISENO GTA V + SIN CALIBRAR - el claxon ya se detecta por el boton 7
// (confirmado en el log de la v1.9: 11 aperturas por "boton 7"), asi que el boton
// "Calibrar claxon" bajo la rueda se quito (la zona tactil queda solo de
// respaldo). La rueda ahora es estilo GTA V: logos grandes flotando en circulo
// sin disco, la seleccionada con anillo cian y las demas atenuadas, y en el
// centro el nombre de la estacion + su genero.
// v1.9.37: DIAGNOSTICO DE CLAVES DE TEXTO (DANIEL, NO CAMBIA NADA DEL JUEGO)
// - Las escrituras de memoria (MS+0x240, +0x35, hook de Render) no consiguen
//   que la fila EMISORA dibuje la emisora de la rueda. Como el hook de
//   CKeyGen::GetUppercaseKey ya demuestra que se controla que nombre dibuja
//   el menu (FEA_NON -> FEA_MP3), esta version solo REGISTRA las claves de
//   texto distintas que el juego pide con el menu de pausa abierto, para
//   arreglar el nombre por texto en la siguiente version.
// - Tambien registra (sin cambiar nada) si el hook del constructor de
//   AudioScreen llega a correr y que filas dibuja SettingSelection::Render
//   (idx y valor de MS+0x240 antes de escribirlo). El log de la 1.9.36
//   mostro todas las variables (mm+0x35, MS+0x240/0x244, TrackManager)
//   iguales a la rueda al abrir y cerrar el dial, y aun asi EMISORA muestra
//   el nombre viejo: ese nombre no se lee en vivo de esas variables.
// v1.9.24: DIAGNOSTICO DE EMISORA (DANIEL, NO CAMBIA NADA DEL JUEGO)
// - El log de la 1.9.23 mostro que AudioEngine+0x149 NO es la variable del
//   menu: tras entrar y salir de opciones de sonido sin tocar nada, la
//   emisora real paso de CSR (8) a Radio X (7) (el juego reaplica una guardada)
//   y +0x149 seguia en 8, asi que no habia nada que resincronizar ahi. Esta
//   version solo VUELCA memoria (dial, TrackManager, AudioEngine,
//   FrontEndMenuManager) a VHudRadioDump.txt al abrir/cerrar el dial y cuando
//   la emisora cambia dentro o justo despues del menu, para localizar esa
//   variable. NO se toco audio, blur, HUD ni la sincronizacion existente.
// v1.9.23: EMISORA SINCRONIZADA TAMBIEN CON LA RUEDA (DANIEL)
// - Tu log de la 1.9.22 me mostro el problema: la escritura SI paso
//   ("EMISORA sincronizada 2 -> 5" y "5 -> 4", y el valor quedo guardado),
//   pero solo se escribia AL ABRIR EL DIAL. EMISORA se lee al abrir
//   opciones de sonido ANTES de que el ctor del dial corra, asi que la
//   primera vista seguia mostrando el valor viejo. Ahora la emisora
//   (AudioEngine+0x149) se sincroniza TAMBIEN cada vez que cambias con
//   la rueda (DoStartRadio), asi siempre esta al dia y EMISORA muestra
//   la que suena desde que abres las opciones. NO se toco audio, blur
//   ni HUD.
// v1.9.22: EMISORA SINCRONIZADA (DANIEL, SIN TOCAR LO DEMAS)
// - Tu log de la 1.9.21 SI registro todo, y ya encontre el valor. Con 3
//   aperturas del dial (CSR 8, Radio X 7, CSR 8) compare los 4 objetos:
//   AudioEngine+0x149 es el UNICO byte constante = 3 (K-DST, la que
//   mostraba EMISORA) en todos los volcados, mientras la emisora real
//   cambiaba 8->7->8. No esta en RadioSelection, TrackManager ni
//   FrontEndMenuManager: vive en el AudioEngine (que maneja los ajustes
//   de audio) y solo se actualiza al elegir en el menu. Ahora, al abrir
//   el dial, se escribe ahi la emisora real (la del TrackManager), asi
//   EMISORA muestra la que suena. Se quito el volcado (era diagnostico).
//   NO se toco audio, blur ni HUD.
// v1.9.21: DIAGNOSTICO EMISORA CORREGIDO (DANIEL, SIN TOCAR LO DEMAS)
// - Tu log de la 1.9.20 SI registro bien los volcados, y me dejo dos cosas:
//   (1) FrontEndMenuManager salio NULL por MI culpa: desreferencie la
//   direccion del simbolo (*(void**)mm) cuando esa direccion ES el objeto
//   (en el log de arranque sim==obj). Ahora se usa directo. (2) El 3
//   (K-DST, la del menu) NO esta en RadioSelection (0xA0), ni en
//   TrackManager (0x100), ni en AudioEngine (0x80, casi todo ceros: su
//   emisora esta mas alla). Por eso ahora el volcado es mas ancho:
//   TrackManager, AudioEngine y FrontEndMenuManager a 0x200. Ademas el
//   TrackManager tiene dos zonas (0xB4 y 0xF4) con 04 05 06 06 06 06 06 y
//   los ultimos dos bytes cambiaron de 04 04 a 07 07 al pasar por Radio X:
//   algo ahi sigue la emisora anterior. NO se toco audio, blur ni HUD.
// v1.9.20: DIAGNOSTICO EMISORA (DANIEL, SIN TOCAR LO DEMAS)
// - Tu prueba con la 1.9.19 lo dejo CLARO: en CSR (8) el juego decia 8 en
//   todo (audio=8 track=8 rueda=8, preview 0xAD/0xE9 en 8) y aun asi EMISORA
//   mostraba K-DST (3, la ultima elegida EN EL MENU). O sea que EMISORA no
//   lee ni los getters ni el preview: la emisora del menu vive en otra
//   variable (ajustes/frontend) que solo se actualiza al elegir en el menu.
//   Esta version NO cambia nada del juego: al abrir el dial vuelca la
//   memoria de RadioSelection, TrackManager, AudioEngine y FrontEndMenuManager
//   para encontrar ese byte. Con dos aperturas (en 8 y en 7) el byte que se
//   queda en 3 es la del menu y el que cambia es la real; la siguiente
//   version lo corrige. NO se toco audio, blur ni HUD.
// v1.9.19: DIAL LEE EL TRACKMANAGER (DANIEL, SIN TOCAR LO DEMAS)
// - Solo falta el dial (Daniel, 1.9.18): estando en Radio X (7), opciones
//   de sonido muestra EMISORA: PLAYBACK FM (1, la elegida antes en el menu).
//   El log de la 1.9.18 probo la causa: el getter del AudioEngine va
//   ATRASADO. Tras cambiar a 7 por la rueda, la lectura decia 1; el dial
//   abria con juego=7 pero luego juego=6 y juego=1, y la rueda volvia a
//   "estacion actual 1 Playback FM": RefreshCurrent pisaba g_current=7 con
//   el valor viejo del AudioEngine. El TrackManager es el que actualizan
//   nuestros hooks (StartRadio), asi que ahora RefreshCurrent Y el ctor del
//   dial leen PRIMERO el getter del TrackManager; el del AudioEngine solo
//   es respaldo. El ctor del dial registra ambos (audio=%d track=%d) para
//   confirmar si difieren. NO se toco audio, blur ni HUD (la captura de la
//   1.9.18 muestra el blur suave y el HUD oculto: eso queda igual).
// v1.9.18: DIAL PREVIEW + BLUR SIN PIXELIADO AL 100% (DANIEL / REHAN)
// - Dial (Daniel, 1.9.17): seguia mostrando la emisora ANTERIOR elegida en
//   el menu. El log de la 1.9.17 probo que la emisora del juego y la de la
//   rueda SI coinciden al abrir el dial (juego=3 rueda=3, 11=11, 10=10), asi
//   que el problema no es la emisora sino el "preview" del dial (mgr+0xAD):
//   ese no se actualiza al cambiar con la rueda. Al abrir el dial ahora se
//   sincronizan mgr+0xAD y mgr+0xE9 con la emisora real (log "UT: dial
//   preview sincronizado %d -> %d") para que el dial resalte la que suena.
// - Blur (Rehan): manejando seguia sin verse como parado. Al 100% el factor
//   era 8x (301x135) y ampliar eso a 2408x1080 se ve pixeliado aunque el
//   gaussiano corra 2 veces. Ahora el factor tiene tope 4x (602x270) y la
//   fuerza a blur alto viene de mas iteraciones del gaussiano (1/2/3 segun
//   el %): fuerte pero suave, sin cuadros.
// - HUD (log 1.9.17): los hooks SI se disparan ("UT: CHud::Draw oculto",
//   "UT: DrawAll oculto", "UT: Radar oculto" aparecen en el log). Falta
//   confirmar con captura si las barras de salud/armor (PlayerInfo, que no
//   registro su "oculto") tambien se ocultan.
// v1.9.17: DIAL SINCRONIZADO (DANIEL)
// - Al entrar a opciones de sonido estando en otra emisora (p. ej. CSR 8),
//   el dial se quedaba en User Track (12) en vez de CSR. El ctor de
//   RadioSelection ahora registra la emisora del juego y de la rueda al
//   abrir el dial ("UT: dial abierto (juego=%d rueda=%d activo=%d)") y,
//   si el reproductor no esta activo y la emisora real no es la 12 pero el
//   juego cree que es la 12, la resincroniza con StartRadio a la emisora
//   real. Asi el dial muestra la emisora que suena, no la 12.
// v1.9.16: BLUR SIN CUADROS + HUD DIAGNOSTICO (DANIEL)
// - Blur pixeliado (Daniel, 1.9.15): el gaussiano corre DOS veces (H+V
//   dos iteraciones): una sola pasada no suaviza bastante el 8x y al 100%
//   se veian los cuadros. Y la captura se estabiliza: los primeros 15
//   cuadros se copia siempre (g_blurFrames) y ahi se congela; el primer
//   cuadro al abrir la rueda puede ser una transicion (camara / camara
//   lenta) y congelarlo dejaba el blur pixeliado.
// - HUD visible (Daniel, 1.9.15): los 4 hooks dicen "ok" en el log pero el
//   HUD se ve con la rueda abierta. Los hooks ahora registran la primera
//   vez que ocultan de verdad ("UT: DrawAll oculto", "CHud::Draw oculto",
//   "PlayerInfo oculto", "Radar oculto") para confirmar si se disparan.
//   El log que mando Daniel no trae ni "rueda abierta" ni "blur: FBO
//   listo": es de antes de abrir la rueda, asi que no confirma nada.
// v1.9.15: PARAR CASI AL INSTANTE AL IR A INICIO (REHAN)
// - Rehan (1.9.13): con el umbral de 1 s la musica sonaba 1-2 s despues
//   de ir a inicio; las nativas (Radio Los Santos) paran al instante.
//   UT_BufferCallback ahora tambien mira la pausa del juego
//   (m_UserPause/m_CodePause, que el juego pone al ir a segundo plano):
//   pausa en el siguiente buffer (~20 ms). Y el hueco sin cuadros baja de
//   1 s a 0.5 s como respaldo (por si el juego no pone su pausa). El
//   preview del menu (ut_menuPreview) no se pausa por la pausa del juego
//   (como en UT_Frame), pero el hueco sin cuadros SI lo pausa: ir a inicio
//   desde el menu tambien corta la musica.
// v1.9.14: BLUR CONGELADO + ICONOS 8/12 OPACOS + SOLO GTAVRadioMusic + PAQUETE PERSONALIZADO
// - Blur (Rehan): la escena se copia UNA VEZ al abrir la rueda y se congela
//   (g_blurFrozen; se descongela al cerrar la rueda en DrawFrame, y se
//   vuelve a copiar si se recrea el FBO). Manejando, la escena cambia cada
//   cuadro y el blur se veia peor que parado; congelada, manejar se ve
//   igual que parado.
// - Iconos (Daniel): las emisoras 8 (CSR) y 12 (User tracks) quedan NORMALES,
//   sin transparencia: su centro negro (User Track) y morado (CSR) no se
//   transparenta (LoadPngFromMemory con blackKey=false para set 1, idx 8/12)
//   y detras va el color de fondoIcono, dibujadas un poco mas chicas para
//   que el color se vea como anillo. La 1.9.13 las teñia; eso era lo
//   contrario (el logo debe quedar con sus colores).
// - Musica (Daniel): la carpeta "music" YA NO se usa (ni como respaldo):
//   solo GTAVRadioMusic (6 carpetas, construidas en tiempo de ejecucion).
// - Paquete (Rehan): el paquete se detecta de /proc/self/cmdline (g_pkgName;
//   si no se lee, com.rockstargames.gtasa) y las rutas de INI/log/musica lo
//   usan: con un paquete personalizado (gtasa2, gtasa23...) el INI y la
//   musica van a su carpeta, no a com.rockstargames.gtasa.
// v1.9.13: PREVIEW DESDE EL MENU DE INICIO + PARAR AL IR A INICIO EN 1 S + ICONOS 8/12 TEÑIDOS
// - Preview en el menu de inicio: en el menu principal no hay vehiculo,
//   asi que UT_Frame ponia ut_pausedByExit y el preview no sonaba (la 1.9.12
//   solo saltaba la pausa por m_UserPause). Ahora ut_menuPreview tambien
//   salta la pausa por salir del vehiculo: menu inicio -> ajustes ->
//   opciones de sonido -> tocar User Track suena.
// - Pantalla de inicio: el umbral del callback de OpenSL baja de 3 s a 1 s
//   (Rehan: con 3 s la musica sonaba 4-5 s al ir a inicio). La 1.9.12 NO
//   toco este mecanismo (el callback es un hilo de OpenSL independiente de
//   ut_menuPreview); el retraso era el umbral de la 1.8.0.
// - Iconos: las emisoras 8 (CSR) y 12 (User tracks) tienen el logo como
//   imagen densa (slotrad, con recuadro); con fondoIcono el circulo de color
//   solo se veia por fuera del logo (el black-key deja el logo opaco, ~16%
//   del cuadro, y el resto ya es fondoIconoColor). Con iconos a color se
//   tiñen con el color de fondoIcono (mezclado con blanco para no
//   oscurecerlos) para que el logo tambien tome el color. Solo esas dos.
// v1.9.12: MIXTAPE SUENA DESDE OPCIONES DE SONIDO + EMISORA CON NOMBRE (Alex 1.8.4, informe SAUserTracks-1.8.4-RE3.md)
// - El tap en el dial SI llega por RetuneRadio (TrackManager), pero el
//   juego esta en pausa (m_UserPause) y UT_Frame pausaba el reproductor:
//   silencio. Alex tiene una rama "menu preview" que (re)arranca su
//   reproductor con el menu abierto. Aqui: flag ut_inAudioMenu (true en el
//   ctor de RadioSelection, false en el dtor) y ut_menuPreview (true al
//   elegir la 12 en el menu, false en UT_Stop y en el dtor); UT_Frame
//   salta la pausa por m_UserPause mientras ut_menuPreview. Al cerrar el
//   menu, UT_Frame vuelve a pausar. La nativa sigue suprimida por
//   StartTrackPlayback (el preview suena SOLO por nuestro reproductor).
// - EMISORA: el juego busca la clave GXT "FEA_NON" (Radio Off) para la 12.
//   Hook a CKeyGen::GetUppercaseKey: con Mixtape sonando, "FEA_NON"
//   devuelve el hash de "FEA_MP3" (0x9109b7c1, JAMCRC verificado) y el
//   menu muestra el nombre propio del juego para User Tracks; "FEA_MP3"
//   devuelve lo mismo (identidad); "SAMP_AUDIO_HINT" devuelve 0x93516cf0
//   (valor de Alex, hash no identificado, se copia tal cual).
// v1.9.11: SILENCIAR LA NATIVA 12 + MENU MIXTAPE SIN CRASH (Alex 1.8.4, informe SAUserTracks-1.8.4-RE2.md)
// - DOBLE AUDIO: la nativa 12 reproduce SUS PROPIOS User Tracks
//   (PlayTrack encola la pista, StartTrackPlayback la empieza). Alex lo
//   silencia en StartTrackPlayback: con mixtape activo y las 3 puertas
//   despejadas y el playback no deshabilitado, TRAGA la llamada (la pista
//   nativa nunca suena). Nosotros no lo enganchabamos: ahi estaba el doble
//   audio. Los bloqueos 0x22 solo quitan la estatica, no la musica.
// - Las 3 puertas son punteros a estado del juego (doble indireccion):
//   libGTASA+0x851b00 -> byte bit 0; +0x84c998 -> byte bit 0;
//   +0x84e6d0 -> WORD (uint32) != 0. Si alguna esta puesta, la nativa pasa.
//   Si un puntero es NULL, se deja pasar (seguro: no suprimir sin verificar).
// - PlayTrack: solo pone ut_playbackDisabled = (a2 == -1) y pasa al
//   original (es una puerta de StartTrackPlayback; sin el, el flag queda
//   en 0 = permitir suprimir).
// - MENU MIXTAPE: enganchar el ctor de RadioSelection
//   (_ZN11AudioScreen14RadioSelectionC2Ev): llamar al original, asignar
//   radio_TPLAYER en +0x80 con refcount+1 en [tex+0x64] via
//   TextureDatabaseRuntime::GetTexture, y SOLO DESPUES aplicar los 2
//   parches del menu (una vez). Sin la textura el parche crashea (1.9.9);
//   si GetTexture falla, ni se intentan los parches.
// v1.9.10: PARCHES DEL MENU REVERTIDOS - CRASH AL ENTRAR A OPCIONES DE SONIDO (Daniel, aml_crashlog_2_7wfq.txt)
// - La 1.9.9 crasheaba al entrar a opciones de sonido: SIGSEGV en
//   libGTASA+0x355044 (MenuScreen::DrawSprite, textura NULL), llamado desde
//   AudioScreen::RadioSelection::Render. Causa: los 2 "12-slot menu patches"
//   amplian los huecos del dial a 12, pero el hueco 12 no tiene textura.
//   Alex ADEMAS asigna radio_TPLAYER en RadioSelection+0x80 en el ctor;
//   sin eso el parche solo crashea. Los quito hasta enganchar el ctor como
//   Alex (RE en curso, informe SAUserTracks-1.8.4-RE2.md). El menu de pausa
//   vuelve a marcar Radio Off en la 12 (como 1.9.2-1.9.6), pero no crashea.
// - El doble audio/estatica SIGUE en la 1.9.9 aunque el segundo bloqueo
//   0x22 se instalo bien ("hook CAEFrontendAudioEntity::AddAudioEvent ok")
//   y la nativa se queda en 12 ("nativa audio=12 track=12"). Sospechosos:
//   los hooks PlayTrack/StartTrackPlayback de Alex (RE en curso).
// v1.9.9: COMO ALEX 1.8.4 — SEGUNDO BLOQUEO 0x22 + PARCHES DEL MENU + SIN STOPRADIO-DESPUES (Daniel, SAUserTracks 1.8.4)
// - Desarme del 1.8.4 de Alex (informe en files/SAUserTracks-1.8.4-RE.md):
//   Mixtape se activa por StartRadio(12), RetuneRadio(12) y el preview de
//   CheckForStationRetune; la nativa NUNCA se redirige a 13; el "< Mixtape >"
//   del HUD lo dibuja el propio mod con CFont (no es del juego); M4A es
//   MediaCodec sin codigo especifico (nuestro BASS ya lo cubre).
// - StopRadio: Alex lo llama SOLO ANTES de StartRadio(12) (en RetuneRadio)
//   y NUNCA DESPUES. Nuestro 1.9.8 lo llamaba despues en los 4 hooks y el
//   log lo probo: resetea la nativa a 13. QUITADO (los 8 + el wasUT al
//   salir por la rueda). La nativa se queda en 12.
// - Doble audio/estatica: Alex tiene DOS bloqueos 0x22 —
//   CAudioEngine::ReportFrontendAudioEvent Y
//   CAEFrontendAudioEntity::AddAudioEvent. Nosotros solo teniamos el
//   primero; la estatica sale por la entidad de audio. Anadido el segundo
//   (UT_FrontendAddAudioEvent_hook, con flag de reentrancia como Alex).
// - Menu de pausa Mixtape: faltaban los 2 "12-slot menu patches" de Alex
//   (0x364628 cmp x15,#46->#50; 0x3646E0 cmp w1,#48->#52), con chequeo de
//   valor, en UT_ApplyStation12Patches (ahora son 4 parches).
// v1.9.8: LISTA SIN TRASPASAR + STOPRADIO DESPUES DE LA NATIVA 12 (Daniel, log 1.9.7 + captura)
// - Lista: la primera fila traspasaba el encabezado (se veia "Darkside" encima
//   de "LISTA DE CANCIONES"). Causa: las filas se dibujaban con la draw list
//   de la VENTANA, que no recorta; al desplazar, la fila que quedaba por
//   encima del borde superior se seguia dibujando. Ahora las filas usan la
//   draw list del HIJO (dlc), que si recorta al rectangulo de la lista.
// - Doble audio: la 1.9.7 probo que la nativa en 12 (Mixtape) NO lo quita, y
//   el log mostro que AudioEngine::RetuneRadio SI disparo dos veces (la radio
//   del juego si pasa por ahi al cambiar a la 12). Queda una sospecha: la
//   nativa en 12 pone su propio audio (el juego tiene su propio User Tracks)
//   y suena junto al reproductor. Ahora StopRadio se llama DESPUES de
//   orig(12) en las 4 vias (rueda, RetuneRadio, AudioRetune, AudioStart)
//   para silenciar la nativa 12, y tambien al SALIR de la 12 por la rueda
//   (el hook de StartRadio no lo hacia; los de RetuneRadio si desde la 1.9.4).
//   Si el menu de pausa vuelve a marcar Radio Off, es que StopRadio resetea
//   la emisora y habra que quitar el de despues.
// v1.9.7: NATIVA 12 (MIXTAPE) + PRIMER TOQUE DE LA LISTA NO CAMBIA LA CANCION (Daniel, log 1.9.6 + OnlineRadio)
// - Doble audio: en el log de la 1.9.6 NINGUN hook de la radio del juego
//   disparo (ni RetuneRadio del TrackManager, ni RetuneRadio del AudioEngine,
//   ni StartRadio del AudioEngine). La radio del juego cambia por una via que
//   aun no esta enganchada. Prueba de la hipotesis de Daniel: la nativa
//   vuelve a la 12 (Mixtape) con StopRadio antes en TODOS los hooks (desde la
//   1.9.3 StopRadio si se llama, cosa que en la 1.9.0/1.9.1 no pasaba en el
//   hook de StartRadio). Si con la 12 el juego ya no falla, se acabo el doble
//   audio y el menu de pausa mostrara Mixtape. Si vuelve el doble audio, se
//   revierte a 13 y la siguiente via es enganchar CheckForStationRetune y el
//   StartRadio en 0x83F5C0 (el que usa OnlineRadio, ver abajo).
// - OnlineRadio (AndroidModLoader/GTASA_OnlineRadio, lo que Alex uso en parte
//   para SAUserTracks): NO activa Mixtape. No engancha el cambio de emisora
//   para mostrar una estacion: cuando CUALQUIER radio arranca, pone su propio
//   stream (BASS) y dibuja su propio texto en el HUD; ademas QUITA la radio
//   de los ajustes de audio del menu de pausa (PlaceB en 0x3638A4). Lo util
//   que deja: engancha un StartRadio por PLT en pGTASA+0x83F5C0 (firma
//   void(self, vehicleInfo)), una funcion sin simbolo que si dispara cuando
//   la radio arranca. Esa es la siguiente via a probar si la 12 no lo arregla.
// - Lista: el primer toque no cambia la cancion. El toque que abre la lista
//   se colaba como un clic en la fila que quedaba bajo el dedo (el boton de
//   la rueda desaparece y la fila ocupa su sitio). Ahora los toques de los
//   primeros 300 ms tras abrir la lista se ignoran; despues, un toque salta
//   a la cancion y un arrastre solo desplaza.
// v1.9.6: LISTA DESLIZABLE + RADIO DEL JUEGO POR AudioStart + GUARD DE CARGA (Daniel, log 1.9.5)
// - Cierre al reanudar (1.9.5, primera vez): al cargar la partida el juego
//   puede llamar a StartRadio/RetuneRadio antes de estar listo. Los hooks de
//   AudioEngine (RetuneRadio y el nuevo StartRadio) ahora dejan pasar la
//   llamada original sin tocar el reproductor mientras la pantalla de carga
//   esta activa (ut_loadingActive). Si vuelve a cerrar al reanudar, dime en
//   que momento exacto.
// - Doble audio por la radio del juego: ninguno de los dos hooks de
//   RetuneRadio disparo en el log de la 1.9.5. Queda CAudioEngine::StartRadio,
//   que es la via que usa el modoCambio=2 y probablemente la radio del juego.
//   Se engancha con la misma logica (StopRadio + nativa 13). Si este tampoco
//   dispara, la radio del juego cambia por otra via y habra que buscarla.
// - Lista de canciones deslizable: el scroll propio de ImGui no responde al
//   toque, asi que es manual como el del panel (arrastre vertical + inercia).
//   Un toque salta a la cancion; un arrastre solo desplaza.
// - Tu sospecha (sin Mixtape el juego falla): es posible, pero antes de
//   volver la nativa a la 12 hay que agotar las vias de cambio de emisora.
//   Si el AudioStart tampoco lo arregla, la siguiente prueba es nativa 12
//   con StopRadio (que ahora si corta la anterior en todos los hooks).
// v1.9.5: BOTONES POR DrawAll + RADIO DEL JUEGO POR AudioEngine (Daniel, log 1.9.4 + VHudRadioSyms.txt)
// - El volcado de simbolos (895) trae la respuesta de los botones:
//   CTouchInterface::DrawAll(bool) SI existe. Los 5 nombres de la sonda eran
//   los equivocados. Se engancha y se ocultan TODOS los botones tactiles con
//   la rueda/panel/lista abiertos. El claxon tambien se oculta mientras la
//   rueda esta abierta, pero el doble-toque sigue funcionando por POSICION
//   (g_evs) y el boton flotante queda visible.
// - Doble audio por la radio del juego: el log 1.9.4 mostro el hook de
//   RetuneRadio del TrackManager instalado pero NUNCA disparo en un cambio a
//   la 12. La radio del juego cambia por CAudioEngine::RetuneRadio, otra
//   funcion. Se engancha tambien con la misma logica (StopRadio + nativa 13).
//   Devuelve la emisora; si en realidad devuelve void, es inocuo.
// - Mixtape: sigue sin salir (la nativa queda en 13 = Radio Off). No hay
//   cambio en esta version: primero el audio limpio y los botones.
// v1.9.4: RADIO DEL JUEGO + WIDGETS DEL HUD (Daniel, log 1.9.2 + AdvancedSettings)
// - El log que mando Daniel es de la 1.9.2, no de la 1.9.3: por eso no se
//   oculto ningun HUD y no salio VHudRadioSyms.txt. La 1.9.3 ya lo trae;
//   esta 1.9.4 lo incluye todo.
// - Doble audio por la radio del juego: Daniel descubrio que pasa al cambiar
//   desde la radio original (RetuneRadio), no desde la rueda, y se quita al
//   salir/entrar del auto. El hook de RetuneRadio paraba el reproductor pero
//   no cortaba la nativa con StopRadio antes del cambio (la rueda si lo hace
//   desde la 1.9.3). Ahora tambien lo hace al salir de la 12.
// - HUD por widgets (AdvancedSettings): el HUD no es CTouchInterface, son
//   clases CWidget. AdvancedSettings referencia CWidgetPlayerInfo::Draw
//   (vida, chaleco, arma, buscado, dinero) y CWidgetRadar::Update (radar),
//   asi que existen en 2.10. Se enganchan y se ocultan con la rueda/panel/
//   lista abiertos (con `self`, son de instancia). Los botones tactiles son
//   widgets en m_pWidgets (el claxon es el 7): siguen pendientes, pero el
//   volcado VHudRadioSyms.txt de la 1.9.3 los va a listar para engancharlos.
// v1.9.3: PARCHE DE CLAUDE VERIFICADO (Daniel lo trajo, 2026-11-21 03:10)
// - StopRadio ANTES de la nativa 13 en el hook de StartRadio (el que usa la
//   rueda): el log de la 1.9.2 mostro que se pasaba de K-Rose (2) a la 13 sin
//   StopRadio, y la anterior podia seguir sonando por debajo (doble audio).
//   Solo se llama cuando se empieza a sonar (no al reanudar). El hook de
//   RetuneRadio ya lo hacia; el de StartRadio no. Es el cambio principal.
// - Prioridad de hilo de audio para el decodificador (setpriority -16; si el
//   sistema la niega, se ignora). underruns=0 en los logs, es precaucion.
// - Guard: si libSAUserTrack64.so esta cargado, el reproductor propio NO se
//   activa (dos reproductores = doble audio). Daniel ya lo quito, es por si
//   vuelve.
// - CHud::Draw se oculta con la rueda/panel/lista abiertos, SI el simbolo
//   existe (radar, vida, dinero). Los botones tactiles NO son CHud: siguen
//   pendientes, pero esta version vuelca los simbolos de libGTASA.so con
//   "Widget"/"Touch"/"Hud"/"Radar" a VHudRadioSyms.txt para encontrar el
//   dibujo de los botones. El hook pasa `self` al original (vale estatico o
//   instancia); el de Claude no llevaba argumento y eso podia fallar.
// - La estatica como problema de decodificador (WAV/FLAC/OGG) sigue siendo
//   hipotesis de Claude: probar con solo los 4 MP3. Y ojo: un archivo
//   "(MP3_160K).wav" puede ser un MP3 renombrado, no un WAV de verdad.
// v1.9.2: NATIVA DE VUELTA A 13 (Daniel, log 1.9.1: "no solucionaste nada")
// - La 1.9.0 y la 1.9.1 probaron que la nativa en 12 SI suena: doble audio
//   (la emisora anterior por debajo) + estatica, aunque el diagnostico diga
//   "nativa 12" y aunque el evento 0x22 este bloqueado. La 12 no es silencio
//   para el juego (no existe para el: no hay audio nativo de User Tracks y
//   el motor no la apaga). Solo Radio Off (13) la silencia de verdad, como
//   en el build #9. Se revierte: StartRadio y RetuneRadio vuelven a pedir
//   13. El menu de pausa marcara Radio Off: es el precio de que no suene
//   doble. El bloqueo de estatica 0x22 se mantiene (por si ayuda al cambiar).
// - Botones: los 5 nombres de CTouchInterface NO existen en 2.10 (log 1.9.1:
//   todos "no"). Los botones se dibujan por otro mecanismo; queda pendiente
//   encontrarlo (sonda del struct del widget). Esta version NO los oculta.
// v1.9.1: SIN ESTATICA 0x22 + SONDEO DE BOTONES (Daniel, log 1.9.0)
// - El "doble audio" de la 1.9.0 NO era la emisora anterior: era la estatica
//   de sintonia (evento 0x22) sonando junto a la musica. SAUserTracks la
//   bloquea ("blocking native retune static (0x22) while mixtape active").
//   Ahora se engancha ReportFrontendAudioEvent y se salta el 0x22 mientras
//   suena el reproductor. La nativa SE QUEDA en 12 (Mixtape), no se revierte
//   a 13: con 13 el menu de pausa marcaria Radio Off y la 12 es la que deja
//   que el menu muestre Mixtape.
// - Botones: CTouchInterface::Draw no existe en 2.10 (log 1.9.0). Ahora se
//   sondean 5 nombres (Draw/Render/DrawWidgets/DrawButtons/DrawWidget) y se
//   registra en el log cual existe, para enganchar el correcto.
// - Desensamblado de SAUserTracks (capstone): solo aplica DOS parches
//   (0x47c9f8 y 0x47f3ac, los mismos que nosotros); no hay tercer parche de
//   memoria. El "tercer parche (display-name)" del informe RE no existe.
// v1.9: BLUR GAUSSIANO + MIXTAPE + SIN ESTATICA + SIN BOTONES (Daniel, log 1.8.0)
// - Blur gaussiano de verdad (shader separable 5-tap, H+V sobre la textura
//   pequena): al 100% ya no se ven los cuadros (la piramide por mitades solo
//   promediaba 2x2). Como el de Void Radio Wheel Expanded.
// - Sin estatica al cambiar con la radio original: hook de RetuneRadio (como
//   SAUserTracks de Alex) que hace StopRadio original (corta la anterior) y
//   luego StartRadio original con 12. La nativa queda en la 12 (Mixtape) en
//   vez de en 13: el menu de pausa ya no marca Radio Off en User Tracks.
// - Botones del juego ocultos con la rueda/panel/lista abierta (hook de
//   CTouchInterface::Draw; si el simbolo no resuelve, no se hace nada).
// v1.9: CLAXON POR BOTON 7 - la sonda de la v1.8 lo confirmo: el byte 84 (0x54)
// del widget m_pWidgets[7] es su bandera de "tocado" (1 al pulsarlo, 0 al
// soltarlo; 16/17 volcados con 1 caen a +-1 volcado de un toque al claxon, y
// el float de 80-83 es el tiempo que lleva pulsado en segundos). Esta version
// lee esa bandera cada frame y el doble toque al claxon abre/cierra la rueda
// sin depender de la posicion del boton. La zona tactil queda
// como respaldo por si el arreglo no resuelve o el widget 7 es null.
// v1.8: BOTON 7 DIRECTO - el escaneo de la v1.7 revelo que IsTouchDown(int)
// cuenta DEDOS (indice multi-touch 0..7), no botones: solo el 0 se encendia
// con cualquier toque. El claxon SI es el widget 7 (GTAForums: 7 = claxon/horn),
// asi que esta version lee el boton 7 directo del arreglo CTouchInterface::
// m_pWidgets[7] y vuelca sus primeros 128 bytes (y los del widget 2, acelerar,
// como control) para encontrar su bandera de "tocado". Si aparece, la v1.9 la
// leera directo y el claxon sera independiente de la posicion del boton.
// v1.7: CLAXON POR WIDGET - verificado desensamblando el mod de rueda de armas que
// mando Daniel (libweapwheeldliiartworks64.so, 1.5 MB): ese mod resuelve por GetSym
// CTouchInterface::IsTouchDown(int) y lo llama como funcion estatica con el id del
// widget en w0 (sin 'this': en su bucle lo llama con w0 = 0..7 y tambien
// GetTouchPosition(id) e IsReleased(id,...) igual). El claxon seria el widget 7
// (WIDGET_HORN, segun la otra IA; se confirma con el escaneo de widgets que trae
// esta version: registra que ids estan pulsados cuando cambia el conjunto).
// La zona tactil/calibrada queda como respaldo. Si mueves el boton de claxon, el
// widget 7 lo sigue detectando igual.
// v1.6: CALIBRAR CLAXON - el volcado CPad de la v1.5 salio IDENTICO en los 120
// volcados: el estado del claxon NO esta en los primeros 256 bytes de CPad (o no
// esta en CPad). Mientras se sigue buscando (volcado ampliado a 2048 bytes), la
// v1.6 (retirado en v2.0): la rueda traia un boton "Calibrar claxon"
// del juego, y la zona queda guardada en /storage/emulated/0/vhud/horn_pos.txt.
// Si mueves el boton de claxon en los controles del juego, lo recalibras con un
// toque. La zona ya no es fija: es la que tu calibraste.
// v1.5: la rueda ahora se queda ABIERTA hasta tocar una estacion (antes solo el
// arrastre elegia: un toque directo a un segmento no lo seleccionaba, asi que habia
// que mantener el dedo puesto). Tocar un segmento lo aplica y cierra; tocar fuera
// de la rueda la cierra sin cambiar; el arrastre sigue funcionando.
// v1.5 tambien vuelca la memoria de CPad para encontrar donde el juego guarda el
// estado del claxon: si se mueve el boton de claxon en los controles del juego, la
// zona tactil (v1.2-v1.4) deja de funcionar; leer el claxon desde CPad no depende
// de la posicion del boton. El volcado es solo diagnostico para la v1.6.
// v1.2: RetuneRadio NO cambia a una estacion concreta (en la prueba de Daniel la
// radio se quedo en 10 aunque se llamo RetuneRadio(4/7/13/6/3): parece que solo
// avanza/retrocede un paso). El cambio real es StartRadio: primero TrackManager
// (Eaafh, el que usa el mod GTA6Radio 4.0 en 2.10), luego AudioEngine (Eaa),
// y RetuneRadio queda solo como ultimo recurso.
// v1.3: BUG DE LA v1.2 - las funciones de radio son metodos C++ y necesitan 'this'
// (el objeto global AudioEngine / AERadioTrackManager). Sin 'this', el id de la
// estacion caia en el registro de 'this' y la funcion tocaba memoria basura:
// por eso StartRadio(12) cerro el juego en la prueba de Daniel, y por eso
// RetuneRadio nunca cambio nada (su argumento real era basura) y GetCurrent
// devolvia valores raros (10, 13...). Ahora todo se llama con su 'this' real.
// NO usar los mangleds de PC (Ea vs Ec, Eific...). CMenuManager::RetuneRadio NO existe.
// Por eso la v1 NUNCA llama a una funcion que no resolvio con GetSym: si RetuneRadio
// falta, la rueda se abre, se ve, pero al soltar solo lo registra en el log.
// El mini-plugin VHudRadioProbe (probe/) prueba estos mismos simbolos y escribe
// ok/FALTA en VHudRadioProbe.txt; la v1 se ajusta a lo que el probe diga.
#include <mod/config.h>
#include <mod/amlmod.h>
#include <android/log.h>
#include <EGL/egl.h>
#include <SLES/OpenSLES.h>  // v2.2: sonido (la libreria se abre con dlopen, no se enlaza)
#include <SLES/OpenSLES_Android.h>  // v2.2: SLDataLocator_AndroidSimpleBufferQueue vive aqui, no en OpenSLES.h
#include <GLES2/gl2.h>
#include <dlfcn.h>
#include <link.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <thread>  // v2.52: hilo de sonido (SoundWorker)
#include <condition_variable>  // v2.52: aviso de clic al hilo de sonido
#include <string>
#include <vector>
#include "imgui.h"
#include "imgui_impl_opengl3.h"
#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#define STBI_NO_SIMD
#define STBI_ONLY_PNG
#include "stb_image.h"
#include "icons_embedded.h"  // v2.29: PNGs incrustados en el .so (base64)

MYMODCFG(net.hunter09.vhudradio, VHud Radio Wheel, 1.9.77-beta, Hunter09)
NEEDGAME(com.rockstargames.gtasa)
#define VHUDRADIO_VERSION "1.9.77-beta"

// ---- log: VHudRadioLog.txt junto al .ini (en VHudRadio/) ----
static std::string g_logPath;
static std::string g_gameFilesDir;  // files del juego (se resuelve en OnModLoad)
static std::string g_pkgName = "com.rockstargames.gtasa";  // v1.9.14: paquete del juego (detectado del proceso; Rehan usa gtasa2/gtasa23)
static std::string g_gameDataDir;  // v2.3: carpeta data del juego (padre de files) - el .ini va en data/configs
static void Log(const char* fmt, ...) {
    // v1.7: buf 8192 (antes 600 y el volcado CPad de 2048 bytes se truncaba a ~295)
    char buf[8192];
    va_list ap; va_start(ap, fmt); vsnprintf(buf, sizeof buf, fmt, ap); va_end(ap);
    __android_log_print(ANDROID_LOG_INFO, "VHudRadio", "%s", buf);
    if (!g_logPath.empty()) { FILE* f = fopen(g_logPath.c_str(), "a"); if (f) { fprintf(f, "%s\n", buf); fclose(f); } }
}

// ---- sonido (v2.2, v2.52): clic corto con OpenSL ES; desde la 2.52 el reproductor
// queda en PAUSED cuando no suena y todo OpenSL corre en un hilo aparte (SoundWorker) ----
static void* g_slHandle = nullptr;
typedef SLresult (*SlCreateEngineFn)(SLObjectItf*, SLuint32, const SLEngineOption*, SLuint32, const SLInterfaceID*, const SLboolean*);
static SlCreateEngineFn g_slCreateEngine = nullptr;
static SLObjectItf g_slEngine = nullptr, g_slMix = nullptr, g_slPlayer = nullptr;
static SLEngineItf g_slEngineItf = nullptr;
static SLPlayItf g_slPlay = nullptr;
static SLBufferQueueItf g_slQueue = nullptr;
static short g_clickLo[1600];
static short g_clickHi[1600];
static bool g_soundTried = false, g_soundOk = false;

// Solo se llama desde el hilo de sonido (SoundWorker).
static void SoundInit() {
    if (g_soundTried) return; g_soundTried = true;
    g_slHandle = dlopen("libOpenSLES.so", RTLD_NOW);
    if (!g_slHandle) { Log("  sonido: libOpenSLES.so no disponible"); return; }
    g_slCreateEngine = (SlCreateEngineFn)dlsym(g_slHandle, "slCreateEngine");
    if (!g_slCreateEngine) { Log("  sonido: slCreateEngine no disponible"); return; }
    if (g_slCreateEngine(&g_slEngine, 0, nullptr, 0, nullptr, nullptr) != SL_RESULT_SUCCESS || !g_slEngine) return;
    if ((*g_slEngine)->Realize(g_slEngine, SL_BOOLEAN_FALSE) != SL_RESULT_SUCCESS) return;
    if ((*g_slEngine)->GetInterface(g_slEngine, SL_IID_ENGINE, &g_slEngineItf) != SL_RESULT_SUCCESS || !g_slEngineItf) return;
    if ((*g_slEngineItf)->CreateOutputMix(g_slEngineItf, &g_slMix, 0, nullptr, nullptr) != SL_RESULT_SUCCESS) return;
    if ((*g_slMix)->Realize(g_slMix, SL_BOOLEAN_FALSE) != SL_RESULT_SUCCESS) return;
    SLDataLocator_AndroidSimpleBufferQueue locBuf = {SL_DATALOCATOR_ANDROIDSIMPLEBUFFERQUEUE, 2};
    SLDataFormat_PCM fmt = {SL_DATAFORMAT_PCM, 1, SL_SAMPLINGRATE_22_05, SL_PCMSAMPLEFORMAT_FIXED_16, SL_PCMSAMPLEFORMAT_FIXED_16, SL_SPEAKER_FRONT_CENTER, SL_BYTEORDER_LITTLEENDIAN};
    SLDataSource src = {&locBuf, &fmt};
    SLDataLocator_OutputMix locMix = {SL_DATALOCATOR_OUTPUTMIX, g_slMix};
    SLDataSink sink = {&locMix, nullptr};
    const SLInterfaceID ids[1] = {SL_IID_BUFFERQUEUE};
    const SLboolean req[1] = {SL_BOOLEAN_TRUE};
    if ((*g_slEngineItf)->CreateAudioPlayer(g_slEngineItf, &g_slPlayer, &src, &sink, 1, ids, req) != SL_RESULT_SUCCESS) return;
    if ((*g_slPlayer)->Realize(g_slPlayer, SL_BOOLEAN_FALSE) != SL_RESULT_SUCCESS) return;
    if ((*g_slPlayer)->GetInterface(g_slPlayer, SL_IID_PLAY, &g_slPlay) != SL_RESULT_SUCCESS) return;
    if ((*g_slPlayer)->GetInterface(g_slPlayer, SL_IID_BUFFERQUEUE, &g_slQueue) != SL_RESULT_SUCCESS) return;
    // CAMBIO: antes aqui se ponia SL_PLAYSTATE_PLAYING y se quedaba asi para siempre.
    if ((*g_slPlay)->SetPlayState(g_slPlay, SL_PLAYSTATE_PAUSED) != SL_RESULT_SUCCESS) return;
    for (int i = 0; i < 1600; i++) {
        float t = (float)i / 22050.0f;
        float env = expf(-t * 60.0f);
        g_clickLo[i] = (short)(sinf(2.0f * 3.14159265f * 880.0f * t) * env * 12000.0f);
        g_clickHi[i] = (short)(sinf(2.0f * 3.14159265f * 1318.0f * t) * env * 12000.0f);
    }
    g_soundOk = true;
    Log("  sonido: OpenSL ES listo (reproductor en pausa hasta el primer clic)");
}

static std::mutex g_sndMtx;
static std::condition_variable g_sndCv;
static int g_clickReq = 0;            // 0 = nada, 1 = clic grave, 2 = clic agudo (protegido por g_sndMtx)
static bool g_sndStarted = false;     // solo se toca desde PlayClick (hilo del juego)

static void SoundWorker() {
    SoundInit();
    std::unique_lock<std::mutex> lk(g_sndMtx);
    for (;;) {
        g_sndCv.wait(lk, [] { return g_clickReq != 0; });
        while (g_clickReq != 0) {
            int req = g_clickReq; g_clickReq = 0;
            lk.unlock();
            if (g_soundOk && g_slQueue && g_slPlay) {
                (*g_slQueue)->Clear(g_slQueue);
                (*g_slQueue)->Enqueue(g_slQueue, req == 2 ? g_clickHi : g_clickLo, 1600 * 2);
                (*g_slPlay)->SetPlayState(g_slPlay, SL_PLAYSTATE_PLAYING);
            }
            lk.lock();
            // El clic dura ~73 ms. Se espera 150 ms; si llega otro clic, se repite.
            g_sndCv.wait_for(lk, std::chrono::milliseconds(150), [] { return g_clickReq != 0; });
        }
        // Sin clics pendientes: el reproductor vuelve a pausa (no queda activo en el mezclador).
        lk.unlock();
        if (g_soundOk && g_slPlay) (*g_slPlay)->SetPlayState(g_slPlay, SL_PLAYSTATE_PAUSED);
        lk.lock();
    }
}

// Se llama desde el hilo de render/toque: ya no toca OpenSL, solo avisa al hilo de sonido.
static void PlayClick(bool hi) {
    // v2.59: clics restaurados (la 2.58 sin clics fallo al 2o intento: el
    // audio del mod NO era la causa del ruido de User Tracks).
    if (!g_sndStarted) { g_sndStarted = true; std::thread(SoundWorker).detach(); }
    { std::lock_guard<std::mutex> lk(g_sndMtx); g_clickReq = hi ? 2 : 1; }
    g_sndCv.notify_one();
}

// ---- estaciones 1..13 (VERIFIED por plugin-sdk eRadioID + GTAMods + v-hud) ----
static const char* kStationName[14] = { "", "Playback FM", "K-Rose", "K-DST", "Bounce FM", "SF-UR",
    "Radio Los Santos", "Radio X", "CSR 103.9", "K-Jah West", "Master Sounds 98.3",
    "WCTR", "User tracks", "Radio Off" };
// v1.4: programa/locutor de cada emisora (lo que se esta transmitiendo), como
// en la radio de GTA V. En la 12 el programa es la cancion que suena.
static const char* kStationProgram[14] = { "", "Forth Right MC", "Mary-Beth Maybell", "The Dusty Show",
    "The Funktipus", "Hans Oberlander", "Julio G", "Sage", "Phillip \"PM\" Michaels",
    "Johnny Lawton", "Johnny \"The Love Giant\" Parkinson", "WCTR News", "", "" };

// v2.16: idiomas - toda la interfaz (panel + generos) en 6 idiomas.
// Los nombres de las estaciones no se traducen (son nombres propios del juego);
// los generos y los textos del panel si. Sin acentos (como el resto del codigo).
enum LangId { L_ES = 0, L_EN, L_ID, L_PT, L_FR, L_DE, L_COUNT };
static const char* kLangNames[L_COUNT] = { "Español", "English", "Bahasa Indonesia", "Portugues (BR)", "Francais", "Deutsch" };
static const char* kLangCodes[L_COUNT] = { "es", "en", "id", "pt", "fr", "de" };
enum StrId { STR_TITLE, STR_HINT, STR_FONDO, STR_TAMANO, STR_ANILLO, STR_TEXTO, STR_PREVIEW, STR_RESET, STR_CLOSE, STR_PREVIEW_LABEL, STR_LANG, STR_BTN_SIZE, STR_BTN_OPAC, STR_BTN_LOCK, STR_BTN_RESET_POS, STR_FONDO_LBL, STR_DESLIZA, STR_CREDIT, STR_ICONOSCOLOR, STR_CAMARALENTA, STR_BOTONVIS, STR_SEC_RUEDA, STR_SEC_BOTON, STR_GENERO, STR_OCULTARNOMBRE, STR_POPUPICONO, STR_POPUPTEXTO, STR_POPUPTAM, STR_FONDOICONO, STR_ANIMACION, STR_BLUR, STR_BLOQUEARJUEGO, STR_TL_BTN, STR_TL_TITLE, STR_TL_PLAYING, STR_BTN_RUEDA_SIZE, STR_BTN_RUEDA_OPAC, STR_BTN_RUEDA_LOCK, STR_COUNT };  // v1.6: lista de canciones traducida  // v2.50: titulos de seccion del panel; v2.53: funciones de Discord; v2.55: sin iconos propios; v2.56: blur de fondo; v2.57: bloquear juego // v1.9.68: interruptor nativo eliminado, el nativo es el unico boton // v1.9.73: aviso de emisora (icono/texto) // v1.9.74: tamano del aviso
static const char* kUI[L_COUNT][STR_COUNT] = {
    { "Personalizar rueda de radio", "Fondo, tamano y colores. Se guardan en configs/VHudRadio.ini", "Oscuridad del fondo", "Tamano de la rueda", "Anillo", "Texto del centro", "Vista previa de la rueda", "Restablecer todo", "Cerrar", "VISTA PREVIA", "Idioma", "Tamano del boton", "Opacidad del boton", "Bloquear posicion del boton", "Restablecer posicion del boton", "Fondo", "Desliza para ver más", "rueda de radio GTAV de Hunter09", "Iconos de las emisoras a color", "Camara lenta al abrir la rueda", "Mostrar boton flotante", "Rueda de radio", "Boton flotante", "Mostrar género en la rueda", "Ocultar ventana de emisora", "Icono en el aviso de emisora", "Texto en el aviso de emisora", "Tamaño del aviso de emisora", "Fondo de color del icono", "Animación al abrir la rueda", "Desenfoque de fondo", "Bloquear juego con la rueda abierta", "Lista de canciones", "LISTA DE CANCIONES", "SONANDO", "Tamano del boton de la rueda", "Opacidad del boton de la rueda", "Bloquear posicion del boton de la rueda" },
    { "Customize radio wheel", "Background, size and colors. Saved in configs/VHudRadio.ini", "Background darkness", "Wheel size", "Ring", "Center text", "Preview the wheel", "Reset all", "Close", "PREVIEW", "Language", "Button size", "Button opacity", "Lock button position", "Reset button position", "Background", "Swipe to see more", "GTAV radio wheel by Hunter09", "Color radio station icons", "Slow motion when opening the wheel", "Show floating button", "Radio wheel", "Floating button", "Show genre on the wheel", "Hide station pop-up", "Icon in the station pop-up", "Text in the station pop-up", "Station pop-up size", "Icon background color", "Wheel open animation", "Background blur", "Block game input while wheel is open", "Song list", "TRACK LIST", "PLAYING", "Wheel button size", "Wheel button opacity", "Lock wheel button position" },
    { "Sesuaikan roda radio", "Latar, ukuran dan warna. Disimpan di configs/VHudRadio.ini", "Kegelapan latar", "Ukuran roda", "Cincin", "Teks tengah", "Pratinjau roda", "Atur ulang semua", "Tutup", "PRATINJAU", "Bahasa", "Ukuran tombol", "Opasitas tombol", "Kunci posisi tombol", "Atur ulang posisi tombol", "Latar", "Geser untuk melihat lebih banyak", "roda radio GTAV oleh Hunter09", "Ikon stasiun radio berwarna", "Gerakan lambat saat membuka roda", "Tampilkan tombol melayang", "Roda radio", "Tombol melayang", "Tampilkan genre di roda", "Sembunyikan jendela stasiun", "Ikon di pop-up stasiun", "Teks di pop-up stasiun", "Ukuran pop-up stasiun", "Warna latar ikon", "Animasi buka roda", "Blur latar belakang", "Blokir input game saat roda terbuka", "Daftar lagu", "DAFTAR LAGU", "MEMUTAR", "Ukuran tombol roda", "Opasitas tombol roda", "Kunci posisi tombol roda" },
    { "Personalizar roda de radio", "Fundo, tamanho e cores. Salvo em configs/VHudRadio.ini", "Escuridao do fundo", "Tamanho da roda", "Anel", "Texto do centro", "Pre-visualizar a roda", "Redefinir tudo", "Fechar", "PREVIA", "Idioma", "Tamanho do botao", "Opacidade do botao", "Bloquear posicao do botao", "Redefinir posicao do botao", "Fundo", "Deslize para ver mais", "roda de rádio GTAV de Hunter09", "Icones coloridos das estacoes", "Camera lenta ao abrir a roda", "Mostrar botao flutuante", "Roda de radio", "Botao flutuante", "Mostrar gênero na roda", "Ocultar janela da estação", "Ícone no aviso da estação", "Texto no aviso da estação", "Tamanho do aviso da estação", "Cor de fundo do ícone", "Animação ao abrir a roda", "Desfoque de fundo", "Bloquear jogo com a roda aberta", "Lista de músicas", "LISTA DE MÚSICAS", "TOCANDO", "Tamanho do botao da roda", "Opacidade do botao da roda", "Bloquear posicao do botao da roda" },
    { "Personnaliser la roue radio", "Fond, taille et couleurs. Enregistre dans configs/VHudRadio.ini", "Obscurite du fond", "Taille de la roue", "Anneau", "Texte central", "Apercu de la roue", "Tout reinitialiser", "Fermer", "APERCU", "Langue", "Taille du bouton", "Opacite du bouton", "Verrouiller la position du bouton", "Reinitialiser la position du bouton", "Fond", "Faites glisser pour voir plus", "roue radio GTAV de Hunter09", "Icones de stations en couleur", "Ralenti a l'ouverture de la roue", "Afficher le bouton flottant", "Roue radio", "Bouton flottant", "Afficher le genre sur la roue", "Masquer la fenêtre de station", "Icône dans l'avis de station", "Texte dans l'avis de station", "Taille de l'avis de station", "Couleur de fond de l'icône", "Animation d'ouverture de la roue", "Flou d'arrière-plan", "Bloquer le jeu quand la roue est ouverte", "Liste des chansons", "LISTE DES CHANSONS", "EN LECTURE", "Taille du bouton de la roue", "Opacite du bouton de la roue", "Verrouiller la position du bouton de la roue" },
    { "Radiorad anpassen", "Hintergrund, Groesse und Farben. Gespeichert in configs/VHudRadio.ini", "Hintergrund-Dunkelheit", "Radgroesse", "Ring", "Mitteltext", "Rad-Vorschau", "Alles zuruecksetzen", "Schliessen", "VORSCHAU", "Sprache", "Buttongroesse", "Button-Deckkraft", "Buttonposition sperren", "Buttonposition zuruecksetzen", "Hintergrund", "Wischen, um mehr zu sehen", "GTAV-Radiorad von Hunter09", "Farbige Radiosender-Symbole", "Zeitlupe beim Oeffnen des Rades", "Schwebenden Button anzeigen", "Radiorad", "Schwebender Button", "Genre auf dem Rad anzeigen", "Stationsfenster ausblenden", "Symbol im Sender-Popup", "Text im Sender-Popup", "Größe des Sender-Popups", "Symbol-Hintergrundfarbe", "Animation beim Öffnen des Rads", "Hintergrundunschärfe", "Spiel sperren, während das Rad offen ist", "Songliste", "SONGLISTE", "LÄUFT", "Rad-Button-Grosse", "Rad-Button-Deckkraft", "Rad-Button-Position sperren" },
};
// v2.0: genero de cada estacion, para el centro de la rueda estilo GTA V
static const char* kGenre[L_COUNT][14] = {
    { "", "Hip hop clasico", "Country", "Rock clasico", "Funk y disco", "House", "Hip hop", "Rock alternativo", "R&B y soul", "Reggae", "Groove y funk", "Charlas y noticias", "Tu propia musica", "Radio apagada" },
    { "", "Classic hip hop", "Country", "Classic rock", "Funk and disco", "House", "Hip hop", "Alternative rock", "R&B and soul", "Reggae", "Groove and funk", "Talk and news", "Your own music", "Radio off" },
    { "", "Hip hop klasik", "Country", "Rock klasik", "Funk dan disko", "House", "Hip hop", "Rock alternatif", "R&B dan soul", "Reggae", "Groove dan funk", "Obrolan dan berita", "Musikmu sendiri", "Radio mati" },
    { "", "Hip hop classico", "Country", "Rock classico", "Funk e disco", "House", "Hip hop", "Rock alternativo", "R&B e soul", "Reggae", "Groove e funk", "Conversa e noticias", "Sua propria musica", "Radio desligado" },
    { "", "Hip-hop classique", "Country", "Rock classique", "Funk et disco", "House", "Hip-hop", "Rock alternatif", "R&B et soul", "Reggae", "Groove et funk", "Debats et infos", "Ta propre musique", "Radio eteinte" },
    { "", "Klassischer Hip-Hop", "Country", "Klassischer Rock", "Funk und Disco", "House", "Hip-Hop", "Alternativer Rock", "R&B und Soul", "Reggae", "Groove und Funk", "Talk und Nachrichten", "Deine eigene Musik", "Radio aus" },
};

// ---- funciones del juego (resueltas por GetSym, nunca llamadas si faltan) ----
static void* (*FindPlayerPed)(int);
static void* (*FindPlayerVehicle)(int, bool);
// Las funciones de radio son metodos C++: el primer argumento es 'this', el objeto
// global del juego (resuelto por GetSym, el Probe ya confirmo que existen).
static void* g_audioEngine = nullptr;   // global CAudioEngine
static void* g_trackManager = nullptr;  // global CAERadioTrackManager
typedef signed char (*RetuneFn)(void*, signed char);  // en la practica devuelve void/char; solo se llama si resolvio
static RetuneFn g_retuneAudio = nullptr, g_retuneTrack = nullptr;
typedef signed char (*GetStationFn)(void*);
static GetStationFn g_getStationAudio = nullptr, g_getStationTrack = nullptr;
typedef void (*StartRadioAudioFn)(void*, signed char, signed char);
static StartRadioAudioFn g_startAudio = nullptr;
typedef void (*StartRadioTrackFn)(void*, signed char, signed char, float, unsigned char);
static StartRadioTrackFn g_startTrack = nullptr;  // el que usa el mod GTA6Radio 4.0 en 2.10 (verificado por Probe: ok)

// ---- funciones RW para crear la textura del boton nativo (v1.9.66) ----
// El cargador de PNG de SAUtils (LoadRwTextureFromPNG) devuelve NULL con
// un PNG valido que existe (log 1.9.65: boton_nativo.png, 19860 bytes,
// vt33=0x27924 correcto), asi que la textura se crea directamente desde
// los pixeles decodificados con stb_image. Nombres mangled C++ exportados
// en libGTASA.so 2.10 (verificados con nm -D). Nunca se llaman si faltan.
typedef void* (*RwImageCreateFn)(int, int, int);
typedef void* (*RwImageAllocFn)(void*);
typedef int (*RwImageFindFmtFn)(void*, int, int*, int*, int*, int*);
typedef void* (*RwRasterCreateFn)(int, int, int, int);
typedef void* (*RwRasterSetFromImageFn)(void*, void*);
typedef void* (*RwTextureCreateFn)(void*);
typedef void (*RwImageDestroyFn)(void*);
typedef void (*RwRasterDestroyFn)(void*);
static RwImageCreateFn g_rwImageCreate = nullptr;
static RwImageAllocFn g_rwImageAlloc = nullptr;
static RwImageFindFmtFn g_rwImageFindFmt = nullptr;
static RwRasterCreateFn g_rwRasterCreate = nullptr;
static RwRasterSetFromImageFn g_rwRasterSetFromImage = nullptr;
static RwTextureCreateFn g_rwTextureCreate = nullptr;
static RwImageDestroyFn g_rwImageDestroy = nullptr;
static RwRasterDestroyFn g_rwRasterDestroy = nullptr;

// ---- claxon por boton 7 directo (v1.8): CTouchInterface::m_pWidgets[7] ----
// IsTouchDown resulto ser indice de dedo, no de boton (v1.7). El claxon es el
// widget 7 (GTAForums: 7 = claxon/horn); aqui se lee directo del arreglo de
// botones del juego y se vuelca su memoria para encontrar su bandera de "tocado".
static void** g_widgets = nullptr;  // CTouchInterface::m_pWidgets
static const int kHornWidgetId = 7;  // WIDGET_HORN (GTAForums: 7 = claxon/horn)

// ---- claxon: doble toque al boton de claxon del juego abre/cierra la rueda ----
// v1.1 intento CPad::GetHorn, pero NO existe en libGTASA.so 2.10 (FALTA en el log).
// v1.2 uso una zona estimada (y 0.40-0.62H) y fallo: los toques reales de Daniel
// (log v1.3, toques #1-7) caen en (2258, 735) en 2408x1080, o sea (0.938W, 0.683H),
// justo DEBAJO de la zona estimada. v1.4: circulo centrado en ese punto real.
// v1.6 (boton "Calibrar claxon" retirado en v2.0: el claxon ya se detecta por el
// boton 7 del juego): el centro de la zona se guarda en horn_pos.txt y se sigue
// cargando al inicio por si se calibro antes; la zona solo es respaldo del boton 7.
static float g_hornCx = 0.938f, g_hornCy = 0.683f;  // centro del claxon (fraccion de W/H)
static void LoadHornPos() {
    // v2.1: horn_pos.txt tambien se busca primero en files del juego
    const std::string hpPaths[2] = {g_gameFilesDir + "/vhud/horn_pos.txt", "/storage/emulated/0/vhud/horn_pos.txt"};
    for (const std::string& hp : hpPaths) {
        FILE* f = fopen(hp.c_str(), "r");
        if (!f) continue;
        float x = 0, y = 0;
        if (fscanf(f, "%f %f", &x, &y) == 2 && x > 0.05f && x < 0.99f && y > 0.05f && y < 0.99f) { g_hornCx = x; g_hornCy = y; }
        fclose(f);
        return;
    }
}

// ---- ajustes de la rueda (v2.2): se guardan en data/configs/VHudRadio.ini (v2.36) ----
// [VHudRadio] fondo=30 (0..100, oscuridad del fondo) tamano=36 (25..45, % de H)
// anillo=0,210,255 (color del anillo de seleccion, 0..255)
// texto=255,255,255 (color del texto del centro de la rueda, 0..255) (v2.6)
// idioma=es (es/en/id/pt/fr/de) botonX/botonY=-1 (posicion del boton flotante,
//   fraccion de W/H; -1 = abajo a la derecha) botonTam=100 (50..150, %)
// botonOpac=100 (30..100, %) botonBloq=0 (0 = se puede mover, 1 = fijo) (v2.16)
// botonRuedaTam=100 (50..200, % tamano del boton de la rueda) botonRuedaOpac=100 (10..100, % opacidad del boton de la rueda) botonRuedaBloq=0 (0 = se puede mover, 1 = fijo) (v1.9.60)
// fondoColor=0,0,0 (color del fondo oscuro de la rueda) panelX/panelY=-1
//   (posicion del panel, fraccion de W/H; -1 = centrado) panelAncho=0.44
//   (ancho del panel, fraccion de W, 0.36..0.70) (v2.17)
// iconosColor=0 (0 = iconos normales de radio/, 1 = iconos a color de radio_color/) (v2.28)
struct WheelCfg { int fondo = 30; int tamano = 36; int ringR = 0; int ringG = 210; int ringB = 255; int textoR = 255; int textoG = 255; int textoB = 255; int idioma = 0; float btnX = -1.0f; float btnY = -1.0f; float btnRuedaX = -1.0f; float btnRuedaY = -1.0f; int btnTam = 100; int btnOpac = 100; int btnBloq = 0; int btnRuedaTam = 100; int btnRuedaOpac = 100; int btnRuedaBloq = 0; int fondoR = 0; int fondoG = 0; int fondoB = 0; float panelX = -1.0f; float panelY = -1.0f; float panelAncho = 0.44f; float panelAlto = -1.0f; int iconosColor = 0; int camaraLenta = 1; int botonVisible = 1; int mostrarGenero = 1; int ocultarNombre = 0; int popupIcono = 1; int popupTexto = 1; int popupTam = 100; int fondoIcono = 0; int fondoIconoR = 20; int fondoIconoG = 40; int fondoIconoB = 80; int animacion = 1; int blur = 0; int bloquearJuego = 1; int modoCambio = 0; };
static WheelCfg g_cfg;
static const char* S(int id) { int l = g_cfg.idioma; if (l < 0 || l >= L_COUNT) l = 0; if (id < 0 || id >= STR_COUNT) return ""; return kUI[l][id]; }  // v2.16: texto del panel en el idioma elegido
static const char* Genre(int id) { int l = g_cfg.idioma; if (l < 0 || l >= L_COUNT) l = 0; if (id < 1 || id > 13) return ""; return kGenre[l][id]; }  // v2.16: genero en el idioma elegido
// v2.53: ocultar la ventana de emisora del juego ("User tracks / Your own
// music" al cambiar). Se engancha CAERadioTrackManager::DisplayRadioStationName
// y, si el interruptor esta activo, no se llama al original (no se dibuja).
// Es solo dibujo: no se toca ningun estado de la radio. Va aqui (tras g_cfg)
// porque el hook lee g_cfg.ocultarNombre y antes no existia (error de compilacion).
static void (*DisplayRadioStationName_orig)(void* self) = nullptr;
// v1.9.73: aviso de emisora propio (icono arriba, nombre debajo). El juego
// dibuja su texto en DisplayRadioStationName y AdvancedSettings lo mueve
// arriba al centro (o pone su icono en vez del texto). VHudRadio dibuja su
// propio aviso en DrawFrame y suprime el del juego, para que el nombre no
// salga dos veces y para que haya icono+texto a la vez. g_advSettings dice
// si AdvancedSettings esta cargado (su texto va arriba al centro); se
// detecta al arrancar y se re-chequea en cada cambio de emisora, por si
// cargo despues que este mod.
static bool g_advSettings = false;
static std::chrono::steady_clock::time_point g_popupUntil = std::chrono::steady_clock::now();
static int g_popupLastStation = -1;
static void DisplayRadioStationName_hook(void* self) {
    if (g_cfg.ocultarNombre) return;
    if (g_cfg.popupIcono || g_cfg.popupTexto) return;  // v1.9.73: el aviso lo dibuja DrawFrame (asi AdvancedSettings tampoco dibuja el suyo: sus hooks estan dentro del original)
    if (DisplayRadioStationName_orig) DisplayRadioStationName_orig(self);
}
static bool g_settings = false;   // v2.2: panel de personalizacion abierto
static bool g_preview = false;    // v2.2: vista previa de la rueda (solo se ve, no cambia la estacion)
static bool g_trackList = false;  // v1.4: lista de canciones (User Tracks) abierta
static std::chrono::steady_clock::time_point g_listOpenedAt;  // v1.9.7: cuando se abrio la lista (el primer toque no cambia la cancion)
static float g_trackListRectX = -1.0f, g_trackListRectY = -1.0f, g_trackListRectW = 0.0f, g_trackListRectH = 0.0f;  // v1.4: rect real de la lista (para repartir los toques, como el panel)
static std::string CfgPath() { return g_gameDataDir + "/configs/VHudRadio.ini"; }  // v2.36: el .ini vuelve a data/configs (v2.3-2.15); la carpeta VHudRadio ya no existe
static void SaveCfg() {
    mkdir((g_gameDataDir + "/configs").c_str(), 0777);
    FILE* f = fopen(CfgPath().c_str(), "w");
    if (!f) { Log("  !! ajustes: no se pudo escribir %s (errno=%d)", CfgPath().c_str(), errno); return; }  // v1.8: avisar si no se crea (a Rehan no le regeneraba)
    fprintf(f, "[VHudRadio]\n");
    fprintf(f, "fondo=%d\n", g_cfg.fondo);
    fprintf(f, "tamano=%d\n", g_cfg.tamano);
    fprintf(f, "anillo=%d,%d,%d\n", g_cfg.ringR, g_cfg.ringG, g_cfg.ringB);
    fprintf(f, "texto=%d,%d,%d\n", g_cfg.textoR, g_cfg.textoG, g_cfg.textoB);
    fprintf(f, "idioma=%s\n", kLangCodes[(g_cfg.idioma >= 0 && g_cfg.idioma < L_COUNT) ? g_cfg.idioma : 0]);
    fprintf(f, "botonX=%.3f\n", g_cfg.btnX);
    fprintf(f, "botonY=%.3f\n", g_cfg.btnY);
    fprintf(f, "botonRuedaX=%.3f\n", g_cfg.btnRuedaX);  // v1.9.55: posicion propia del boton de la rueda (-1 = encima del flotante)
    fprintf(f, "botonRuedaY=%.3f\n", g_cfg.btnRuedaY);
    fprintf(f, "botonTam=%d\n", g_cfg.btnTam);
    fprintf(f, "botonOpac=%d\n", g_cfg.btnOpac);
    fprintf(f, "botonBloq=%d\n", g_cfg.btnBloq);
    fprintf(f, "botonRuedaTam=%d\n", g_cfg.btnRuedaTam);  // v1.9.60
    fprintf(f, "botonRuedaOpac=%d\n", g_cfg.btnRuedaOpac);
    fprintf(f, "botonRuedaBloq=%d\n", g_cfg.btnRuedaBloq);
    fprintf(f, "fondoColor=%d,%d,%d\n", g_cfg.fondoR, g_cfg.fondoG, g_cfg.fondoB);
    fprintf(f, "panelX=%.3f\n", g_cfg.panelX);
    fprintf(f, "panelY=%.3f\n", g_cfg.panelY);
    fprintf(f, "panelAncho=%.3f\n", g_cfg.panelAncho);
    fprintf(f, "panelAlto=%.3f\n", g_cfg.panelAlto);
    fprintf(f, "iconosColor=%d\n", g_cfg.iconosColor);
    fprintf(f, "camaraLenta=%d\n", g_cfg.camaraLenta);
    fprintf(f, "botonVisible=%d\n", g_cfg.botonVisible);
    fprintf(f, "mostrarGenero=%d\n", g_cfg.mostrarGenero);
    fprintf(f, "ocultarNombre=%d\n", g_cfg.ocultarNombre);
    fprintf(f, "popupIcono=%d\n", g_cfg.popupIcono);  // v1.9.73
    fprintf(f, "popupTexto=%d\n", g_cfg.popupTexto);  // v1.9.73
    fprintf(f, "popupTam=%d\n", g_cfg.popupTam);  // v1.9.74: tamano del aviso (50-200%)
    fprintf(f, "fondoIcono=%d\n", g_cfg.fondoIcono);
    fprintf(f, "fondoIconoColor=%d,%d,%d\n", g_cfg.fondoIconoR, g_cfg.fondoIconoG, g_cfg.fondoIconoB);
    fprintf(f, "animacion=%d\n", g_cfg.animacion);
    fprintf(f, "blur=%d\n", g_cfg.blur);
    fprintf(f, "bloquearJuego=%d\n", g_cfg.bloquearJuego);
    fprintf(f, "modoCambio=%d\n", g_cfg.modoCambio);
    fclose(f);
}
static void LoadCfg() {
    g_cfg.fondo = 30; g_cfg.tamano = 36; g_cfg.ringR = 0; g_cfg.ringG = 210; g_cfg.ringB = 255; g_cfg.textoR = 255; g_cfg.textoG = 255; g_cfg.textoB = 255; g_cfg.idioma = 0; g_cfg.btnX = -1.0f; g_cfg.btnY = -1.0f; g_cfg.btnRuedaX = -1.0f; g_cfg.btnRuedaY = -1.0f; g_cfg.btnTam = 100; g_cfg.btnOpac = 100; g_cfg.btnBloq = 0; g_cfg.btnRuedaTam = 100; g_cfg.btnRuedaOpac = 100; g_cfg.btnRuedaBloq = 0; g_cfg.fondoR = 0; g_cfg.fondoG = 0; g_cfg.fondoB = 0; g_cfg.panelX = -1.0f; g_cfg.panelY = -1.0f; g_cfg.panelAncho = 0.44f; g_cfg.panelAlto = -1.0f; g_cfg.iconosColor = 0; g_cfg.camaraLenta = 1; g_cfg.botonVisible = 1; g_cfg.mostrarGenero = 1; g_cfg.ocultarNombre = 0; g_cfg.popupIcono = 1; g_cfg.popupTexto = 1; g_cfg.popupTam = 100; g_cfg.fondoIcono = 0; g_cfg.fondoIconoR = 20; g_cfg.fondoIconoG = 40; g_cfg.fondoIconoB = 80; g_cfg.animacion = 1; g_cfg.blur = 0; g_cfg.bloquearJuego = 1; g_cfg.modoCambio = 0;
    bool fromOld = false;
    FILE* f = fopen(CfgPath().c_str(), "r");
    if (!f) {
        // v2.36: antes estaba en files/VHudRadio (v2.16-2.35) - mudarlo si existe ahi
        std::string old = g_gameFilesDir + "/VHudRadio/VHudRadio.ini";
        f = fopen(old.c_str(), "r");
        if (f) { fromOld = true; Log("  ajustes: mudando el .ini viejo de %s", old.c_str()); }
    }
    if (!f) {
        // v2.2: el primer .ini estaba en files/configs - mudarlo si existe ahi
        std::string old2 = g_gameFilesDir + "/configs/VHudRadio.ini";
        f = fopen(old2.c_str(), "r");
        if (f) { fromOld = true; Log("  ajustes: mudando el .ini viejo de %s", old2.c_str()); }
    }
    if (!f) { SaveCfg(); Log("  ajustes: creando %s con valores por defecto", CfgPath().c_str()); return; }  // no existe: crearlo con los valores por defecto
    char line[256];
    while (fgets(line, sizeof line, f)) {
        char* eq = strchr(line, '=');
        if (!eq) continue;
        *eq = 0;
        char* k = line;
        while (*k == ' ' || *k == '\t') k++;
        size_t kl = strlen(k);
        while (kl > 0 && (k[kl-1] == ' ' || k[kl-1] == '\t' || k[kl-1] == '\n' || k[kl-1] == '\r')) k[--kl] = 0;
        const char* v = eq + 1;
        if (strcmp(k, "fondo") == 0) g_cfg.fondo = atoi(v);
        else if (strcmp(k, "tamano") == 0) g_cfg.tamano = atoi(v);
        else if (strcmp(k, "anillo") == 0) {
            int r = 0, g = 0, b = 0;
            if (sscanf(v, "%d,%d,%d", &r, &g, &b) == 3) { g_cfg.ringR = r; g_cfg.ringG = g; g_cfg.ringB = b; }
        }
        else if (strcmp(k, "texto") == 0) {
            int r = 255, g = 255, b = 255;
            if (sscanf(v, "%d,%d,%d", &r, &g, &b) == 3) { g_cfg.textoR = r; g_cfg.textoG = g; g_cfg.textoB = b; }
        }
        else if (strcmp(k, "idioma") == 0) {
            g_cfg.idioma = 0;
            for (int i = 0; i < L_COUNT; i++) if (strncmp(v, kLangCodes[i], 2) == 0) { g_cfg.idioma = i; break; }
        }
        else if (strcmp(k, "botonX") == 0) g_cfg.btnX = (float)atof(v);
        else if (strcmp(k, "botonY") == 0) g_cfg.btnY = (float)atof(v);
        else if (strcmp(k, "botonRuedaX") == 0) g_cfg.btnRuedaX = (float)atof(v);  // v1.9.55: posicion propia del boton de la rueda
        else if (strcmp(k, "botonRuedaY") == 0) g_cfg.btnRuedaY = (float)atof(v);
        else if (strcmp(k, "botonTam") == 0) g_cfg.btnTam = atoi(v);
        else if (strcmp(k, "botonOpac") == 0) g_cfg.btnOpac = atoi(v);
        else if (strcmp(k, "botonBloq") == 0) g_cfg.btnBloq = atoi(v);
        else if (strcmp(k, "botonRuedaTam") == 0) g_cfg.btnRuedaTam = atoi(v);  // v1.9.60
        else if (strcmp(k, "botonRuedaOpac") == 0) g_cfg.btnRuedaOpac = atoi(v);
        else if (strcmp(k, "botonRuedaBloq") == 0) g_cfg.btnRuedaBloq = atoi(v);
        else if (strcmp(k, "fondoColor") == 0) {
            int r = 0, g = 0, b = 0;
            if (sscanf(v, "%d,%d,%d", &r, &g, &b) == 3) { g_cfg.fondoR = r; g_cfg.fondoG = g; g_cfg.fondoB = b; }
        }
        else if (strcmp(k, "panelX") == 0) g_cfg.panelX = (float)atof(v);
        else if (strcmp(k, "panelY") == 0) g_cfg.panelY = (float)atof(v);
        else if (strcmp(k, "panelAncho") == 0) g_cfg.panelAncho = (float)atof(v);
        else if (strcmp(k, "panelAlto") == 0) g_cfg.panelAlto = (float)atof(v);
        else if (strcmp(k, "iconosColor") == 0) g_cfg.iconosColor = atoi(v);
        else if (strcmp(k, "camaraLenta") == 0) g_cfg.camaraLenta = atoi(v);
        else if (strcmp(k, "botonVisible") == 0) g_cfg.botonVisible = atoi(v);
        else if (strcmp(k, "mostrarGenero") == 0) g_cfg.mostrarGenero = atoi(v);
        else if (strcmp(k, "ocultarNombre") == 0) g_cfg.ocultarNombre = atoi(v);
        else if (strcmp(k, "popupIcono") == 0) g_cfg.popupIcono = atoi(v);  // v1.9.73
        else if (strcmp(k, "popupTexto") == 0) g_cfg.popupTexto = atoi(v);  // v1.9.73
        else if (strcmp(k, "popupTam") == 0) g_cfg.popupTam = atoi(v);  // v1.9.74
        else if (strcmp(k, "fondoIcono") == 0) g_cfg.fondoIcono = atoi(v);
        else if (strcmp(k, "fondoIconoColor") == 0) {
            int r = 20, g = 40, b = 80;
            if (sscanf(v, "%d,%d,%d", &r, &g, &b) == 3) { g_cfg.fondoIconoR = r; g_cfg.fondoIconoG = g; g_cfg.fondoIconoB = b; }
        }
        else if (strcmp(k, "animacion") == 0) g_cfg.animacion = atoi(v);
        else if (strcmp(k, "blur") == 0) g_cfg.blur = atoi(v);
        else if (strcmp(k, "bloquearJuego") == 0) g_cfg.bloquearJuego = atoi(v);
        else if (strcmp(k, "modoCambio") == 0) g_cfg.modoCambio = atoi(v);
    }
    fclose(f);
    if (g_cfg.fondo < 0) g_cfg.fondo = 0; if (g_cfg.fondo > 100) g_cfg.fondo = 100;
    if (g_cfg.tamano < 25) g_cfg.tamano = 25; if (g_cfg.tamano > 45) g_cfg.tamano = 45;
    if (g_cfg.ringR < 0) g_cfg.ringR = 0; if (g_cfg.ringR > 255) g_cfg.ringR = 255;
    if (g_cfg.ringG < 0) g_cfg.ringG = 0; if (g_cfg.ringG > 255) g_cfg.ringG = 255;
    if (g_cfg.ringB < 0) g_cfg.ringB = 0; if (g_cfg.ringB > 255) g_cfg.ringB = 255;
    if (g_cfg.textoR < 0) g_cfg.textoR = 0; if (g_cfg.textoR > 255) g_cfg.textoR = 255;
    if (g_cfg.textoG < 0) g_cfg.textoG = 0; if (g_cfg.textoG > 255) g_cfg.textoG = 255;
    if (g_cfg.textoB < 0) g_cfg.textoB = 0; if (g_cfg.textoB > 255) g_cfg.textoB = 255;
    if (g_cfg.idioma < 0) g_cfg.idioma = 0; if (g_cfg.idioma >= L_COUNT) g_cfg.idioma = 0;
    if (g_cfg.btnX < -1.0f) g_cfg.btnX = -1.0f; if (g_cfg.btnX > 1.0f) g_cfg.btnX = 1.0f;
    if (g_cfg.btnY < -1.0f) g_cfg.btnY = -1.0f; if (g_cfg.btnY > 1.0f) g_cfg.btnY = 1.0f;
    if (g_cfg.btnRuedaX < -1.0f) g_cfg.btnRuedaX = -1.0f; if (g_cfg.btnRuedaX > 1.0f) g_cfg.btnRuedaX = 1.0f;  // v1.9.55
    if (g_cfg.btnRuedaY < -1.0f) g_cfg.btnRuedaY = -1.0f; if (g_cfg.btnRuedaY > 1.0f) g_cfg.btnRuedaY = 1.0f;
    if (g_cfg.btnTam < 50) g_cfg.btnTam = 50; if (g_cfg.btnTam > 150) g_cfg.btnTam = 150;
    if (g_cfg.btnOpac < 30) g_cfg.btnOpac = 30; if (g_cfg.btnOpac > 100) g_cfg.btnOpac = 100;
    if (g_cfg.btnBloq != 0 && g_cfg.btnBloq != 1) g_cfg.btnBloq = 0;
    if (g_cfg.btnRuedaTam < 50) g_cfg.btnRuedaTam = 50; if (g_cfg.btnRuedaTam > 200) g_cfg.btnRuedaTam = 200;  // v1.9.60
    if (g_cfg.btnRuedaOpac < 10) g_cfg.btnRuedaOpac = 10; if (g_cfg.btnRuedaOpac > 100) g_cfg.btnRuedaOpac = 100;
    if (g_cfg.btnRuedaBloq != 0 && g_cfg.btnRuedaBloq != 1) g_cfg.btnRuedaBloq = 0;
    if (g_cfg.fondoR < 0) g_cfg.fondoR = 0; if (g_cfg.fondoR > 255) g_cfg.fondoR = 255;
    if (g_cfg.fondoG < 0) g_cfg.fondoG = 0; if (g_cfg.fondoG > 255) g_cfg.fondoG = 255;
    if (g_cfg.fondoB < 0) g_cfg.fondoB = 0; if (g_cfg.fondoB > 255) g_cfg.fondoB = 255;
    if (g_cfg.panelX < -1.0f) g_cfg.panelX = -1.0f; if (g_cfg.panelX > 1.0f) g_cfg.panelX = 1.0f;
    if (g_cfg.panelY < -1.0f) g_cfg.panelY = -1.0f; if (g_cfg.panelY > 1.0f) g_cfg.panelY = 1.0f;
    if (g_cfg.panelAncho < 0.36f) g_cfg.panelAncho = 0.36f; if (g_cfg.panelAncho > 0.70f) g_cfg.panelAncho = 0.70f;
    if (g_cfg.panelAlto < -1.0f) g_cfg.panelAlto = -1.0f; if (g_cfg.panelAlto >= 0.0f && g_cfg.panelAlto < 0.30f) g_cfg.panelAlto = 0.30f; if (g_cfg.panelAlto > 0.95f) g_cfg.panelAlto = 0.95f;
    if (g_cfg.iconosColor != 0 && g_cfg.iconosColor != 1) g_cfg.iconosColor = 0;
    if (g_cfg.camaraLenta != 0 && g_cfg.camaraLenta != 1) g_cfg.camaraLenta = 1;
    if (g_cfg.botonVisible != 0 && g_cfg.botonVisible != 1) g_cfg.botonVisible = 1;
    if (g_cfg.mostrarGenero != 0 && g_cfg.mostrarGenero != 1) g_cfg.mostrarGenero = 1;
    if (g_cfg.ocultarNombre != 0 && g_cfg.ocultarNombre != 1) g_cfg.ocultarNombre = 0;
    if (g_cfg.popupIcono != 0 && g_cfg.popupIcono != 1) g_cfg.popupIcono = 1;  // v1.9.73
    if (g_cfg.popupTexto != 0 && g_cfg.popupTexto != 1) g_cfg.popupTexto = 1;  // v1.9.73
    if (g_cfg.popupTam < 50 || g_cfg.popupTam > 200) g_cfg.popupTam = 100;  // v1.9.74
    if (g_cfg.fondoIcono != 0 && g_cfg.fondoIcono != 1) g_cfg.fondoIcono = 0;
    if (g_cfg.fondoIconoR < 0) g_cfg.fondoIconoR = 0; if (g_cfg.fondoIconoR > 255) g_cfg.fondoIconoR = 255;
    if (g_cfg.fondoIconoG < 0) g_cfg.fondoIconoG = 0; if (g_cfg.fondoIconoG > 255) g_cfg.fondoIconoG = 255;
    if (g_cfg.fondoIconoB < 0) g_cfg.fondoIconoB = 0; if (g_cfg.fondoIconoB > 255) g_cfg.fondoIconoB = 255;
    if (g_cfg.animacion != 0 && g_cfg.animacion != 1) g_cfg.animacion = 1;
    if (g_cfg.blur < 0) g_cfg.blur = 0; if (g_cfg.blur > 100) g_cfg.blur = 100;
    if (g_cfg.bloquearJuego != 0 && g_cfg.bloquearJuego != 1) g_cfg.bloquearJuego = 1;
    if (g_cfg.modoCambio < 0 || g_cfg.modoCambio > 2) g_cfg.modoCambio = 0;
    // v2.20: el tamano por defecto vuelve a ser mediano (0.44 x automatico);
    // un .ini con los valores grandes de la v2.19 (0.60 x 0.90, nunca se
    // redimensiono o se restablecio en la v2.19) vuelve al mediano
    if (g_cfg.panelAncho == 0.60f && g_cfg.panelAlto == 0.90f) { g_cfg.panelAncho = 0.44f; g_cfg.panelAlto = -1.0f; }
    if (fromOld) SaveCfg();  // escribirlo ya en la ruta nueva
    Log("  ajustes: %s (fondo=%d tamano=%d anillo=%d,%d,%d texto=%d,%d,%d idioma=%s boton=%.2f,%.2f tam=%d%% opac=%d%% bloq=%d fondoColor=%d,%d,%d panel=%.2f,%.2f ancho=%.2f alto=%.2f color=%d lenta=%d btnVis=%d blur=%d%%)", CfgPath().c_str(), g_cfg.fondo, g_cfg.tamano, g_cfg.ringR, g_cfg.ringG, g_cfg.ringB, g_cfg.textoR, g_cfg.textoG, g_cfg.textoB, kLangCodes[g_cfg.idioma], g_cfg.btnX, g_cfg.btnY, g_cfg.btnTam, g_cfg.btnOpac, g_cfg.btnBloq, g_cfg.fondoR, g_cfg.fondoG, g_cfg.fondoB, g_cfg.panelX, g_cfg.panelY, g_cfg.panelAncho, g_cfg.panelAlto, g_cfg.iconosColor, g_cfg.camaraLenta, g_cfg.botonVisible, g_cfg.blur);
}

static std::string g_vhudDir;   // /storage/emulated/0/vhud
static int g_current = 1;       // estacion actual conocida (1..13)
static std::chrono::steady_clock::time_point g_lastStationCmd;  // v1.2: ultimo cambio pedido (para no fiarse de la lectura nativa vieja)
static int g_selected = 1;      // segmento bajo el dedo mientras la rueda esta abierta
static bool g_open = false;
static std::chrono::steady_clock::time_point g_wheelAnimStart = std::chrono::steady_clock::now();  // v2.54: cuando se abrio la rueda (para la animacion)
static bool g_wheelClosing = false;  // v1.9.55: la rueda se esta cerrando con animacion (encoge y se funde)
static std::chrono::steady_clock::time_point g_wheelCloseStart = std::chrono::steady_clock::now();  // v1.9.55: cuando empezo a cerrarse
static bool g_btnPending = false;  // v2.1: un toque al boton flotante esperando (siguiente estacion si no hay segundo toque)
static std::chrono::steady_clock::time_point g_btnPendingAt;
static bool g_btnDrag = false;   // v2.16: el toque actual empezo en el boton flotante (puede ser arrastre)
static bool g_btnMoved = false;  // v2.16: el arrastre supero el umbral (ya no es un toque)
static bool g_wheelBtnDown = false;  // v1.9.54: el toque actual empezo en el boton propio de la rueda
static float g_wheelBtnSX = 0.0f, g_wheelBtnSY = 0.0f;  // v1.9.54: posicion inicial del toque en el boton de la rueda
static bool g_wheelBtnMoved = false;  // v1.9.55: el arrastre del boton de la rueda supero el umbral (ya no es un toque)
static float g_wheelDragBX = 0.0f, g_wheelDragBY = 0.0f;  // v1.9.55: posicion base (fraccion) del boton de la rueda al empezar el arrastre
static float g_dragSX = 0, g_dragSY = 0;  // v2.16: donde empezo el dedo
static float g_dragBX = 0, g_dragBY = 0;  // v2.16: posicion del boton al empezar (fraccion de W/H)
static bool g_wheelTouch = false;   // v1.5: el toque actual empezo/sobre el anillo de la rueda
static bool g_wheelCenterTap = false;  // v1.9.71: el toque actual empezo en el centro de la rueda (es la unica forma de cerrarla)
static bool g_listBtnTap = false;   // v1.4: el toque actual empezo en el boton "Lista de canciones" (abre la lista al soltar)
static bool g_ignoreNextUp = false; // v1.5: el toque que ABRIO la rueda no debe cerrarla al soltar
static float g_touchX = -1.0f, g_touchY = -1.0f;  // v2.18: ultima posicion de toque (io.MousePos llega con un frame de retraso y el bajar leia una posicion vieja)
static bool g_panelScrolling = false;  // v2.20: el arrastre actual es un desplazamiento del panel (no un deslizador)
static bool g_panelJustOpened = false;  // v2.48: el panel se abrio en este cuadro (para descartar el estado de arrastre viejo)
static float g_panelRectX = -1.0f, g_panelRectY = -1.0f, g_panelRectW = 0.0f, g_panelRectH = 0.0f;  // v2.49: rect real del panel en pantalla (lo escribe DrawSettings)
static bool g_touchForPanel = false;  // v2.49: el toque actual (dedo 0) pertenece al panel

// v2.2: geometria de la rueda segun los ajustes (fondo/tamano/anillo)
static float WheelR(float H) { return H * g_cfg.tamano / 100.0f; }
static float WheelRi(float H) { return WheelR(H) * 0.83f; }
static float WheelIconS(float H) { return WheelR(H) * 0.175f; }
static ImU32 RingColor(float anim = 1.0f) { return IM_COL32(g_cfg.ringR, g_cfg.ringG, g_cfg.ringB, (int)(255 * anim)); }  // v1.9.60: anim para que el anillo se cierre a la vez que todo

// v2.3: segmento bajo un punto (13 segmentos; apagar va arriba del anillo, no en el centro)
static int SegmentAt(float x, float y, float W, float H) {
    float dx = x - W*0.5f, dy = y - H*0.5f;
    float dist = sqrtf(dx*dx + dy*dy);
    float Ri = WheelRi(H), s = WheelIconS(H);
    // v1.9.47: banda exterior mas generosa (1.6x en vez de 1.3x): a veces el
    // toque cae justo fuera del anillo y la rueda no lo detectaba.
    if (dist < Ri - s*1.3f || dist > Ri + s*1.6f) return 0;  // centro o fuera de la rueda
    float ang = atan2f(dy, dx) + 3.14159265f/2; if (ang < 0) ang += 2*3.14159265f;
    int seg = (int)(ang / (2*3.14159265f/13.0f)) + 1; if (seg > 13) seg = 13;
    return seg;
}
// v1.9.71: el centro de la rueda (el agujero interior del anillo). SegmentAt
// devuelve 0 tanto para el centro como para fuera del anillo; para cerrar
// SOLO desde el centro hay que distinguirlos (Daniel: la rueda se seguia
// cerrando al tocar fuera del anillo).
static bool CenterAt(float x, float y, float W, float H) {
    float dx = x - W*0.5f, dy = y - H*0.5f;
    float dist = sqrtf(dx*dx + dy*dy);
    float Ri = WheelRi(H), s = WheelIconS(H);
    return dist < Ri - s*1.3f;
}
static bool g_inVehicle = false;
static bool g_hasRadio = true;  // v2.14: el vehiculo actual tiene radio civil (RadioType == 0); true hasta que se compruebe
static void* g_menuManager = nullptr;  // v2.3: global CMenuManager (FrontEndMenuManager)
static bool g_paused = false;          // v2.3: menu de pausa (o cualquier menu) abierto
static int g_menuCheck = 0;            // v2.4: 0 = sin verificar, 1 = offset valido, -1 = offset no valido (pausa desactivada)
static bool MenuActive() {
    if (!g_menuManager || g_menuCheck == -1) return false;
    return ((const unsigned char*)g_menuManager)[0x5C] != 0;  // m_bMenuActive (plugin-sdk CMenuManager)
}
// v2.6: mas senales de pausa (el byte 0x5C solo no basto en la v2.5) y ralentizacion
static bool* g_userPause = nullptr;   // CTimer::m_UserPause (pausa del usuario)
static bool* g_codePause = nullptr;   // CTimer::m_CodePause (pausa por codigo/cinematica)
static bool g_menuDrawn = false;      // CMenuManager::DrawFrontEnd se llamo en este frame
static bool* g_slowActive = nullptr;  // CTimer::bSlowMotionActive (slow-motion propio del juego)
static float* g_slowScale = nullptr;  // CTimer::ms_fSlowMotionScale
static float* g_timeScale = nullptr;  // CTimer::ms_fTimeScale (respaldo si no hay slow-motion)
static bool* g_loadingActive = nullptr;  // v2.48: CLoadingScreen::m_bActive (true mientras carga la partida)
// v1: reproductor de User Tracks propio (minimp3 + OpenSL). Se incluye aqui
// porque usa las globales de arriba (g_startTrack, g_userPause, g_inVehicle...).
#include "UserTracks.h"
static void (*DrawFrontEnd_orig)() = nullptr;
static void DrawFrontEnd_hook() {
    g_menuDrawn = true;
    // v1.9.32: EMISORA se lee en vivo en SettingSelection::Render (libGTASA+0x361d38),
    // dentro de DrawFrontEnd -> DrawBackground -> DrawStandardMenus -> SelectScreen::Render.
    // Escribir MS+0x240/MS+0x244 AQUI, antes del original, deja el valor correcto en el
    // momento exacto del dibujado. La escritura de UT_DiagTick (tras el swap, 1.9.29)
    // llega un cuadro tarde y algo la revierte a la guardada antes del siguiente Render.
    // v1.9.30 usaba ut_inAudioMenu como condicion, pero ese flag solo es true con el
    // dial (RadioSelection) abierto; la fila EMISORA esta en la pantalla principal de
    // Configuracion del sonido (AudioScreen), donde ut_inAudioMenu es false y la
    // escritura nunca corria (detectado por Gemini, via Daniel, 2026-11-21).
    // v1.9.43: escritura INCONDICIONAL. La 1.9.42 engancho RadioSelection::Render
    // pero el icono siguio en K-DST: el resaltado se dibuja antes de que ese
    // hook corra en el cuadro, o lo dibuja otra funcion. Escribir MS+0x240
    // AQUI, al inicio de DrawFrontEnd (antes de que se dibuje TODO el menu:
    // tira de iconos, fila EMISORA, dial), deja el valor correcto para todos
    // los elementos del menu en el mismo cuadro. La condicion g_paused era
    // el problema antes (nunca se vio la linea de confirmacion en el log);
    // escribir g_current es inocuo fuera del menu (es la emisora actual).
    if (g_current >= 1 && g_current <= 13) {
        static bool s_dfeLogged = false;
        if (!s_dfeLogged) { s_dfeLogged = true; Log("UT: DrawFrontEnd: escritura EMISORA activa (menu)"); }
        if (g_menuManager) {
            unsigned char* mm = (unsigned char*)g_menuManager;
            if (mm[0x35] != (unsigned char)g_current) {
                unsigned char old = mm[0x35];
                mm[0x35] = (unsigned char)g_current;
                Log("UT: DrawFrontEnd EMISORA %d -> %d (FrontEndMenuManager+0x35)", (int)old, g_current);
            }
        }
        if (ut_mobileSettings) {
            int* msCur = (int*)((char*)ut_mobileSettings + 0x240);
            int* msDef = (int*)((char*)ut_mobileSettings + 0x244);
            if (*msCur != g_current) {
                int old = *msCur;
                *msCur = g_current;
                Log("UT: DrawFrontEnd EMISORA %d -> %d (MobileSettings+0x240)", old, g_current);
            }
            if (*msDef != g_current) *msDef = g_current;
        }
    }
    if (DrawFrontEnd_orig) DrawFrontEnd_orig();
}
// v2.6: diagnostico - registra que bytes del objeto CMenuManager cambian, para
// encontrar el byte real de pausa en este telefono (el 0x5C no basto en la v2.5)
// v2.51: MenuSnapCheck eliminado (el byte de menu 0x5C ya se conoce; era solo diagnostico).
// v2.4: FrontEndMenuManager es una REFERENCIA (puntero al objeto) en plugin-sdk:
// GetSym devuelve la direccion del puntero, hay que desreferenciarla. Si el
// simbolo ya es el objeto, sus primeros 8 bytes no son una direccion valida
// del juego (son ~0 o un float desplazado), asi que se distingue solo.
static void* ResolveMenuManager(void* sym) {
    if (!sym) return nullptr;
    uintptr_t v = *(uintptr_t*)sym;
    if (v > 0x100000000ULL && v < 0x1000000000000ULL) return (void*)v;  // es una referencia: desreferenciar
    return sym;  // el simbolo ya es el objeto
}

// ---- texturas ImGui de los iconos (se crean en el primer frame, con contexto GL) ----
static GLuint g_tex[14] = {0};  // 1..13 + radio_current en [0] como marco
static GLuint g_texColor[14] = {0};  // v2.28: a color (radio_color/), 1..13 + radio_current en [0]
// v1.9.54: icono del boton de la rueda (rueda con nota, 128x128, fondo
// transparente). PNG incrustado en base64 (de la imagen que mando Daniel).
static const char kWheelIconB64[] =
    "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"
    "BfT/ARHK4jZPsuvzAAAAAElFTkSuQmCC";
static GLuint g_texWheel = 0;  // textura GL del icono del boton de la rueda
static bool g_texTried = false;
// v2.29: decodifica base64 (los PNG incrustados vienen en base64)
static int B64Val(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}
static std::vector<unsigned char> B64Decode(const char* s) {
    std::vector<unsigned char> out;
    int val = 0, bits = 0;
    for (const char* p = s; *p; p++) {
        int v = B64Val(*p);
        if (v < 0) continue;  // ignora '=' y saltos
        val = (val << 6) | v;
        bits += 6;
        if (bits >= 8) { bits -= 8; out.push_back((unsigned char)((val >> bits) & 0xFF)); }
    }
    return out;
}
// v1.9.70: lineArt compartido (antes habia dos copias: LoadPngFromMemory
// para el boton GL y NativeWheelTextureFromFile para el nativo). Si la
// imagen ya tiene transparencia se usa tal cual (1.9.58). Si es opaca, se
// quita el fondo con flood-fill desde las esquinas: el fondo es el color
// de la esquina (blanco, negro o de color, tolerancia 24 por canal), no
// solo blanco >240 (1.9.58 dejaba el cuadrado en un icono opaco con fondo
// oscuro). Y el disco gris solo se quita si el pixel es GRIS (canales
// parecidos, tono medio 96-240): antes se quitaba cualquier pixel de tono
// medio y un icono de color quedaba comido.
static void ApplyWheelLineArt(unsigned char* px, int w, int h) {
    if (!px || w <= 0 || h <= 0) return;
    bool hasAlpha = false;
    for (int i = 0; i < w * h; i++) if (px[i * 4 + 3] < 255) { hasAlpha = true; break; }
    if (hasAlpha) return;  // ya tiene transparencia: se usa tal cual
    std::vector<char> seen((size_t)w * h, 0);
    std::vector<int> stack;
    auto closeTo = [&](int x, int y, int r, int g, int b) {
        unsigned char* p = px + (y * w + x) * 4;
        int dr = (int)p[0] - r, dg = (int)p[1] - g, db = (int)p[2] - b;
        if (dr < 0) dr = -dr; if (dg < 0) dg = -dg; if (db < 0) db = -db;
        return dr <= 24 && dg <= 24 && db <= 24;
    };
    const int sx[4] = {0, w - 1, 0, w - 1};
    const int sy[4] = {0, 0, h - 1, h - 1};
    for (int k = 0; k < 4; k++) {
        int x0 = sx[k], y0 = sy[k];
        unsigned char* c = px + (y0 * w + x0) * 4;
        int sr = c[0], sg = c[1], sb = c[2];
        if (seen[y0 * w + x0]) continue;
        seen[y0 * w + x0] = 1; stack.push_back(y0 * w + x0);
        while (!stack.empty()) {
            int idx = stack.back(); stack.pop_back();
            px[idx * 4 + 3] = 0;
            int x = idx % w, y = idx / w;
            const int dx[4] = {1, -1, 0, 0};
            const int dy[4] = {0, 0, 1, -1};
            for (int d = 0; d < 4; d++) {
                int nx = x + dx[d], ny = y + dy[d];
                if (nx >= 0 && nx < w && ny >= 0 && ny < h) {
                    int nidx = ny * w + nx;
                    if (!seen[nidx] && closeTo(nx, ny, sr, sg, sb)) { seen[nidx] = 1; stack.push_back(nidx); }
                }
            }
        }
    }
    // Quitar el disco gris: solo los pixeles grises (canales parecidos)
    // de tono medio 96-240 se vuelven transparentes; quedan los segmentos
    // y la nota en blanco y los contornos negros, sobre transparente.
    for (int i = 0; i < w * h; i++) {
        unsigned char* p = px + i * 4;
        if (p[3] == 0) continue;
        int mx = p[0] > p[1] ? (p[0] > p[2] ? p[0] : p[2]) : (p[1] > p[2] ? p[1] : p[2]);
        int mn = p[0] < p[1] ? (p[0] < p[2] ? p[0] : p[2]) : (p[1] < p[2] ? p[1] : p[2]);
        if (mx >= 96 && mx <= 240 && (mx - mn) <= 24) p[3] = 0;
    }
}
static GLuint LoadPngFromMemory(const unsigned char* buf, size_t len, bool blackKey = true, bool lineArt = false) {  // v1.9.14: blackKey=false deja el icono opaco (8 y 12 a color)
    int w = 0, h = 0, ch = 0;
    unsigned char* px = stbi_load_from_memory(buf, (int)len, &w, &h, &ch, 4);
    if (!px || w <= 0 || h <= 0) { if (px) stbi_image_free(px); return 0; }
    if (lineArt) ApplyWheelLineArt(px, w, h);
    // v2.35: los iconos a color son de 256x256 y se dibujan a ~94 px sin
    // mipmaps; achicarlos a 128x128 (como los normales) antes de subirlos.
    unsigned char* pxSmall = nullptr;
    unsigned char* upPx = px;
    int upW = w, upH = h;
    if (w > 128 || h > 128) {
        int nw = w > 128 ? 128 : w, nh = h > 128 ? 128 : h;
        pxSmall = (unsigned char*)malloc((size_t)nw * nh * 4);
        if (pxSmall) {
            for (int y = 0; y < nh; y++) {
                for (int x = 0; x < nw; x++) {
                    int sx = x * w / nw, sy = y * h / nh;
                    int ex = (x + 1) * w / nw, ey = (y + 1) * h / nh;
                    if (ex <= sx) ex = sx + 1; if (ey <= sy) ey = sy + 1;
                    for (int c = 0; c < 4; c++) {
                        int sum = 0, cnt = 0;
                        for (int yy = sy; yy < ey && yy < h; yy++)
                            for (int xx = sx; xx < ex && xx < w; xx++) { sum += px[(yy * w + xx) * 4 + c]; cnt++; }
                        pxSmall[(y * nw + x) * 4 + c] = (unsigned char)(cnt ? sum / cnt : 0);
                    }
                }
            }
            upPx = pxSmall; upW = nw; upH = nh;
        }
    }
    // v2.54: quitar el fondo negro opaco de los iconos (los PNG de V-HUD y
    // slotrad traen el logo sobre negro opaco, no transparente: el normal
    // tiene ~4670 px negros puros y el de color ~28085). Si no, el circulo de
    // color de fondoIcono queda tapado por el negro y solo se ve por los
    // bordes. Se vuelve transparente todo pixel opaco casi negro (max canal
    // < 32); el logo es claro/de color y no se toca (hay un hueco claro entre
    // el negro puro y el logo). Se hace aqui (tras achicar) para que el borde
    // del achicado tambien se limpie. No toca radio_current (sin negros).
    // v2.55: SOLO cuando fondoIcono esta activado; apagado, los iconos
    // vuelven a su fondo negro normal de siempre (al cambiar el interruptor
    // se recargan las texturas con ReloadIcons).
    if (g_cfg.fondoIcono && blackKey) for (int i = 0; i < upW * upH; i++) {
        unsigned char* p = upPx + i * 4;
        if (p[3] > 0 && p[0] < 32 && p[1] < 32 && p[2] < 32) p[3] = 0;
    }
    GLint last = 0; glGetIntegerv(GL_TEXTURE_BINDING_2D, &last);
    GLuint tex = 0; glGenTextures(1, &tex); glBindTexture(GL_TEXTURE_2D, tex);
    // Mipmaps solo si el tamano es potencia de 2 (en ES 2.0 no hay mipmaps
    // para tamanos no potencia de 2; en ES 3.x si, pero asi no se rompe nada).
    bool pot = (upW & (upW - 1)) == 0 && (upH & (upH - 1)) == 0;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, pot ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, upW, upH, 0, GL_RGBA, GL_UNSIGNED_BYTE, upPx);
    if (pot) glGenerateMipmap(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, (GLuint)last);
    if (pxSmall) free(pxSmall);
    stbi_image_free(px);
    return glGetError() == GL_NO_ERROR ? tex : 0;
}
// v2.55: un icono: SOLO el PNG incrustado en el .so (se quitaron los iconos
// personalizados de carpeta por peticion de Daniel). srcOut dice de donde salio.
static GLuint LoadIconFileOrEmbedded(int set, int idx, const char** srcOut) {
    GLuint t = 0;
    const char* b64 = EmbeddedIconB64(set, idx);
    if (b64) {
        std::vector<unsigned char> raw = B64Decode(b64);
        if (!raw.empty()) t = LoadPngFromMemory(raw.data(), raw.size(), !(set == 1 && (idx == 8 || idx == 12)));  // v1.9.14: CSR y User Track a color quedan opacos (su centro negro/morado no es transparente)
    }
    if (t) { if (srcOut) *srcOut = "en el .so"; return t; }
    if (srcOut) *srcOut = "-";
    return 0;
}
// v2.55: carga un juego de iconos SOLO del .so (incrustados). La carpeta de
// iconos personalizados se quito por peticion de Daniel.
static void LoadIconSet(GLuint* tex, const char* etiqueta, int set) {
    Log("iconos %s: incrustados en el .so", etiqueta);
    int ok = 0;
    for (int i = 1; i <= 13; i++) {
        const char* src = "-";
        tex[i] = LoadIconFileOrEmbedded(set, i, &src);
        if (tex[i]) ok++;
        Log("  %s %s radio_%d.png (%s)", etiqueta, tex[i] ? "ok    " : "FALTA ", i, src);
    }
    const char* src0 = "-";
    tex[0] = LoadIconFileOrEmbedded(set, 0, &src0);
    Log("  %s %s radio_current.png (%s)", etiqueta, tex[0] ? "ok    " : "FALTA ", src0);
    Log("iconos %s cargados: %d/13 (+marco %s)", etiqueta, ok, tex[0] ? "si" : "no");
}
// v1.9.55: icono propio del boton de la rueda desde carpeta (personalizable):
// si hay un PNG en GTAVRadioMusic/boton/ (en cualquiera de las carpetas de
// musica), ese PNG REEMPLAZA al icono incrustado del .so; si no hay ninguno,
// se usa el incrustado. Busca "boton.png" primero y si no, el primer *.png
// en orden alfabetico. El PNG debe tener fondo transparente.
static GLuint LoadCustomWheelIcon() {
    // v1.9.56: construir las carpetas de musica ANTES de buscar. En la
    // 1.9.55 UT_MUSIC_DIRS aun estaba vacio cuando LoadRadioTextures corria
    // (el reproductor todavia no habia escaneado musica, Daniel iba en la
    // emisora 2), asi que buscaba en "/boton" y nunca encontraba el PNG.
    UT_BuildMusicDirs();
    // v1.9.56: la carpeta boton/ se crea sola (Daniel: "Haz que la carpeta
    // se cree sola"): en la primera carpeta de musica que exista, o en la
    // principal si ninguna existe todavia.
    {
        std::string target;
        for (int d = 0; d < UT_NUM_MUSIC_DIRS; d++) {
            struct stat st;
            if (stat(UT_MUSIC_DIRS[d], &st) == 0 && S_ISDIR(st.st_mode)) { target = UT_MUSIC_DIRS[d]; break; }
        }
        if (target.empty()) target = UT_MUSIC_DIRS[0];
        std::string botonDir = target + "/boton";
        struct stat bst;
        if (stat(botonDir.c_str(), &bst) != 0) {
            if (mkdir(botonDir.c_str(), 0777) == 0) Log("  boton rueda: carpeta creada %s", botonDir.c_str());
        }
    }
    for (int d = 0; d < UT_NUM_MUSIC_DIRS; d++) {
        std::string dir = std::string(UT_MUSIC_DIRS[d]) + "/boton";
        std::string cand;
        // 1) boton.png exacto
        {
            std::string p = dir + "/boton.png";
            FILE* f = fopen(p.c_str(), "rb");
            if (f) { fclose(f); cand = p; }
        }
        // 2) si no, el primer *.png alfabetico de la carpeta
        if (cand.empty()) {
            DIR* dp = opendir(dir.c_str());
            if (dp) {
                std::string best;
                struct dirent* ent;
                while ((ent = readdir(dp)) != nullptr) {
                    std::string n = ent->d_name;
                    if (n.size() > 4 && n.substr(n.size() - 4) == ".png") {
                        if (best.empty() || n < best) best = n;
                    }
                }
                closedir(dp);
                if (!best.empty()) cand = dir + "/" + best;
            }
        }
        if (cand.empty()) continue;
        FILE* f = fopen(cand.c_str(), "rb");
        if (!f) continue;
        fseek(f, 0, SEEK_END);
        long len = ftell(f);
        fseek(f, 0, SEEK_SET);
        if (len <= 0 || len > 8 * 1024 * 1024) { fclose(f); continue; }
        std::vector<unsigned char> buf((size_t)len);
        size_t rd = fread(buf.data(), 1, buf.size(), f);
        fclose(f);
        if (rd != buf.size()) continue;
        GLuint t = LoadPngFromMemory(buf.data(), buf.size(), false, true);  // v1.9.56: lineArt=true (si es opaco, extrae el arte negro como blanco sobre transparente)
        if (t) { Log("  boton rueda: icono propio %s (%ld bytes)", cand.c_str(), len); return t; }
        Log("  boton rueda: no se pudo decodificar %s (se usa el incrustado)", cand.c_str());
        return 0;
    }
    return 0;
}
static void LoadRadioTextures() {
    if (g_texTried) return; g_texTried = true;
    // v2.16: iconos normales en files del juego (Android/data/.../files/VHudRadio/radio/);
    // v2.55: solo incrustados en el .so (sin carpeta).
    LoadIconSet(g_tex, "normales", 0);
    // v2.28: iconos a color (incrustados); si a alguno le falta, se usa el
    // normal de esa emisora para que la rueda no se rompa.
    LoadIconSet(g_texColor, "a color", 1);
    for (int i = 0; i <= 13; i++) if (!g_texColor[i]) g_texColor[i] = g_tex[i];
    int ok = 0; for (int i = 1; i <= 13; i++) if (g_tex[i]) ok++;
    if (ok == 0) Log("  !! sin iconos incrustados en el .so (no deberia pasar)");
    // v1.9.54: icono del boton de la rueda (incrustado, no de archivo).
    // v1.9.55: si hay un PNG en GTAVRadioMusic/boton/, ESE reemplaza al
    // incrustado; si no, se usa el incrustado (regla de Daniel).
    {
        GLuint custom = LoadCustomWheelIcon();
        if (custom) { g_texWheel = custom; }
        else {
            std::vector<unsigned char> raw = B64Decode(kWheelIconB64);
            if (!raw.empty()) g_texWheel = LoadPngFromMemory(raw.data(), raw.size(), false, true);  // v1.9.56: lineArt=true (el incrustado ya es arte blanco sobre transparente)
            Log("  boton rueda: %s", g_texWheel ? "ok (icono incrustado)" : "FALTA (usa el icono de la emisora)");
        }
    }
}
// v2.55: recargar iconos (al cambiar el interruptor de fondo de color):
// borra las texturas GL y marca para recargar en el siguiente cuadro, con el
// black-key segun el nuevo valor de fondoIcono (apagado = negro normal).
static void ReloadIcons() {
    for (int i = 0; i < 14; i++) {
        if (g_tex[i]) { glDeleteTextures(1, &g_tex[i]); g_tex[i] = 0; }
        if (g_texColor[i]) { glDeleteTextures(1, &g_texColor[i]); g_texColor[i] = 0; }
    }
    g_texTried = false;
}
// v2.28: el icono que se dibuja segun el interruptor (a color si esta activo y existe)
static GLuint IconTex(int i) {
    if (i < 0 || i > 13) return 0;
    if (g_cfg.iconosColor && g_texColor[i]) return g_texColor[i];
    return g_tex[i];
}

// v1.9.73: geometria del aviso de emisora.
// Con AdvancedSettings: icono arriba, nombre debajo, arriba al centro.
// Sin el: icono a la IZQUIERDA del nombre, en la misma fila, abajo a la
// izquierda como el texto vainilla del juego (v1.9.76: icono compacto de
// 2.5x el texto para que la fila quepa en la esquina sin subirse sobre
// los botones del dial).
// Devuelve false si no hay aviso que dibujar (temporizador vencido,
// emisora invalida, todo apagado, o la ventana oculta). i0/i1 = rect del
// icono, tPos = esquina del texto, showIcon/showText = que partes dibujar
// (el icono solo si hay textura).
static bool PopupGeom(float W, float H, float ns, float textW, ImVec2& i0, ImVec2& i1, ImVec2& tPos, bool& showIcon, bool& showText) {
    showIcon = false; showText = false;
    if (g_cfg.ocultarNombre) return false;
    if (!g_cfg.popupIcono && !g_cfg.popupTexto) return false;
    if (g_current < 1 || g_current > 13) return false;
    if (std::chrono::steady_clock::now() >= g_popupUntil) return false;
    showIcon = (g_cfg.popupIcono != 0) && (IconTex(g_current) != 0);
    showText = (g_cfg.popupTexto != 0);
    if (!showIcon && !showText) return false;
    float sc = g_cfg.popupTam / 100.0f;  // v1.9.74: barra de tamano (50-200%)
    float gap = H * 0.012f * sc;   // hueco entre icono y texto
    if (g_advSettings) {
        // Arriba al centro, icono y texto centrados en W/2.
        float icon = H * 0.10f * sc;   // tamano del icono
        float cx = W * 0.5f;
        float y = H * 0.03f;
        if (showIcon) { i0 = ImVec2(cx - icon / 2, y); i1 = ImVec2(cx + icon / 2, y + icon); y += icon + gap; }
        else { i0 = i1 = ImVec2(0, 0); }
        tPos = ImVec2(cx - textW / 2, y);
    } else {
        // Abajo a la izquierda, como el texto vainilla del juego.
        // v1.9.76: icono compacto (2.5x el texto) a la izquierda del nombre,
        // en una fila pegada a la esquina: con H*0.10 el icono era tan alto
        // que se subia sobre los botones del dial y el aviso no quedaba en
        // la esquina que marco Daniel. La barra de tamano sigue escalando
        // icono y texto juntos.
        float icon = ns * 2.5f;   // tamano del icono (ns ya trae popupTam)
        float x = (W / H > 1.9f) ? (100.0f / 640.0f) * W : 0.0f;
        float bottom = H - (20.0f / 448.0f) * H;
        if (showIcon) { i0 = ImVec2(x, bottom - icon); i1 = ImVec2(x + icon, bottom); }
        else { i0 = i1 = ImVec2(0, 0); }
        float tx = x + (showIcon ? icon + gap : 0.0f);
        tPos = ImVec2(tx, bottom - ns);
    }
    return true;
}

// ---- cambiar de estacion: solo con funciones que resolvieron ----
// v2.59: modoCambio=1 aplaza el cambio 3 cuadros (la rueda ya esta cerrada).
static int g_pendingStation = 0;   // 0 = no hay cambio pendiente
static int g_pendingFrames = 0;    // cuadros que faltan para el cambio

// v2.59: el cambio real (llamada al juego + lectura). Lo hace ApplyStation
// directo (modo 0 y 2) o 3 cuadros despues de cerrar la rueda (modo 1).
static void DoStartRadio(int id) {
    g_lastStationCmd = std::chrono::steady_clock::now();  // v1.2: la lectura nativa va por detras; RefreshCurrent no la usa 2 s
    // v2.59 modo 2: por CAudioEngine::StartRadio (ya resolvia en el arranque
    // y es el respaldo del modo 0; aqui es la via principal). El simbolo
    // Android es StartRadio(signed char, signed char), no el (int, char)
    // del plugin-sdk de PC: el typedef ya es (void*, signed char, signed
    // char) y pasa (signed char)id y 0. v1.9.25: probado por Daniel (2026-11-21,
    // modoCambio=2): la rueda cambia bien PERO no actualiza EMISORA (el juego
    // sigue reaplicando la guardada al abrir el dial); la 1.9.25 lo corrige
    // redirigiendo la reaplicacion en UT_DiagTick, no cambiando de via.
    // v1.9.66: marcar el cambio como pedido por el usuario TAMBIEN en modo
    // 2 (antes solo el modo 0 lo marcaba): sin la marca, el hook lo
    // confunde con el Radio Off que el juego pone solo al entrar al auto
    // y la gracia de 2 s se queda en la 12 sin parar el reproductor (log
    // 1.9.65: "se queda en la 12 al subir al vehiculo (AudioStart pedia
    // 13)" tras tocar el boton flotante 12 -> 13). La llamada del juego no
    // pasa por aqui, asi que su gracia sigue intacta.
    if (g_cfg.modoCambio == 2 && g_startAudio && g_audioEngine) { Log("cambio a %d (%s) por CAudioEngine::StartRadio(id,0) [modoCambio=2]", id, kStationName[id]); ut_userStationCmd = true; g_startAudio(g_audioEngine, (signed char)id, 0); ut_userStationCmd = false; }
    // v1.2: StartRadio primero (es el que cambia a una estacion concreta).
    // v1.3: cada llamada lleva su 'this' (el objeto global del juego).
    // v1.8: se marca que el cambio lo pidio el usuario (rueda/boton) para
    // que el hook no lo confunda con el Radio Off que el juego pone solo al
    // entrar al auto (ese NO para el reproductor: gracia de 2 s).
    else if (g_startTrack && g_trackManager) { Log("cambio a %d (%s) por CAERadioTrackManager::StartRadio(id,0,1.0,0) [modoCambio=%d]", id, kStationName[id], g_cfg.modoCambio); ut_userStationCmd = true; g_startTrack(g_trackManager, (signed char)id, 0, 1.0f, 0); ut_userStationCmd = false; }
    else if (g_startAudio && g_audioEngine) { Log("cambio a %d (%s) por CAudioEngine::StartRadio(id,0)", id, kStationName[id]); ut_userStationCmd = true; g_startAudio(g_audioEngine, (signed char)id, 0); ut_userStationCmd = false; }
    else if (g_retuneAudio && g_audioEngine) { Log("cambio a %d (%s) por CAudioEngine::RetuneRadio (ultimo recurso)", id, kStationName[id]); ut_userStationCmd = true; g_retuneAudio(g_audioEngine, (signed char)id); ut_userStationCmd = false; }
    else if (g_retuneTrack && g_trackManager) { Log("cambio a %d (%s) por CAERadioTrackManager::RetuneRadio (ultimo recurso)", id, kStationName[id]); ut_userStationCmd = true; g_retuneTrack(g_trackManager, (signed char)id); ut_userStationCmd = false; }
    else { Log("!! sin Retune/Start con su objeto global: la rueda se ve pero NO puede cambiar la estacion (ver VHudRadioProbe.txt)"); return; }
    // verificar lo que el juego dice que suena ahora (diagnostico v1.2).
    // v1.9.2: en la 12 la nativa queda en 13 (Radio Off) por diseno, asi que
    // la lectura esperada es 13. (En modoCambio=2 la llamada va por
    // CAudioEngine::StartRadio; si esa via no pasa por el hook del
    // reproductor, la 12 no sonara: usar modo 0 o 1.)
    if (g_getStationAudio && g_audioEngine) { int v = (int)g_getStationAudio(g_audioEngine); int expect = id; Log("  estacion leida tras cambio: %d (%s)%s", v, (v >= 1 && v <= 13) ? kStationName[v] : "?", v == expect ? "" : "  <-- NO COINCIDE"); }  // v1.9.7: nativa a 12 (Mixtape), no a 13
    // v1.9.23: sincronizar EMISORA al cambiar con la rueda (no solo al
    // abrir el dial): AudioEngine+0x149 es la emisora que muestra EMISORA
    // y la rueda usa TrackManager::StartRadio, que no la actualiza.
    UT_SyncEmisora(id);
}
static void ApplyStation(int id) {
    if (id < 1 || id > 13) return;
    // v2.32: restaurar la escala de tiempo ANTES de cambiar la estacion.
    // El mod de User Tracks (SAUserTracks) arranca su reproductor al
    // llamar StartRadio; si lo hace con la camara lenta puesta (0.30),
    // su decodificador MP3 arranca a ritmo lento y la cancion se escucha
    // trabada aunque la rueda ya se haya cerrado. Con la escala en 1.00
    // el mod arranca a ritmo normal. La rueda se cierra justo despues,
    // asi que el efecto visual no cambia.
    if (g_timeScale) *g_timeScale = 1.0f;
    if (g_slowActive) *g_slowActive = false;
    // v2.47 (Google AI Mode): restaurar TAMBIEN ms_fSlowMotionScale.
    // La v2.32 solo restauraba dos de las tres; con g_slowScale en 0.30
    // SAUserTracks puede arrancar su MP3 a ritmo lento al interceptar
    // StartRadio(12).
    if (g_slowScale) *g_slowScale = 1.0f;
    // v1: vehiculo sin radio -> la 12 es Radio Off (misma lista que SAUserTracks).
    if (id == 12 && UT_VehicleBlocked()) { id = 13; Log("rueda: vehiculo sin radio (modelo %d): User Tracks -> Radio Off", UT_BlockedModel()); }
    // v2.59: modo 1 espera 3 cuadros con la rueda ya cerrada (sin UI ni
    // camara lenta) antes de llamar a StartRadio, para que SAUserTracks
    // arranque sin la carga de dibujar la rueda. El cambio pendiente se
    // ejecuta en DrawFrame.
    if (g_cfg.modoCambio == 1) {
        g_pendingStation = id;
        g_pendingFrames = 3;
        Log("cambio a %d (%s) aplazado 3 cuadros [modoCambio=1]", id, kStationName[id]);
    } else {
        DoStartRadio(id);
    }
    g_current = id;
}
static void RefreshCurrent() {
    // v1.9.51 (Daniel): con el dial abierto NO se toca g_current. El dial
    // (AudioScreen) actualiza g_current via los hooks RetuneRadio en cada
    // tap, pero RefreshCurrent corre cada 30 cuadros en DrawFrame (con
    // !g_open y en vehiculo, que es justo el estado con el dial abierto) y
    // lee el getter nativo, que va UNA emisora por detras tras cambiar.
    // Su guarda de 2 s usa g_lastStationCmd, que solo pone DoStartRadio
    // (la rueda), NO los taps del dial, asi que con el dial abierto la
    // guarda no protege: RefreshCurrent pisaba g_current con la emisora
    // anterior y UT_DiagTick escribia MS+0x240 con ese valor viejo, y la
    // EMISORA se veia retroceder a la anterior (sobre todo al pasar
    // rapido, cuando el getter siempre va por detras). El [diag] no lo
    // mostraba porque el getter si avanzaba limpio; el revertido era solo
    // en g_current/MS+0x240 (el display). Con el dial cerrado la rueda se
    // sincroniza al abrirse (ToggleWheel llama RefreshCurrent), asi que
    // esto no afecta a la rueda.
    if (ut_inAudioMenu) return;
    // v1: en la 12 manda el reproductor, no la radio nativa. La nativa NUNCA
    // cambia a la 12 (queda en la anterior), asi que sin esta guarda la rueda
    // mostraba el anillo en otra estacion aunque User Tracks siguiera sonando.
    if (ut_active || g_current == 12) return;
    // v1.2: la lectura nativa va una estacion por detras justo despues de
    // cambiar (el juego actualiza el campo mas tarde). No pisar g_current
    // con esa lectura vieja durante 2 s tras el cambio: la rueda y el boton
    // se quedaban en la estacion anterior aunque el cambio ya se habia hecho.
    if (std::chrono::steady_clock::now() - g_lastStationCmd < std::chrono::seconds(2)) return;
    // v1.9.19 (Daniel): el getter del AudioEngine va ATRASADO (devuelve la
    // emisora anterior, p. ej. 1 Playback FM cuando se esta en 7 Radio X) y
    // RefreshCurrent pisaba g_current con ese valor viejo: la rueda saltaba
    // a la emisora anterior tras abrir el dial. El TrackManager es el que
    // actualizan nuestros hooks (StartRadio), asi que se lee PRIMERO.
    if (g_getStationTrack && g_trackManager) { int v = (int)g_getStationTrack(g_trackManager); if (v >= 1 && v <= 13) g_current = v; return; }
    if (g_getStationAudio && g_audioEngine) { int v = (int)g_getStationAudio(g_audioEngine); if (v >= 1 && v <= 13) g_current = v; }
}

// Abrir/cerrar la rueda con periodo refractario: el doble toque al claxon llega
// por DOS caminos a la vez (widget 7 y zona tactil) y sin esto la abriria y la
// cerraria en el mismo instante.
static void ToggleWheel(const char* why) {
    static auto lastToggle = std::chrono::steady_clock::now() - std::chrono::seconds(5);
    auto now = std::chrono::steady_clock::now();
    if (now - lastToggle < std::chrono::milliseconds(600)) return;
    lastToggle = now;
    if (!g_open) { g_open = true; g_wheelAnimStart = std::chrono::steady_clock::now(); g_ignoreNextUp = true; RefreshCurrent(); g_selected = g_current; Log("rueda abierta por %s (estacion actual %d %s)", why, g_current, kStationName[g_current]); PlayClick(false); }
    else { g_open = false; g_listBtnTap = false; Log("rueda cerrada por %s", why); }
}


// ---- toque: como ARCX (type 2 = pulsar, 3 = mover, 1/4 = soltar) ----
static std::mutex g_inMtx;
struct TouchEv { int kind; float x, y; };
static std::vector<TouchEv> g_evs;
static bool g_cap = false;
static uintptr_t (*Touch_orig)(int, int, int, int);
static bool g_touchForWheel = false;  // v2.57: el toque es de la rueda (el juego no lo ve)
static uintptr_t Touch_hook(int type, int idx, int x, int y) {
    bool down = (type == 2), up = (type == 1 || type == 4);
    if (idx == 0) {
        // v2.49: si el panel esta abierto, cada toque pertenece al panel (si
        // cae dentro de el al bajar el dedo) o al juego (si cae fuera). Los del
        // panel van solo a ImGui (el juego no los ve: asi no se pulsan los
        // botones del juego que estan detras ni se mueve la camara); los
        // movimientos del juego no llegan a ImGui (asi no se mueven las barras
        // ni los botones del panel solos). El dueno se decide al bajar y no
        // cambia durante el arrastre, aunque el panel se cierre a mitad del toque.
        if (down) {
            bool inPanel = false;
            if (g_settings && g_panelRectW > 0.0f) {
                inPanel = (float)x >= g_panelRectX && (float)x <= g_panelRectX + g_panelRectW &&
                          (float)y >= g_panelRectY && (float)y <= g_panelRectY + g_panelRectH;
            }
            // v1.4: la lista de canciones tambien es un panel (los toques
            // dentro van solo a ImGui, el juego no los ve).
            if (!inPanel && g_trackList && g_trackListRectW > 0.0f) {
                inPanel = (float)x >= g_trackListRectX && (float)x <= g_trackListRectX + g_trackListRectW &&
                          (float)y >= g_trackListRectY && (float)y <= g_trackListRectY + g_trackListRectH;
            }
            g_touchForPanel = inPanel;
            // v2.57: con la rueda abierta (y no el panel), el toque es de la
            // rueda: el juego no lo ve, asi no se mueve la camara ni
            // responden los botones del juego. El doble toque del claxon
            // sigue funcionando porque se detecta por POSICION en g_evs
            // (DrawFrame), no por el juego. El dueno se decide al bajar.
            g_touchForWheel = (!inPanel && g_open && g_cfg.bloquearJuego);
        }
        bool forPanel = g_touchForPanel;
        std::lock_guard<std::mutex> lk(g_inMtx);
        if (down) { g_evs.push_back({0, (float)x, (float)y}); g_evs.push_back({1, 0, 0}); }
        // v2.18: el soltar SIEMPRE se encola CON su posicion - la v2.17 lo
        // encolaba sin posicion (solo {2}) y la rueda se cerraba sin cambiar:
        // la posicion del soltar era lo que rearmaba g_wheelTouch justo antes
        // de aplicar el cambio (como en la v2.16). El soltar sigue llegando
        // siempre (el arreglo del doble toque de la v2.17 se mantiene).
        else if (up) { g_evs.push_back({0, (float)x, (float)y}); g_evs.push_back({2, 0, 0}); }
        // v2.4: sin esto el panel no funcionaba - los eventos de mover solo
        // llegaban a ImGui con la rueda abierta; con el panel abierto (rueda cerrada)
        // ImGui recibia el toque de bajar pero nunca el de mover.
        // v2.49: con el panel abierto el mover solo llega si el toque es del panel;
        // si es del juego (fuera del panel) no se encola.
        else if (forPanel || g_open || g_cap || g_preview || g_btnDrag || g_wheelBtnDown) { g_evs.push_back({0, (float)x, (float)y}); }  // v1.9.58: g_wheelBtnDown: sin esto los eventos de mover se DESCARTABAN mientras se arrastraba el boton de la rueda (solo llegaban el de bajar y el de soltar), asi que el boton no seguia al dedo y solo saltaba al soltar; el flotante si era fluido porque g_btnDrag si estaba en la lista.
        if (up) { g_touchForPanel = false; g_touchForWheel = false; }
        if (forPanel) return 0;  // consumido por el panel: el juego no lo ve
        if (g_touchForWheel) return 0;  // v2.57: consumido por la rueda: el juego no lo ve
    }
    return Touch_orig ? Touch_orig(type, idx, x, y) : 0;
}

// ---- dibujo ImGui justo antes de eglSwapBuffers (patron ARCX) ----
static bool g_inited = false, g_failed = false;
static EGLBoolean (*Swap_orig)(EGLDisplay, EGLSurface);
static bool PatchGot(const char* libName, const char* sym, void* repl, void** orig) {
    struct Ctx { const char* lib; uintptr_t base; const ElfW(Phdr)* ph; int n; } c = {libName, 0, nullptr, 0};
    dl_iterate_phdr([](struct dl_phdr_info* i, size_t, void* d) -> int {
        Ctx* x = (Ctx*)d;
        if (i->dlpi_name && strstr(i->dlpi_name, x->lib)) { x->base = i->dlpi_addr; x->ph = i->dlpi_phdr; x->n = i->dlpi_phnum; return 1; }
        return 0; }, &c);
    if (!c.ph) return false;
    const ElfW(Dyn)* dyn = nullptr;
    for (int i = 0; i < c.n; i++) if (c.ph[i].p_type == PT_DYNAMIC) dyn = (const ElfW(Dyn)*)(c.base + c.ph[i].p_vaddr);
    if (!dyn) return false;
    auto fix = [&](uintptr_t p) { return p < c.base ? p + c.base : p; };
    const ElfW(Sym)* symtab = nullptr; const char* strtab = nullptr;
    const ElfW(Rela)* jmprel = nullptr; size_t jmpsz = 0; const ElfW(Rela)* rela = nullptr; size_t relasz = 0;
    for (; dyn->d_tag != DT_NULL; dyn++) switch (dyn->d_tag) {
        case DT_SYMTAB: symtab = (const ElfW(Sym)*)fix(dyn->d_un.d_ptr); break;
        case DT_STRTAB: strtab = (const char*)fix(dyn->d_un.d_ptr); break;
        case DT_JMPREL: jmprel = (const ElfW(Rela)*)fix(dyn->d_un.d_ptr); break;
        case DT_PLTRELSZ: jmpsz = dyn->d_un.d_val; break;
        case DT_RELA: rela = (const ElfW(Rela)*)fix(dyn->d_un.d_ptr); break;
        case DT_RELASZ: relasz = dyn->d_un.d_val; break;
        default: break; }
    if (!symtab || !strtab) return false;
    auto scan = [&](const ElfW(Rela)* r, size_t bytes) -> bool {
        if (!r) return false;
        for (size_t k = 0; k < bytes / sizeof(ElfW(Rela)); k++) {
            unsigned type = ELF64_R_TYPE(r[k].r_info); unsigned si = ELF64_R_SYM(r[k].r_info);
            if ((type != 1026 && type != 1025) || si == 0) continue;
            if (strcmp(strtab + symtab[si].st_name, sym) != 0) continue;
            void** slot = (void**)(c.base + r[k].r_offset);
            long pg = sysconf(_SC_PAGESIZE); uintptr_t page = (uintptr_t)slot & ~((uintptr_t)pg - 1);
            if (mprotect((void*)page, (size_t)pg, PROT_READ | PROT_WRITE) != 0) return false;
            *orig = *slot; *slot = repl; return true; }
        return false; };
    return scan(jmprel, jmpsz) || scan(rela, relasz);
}

static ImFont* g_fontTitle = nullptr;  // v2.1: letra del centro (Roboto Condensed negrita del sistema)
static ImFont* g_fontUI = nullptr;  // v2.3: letra del panel (32 px, legible al tacto)

// v2.16: tamano y posicion del boton flotante (se arrastra con el dedo y se
// bloquea desde el panel; la etiqueta va a la izquierda, o a la derecha si
// el boton esta en la mitad izquierda de la pantalla para que no se salga).
static float BtnSize(float H) { return H * 0.06f * g_cfg.btnTam / 100.0f; }
static ImVec2 BtnPos(float W, float H, float bs) {
    if (g_cfg.btnX >= 0.0f && g_cfg.btnY >= 0.0f) return ImVec2(g_cfg.btnX * W, g_cfg.btnY * H);
    return ImVec2(W - bs * 1.15f, H * 0.86f);  // v2.2: mas abajo (defecto)
}
// v1.3: marquesina (el nombre se desplaza si no cabe). Devuelve el
// desplazamiento X (0 = quieto). Se reinicia al cambiar la cancion; el
// texto se queda 1 s quieto y luego se desplaza a 40 px/s, con un hueco
// de 60 px al dar la vuelta. La rueda y el boton comparten el temporizador
// (van sincronizados).
static std::string g_marqTrack;
static std::chrono::steady_clock::time_point g_marqStart = std::chrono::steady_clock::now();
static float MarqueeOff(const char* track, float textW, float availW) {
    if (textW <= availW) return 0.0f;
    if (g_marqTrack != track) { g_marqTrack = track; g_marqStart = std::chrono::steady_clock::now(); }
    double el = std::chrono::duration<double>(std::chrono::steady_clock::now() - g_marqStart).count();
    if (el <= 1.0) return 0.0f;
    return (float)fmod((el - 1.0) * 40.0, (double)textW + 60.0);
}
// v1.3: segunda linea de la etiqueta del boton flotante. En la 12 muestra
// la cancion que suena COMPLETA (la marquesina la desplaza si no cabe);
// en las demas, el genero.
static const char* BtnSubLine() {
    if (g_current == 12 && ut_active) return UT_TrackDisplayName();
    return Genre(g_current);
}
// v1.2: flechas anterior/siguiente del centro de la rueda (solo en la 12
// cuando suena el reproductor). DrawWheel guarda sus centros y radio; el
// toque las resuelve por posicion.
static ImVec2 g_trkPrevC = ImVec2(0, 0), g_trkNextC = ImVec2(0, 0);
static float g_trkArrowR = 0;
static bool g_trkArrows = false;
static int g_trackBtn = 0;  // -1 anterior, +1 siguiente (toque en curso)
// v1.4: boton "Lista de canciones" en el centro de la rueda (solo en la 12).
static bool g_listBtn = false;
static ImVec4 g_listBtnRect = ImVec4(0, 0, 0, 0);  // x, y, ancho, alto
static int TrackArrowAt(float x, float y) {
    if (!g_trkArrows) return 0;
    float rr = g_trkArrowR * 1.2f;  // 20% de margen para el dedo
    float dx = x - g_trkPrevC.x, dy = y - g_trkPrevC.y;
    if (dx*dx + dy*dy <= rr*rr) return -1;
    dx = x - g_trkNextC.x; dy = y - g_trkNextC.y;
    if (dx*dx + dy*dy <= rr*rr) return 1;
    return 0;
}
// v1.5: tamanos de texto de la etiqueta del boton flotante. Escalan con el
// tamano del boton (btnTam), asi el boton entero (icono + etiqueta) crece
// o encoge junto (antes el texto era fijo y solo el icono escalaba).
static void BtnLabelSizes(float H, float& ns, float& ts) {
    float sc = g_cfg.btnTam / 100.0f;
    ns = H * 0.022f * sc;
    ts = H * 0.018f * sc;
}
// v1.5: flechas anterior/siguiente DENTRO de la ventana de la etiqueta (solo
// en la 12): van a los lados del nombre de la cancion, dentro de la etiqueta
// (antes iban fuera, a los lados del icono y la etiqueta, y quedaban mal).
static ImVec2 g_lblPrevC, g_lblNextC;
static float g_lblArrowR = 0;
static bool g_lblArrows = false;
static float g_lblClipL = 0, g_lblClipR = 0;  // hueco del nombre entre las flechas
static float g_lblTrackY = 0;  // Y de la linea de la cancion en la etiqueta
// v2.2: geometria del boton flotante (icono + etiqueta con estacion y genero).
// El toque vale en todo el conjunto (icono o etiqueta).
static void FloatBtnGeom(float W, float H, ImVec2& iconPos, ImVec2& labelPos, ImVec2& labelSize, float& bs) {
    bs = BtnSize(H);
    iconPos = BtnPos(W, H, bs);
    ImFont* font = g_fontTitle ? g_fontTitle : ImGui::GetFont();
    float ns, ts;
    BtnLabelSizes(H, ns, ts);
    ImVec2 nsz = font->CalcTextSizeA(ns, FLT_MAX, 0.0f, kStationName[g_current], nullptr);
    ImVec2 tsz = font->CalcTextSizeA(ts, FLT_MAX, 0.0f, BtnSubLine(), nullptr);
    // v1.7: "Ocultar genero" solo quita la SEGUNDA linea (genero/cancion);
    // la ventana (nombre de la emisora) sigue. Antes ocultaba toda la
    // ventana; para eso ya esta "Ocultar ventana de emisora" (ocultarNombre).
    // v1.8: en la 12 la 2a linea es el NOMBRE DE LA CANCION (no un genero) y
    // las flechas cambian de cancion: "Ocultar genero" NO quita nada en la
    // 12 (ni el nombre ni las flechas). Solo quita el genero en la 1-11.
    bool showSub = g_cfg.mostrarGenero || (g_current == 12 && ut_active);
    float subW = 0.0f;
    float arrowW = 0.0f;
    float ar = ts * 0.8f;
    float agap = H * 0.008f;
    if (showSub) {
        subW = tsz.x;
        float cap = H * 0.22f * (g_cfg.btnTam / 100.0f);
        if (g_current == 12 && ut_active && subW > cap) subW = cap;  // v1.3: la etiqueta no crece con nombres largos (marquesina)
        if (g_current == 12 && ut_active) arrowW = 2.0f * (2.0f * ar + agap);
    }
    float lw = (nsz.x > subW + arrowW ? nsz.x : subW + arrowW) + H * 0.02f;
    float lh = ns + (showSub ? ts : 0.0f) + H * 0.02f;
    labelSize = ImVec2(lw, lh);
    if (iconPos.x < W * 0.5f) labelPos = ImVec2(iconPos.x + bs + H * 0.015f, iconPos.y + bs * 0.5f - lh * 0.5f);
    else labelPos = ImVec2(iconPos.x - H * 0.015f - lw, iconPos.y + bs * 0.5f - lh * 0.5f);
    // Geometria de las flechas dentro de la etiqueta (para dibujarlas y tocarlas).
    g_lblArrows = (g_current == 12 && ut_active && !g_cfg.ocultarNombre);  // v1.8: las flechas de la 12 NO dependen de mostrarGenero (cambian la cancion, no son genero)
    if (g_lblArrows) {
        g_lblArrowR = ar;
        float subY = labelPos.y + H * 0.008f + ns;  // Y de la linea 2 (la cancion)
        float cy = subY + ts * 0.5f;
        float pad = H * 0.01f;
        g_lblPrevC = ImVec2(labelPos.x + pad + ar, cy);
        g_lblNextC = ImVec2(labelPos.x + lw - pad - ar, cy);
        g_lblClipL = g_lblPrevC.x + ar + agap;
        g_lblClipR = g_lblNextC.x - ar - agap;
        g_lblTrackY = subY;
    }
}
static bool FloatBtnHit(float x, float y, float W, float H) {
    if (!g_hasRadio) return false;  // v2.16: sin radio no hay boton que tocar
    ImVec2 ip, lp, ls; float bs;
    FloatBtnGeom(W, H, ip, lp, ls, bs);
    if (x >= ip.x && x <= ip.x + bs && y >= ip.y && y <= ip.y + bs) return true;
    if (x >= lp.x && x <= lp.x + ls.x && y >= lp.y && y <= lp.y + ls.y) return true;
    return false;
}
// v1.9.54: boton propio de la rueda (abre/cierra la rueda sin usar el
// claxon). v1.9.55: SEPARADO del flotante: tiene su propia posicion
// (botonRuedaX/botonRuedaY en el .ini); si es -1 (sin mover), va encima del
// flotante como antes. Se sujeta a la pantalla si queda fuera.
static void WheelBtnGeom(float W, float H, ImVec2& wp, float& ws) {
    ImVec2 ip, lp, ls; float bs;
    FloatBtnGeom(W, H, ip, lp, ls, bs);
    ws = bs * g_cfg.btnRuedaTam / 100.0f;  // v1.9.60: tamano propio del boton de la rueda (50..200%)
    float wx, wy;
    if (g_cfg.btnRuedaX >= 0.0f && g_cfg.btnRuedaY >= 0.0f) {
        wx = g_cfg.btnRuedaX * W; wy = g_cfg.btnRuedaY * H;
    } else {
        float gap = H * 0.012f;
        wx = ip.x; wy = ip.y - ws - gap;
    }
    if (wy < 0.0f) wy = 0.0f;
    if (wy + ws > H) wy = H - ws;
    if (wx < 0.0f) wx = 0.0f;
    if (wx + ws > W) wx = W - ws;
    wp = ImVec2(wx, wy);
}
static bool WheelBtnHit(float x, float y, float W, float H) {
    if (!g_hasRadio) return false;
    ImVec2 wp; float ws;
    WheelBtnGeom(W, H, wp, ws);
    return x >= wp.x && x <= wp.x + ws && y >= wp.y && y <= wp.y + ws;
}

// ---- v1.9.63: boton nativo del juego (widget real via SAUtils) ----
// El boton de la rueda pasa a ser un widget REAL del juego (como el del
// Manual Driveby de chzbrgr), creado con SAUtils::CreateWidget (slot libre
// 190..254) y con el icono cargado por LoadRwTextureFromPNG. No hay
// dependencia en compilacion: todo se resuelve en runtime (dlopen
// libAML.so -> GetInterface("SAUtils") -> vtable). Sin SAUtils, o si algo
// falla, se usa el boton GL de siempre (el de la 1.9.54-1.9.62).
// Datos verificados por desensamblado (NativeWidget-RE.md, 2026-11-23):
// - vtable SAUtils (63 entradas): FindFreeWidgetId=18, CreateWidget=19,
//   SetWidgetIcon(ptr)=21, SetWidgetPos=32, LoadRwTextureFromPNG=33.
// - CWidget: posicion +0x18..0x27 (x,y,sx,sy en unidades 640x448 del
//   juego: x_px = x*maxW/640, y_px = y*maxH/448), rect en pixeles
//   +0x2c..0x38, sprite +0x10, tocado +0x54, color RGBA +0x55..0x58
//   (alfa +0x58: 0 = no se dibuja), habilitado +0x59. El ctor deja
//   +0x58=0 y +0x59=0: hay que ponerlos o el widget no se ve.
// - El juego lee el arreglo de widgets por el puntero GOT base+0x850910
//   (SAUtils lo redirige a su pNewWidgets[255]); el simbolo m_pWidgets
//   (g_widgets) solo tiene las primeras 150 entradas tras SAUtils.
// - SAUtils ToggleWidget escribe en +0x5C (marca de tiempo del toque en
//   el juego), NO en +0x59: NO se usa; se escribe +0x59 directo.
// - DrawAll NO salta los widgets con +0x59=0; el que manda es el alfa.
// - El toque se detecta igual que el boton GL (WheelBtnHit + soltar),
//   asi que no hace falta leer +0x54.
static void* g_sautils = nullptr;       // ISAUtils* (runtime, puede ser NULL)
static void** g_sautilsVt = nullptr;    // su vtable
static void* g_natTex = nullptr;        // RwTexture* del icono
static void* g_natWidget = nullptr;     // CWidgetButton* creado
static int g_natId = -1;                // id en pNewWidgets (190..254)
static bool g_natLoggedNoSA = false;    // "SAUtils no detectado" ya registrado
static bool g_natLoggedConvert = false; // "icono propio convertido" ya registrado (v1.9.64)
static bool g_natDiagLogged = false;    // diagnostico por paso ya registrado (v1.9.65)
static bool g_natRectLogged = false;    // rect del juego ya registrado
static float g_natX = -1e9f, g_natY = -1e9f, g_natS = -1e9f;  // ultima pos aplicada (unidades widget)
static bool g_natShown = false;
static uintptr_t g_gameBase = 0;        // base de libGTASA.so (para GOT)

typedef int (*SA_FindFreeWidgetIdFn)(void*);
typedef void* (*SA_CreateWidgetFn)(void*, int, int, int, float, const char*);
typedef void (*SA_SetWidgetIconFn)(void*, void*, uintptr_t);
typedef void (*SA_SetWidgetPosFn)(void*, int, float, float, float, float);
typedef void* (*SA_LoadRwTextureFromPNGFn)(void*, const char*);

static uintptr_t GameLibBase() {
    if (g_gameBase) return g_gameBase;
    struct Ctx { uintptr_t base; } c = {0};
    dl_iterate_phdr([](struct dl_phdr_info* i, size_t, void* d) -> int {
        Ctx* x = (Ctx*)d;
        if (i->dlpi_name && strstr(i->dlpi_name, "libGTASA.so")) { x->base = i->dlpi_addr; return 1; }
        return 0; }, &c);
    g_gameBase = c.base;
    return g_gameBase;
}
static void** WidgetsArrayGOT() {
    uintptr_t b = GameLibBase();
    if (!b) return nullptr;
    return *(void***)(b + 0x850910);  // puntero GOT al arreglo (pNewWidgets con SAUtils)
}
static bool SAUtilsGet() {
    if (g_sautils && g_sautilsVt) return true;
    // Reintentar como mucho cada 5 s (SAUtils puede cargar despues que
    // este mod); sin esto se haria dlopen+dlsym en cada cuadro.
    static auto lastTry = std::chrono::steady_clock::now() - std::chrono::seconds(10);
    auto now = std::chrono::steady_clock::now();
    if (now - lastTry < std::chrono::seconds(5)) return false;
    lastTry = now;
    void* h = dlopen("libAML.so", RTLD_NOW);
    if (!h) return false;
    typedef void* (*GetInterfaceFn)(const char*);
    GetInterfaceFn gi = (GetInterfaceFn)dlsym(h, "GetInterface");
    if (!gi) return false;
    void* sa = gi("SAUtils");
    if (!sa) {
        if (!g_natLoggedNoSA) { g_natLoggedNoSA = true; Log("  boton nativo: SAUtils no detectado (se usa el boton GL)"); }
        return false;
    }
    g_sautils = sa;
    g_sautilsVt = *(void***)sa;
    Log("  boton nativo: SAUtils detectado (%p)", sa);
    return true;
}
// Geometria del boton en unidades del juego (640x448): centro + escala.
// Se calcula desde WheelBtnGeom (misma geometria que el boton GL), asi el
// toque (WheelBtnHit) y el widget dibujado coinciden.
static bool NativeWheelGeom(float W, float H, float maxW, float maxH, float& wx, float& wy, float& sc) {
    if (maxW < 200.0f || maxW > 10000.0f || maxH < 200.0f || maxH > 10000.0f) return false;
    ImVec2 wp; float ws;
    WheelBtnGeom(W, H, wp, ws);
    float cx = wp.x + ws * 0.5f, cy = wp.y + ws * 0.5f;
    wx = cx * 640.0f / maxW;
    wy = cy * 448.0f / maxH;
    sc = (ws * 0.5f) * 640.0f / maxW;
    return true;
}
// Tamano de pantalla que usa el juego para los widgets (RsGlobal via GOT:
// +8 = ancho maximo, +12 = alto maximo, los dos valores que usa
// CWidget::Update). Si no se puede leer, se usa el tamano de la
// superficie (w/h de DrawFrame).
static bool NativeScreenSize(float W, float H, float& maxW, float& maxH) {
    uintptr_t b = GameLibBase();
    if (b) {
        void* rs = *(void**)(b + 0x850240);  // GOT: RsGlobal
        if (rs) {
            uint32_t mw = *(const uint32_t*)((const char*)rs + 8);
            uint32_t mh = *(const uint32_t*)((const char*)rs + 12);
            if (mw >= 200 && mw <= 10000 && mh >= 200 && mh <= 10000) { maxW = (float)mw; maxH = (float)mh; return true; }
        }
    }
    maxW = W; maxH = H;
    return false;
}
// Ruta del PNG del icono para el widget nativo: primero un PNG en
// GTAVRadioMusic/boton/ (boton.png, o el primer *.png alfabetico; misma
// regla que el boton GL), y si no hay, el icono incrustado escrito a un
// archivo (LoadRwTextureFromPNG solo lee archivos, no memoria).
// v1.9.64: ruta del icono PROPIO (GTAVRadioMusic/boton/*.png). Antes era
// parte de NativeWheelIconPath; se separa del incrustado para poder
// convertir el propio cuando RW no lo lee (JPEG con extension .png) antes
// de rendirse al incrustado.
static bool NativeWheelCustomIconPath(std::string& out) {
    UT_BuildMusicDirs();
    {
        std::string target;
        for (int d = 0; d < UT_NUM_MUSIC_DIRS; d++) {
            struct stat st;
            if (stat(UT_MUSIC_DIRS[d], &st) == 0 && S_ISDIR(st.st_mode)) { target = UT_MUSIC_DIRS[d]; break; }
        }
        if (target.empty()) target = UT_MUSIC_DIRS[0];
        std::string botonDir = target + "/boton";
        struct stat bst;
        if (stat(botonDir.c_str(), &bst) != 0) mkdir(botonDir.c_str(), 0777);
    }
    for (int d = 0; d < UT_NUM_MUSIC_DIRS; d++) {
        std::string dir = std::string(UT_MUSIC_DIRS[d]) + "/boton";
        std::string cand;
        {
            std::string p = dir + "/boton.png";
            FILE* f = fopen(p.c_str(), "rb");
            if (f) { fclose(f); cand = p; }
        }
        if (cand.empty()) {
            DIR* dp = opendir(dir.c_str());
            if (dp) {
                std::string best;
                struct dirent* ent;
                while ((ent = readdir(dp)) != nullptr) {
                    std::string n = ent->d_name;
                    if (n.size() > 4 && n.substr(n.size() - 4) == ".png") {
                        if (best.empty() || n < best) best = n;
                    }
                }
                closedir(dp);
                if (!best.empty()) cand = dir + "/" + best;
            }
        }
        if (!cand.empty()) { out = cand; return true; }
    }
    return false;
}
// v1.9.63: escribe el icono incrustado a VHudRadio/boton_nativo.png.
// v1.9.65: en data/ (g_gameDataDir), no en files/: el .ini y el log ya
// escriben ahi, asi que esta carpeta esta probada como escribible; la
// escritura a files/VHudRadio/ nunca se probo y es sospechosa del fallo.
static bool NativeWheelEmbeddedIconPath(std::string& out) {
    std::string dir = g_gameDataDir + "/VHudRadio";
    mkdir(dir.c_str(), 0777);
    std::string path = dir + "/boton_nativo.png";
    std::vector<unsigned char> raw = B64Decode(kWheelIconB64);
    if (raw.empty()) return false;
    FILE* f = fopen(path.c_str(), "wb");
    if (!f) return false;
    size_t wr = fwrite(raw.data(), 1, raw.size(), f);
    fclose(f);
    if (wr != raw.size()) return false;
    out = path;
    return true;
}
static void NativeWheelShow(bool show) {
    if (!g_natWidget) return;
    unsigned char* w = (unsigned char*)g_natWidget;
    if (show) {
        // El ctor deja alfa=0 y habilitado=0; sin esto el widget no se
        // dibuja. La opacidad es la del .ini (btnRuedaOpac, 10..100%).
        w[0x58] = (unsigned char)(255 * g_cfg.btnRuedaOpac / 100);
        w[0x59] = 1;
    } else {
        w[0x58] = 0;
        w[0x59] = 0;
    }
    g_natShown = show;
}
// v1.9.64: escritor PNG minimo (RGBA 8 bits, sin compresion) para convertir
// el icono propio a un PNG real que LoadRwTextureFromPNG si pueda leer.
// RtPNGImageRead solo lee PNG de verdad; un JPEG con extension .png (como
// radiostation.png) lo lee stb_image pero no RW. Sin dependencias externas.
static uint32_t PngCrc32(const unsigned char* p, size_t n) {
    static uint32_t table[256];
    static bool init = false;
    if (!init) {
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t c = i;
            for (int k = 0; k < 8; k++) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : (c >> 1);
            table[i] = c;
        }
        init = true;
    }
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; i++) c = table[(c ^ p[i]) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}
static void PngPut32(std::vector<unsigned char>& out, uint32_t v) {
    out.push_back((unsigned char)((v >> 24) & 0xFF));
    out.push_back((unsigned char)((v >> 16) & 0xFF));
    out.push_back((unsigned char)((v >> 8) & 0xFF));
    out.push_back((unsigned char)(v & 0xFF));
}
static void PngChunk(std::vector<unsigned char>& out, const char* type, const unsigned char* data, size_t len) {
    PngPut32(out, (uint32_t)len);
    size_t tpos = out.size();
    out.insert(out.end(), type, type + 4);
    if (len) out.insert(out.end(), data, data + len);
    PngPut32(out, PngCrc32(out.data() + tpos, 4 + len));
}
static bool NativeWritePng(const std::string& path, const unsigned char* rgba, int w, int h) {
    if (!rgba || w <= 0 || h <= 0) return false;
    std::vector<unsigned char> png;
    static const unsigned char sig[8] = {137, 80, 78, 71, 13, 10, 26, 10};
    png.insert(png.end(), sig, sig + 8);
    unsigned char ihdr[13] = {0};
    ihdr[0] = (unsigned char)((w >> 24) & 0xFF); ihdr[1] = (unsigned char)((w >> 16) & 0xFF);
    ihdr[2] = (unsigned char)((w >> 8) & 0xFF); ihdr[3] = (unsigned char)(w & 0xFF);
    ihdr[4] = (unsigned char)((h >> 24) & 0xFF); ihdr[5] = (unsigned char)((h >> 16) & 0xFF);
    ihdr[6] = (unsigned char)((h >> 8) & 0xFF); ihdr[7] = (unsigned char)(h & 0xFF);
    ihdr[8] = 8; ihdr[9] = 6; ihdr[10] = 0; ihdr[11] = 0; ihdr[12] = 0;
    PngChunk(png, "IHDR", ihdr, 13);
    // Datos sin filtrar: un byte 0 por fila + RGBA. zlib con bloques
    // almacenados (sin compresion): cabecera 0x78 0x01, bloques de maximo
    // 65535 bytes (byte final + LEN + NLEN + datos) y Adler-32 al final.
    std::vector<unsigned char> raw;
    raw.reserve((size_t)(w * 4 + 1) * (size_t)h);
    for (int y = 0; y < h; y++) {
        raw.push_back(0);
        raw.insert(raw.end(), rgba + (size_t)y * w * 4, rgba + (size_t)(y + 1) * w * 4);
    }
    std::vector<unsigned char> z;
    z.push_back(0x78); z.push_back(0x01);
    size_t off = 0;
    while (true) {
        size_t chunk = raw.size() - off;
        if (chunk > 65535) chunk = 65535;
        bool last = (off + chunk >= raw.size());
        z.push_back(last ? (unsigned char)1 : (unsigned char)0);
        z.push_back((unsigned char)(chunk & 0xFF));
        z.push_back((unsigned char)((chunk >> 8) & 0xFF));
        uint16_t nlen = (uint16_t)~(uint16_t)chunk;
        z.push_back((unsigned char)(nlen & 0xFF));
        z.push_back((unsigned char)((nlen >> 8) & 0xFF));
        z.insert(z.end(), raw.begin() + off, raw.begin() + off + chunk);
        off += chunk;
        if (last) break;
    }
    uint32_t a = 1, b = 0;
    for (size_t i = 0; i < raw.size(); i++) { a = (a + raw[i]) % 65521; b = (b + a) % 65521; }
    uint32_t adler = (b << 16) | a;
    z.push_back((unsigned char)((adler >> 24) & 0xFF));
    z.push_back((unsigned char)((adler >> 16) & 0xFF));
    z.push_back((unsigned char)((adler >> 8) & 0xFF));
    z.push_back((unsigned char)(adler & 0xFF));
    PngChunk(png, "IDAT", z.data(), z.size());
    PngChunk(png, "IEND", nullptr, 0);
    FILE* f = fopen(path.c_str(), "wb");
    if (!f) return false;
    size_t wr = fwrite(png.data(), 1, png.size(), f);
    fclose(f);
    return wr == png.size();
}
// v1.9.64: convierte el icono propio a un PNG real que RW pueda leer.
// Decodifica con stb_image (lee JPEG, PNG, etc.) y re-escribe PNG. El
// tratamiento lineArt es el compartido (ApplyWheelLineArt, 1.9.70): si ya
// tiene alfa se usa tal cual; si es opaca se quita el fondo (color de la
// esquina) y el disco gris.
static bool NativeWheelConvertIcon(const std::string& src, const std::string& dst) {
    // v1.9.64: stb_image esta compilado con STBI_NO_STDIO (linea 949), asi
    // que stbi_load no existe: leer el archivo a memoria y usar
    // stbi_load_from_memory, igual que LoadPngFromMemory.
    FILE* f = fopen(src.c_str(), "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (len <= 0) { fclose(f); return false; }
    std::vector<unsigned char> buf((size_t)len);
    size_t rd = fread(buf.data(), 1, (size_t)len, f);
    fclose(f);
    if (rd != (size_t)len) return false;
    int w = 0, h = 0, comp = 0;
    unsigned char* px = stbi_load_from_memory(buf.data(), (int)len, &w, &h, &comp, 4);
    if (!px || w <= 0 || h <= 0) { if (px) stbi_image_free(px); return false; }
    // v1.9.70: lineArt compartido (ApplyWheelLineArt): si ya tiene
    // transparencia se usa tal cual; si es opaca se quita el fondo.
    ApplyWheelLineArt(px, w, h);
    // v1.9.75: achicar a 512 px como maximo ANTES de escribir (igual que
    // la textura RW): el convertido salia de 6 MB con un icono de 1254 px
    // y el cargador de SAUtils lo tiene que leer entero en memoria.
    if (w > 512 || h > 512) {
        int nw = w, nh = h;
        if (w >= h) { nw = 512; nh = h * 512 / w; } else { nh = 512; nw = w * 512 / h; }
        if (nw < 1) nw = 1; if (nh < 1) nh = 1;
        unsigned char* small = (unsigned char*)malloc((size_t)nw * nh * 4);
        if (small) {
            for (int y = 0; y < nh; y++) {
                for (int x = 0; x < nw; x++) {
                    int sx = x * w / nw, sy = y * h / nh;
                    int ex = (x + 1) * w / nw, ey = (y + 1) * h / nh;
                    if (ex <= sx) ex = sx + 1; if (ey <= sy) ey = sy + 1;
                    long sr = 0, sg = 0, sb = 0, sa = 0; int n = 0;
                    for (int yy = sy; yy < ey; yy++) for (int xx = sx; xx < ex; xx++) {
                        unsigned char* p = px + (yy * w + xx) * 4;
                        sr += p[0]; sg += p[1]; sb += p[2]; sa += p[3]; n++;
                    }
                    unsigned char* d = small + (y * nw + x) * 4;
                    d[0] = (unsigned char)(sr / n); d[1] = (unsigned char)(sg / n); d[2] = (unsigned char)(sb / n); d[3] = (unsigned char)(sa / n);
                }
            }
            stbi_image_free(px); px = small; w = nw; h = nh;
        }
    }
    bool ok = NativeWritePng(dst, px, w, h);
    stbi_image_free(px);
    return ok;
}
// v1.9.65: base de libSAUtils64.so (mismo patron que GameLibBase) para
// verificar los indices de la vtable contra el binario analizado.
static uintptr_t SAUtilsLibBase() {
    static uintptr_t base = 0;
    if (base) return base;
    struct Ctx { uintptr_t base; } c = {0};
    dl_iterate_phdr([](struct dl_phdr_info* i, size_t, void* d) -> int {
        Ctx* x = (Ctx*)d;
        if (i->dlpi_name && strstr(i->dlpi_name, "libSAUtils64.so")) { x->base = i->dlpi_addr; return 1; }
        return 0; }, &c);
    base = c.base;
    return base;
}
// v1.9.65: registra los desplazamientos de la vtable de SAUtils respecto a
// su base, una sola vez. Si los indices son los correctos y el SAUtils del
// telefono es el analizado (1.6), debe salir vt18=0x274c4 (FindFreeWidgetId),
// vt19=0x274ec (CreateWidget), vt21=0x275f8 (SetWidgetIcon), vt33=0x27924
// (LoadRwTextureFromPNG) y vt35=0x27b20. Cualquier otro valor dice que el
// SAUtils instalado es otra version y los indices hay que ajustarlos.
static void NativeWheelLogVtable() {
    uintptr_t base = SAUtilsLibBase();
    if (!base || !g_sautilsVt) { Log("  boton nativo: diag vtable sin base (%p) o sin vtable (%p)", (void*)base, (void*)g_sautilsVt); return; }
    Log("  boton nativo: diag vtable base=%p vt18=0x%lx vt19=0x%lx vt21=0x%lx vt33=0x%lx vt35=0x%lx",
        (void*)base,
        (unsigned long)((uintptr_t)g_sautilsVt[18] - base),
        (unsigned long)((uintptr_t)g_sautilsVt[19] - base),
        (unsigned long)((uintptr_t)g_sautilsVt[21] - base),
        (unsigned long)((uintptr_t)g_sautilsVt[33] - base),
        (unsigned long)((uintptr_t)g_sautilsVt[35] - base));
}
// v1.9.66: crea una RwTexture directamente desde un archivo de imagen
// (PNG o JPEG), sin usar LoadRwTextureFromPNG de SAUtils: ese cargador
// devuelve NULL con un PNG valido que existe (log 1.9.65:
// boton_nativo.png, 19860 bytes, vt33=0x27924 correcto). Pasos: stb
// decodifica el archivo (lee cualquier formato, igual que el boton GL),
// lineArt si no hay alfa (copia del bloque 1.9.58 de LoadPngFromMemory),
// RwImage con los pixeles, RwImageFindRasterFormat + RwRasterCreate +
// RwRasterSetFromImage + RwTextureCreate (mismo orden que el cargador de
// SAUtils, pero con los pixeles ya decodificados). Devuelve la textura o
// nullptr. Los fallos se registran una sola vez por sesion (static).
static void* NativeWheelTextureFromFile(const std::string& src, int* outW = nullptr, int* outH = nullptr) {
    static bool logged = false;
    // Las funciones RW se resolvieron en UT_InstallHooks (ver el log de
    // arranque: "ok _Z13RwImageCreateiii" etc.); si falta alguna, no se
    // intenta nada y se cae al boton GL.
    if (!g_rwImageCreate || !g_rwImageAlloc || !g_rwImageFindFmt ||
        !g_rwRasterCreate || !g_rwRasterSetFromImage || !g_rwTextureCreate ||
        !g_rwImageDestroy || !g_rwRasterDestroy) {
        if (!logged) { logged = true; Log("  boton nativo: textura RW sin funciones RW (FALTA en el log de arranque)"); }
        return nullptr;
    }
    FILE* f = fopen(src.c_str(), "rb");
    if (!f) { if (!logged) { logged = true; Log("  boton nativo: textura RW no se pudo leer %s", src.c_str()); } return nullptr; }
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (len <= 0) { fclose(f); return nullptr; }
    std::vector<unsigned char> buf((size_t)len);
    size_t rd = fread(buf.data(), 1, (size_t)len, f);
    fclose(f);
    if (rd != (size_t)len) return nullptr;
    int w = 0, h = 0, comp = 0;
    unsigned char* px = stbi_load_from_memory(buf.data(), (int)len, &w, &h, &comp, 4);
    if (!px || w <= 0 || h <= 0) {
        if (px) stbi_image_free(px);
        if (!logged) { logged = true; Log("  boton nativo: textura RW stb no pudo decodificar %s", src.c_str()); }
        return nullptr;
    }
    // v1.9.70: lineArt compartido (ApplyWheelLineArt): si ya tiene
    // transparencia se usa tal cual; si es opaca se quita el fondo (color
    // de la esquina, no solo blanco) y el disco gris.
    ApplyWheelLineArt(px, w, h);
    // v1.9.70: un icono de 1500 px no se ve mejor que uno de 512 px en un
    // boton de ~280 px, pero ocupa 9 veces mas memoria de GPU (y el PNG
    // convertido salia de 6 MB). Achicar a 512 px como maximo.
    if (w > 512 || h > 512) {
        int nw = w, nh = h;
        if (w >= h) { nw = 512; nh = h * 512 / w; } else { nh = 512; nw = w * 512 / h; }
        if (nw < 1) nw = 1; if (nh < 1) nh = 1;
        unsigned char* small = (unsigned char*)malloc((size_t)nw * nh * 4);
        if (small) {
            for (int y = 0; y < nh; y++) {
                for (int x = 0; x < nw; x++) {
                    int sx = x * w / nw, sy = y * h / nh;
                    int ex = (x + 1) * w / nw, ey = (y + 1) * h / nh;
                    if (ex <= sx) ex = sx + 1; if (ey <= sy) ey = sy + 1;
                    long sr = 0, sg = 0, sb = 0, sa = 0; int n = 0;
                    for (int yy = sy; yy < ey; yy++) for (int xx = sx; xx < ex; xx++) {
                        unsigned char* p = px + (yy * w + xx) * 4;
                        sr += p[0]; sg += p[1]; sb += p[2]; sa += p[3]; n++;
                    }
                    unsigned char* d = small + (y * nw + x) * 4;
                    d[0] = (unsigned char)(sr / n); d[1] = (unsigned char)(sg / n); d[2] = (unsigned char)(sb / n); d[3] = (unsigned char)(sa / n);
                }
            }
            stbi_image_free(px); px = small; w = nw; h = nh;
        }
    }
    // RwImage de 32 bits con los pixeles (layout real: 40 bytes,
    // cpPixels en +24; ver el trabajo de ARCX).
    struct RwImageLite { int32_t flags, width, height, depth, stride; uint8_t _pad[4]; uint8_t* cpPixels; void* palette; };
    void* img = g_rwImageCreate(w, h, 32);
    if (!img) { stbi_image_free(px); if (!logged) { logged = true; Log("  boton nativo: textura RW RwImageCreate devolvio NULL"); } return nullptr; }
    if (!g_rwImageAlloc(img)) { g_rwImageDestroy(img); stbi_image_free(px); if (!logged) { logged = true; Log("  boton nativo: textura RW RwImageAllocatePixels devolvio NULL"); } return nullptr; }
    RwImageLite* im = (RwImageLite*)img;
    if (!im->cpPixels) { g_rwImageDestroy(img); stbi_image_free(px); if (!logged) { logged = true; Log("  boton nativo: textura RW sin cpPixels"); } return nullptr; }
    for (int y = 0; y < h; y++) memcpy(im->cpPixels + (size_t)y * (size_t)im->stride, px + (size_t)y * (size_t)w * 4, (size_t)w * 4);
    stbi_image_free(px);
    int fw = 0, fh = 0, fd = 0, ff = 0;
    if (!g_rwImageFindFmt(img, 4, &fw, &fh, &fd, &ff)) { g_rwImageDestroy(img); if (!logged) { logged = true; Log("  boton nativo: textura RW RwImageFindRasterFormat devolvio 0"); } return nullptr; }
    // v1.9.77: diagnostico del crash del tester con el icono "SA DE": el
    // formato de raster (profundidad/banderas) que RenderWare le asigna a
    // la textura es lo unico que puede diferir entre dos PNG iguales en
    // tamano y con alfa; se registra siempre (no solo una vez) para poder
    // comparar el SA DE con el SA y con el incrustado.
    Log("  boton nativo: diag textura RW formato depth=%d flags=0x%x (%dx%d)", fd, ff, fw, fh);
    void* ras = g_rwRasterCreate(fw, fh, fd, ff);
    if (!ras) { g_rwImageDestroy(img); if (!logged) { logged = true; Log("  boton nativo: textura RW RwRasterCreate devolvio NULL"); } return nullptr; }
    if (!g_rwRasterSetFromImage(ras, img)) { g_rwRasterDestroy(ras); g_rwImageDestroy(img); if (!logged) { logged = true; Log("  boton nativo: textura RW RwRasterSetFromImage devolvio NULL"); } return nullptr; }
    void* tex = g_rwTextureCreate(ras);
    g_rwImageDestroy(img);
    if (!tex) { g_rwRasterDestroy(ras); if (!logged) { logged = true; Log("  boton nativo: textura RW RwTextureCreate devolvio NULL"); } return nullptr; }
    if (outW) *outW = w; if (outH) *outH = h;  // v1.9.70: tamano final (tras achicar) para el diagnostico
    // No se destruyen ras ni tex: el widget usa la textura directo (o una
    // copia si habia textura previa); la original se queda viva en g_natTex.
    return tex;
}
static bool NativeWheelCreate(float W, float H) {
    if (g_natWidget) return true;
    if (!SAUtilsGet()) return false;
    float maxW = 0, maxH = 0;
    NativeScreenSize(W, H, maxW, maxH);
    float wx = 0, wy = 0, sc = 0;
    if (!NativeWheelGeom(W, H, maxW, maxH, wx, wy, sc)) { Log("  boton nativo: sin tamano de pantalla valido"); return false; }
    int id = ((SA_FindFreeWidgetIdFn)g_sautilsVt[18])(g_sautils);
    if (id < 190 || id > 254) { Log("  boton nativo: sin slot libre (FindFreeWidgetId=%d)", id); return false; }
    // v1.9.64: orden de intentos: (1) el icono propio tal cual, (2) el
    // propio convertido a PNG real (JPEG con .png y demas), (3) el
    // incrustado. Solo si los tres fallan se usa el boton GL.
    // v1.9.65: diagnostico por paso, una sola vez por sesion: el log 1.9.64
    // muestra que los 3 intentos fallan pero no dice en cual paso. Cada
    // linea "diag" se registra solo la primera vez que se llega aqui.
    // v1.9.66: si el cargador de SAUtils fallo en todo (log 1.9.65: PNG
    // valido, loader -> 0x0), ultimo intento: crear la textura RW
    // directamente desde los pixeles (NativeWheelTextureFromFile).
    bool diag = !g_natDiagLogged;
    if (diag) { g_natDiagLogged = true; NativeWheelLogVtable(); }
    std::string png;
    std::string customPng;  // v1.9.66: ruta del icono propio, si existe
    std::string embPng;     // v1.9.66: ruta del incrustado escrito, si se escribio
    void* tex = nullptr;
    if (NativeWheelCustomIconPath(png)) {
        customPng = png;
        tex = ((SA_LoadRwTextureFromPNGFn)g_sautilsVt[33])(g_sautils, png.c_str());
        if (diag) Log("  boton nativo: diag propio %s -> %p", png.c_str(), tex);
        if (!tex) {
            // v1.9.65: el convertido va a data/VHudRadio/ (probado
            // escribible: ahi viven el .ini y el log) en vez de
            // files/VHudRadio/, cuya escritura nunca se probo.
            std::string conv = g_gameDataDir + "/VHudRadio/boton_convertido.png";
            bool convOk = NativeWheelConvertIcon(png, conv);
            struct stat cst;
            bool convExists = (stat(conv.c_str(), &cst) == 0);
            if (diag) Log("  boton nativo: diag conversion ok=%d existe=%d tam=%ld", convOk ? 1 : 0, convExists ? 1 : 0, convExists ? (long)cst.st_size : 0L);
            if (convOk) {
                tex = ((SA_LoadRwTextureFromPNGFn)g_sautilsVt[33])(g_sautils, conv.c_str());
                if (diag) Log("  boton nativo: diag convertido -> %p", tex);
                if (tex) {
                    png = conv;
                    if (!g_natLoggedConvert) { g_natLoggedConvert = true; Log("  boton nativo: icono propio convertido a PNG real"); }
                }
            }
            if (!tex && diag) Log("  boton nativo: diag el propio no cargo, se usa el incrustado");
        }
    } else if (diag) {
        Log("  boton nativo: diag sin icono propio en boton/");
    }
    if (!tex) {
        std::string emb;
        bool embOk = NativeWheelEmbeddedIconPath(emb);
        if (embOk) embPng = emb;
        struct stat est;
        bool embExists = embOk && (stat(emb.c_str(), &est) == 0);
        if (diag) Log("  boton nativo: diag incrustado escrito=%d existe=%d tam=%ld ruta=%s", embOk ? 1 : 0, embExists ? 1 : 0, embExists ? (long)est.st_size : 0L, emb.c_str());
        if (embOk) {
            tex = ((SA_LoadRwTextureFromPNGFn)g_sautilsVt[33])(g_sautils, emb.c_str());
            if (diag) Log("  boton nativo: diag incrustado -> %p", tex);
            if (tex) png = emb;
        }
    }
    // v1.9.66: el cargador de SAUtils no pudo con ningun PNG: crear la
    // textura RW desde los pixeles del icono propio (cualquier formato,
    // stb lo lee) o del incrustado.
    if (!tex) {
        std::string rwSrc = !customPng.empty() ? customPng : embPng;
        if (!rwSrc.empty()) {
            int rwW = 0, rwH = 0;
            tex = NativeWheelTextureFromFile(rwSrc, &rwW, &rwH);
            if (diag) Log("  boton nativo: diag textura RW %s -> %p (%dx%d)", rwSrc.c_str(), tex, rwW, rwH);
            if (tex) png = rwSrc;
        }
    }
    if (!tex) {
        if (diag) Log("  boton nativo: sin icono PNG (se usa el boton GL)");
        return false;
    }
    void* widget = ((SA_CreateWidgetFn)g_sautilsVt[19])(g_sautils, id, (int)wx, (int)wy, sc, nullptr);
    if (!widget) { Log("  boton nativo: CreateWidget(%d) devolvio NULL", id); return false; }
    // v1.9.67: NO llamar a SetWidgetIcon si el widget no tiene textura:
    // SAUtils 1.6 la destruye SIN comprobar NULL (SetWidgetIcon 0x275f8
    // llama a RwTextureDestroy(widget+0x10) antes de nada) y un widget
    // recien creado con nombre NULL tiene +0x10 = NULL: RwTextureDestroy
    // lee el refCount en +0x64 y cae con SIGSEGV en 0x64 (crash 1.9.66 al
    // activar el interruptor nativo; la textura RW si se habia creado
    // bien). En ese caso se escribe la textura directo en widget+0x10
    // (m_pTexture del sprite); si ya hay una textura, SetWidgetIcon la
    // destruye bien y pone la copia.
    void* oldTex = *(void**)((char*)widget + 0x10);
    if (!oldTex) {
        *(void**)((char*)widget + 0x10) = tex;
        Log("  boton nativo: icono directo en widget+0x10 (sin textura previa)");
    } else {
        ((SA_SetWidgetIconFn)g_sautilsVt[21])(g_sautils, widget, (uintptr_t)tex);
    }
    // Posicion exacta en flotante (CreateWidget solo admite int x/y).
    ((SA_SetWidgetPosFn)g_sautilsVt[32])(g_sautils, id, wx, wy, sc, sc);
    g_natTex = tex; g_natWidget = widget; g_natId = id;
    g_natX = wx; g_natY = wy; g_natS = sc;
    NativeWheelShow(true);
    Log("  boton nativo: creado id=%d pos=(%.1f,%.1f) escala=%.1f png=%s", id, wx, wy, sc, png.c_str());
    return true;
}
// Activo = el widget nativo existe (el GL solo se dibuja si no hay widget).
static bool NativeWheelActive() { return g_natWidget != nullptr; }
// Se llama cada cuadro desde DrawFrame (antes de decidir que dibujar).
static void NativeWheelUpdate(float W, float H) {
    bool want = g_inVehicle && g_hasRadio && g_cfg.botonVisible && !g_paused && !g_open && !g_preview && !g_settings && !g_trackList && !g_wheelClosing;
    if (!SAUtilsGet()) {
        // Sin SAUtils: esconder el nativo si existia y usar el GL.
        if (g_natWidget) NativeWheelShow(false);
        return;
    }
    if (!g_natWidget) {
        if (want) NativeWheelCreate(W, H);
        return;
    }
    // El widget existe: verificar que sigue en el arreglo (si el juego lo
    // borro al salir al menu, recrearlo la proxima vez que se quiera).
    void** arr = WidgetsArrayGOT();
    if (!arr || g_natId < 0 || g_natId > 254 || arr[g_natId] != g_natWidget) {
        Log("  boton nativo: el widget desaparecio del arreglo (se recreara)");
        g_natWidget = nullptr; g_natId = -1; g_natX = -1e9f; g_natShown = false;
        return;
    }
    float maxW = 0, maxH = 0;
    NativeScreenSize(W, H, maxW, maxH);
    float wx = 0, wy = 0, sc = 0;
    if (!NativeWheelGeom(W, H, maxW, maxH, wx, wy, sc)) return;
    if (want) {
        // Mover solo si cambio (tamano, opacidad o posicion) para no llamar
        // a SetWidgetPos en cada cuadro.
        unsigned char wantAlpha = (unsigned char)(255 * g_cfg.btnRuedaOpac / 100);
        unsigned char* w = (unsigned char*)g_natWidget;
        if (wx != g_natX || wy != g_natY || sc != g_natS) {
            ((SA_SetWidgetPosFn)g_sautilsVt[32])(g_sautils, g_natId, wx, wy, sc, sc);
            g_natX = wx; g_natY = wy; g_natS = sc;
        }
        if (w[0x58] != wantAlpha) w[0x58] = wantAlpha;
        if (w[0x59] != 1) w[0x59] = 1;
        g_natShown = true;
        // Rectangulo real que calculo el juego (pixeles) vs geometria GL,
        // una vez, para verificar que las unidades 640x448 son correctas
        // en este telefono (el rect se calcula en el Update del juego, asi
        // que en el cuadro de la creacion aun vale 0).
        if (!g_natRectLogged) {
            const float* r = (const float*)((const char*)g_natWidget + 0x2c);
            if (r[2] > r[0] && r[3] > r[1]) {
                ImVec2 wp; float ws; WheelBtnGeom(W, H, wp, ws);
                Log("  boton nativo: rect juego=(%.0f,%.0f)-(%.0f,%.0f) rect GL=(%.0f,%.0f)-(%.0f,%.0f)", r[0], r[1], r[2], r[3], wp.x, wp.y, wp.x + ws, wp.y + ws);
                g_natRectLogged = true;
            }
        }
    } else if (g_natShown) {
        NativeWheelShow(false);
    }
}

// v2.33: boton flotante con OpenGL directo (sin ImGui) para no hacer un
// cuadro ImGui cada cuadro solo por el boton. El decodificador MP3 de
// SAUserTracks se quedaba sin CPU en el A14; ARCX no dibuja ImGui cuando
// su menu esta cerrado. La rueda/panel/vista previa siguen con ImGui.
struct GLBtnVert { float x, y, u, v, r, g, b, a; };
static GLuint g_glBtnProg = 0, g_glBtnVBO = 0, g_glBtnWhite = 0, g_glBtnVAO = 0;
static GLint g_glBtnUProj = -1, g_glBtnUTex = -1;  // v2.51: ubicaciones de uniformes cacheadas (antes 2 glGetUniformLocation por cuadro)
static bool g_glBtnTried = false, g_glBtnOk = false, g_glBtnUseVAO = false;
#ifndef GL_VERTEX_ARRAY_BINDING
#define GL_VERTEX_ARRAY_BINDING 0x85B5
#endif
// v1.4: constantes de ES 3.0 (no estan en <GLES2/gl2.h>) para el blit del blur.
#ifndef GL_READ_FRAMEBUFFER
#define GL_READ_FRAMEBUFFER 0x8CA8
#endif
#ifndef GL_DRAW_FRAMEBUFFER
#define GL_DRAW_FRAMEBUFFER 0x8CA9
#endif
typedef void (*PFNGLGENVA)(GLsizei, GLuint*);
typedef void (*PFNGLBINDVA)(GLuint);
static PFNGLGENVA g_pGenVA = nullptr;
static PFNGLBINDVA g_pBindVA = nullptr;
// v1.4: glBlitFramebuffer (ES 3.0) para resolver el MSAA del juego en el blur.
// Se declara aqui (antes de GLBtnInit) porque GLBtnInit lo resuelve.
typedef void (*PFNGLBLITFB)(GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLbitfield, GLenum);
static PFNGLBLITFB g_pBlitFB = nullptr;
static GLuint GLBtnCompile(GLenum type, const char* src) {
    GLuint sh = glCreateShader(type);
    glShaderSource(sh, 1, &src, nullptr);
    glCompileShader(sh);
    GLint ok = 0; glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok) { char buf[512]; glGetShaderInfoLog(sh, sizeof(buf), nullptr, buf); Log("  !! boton GL: shader no compilo: %s", buf); glDeleteShader(sh); return 0; }
    return sh;
}
static bool GLBtnInit() {
    if (g_glBtnTried) return g_glBtnOk;
    g_glBtnTried = true;
    const char* vs = "attribute vec2 aPos; attribute vec2 aUV; attribute vec4 aColor;\n"
        "varying vec2 vUV; varying vec4 vColor; uniform mat4 uProj;\n"
        "void main() { gl_Position = uProj * vec4(aPos, 0.0, 1.0); vUV = aUV; vColor = aColor; }\n";
    const char* fs = "precision mediump float; varying vec2 vUV; varying vec4 vColor;\n"
        "uniform sampler2D uTex;\n"
        "void main() { vec4 t = texture2D(uTex, vUV); gl_FragColor = vec4(vColor.rgb * t.rgb, vColor.a * t.a); }\n";
    GLuint vsh = GLBtnCompile(GL_VERTEX_SHADER, vs);
    GLuint fsh = GLBtnCompile(GL_FRAGMENT_SHADER, fs);
    if (!vsh || !fsh) { if (vsh) glDeleteShader(vsh); if (fsh) glDeleteShader(fsh); return false; }
    g_glBtnProg = glCreateProgram();
    glAttachShader(g_glBtnProg, vsh); glAttachShader(g_glBtnProg, fsh);
    glBindAttribLocation(g_glBtnProg, 0, "aPos");
    glBindAttribLocation(g_glBtnProg, 1, "aUV");
    glBindAttribLocation(g_glBtnProg, 2, "aColor");
    glLinkProgram(g_glBtnProg);
    GLint ok = 0; glGetProgramiv(g_glBtnProg, GL_LINK_STATUS, &ok);
    glDeleteShader(vsh); glDeleteShader(fsh);
    if (!ok) { char buf[512]; glGetProgramInfoLog(g_glBtnProg, sizeof(buf), nullptr, buf); Log("  !! boton GL: programa no enlazo: %s", buf); glDeleteProgram(g_glBtnProg); g_glBtnProg = 0; return false; }
    g_glBtnUProj = glGetUniformLocation(g_glBtnProg, "uProj");  // v2.51: se cachean una vez al crear el programa
    g_glBtnUTex = glGetUniformLocation(g_glBtnProg, "uTex");
    glGenBuffers(1, &g_glBtnVBO);
    // v2.34: VAO propio. Sin el, glVertexAttribPointer toca el VAO del
    // juego (ES 3.x) y la pantalla se pone en negro al subir a un auto.
    g_pGenVA = (PFNGLGENVA)eglGetProcAddress("glGenVertexArrays");
    g_pBindVA = (PFNGLBINDVA)eglGetProcAddress("glBindVertexArray");
    g_pBlitFB = (PFNGLBLITFB)eglGetProcAddress("glBlitFramebuffer");  // v1.4: resolver MSAA para el blur
    if (g_pGenVA && g_pBindVA) {
        g_pGenVA(1, &g_glBtnVAO);
        g_pBindVA(g_glBtnVAO);
        glBindBuffer(GL_ARRAY_BUFFER, g_glBtnVBO);
        glEnableVertexAttribArray(0); glEnableVertexAttribArray(1); glEnableVertexAttribArray(2);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(GLBtnVert), (void*)0);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(GLBtnVert), (void*)8);
        glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(GLBtnVert), (void*)16);
        g_pBindVA(0);
        g_glBtnUseVAO = true;
        Log("boton GL: VAO %u listo", (unsigned)g_glBtnVAO);
    } else {
        // Sin VAO (ES 2.0): no hay VAO que corromper; los atributos se
        // ponen cada cuadro en DrawButtonGL.
        g_glBtnUseVAO = false;
        Log("boton GL: sin VAO (ES 2.0)");
    }
    unsigned char px[4] = {255, 255, 255, 255};
    glGenTextures(1, &g_glBtnWhite);
    glBindTexture(GL_TEXTURE_2D, g_glBtnWhite);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    g_glBtnOk = true;
    Log("boton GL listo (programa %u)", (unsigned)g_glBtnProg);
    return true;
}
// v1.9: BLUR GAUSSIANO (separable, 5-tap optimizado). El bajado por piramide
// suaviza pero al 100% (8x) se seguian viendo los cuadros: el filtro lineal
// solo promedia 2x2. Un gaussiano de verdad promedia con pesos de campana y
// sale suave como el de Void Radio Wheel Expanded. Se aplica en 2 pases
// (horizontal + vertical) sobre la textura pequena, con ping-pong a una
// textura temporal. Los pesos/offsets son los del gaussiano 9-tap optimizado
// (5 lecturas con filtrado lineal): 0.2270 centro, 0.3162 a +-1.3846,
// 0.0703 a +-3.2308.
// v1.9: programa del blur gaussiano. Se declara aqui (antes de GLBlurInit)
// porque GLBlurInit lo usa; el resto de globales del blur esta junto a
// BlurDrawBackground. (El build #27 fallo por declararlas despues de usarlas.)
static GLuint g_blurProg = 0;  // programa del blur gaussiano (separable 5-tap)
static GLint g_blurUProj = -1, g_blurUTex = -1, g_blurUDir = -1;
static bool g_blurTried = false, g_blurOk = false;
static bool GLBlurInit() {
    if (g_blurTried) return g_blurOk;
    g_blurTried = true;
    const char* vs = "attribute vec2 aPos; attribute vec2 aUV; uniform mat4 uProj;\n"
        "varying vec2 vUV;\n"
        "void main() { gl_Position = uProj * vec4(aPos, 0.0, 1.0); vUV = aUV; }\n";
    const char* fs = "precision mediump float; varying vec2 vUV;\n"
        "uniform sampler2D uTex; uniform vec2 uDir;\n"
        "void main() {\n"
        "  vec4 c = texture2D(uTex, vUV) * 0.2270270270;\n"
        "  c += texture2D(uTex, vUV + uDir * 1.3846153846) * 0.3162162162;\n"
        "  c += texture2D(uTex, vUV - uDir * 1.3846153846) * 0.3162162162;\n"
        "  c += texture2D(uTex, vUV + uDir * 3.2307692308) * 0.0702702703;\n"
        "  c += texture2D(uTex, vUV - uDir * 3.2307692308) * 0.0702702703;\n"
        "  gl_FragColor = c;\n"
        "}\n";
    GLuint vsh = GLBtnCompile(GL_VERTEX_SHADER, vs);
    GLuint fsh = GLBtnCompile(GL_FRAGMENT_SHADER, fs);
    if (!vsh || !fsh) { if (vsh) glDeleteShader(vsh); if (fsh) glDeleteShader(fsh); Log("  !! blur gaussiano: no compilo"); return false; }
    g_blurProg = glCreateProgram();
    glAttachShader(g_blurProg, vsh); glAttachShader(g_blurProg, fsh);
    glBindAttribLocation(g_blurProg, 0, "aPos");
    glBindAttribLocation(g_blurProg, 1, "aUV");
    glLinkProgram(g_blurProg);
    GLint ok = 0; glGetProgramiv(g_blurProg, GL_LINK_STATUS, &ok);
    glDeleteShader(vsh); glDeleteShader(fsh);
    if (!ok) { char buf[512]; glGetProgramInfoLog(g_blurProg, sizeof(buf), nullptr, buf); Log("  !! blur gaussiano: no enlazo: %s", buf); glDeleteProgram(g_blurProg); g_blurProg = 0; return false; }
    g_blurUProj = glGetUniformLocation(g_blurProg, "uProj");
    g_blurUTex = glGetUniformLocation(g_blurProg, "uTex");
    g_blurUDir = glGetUniformLocation(g_blurProg, "uDir");
    g_blurOk = true;
    Log("blur gaussiano listo (programa %u)", (unsigned)g_blurProg);
    return true;
}
// Decodifica un caracter UTF-8. Devuelve los bytes consumidos (1-4), 0 si es invalido.
// (ImTextCharFromUtf8 es interna de ImGui y no esta en imgui.h de la v1.89.7.)
static int GLBtnUtf8(unsigned int* out, const char* p) {
    unsigned char c = (unsigned char)*p;
    if (c < 0x80) { *out = c; return 1; }
    if ((c & 0xE0) == 0xC0) {
        unsigned char c1 = (unsigned char)p[1];
        if ((c1 & 0xC0) != 0x80) return 0;
        *out = ((c & 0x1F) << 6) | (c1 & 0x3F);
        return 2;
    }
    if ((c & 0xF0) == 0xE0) {
        unsigned char c1 = (unsigned char)p[1], c2 = (unsigned char)p[2];
        if ((c1 & 0xC0) != 0x80 || (c2 & 0xC0) != 0x80) return 0;
        *out = ((c & 0x0F) << 12) | ((c1 & 0x3F) << 6) | (c2 & 0x3F);
        return 3;
    }
    if ((c & 0xF8) == 0xF0) {
        unsigned char c1 = (unsigned char)p[1], c2 = (unsigned char)p[2], c3 = (unsigned char)p[3];
        if ((c1 & 0xC0) != 0x80 || (c2 & 0xC0) != 0x80 || (c3 & 0xC0) != 0x80) return 0;
        *out = ((c & 0x07) << 18) | ((c1 & 0x3F) << 12) | ((c2 & 0x3F) << 6) | (c3 & 0x3F);
        return 4;
    }
    return 0;
}
// Dibuja el boton flotante con OpenGL directo. Devuelve false si no se pudo
// (entonces el llamador usa ImGui como antes). W/H en pixeles.
static bool DrawButtonGL(float W, float H) {
    if (!GLBtnInit()) return false;
    ImFont* font = g_fontTitle ? g_fontTitle : ImGui::GetFont();
    if (!font) return false;
    ImTextureID atlas = ImGui::GetIO().Fonts->TexID;
    if (!atlas) return false;
    GLuint atlasTex = (GLuint)(uintptr_t)atlas;
    ImVec2 bp, lp, ls; float bs;
    FloatBtnGeom(W, H, bp, lp, ls, bs);
    float ns, ts;
    BtnLabelSizes(H, ns, ts);  // v1.5: la etiqueta escala con el tamano del boton
    int op = g_cfg.btnOpac * 255 / 100;
    float opF = op / 255.0f;
    float opRuedaF = g_cfg.btnRuedaOpac / 100.0f;  // v1.9.60: opacidad PROPIA del boton de la rueda (10..100%), separada de la del flotante
    std::vector<GLBtnVert> vBg, vTxt, vIco, vTrk, vWheel;  // v1.3: vTrk = nombre de la cancion (se dibuja con tijera); v1.9.54: vWheel = icono del boton de la rueda
    // v1.9.60 (optimizar): reservar capacidad para no realocar los vectores
    // varias veces por cuadro al ir anadiendo vertices; y la geometria del
    // boton de la rueda se calcula UNA vez aqui (antes se calculaba 2 veces
    // por cuadro: disco/icono y contorno).
    vBg.reserve(256); vTxt.reserve(64); vIco.reserve(12); vTrk.reserve(64); vWheel.reserve(12);
    ImVec2 wp; float ws;
    WheelBtnGeom(W, H, wp, ws);
    float trkSubY = 0;  // v1.3: Y de la linea de la cancion (para la tijera)
    auto quadTo = [&](std::vector<GLBtnVert>& dst, float x0, float y0, float x1, float y1, float u0, float vv0, float u1, float v1, float r, float g, float b, float a) {
        dst.push_back({x0, y0, u0, vv0, r, g, b, a});
        dst.push_back({x1, y0, u1, vv0, r, g, b, a});
        dst.push_back({x1, y1, u1, v1, r, g, b, a});
        dst.push_back({x0, y0, u0, vv0, r, g, b, a});
        dst.push_back({x1, y1, u1, v1, r, g, b, a});
        dst.push_back({x0, y1, u0, v1, r, g, b, a});
    };
    // v2.56: disco lleno (abanico de triangulos) para el fondo del boton: la
    // 2.55 usaba un cuadro negro y se notaban las esquinas del cuadrado.
    auto circleTo = [&](std::vector<GLBtnVert>& dst, float cx, float cy, float rr, float r, float g, float b, float a) {
        const int SEG = 32;
        for (int i = 0; i < SEG; i++) {
            float a0 = (float)i / SEG * 2.0f * 3.14159265f;
            float a1 = (float)(i + 1) / SEG * 2.0f * 3.14159265f;
            dst.push_back({cx, cy, 0.0f, 0.0f, r, g, b, a});
            dst.push_back({cx + cosf(a0) * rr, cy + sinf(a0) * rr, 0.0f, 0.0f, r, g, b, a});
            dst.push_back({cx + cosf(a1) * rr, cy + sinf(a1) * rr, 0.0f, 0.0f, r, g, b, a});
        }
    };
    // Fondo de la etiqueta (negro semitransparente, textura blanca 1x1).
    // v2.54: el recuadro entero se oculta como en ImGui (ver DrawWheel).
    // v1.7: "Ocultar genero" ya no oculta la ventana (solo la 2a linea);
    // la ventana la oculta "Ocultar ventana de emisora" (ocultarNombre).
    if (!g_cfg.ocultarNombre) quadTo(vBg, lp.x, lp.y, lp.x + ls.x, lp.y + ls.y, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 140.0f * opF / 255.0f);
    // Texto: nombre y genero con los glifos del atlas de ImGui (UTF-8).
    auto drawTextTo = [&](std::vector<GLBtnVert>& dst, const char* str, float x, float y, float size, float r, float g, float b, float a) {
        float scale = size / font->FontSize;
        float cx = x;
        const char* p = str;
        while (*p) {
            unsigned int cp = 0;
            int n = GLBtnUtf8(&cp, p);
            if (n <= 0) break;
            p += n;
            const ImFontGlyph* gl = font->FindGlyph((ImWchar)cp);
            if (!gl) continue;
            float x0 = cx + gl->X0 * scale, y0 = y + gl->Y0 * scale;
            float x1 = cx + gl->X1 * scale, y1 = y + gl->Y1 * scale;
            quadTo(dst, x0, y0, x1, y1, gl->U0, gl->V0, gl->U1, gl->V1, r, g, b, a);
            cx += gl->AdvanceX * scale;
        }
    };
    auto drawText = [&](const char* str, float x, float y, float size, float r, float g, float b, float a) {
        drawTextTo(vTxt, str, x, y, size, r, g, b, a);
    };
    if (!g_cfg.ocultarNombre) {
        drawText(kStationName[g_current], lp.x + H * 0.01f, lp.y + H * 0.008f, ns, 1.0f, 1.0f, 1.0f, opF);
        // v1.3: en la 12, el nombre de la cancion se desplaza si no cabe en la etiqueta.
        // v1.5: el nombre va ENTRE las flechas (dentro de la etiqueta) y la
        // tijera recorta solo ese hueco (antes era toda la etiqueta).
        // v1.7: la 2a linea (genero/cancion) solo se dibuja si mostrarGenero;
        // la ventana y el nombre quedan aunque el genero este oculto.
        // v1.8: en la 12 la 2a linea es el nombre de la cancion (no genero):
        // se dibuja siempre, aunque mostrarGenero este apagado.
        if (!g_cfg.mostrarGenero && !(g_current == 12 && ut_active)) {
            // Solo el nombre de la emisora.
        } else {
        const char* sub = BtnSubLine();
        float subX = lp.x + H * 0.01f;
        float subY = lp.y + H * 0.008f + ns;
        if (g_current == 12 && ut_active) {
            float subW = font->CalcTextSizeA(ts, FLT_MAX, 0.0f, sub, nullptr).x;
            float availW = g_lblClipR - g_lblClipL;
            float off = MarqueeOff(sub, subW, availW);
            trkSubY = subY;
            drawTextTo(vTrk, sub, g_lblClipL - off, subY, ts, 200.0f/255.0f, 225.0f/255.0f, 235.0f/255.0f, 230.0f * opF / 255.0f);
            if (off > 0) drawTextTo(vTrk, sub, g_lblClipL - off + subW + 60.0f, subY, ts, 200.0f/255.0f, 225.0f/255.0f, 235.0f/255.0f, 230.0f * opF / 255.0f);
        } else {
            drawText(sub, subX, subY, ts, 200.0f/255.0f, 225.0f/255.0f, 235.0f/255.0f, 230.0f * opF / 255.0f);
        }
        }
    }
    // Icono de la estacion.
    // v2.55: fondo negro detras del icono (como en ImGui: el fondo negro que
    // siempre tuvo el boton; el icono puede ser transparente con el fondo de
    // color encendido). v2.56: DISCO negro (no cuadro: se notaba el cuadrado).
    circleTo(vBg, bp.x + bs / 2, bp.y + bs / 2, bs / 2, 0.0f, 0.0f, 0.0f, opF);
    GLuint it = IconTex(g_current);
    if (it) quadTo(vIco, bp.x, bp.y, bp.x + bs, bp.y + bs, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, opF);
    // v1.9.54: boton propio de la rueda (encima del flotante): disco negro +
    // icono de rueda incrustado (si no cargo, el icono de la emisora).
    // v1.9.63: con el boton nativo activo, el widget del juego lo dibuja y
    // aqui no se dibuja nada (vWheel queda vacio).
    if (!NativeWheelActive()) {
        // v1.9.60: el disco es CASI tan transparente como los botones del
        // juego (35% de la opacidad propia del boton de la rueda); el icono
        // blanco va a la opacidad propia (opRuedaF), no a la del flotante.
        // La geometria (wp/ws) ya se calculo una vez arriba (optimizar).
        circleTo(vBg, wp.x + ws / 2, wp.y + ws / 2, ws / 2, 0.0f, 0.0f, 0.0f, opRuedaF * 0.35f);
        GLuint wt = g_texWheel ? g_texWheel : it;
        if (wt) quadTo(vWheel, wp.x, wp.y, wp.x + ws, wp.y + ws, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, opRuedaF);
    }
    // v1.5: flechas anterior/siguiente DENTRO de la etiqueta (solo en la 12),
    // a los lados del nombre de la cancion.
    if (g_lblArrows) {
        ImVec2 ap = g_lblPrevC, an = g_lblNextC;
        float ar = g_lblArrowR;
        circleTo(vBg, ap.x, ap.y, ar, 1.0f, 1.0f, 1.0f, 60.0f * opF / 255.0f);
        circleTo(vBg, an.x, an.y, ar, 1.0f, 1.0f, 1.0f, 60.0f * opF / 255.0f);
        float tr = ar * 0.55f;
        vBg.push_back({ap.x - tr*0.4f, ap.y - tr, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 230.0f * opF / 255.0f});
        vBg.push_back({ap.x - tr*0.4f, ap.y + tr, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 230.0f * opF / 255.0f});
        vBg.push_back({ap.x + tr*0.6f, ap.y, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 230.0f * opF / 255.0f});
        vBg.push_back({an.x + tr*0.4f, an.y - tr, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 230.0f * opF / 255.0f});
        vBg.push_back({an.x + tr*0.4f, an.y + tr, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 230.0f * opF / 255.0f});
        vBg.push_back({an.x - tr*0.6f, an.y, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 230.0f * opF / 255.0f});
    }
    // v1.9.73: aviso de emisora (icono + nombre) en el camino GL (cuando
    // solo esta el boton, el cuadro ImGui no corre). El icono va en vIco
    // (misma textura de emisora que el boton) y el texto en vTxt (atlas).
    {
        const char* popName = kStationName[g_current];
        float pns = ns * g_cfg.popupTam / 100.0f;  // v1.9.74: tamano del aviso
        float popTextW = font->CalcTextSizeA(pns, FLT_MAX, 0.0f, popName, nullptr).x;
        ImVec2 pi0, pi1, ptPos; bool popIco, popTxt;
        if (PopupGeom(W, H, pns, popTextW, pi0, pi1, ptPos, popIco, popTxt)) {
            if (popIco) quadTo(vIco, pi0.x, pi0.y, pi1.x, pi1.y, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f);
            if (popTxt) {
                drawTextTo(vTxt, popName, ptPos.x + 2.0f, ptPos.y + 2.0f, pns, 0.0f, 0.0f, 0.0f, 1.0f);
                drawTextTo(vTxt, popName, ptPos.x, ptPos.y, pns, 1.0f, 1.0f, 1.0f, 1.0f);
            }
        }
    }
    // Guardar estado GL del juego (incluido el VAO y el framebuffer).
    GLint prevProg = 0, prevTex = 0, prevArr = 0, prevElem = 0, prevActive = 0;
    glGetIntegerv(GL_CURRENT_PROGRAM, &prevProg);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &prevTex);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &prevArr);
    glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING, &prevElem);
    glGetIntegerv(GL_ACTIVE_TEXTURE, &prevActive);
    GLint prevVAO = 0, prevFB = 0;
    if (g_glBtnUseVAO) glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &prevVAO);
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFB);
    GLboolean prevBlend = glIsEnabled(GL_BLEND);
    GLboolean prevScissor = glIsEnabled(GL_SCISSOR_TEST);
    GLboolean prevDepth = glIsEnabled(GL_DEPTH_TEST);
    GLint vp[4]; glGetIntegerv(GL_VIEWPORT, vp);
    // Proyeccion ortografica (pixeles -> NDC, Y hacia abajo).
    float proj[16] = {0};
    proj[0] = 2.0f / W; proj[5] = -2.0f / H; proj[10] = -1.0f; proj[12] = -1.0f; proj[13] = 1.0f; proj[15] = 1.0f;
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_SCISSOR_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    if (g_glBtnUseVAO) g_pBindVA(g_glBtnVAO);
    else {
        glBindBuffer(GL_ARRAY_BUFFER, g_glBtnVBO);
        glEnableVertexAttribArray(0); glEnableVertexAttribArray(1); glEnableVertexAttribArray(2);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(GLBtnVert), (void*)0);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(GLBtnVert), (void*)8);
        glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(GLBtnVert), (void*)16);
    }
    glActiveTexture(GL_TEXTURE0);
    glUseProgram(g_glBtnProg);
    GLint uProj = g_glBtnUProj;  // v2.51: cacheados en GLBtnInit
    GLint uTex = g_glBtnUTex;
    glUniformMatrix4fv(uProj, 1, GL_FALSE, proj);
    glUniform1i(uTex, 0);
    glBindBuffer(GL_ARRAY_BUFFER, g_glBtnVBO);
    auto drawVerts = [&](const std::vector<GLBtnVert>& verts, GLuint tex) {
        if (verts.empty()) return;
        glBindTexture(GL_TEXTURE_2D, tex);
        glBufferData(GL_ARRAY_BUFFER, verts.size() * sizeof(GLBtnVert), verts.data(), GL_STREAM_DRAW);
        glDrawArrays(GL_TRIANGLES, 0, (GLsizei)verts.size());
    };
    drawVerts(vBg, g_glBtnWhite);
    drawVerts(vTxt, atlasTex);
    // v1.3: el nombre de la cancion se recorta a la etiqueta (marquesina).
    // v1.5: el recorte es el hueco ENTRE las flechas (no toda la etiqueta).
    // La tijera va en coordenadas GL (Y desde abajo).
    if (!vTrk.empty()) {
        glEnable(GL_SCISSOR_TEST);
        float scX = g_lblArrows ? g_lblClipL : lp.x;
        float scW = g_lblArrows ? (g_lblClipR - g_lblClipL) : ls.x;
        glScissor((GLint)scX, (GLint)(H - (trkSubY + ts * 1.2f)), (GLint)scW, (GLint)(ts * 1.2f));
        drawVerts(vTrk, atlasTex);
        glDisable(GL_SCISSOR_TEST);
    }
    drawVerts(vIco, it ? it : g_glBtnWhite);
    if (!vWheel.empty()) drawVerts(vWheel, g_texWheel ? g_texWheel : (it ? it : g_glBtnWhite));  // v1.9.54: icono del boton de la rueda
    // Circulo blanco alrededor del icono (linea).
    {
        const int SEG = 40;
        std::vector<GLBtnVert> circ;
        float cx = bp.x + bs / 2, cy = bp.y + bs / 2, rr = bs / 2;
        for (int i = 0; i <= SEG; i++) {
            float a = (float)i / SEG * 2.0f * 3.14159265f;
            circ.push_back({cx + cosf(a) * rr, cy + sinf(a) * rr, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 200.0f * opF / 255.0f});
        }
        if (!circ.empty()) {
            glBindTexture(GL_TEXTURE_2D, g_glBtnWhite);
            glBufferData(GL_ARRAY_BUFFER, circ.size() * sizeof(GLBtnVert), circ.data(), GL_STREAM_DRAW);
            glDrawArrays(GL_LINE_STRIP, 0, (GLsizei)circ.size());
        }
    }
    // v1.9.54: circulo blanco alrededor del boton de la rueda (linea).
    // v1.9.60: con la opacidad propia del boton de la rueda (opRuedaF); la
    // geometria (wp/ws) ya se calculo una vez arriba (optimizar).
    // v1.9.63: con el boton nativo activo no se dibuja (lo dibuja el juego).
    if (!NativeWheelActive()) {
        const int SEG = 40;
        std::vector<GLBtnVert> circ;
        circ.reserve(48);
        float cx = wp.x + ws / 2, cy = wp.y + ws / 2, rr = ws / 2;
        for (int i = 0; i <= SEG; i++) {
            float a = (float)i / SEG * 2.0f * 3.14159265f;
            circ.push_back({cx + cosf(a) * rr, cy + sinf(a) * rr, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 200.0f * opRuedaF / 255.0f});
        }
        if (!circ.empty()) {
            glBindTexture(GL_TEXTURE_2D, g_glBtnWhite);
            glBufferData(GL_ARRAY_BUFFER, circ.size() * sizeof(GLBtnVert), circ.data(), GL_STREAM_DRAW);
            glDrawArrays(GL_LINE_STRIP, 0, (GLsizei)circ.size());
        }
    }
    // Restaurar estado.
    if (g_glBtnUseVAO) g_pBindVA((GLuint)prevVAO);
    else { glDisableVertexAttribArray(0); glDisableVertexAttribArray(1); glDisableVertexAttribArray(2); }
    glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)prevFB);
    glBindBuffer(GL_ARRAY_BUFFER, (GLuint)prevArr);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, (GLuint)prevElem);
    glBindTexture(GL_TEXTURE_2D, (GLuint)prevTex);
    glActiveTexture((GLenum)prevActive);
    glUseProgram((GLuint)prevProg);
    if (prevBlend) glEnable(GL_BLEND); else glDisable(GL_BLEND);
    if (prevScissor) glEnable(GL_SCISSOR_TEST); else glDisable(GL_SCISSOR_TEST);
    if (prevDepth) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
    glViewport(vp[0], vp[1], vp[2], vp[3]);
    return true;
}

// v2.57: BLUR DE FONDO POR REDUCCION (FBO). La 2.56 no se notaba nada al
// 100%: mezclaba 5 copias con solo 12 px de desplazamiento, imperceptible
// en 2408 px. Ahora: se copia el framebuffer a una textura, se reduce a una
// textura pequena (2x/4x/8x segun la barra) dibujandola en un FBO con el
// programa del boton, y se amplifica a pantalla completa con filtro lineal:
// eso SI se ve borroso. La barra 0-100 sigue igual (0 = apagado, 1-33 = 2x
// suave, 34-66 = 4x medio, 67-100 = 8x fuerte). v1.8: la reduccion es una
// PIRAMIDE POR MITADES (cada paso baja solo 2x: escena -> mitad -> cuarto
// -> pequena), que promedia bien y sale suave como un blur gaussiano, sin
// los bloques del bajado en un solo salto grande. Si el FBO no esta
// completo, no hay blur (solo el fondo oscuro). Cuesta una copia + hasta 3
// quads por cuadro con la rueda abierta. Las UVs van invertidas en Y (la
// copia del framebuffer sale al reves). Se dibuja con GL directo (no ImGui)
// antes de que ImGui pinte el fondo oscuro y la rueda.
static GLuint g_blurCopyTex = 0, g_blurSmallTex = 0, g_blurFBO = 0, g_blurResolveFBO = 0;
static GLuint g_blurMidTex = 0, g_blurMidFBO = 0;  // v1.6: textura intermedia (mitad) para bajar en 2 pasos y que no se pixele
static GLuint g_blurQtrTex = 0, g_blurQtrFBO = 0;  // v1.8: textura intermedia (cuarto) para la piramide por mitades (blur gaussiano)
static GLuint g_blurTmpTex = 0, g_blurTmpFBO = 0;  // v1.9: textura temporal para el pase gaussiano (ping-pong con la pequena)
static int g_blurCW = 0, g_blurCH = 0, g_blurSW = 0, g_blurSH = 0, g_blurFactor = 0;
static int g_blurMW = 0, g_blurMH = 0;
static int g_blurQW = 0, g_blurQH = 0;
static int BlurFactor() {
    if (g_cfg.blur <= 0) return 0;
    if (g_cfg.blur <= 33) return 2;   // v1.7: menos pixeliado (el 16x dejaba la textura en 150x67 y se veian los cuadros al 100%)
    // v1.9.18: tope 4x. El 8x a 100% (301x135) se ve pixeliado al ampliar a
    // 2408x1080 aunque el gaussiano corra 2 veces: no hay resolucion que
    // suavizar. La fuerza extra a blur alto viene de mas iteraciones del
    // gaussiano (BlurGaussIters), no de bajar mas la resolucion.
    return 4;
}
// Iteraciones del gaussiano segun el % de blur: a mas blur, mas pasadas
// (cada pasada ensancha el desenfoque) sin bajar la resolucion.
static int BlurGaussIters() {
    int b = g_cfg.blur;
    if (b <= 33) return 1;
    if (b <= 66) return 2;
    return 3;
}
static void BlurDrawBackground(float W, float H) {
    int factor = BlurFactor();
    if (factor <= 0) return;
    int w = (int)W, h = (int)H;
    if (w <= 0 || h <= 0) return;
    if (!GLBtnInit()) return;
    int sw = w / factor, sh = h / factor;
    if (sw < 8) sw = 8; if (sh < 8) sh = 8;
    int mw = w / 2, mh = h / 2;  // v1.6: paso intermedio (mitad) para el blur en 2 pasos
    if (mw < 8) mw = 8; if (mh < 8) mh = 8;
    int qw = w / 4, qh = h / 4;  // v1.8: paso intermedio (cuarto) para la piramide por mitades
    if (qw < 8) qw = 8; if (qh < 8) qh = 8;
    bool blurRecreated = false;  // v1.9.69: la copia es obligatoria tras recrear texturas
    if (!g_blurCopyTex || g_blurCW != w || g_blurCH != h || !g_blurSmallTex || g_blurSW != sw || g_blurSH != sh || g_blurFactor != factor || !g_blurFBO || !g_blurMidTex || g_blurMW != mw || g_blurMH != mh || !g_blurMidFBO || !g_blurQtrTex || g_blurQW != qw || g_blurQH != qh || !g_blurQtrFBO || !g_blurTmpTex || !g_blurTmpFBO) {
        blurRecreated = true;
        if (g_blurFBO) { glDeleteFramebuffers(1, &g_blurFBO); g_blurFBO = 0; }
        if (g_blurResolveFBO) { glDeleteFramebuffers(1, &g_blurResolveFBO); g_blurResolveFBO = 0; }
        if (g_blurMidFBO) { glDeleteFramebuffers(1, &g_blurMidFBO); g_blurMidFBO = 0; }
        if (g_blurQtrFBO) { glDeleteFramebuffers(1, &g_blurQtrFBO); g_blurQtrFBO = 0; }
        if (g_blurTmpFBO) { glDeleteFramebuffers(1, &g_blurTmpFBO); g_blurTmpFBO = 0; }
        if (g_blurCopyTex) { glDeleteTextures(1, &g_blurCopyTex); g_blurCopyTex = 0; }
        if (g_blurSmallTex) { glDeleteTextures(1, &g_blurSmallTex); g_blurSmallTex = 0; }
        if (g_blurMidTex) { glDeleteTextures(1, &g_blurMidTex); g_blurMidTex = 0; }
        if (g_blurQtrTex) { glDeleteTextures(1, &g_blurQtrTex); g_blurQtrTex = 0; }
        if (g_blurTmpTex) { glDeleteTextures(1, &g_blurTmpTex); g_blurTmpTex = 0; }
        glGenTextures(1, &g_blurCopyTex);
        glBindTexture(GL_TEXTURE_2D, g_blurCopyTex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glGenTextures(1, &g_blurSmallTex);
        glBindTexture(GL_TEXTURE_2D, g_blurSmallTex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, sw, sh, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glGenFramebuffers(1, &g_blurFBO);
        glBindFramebuffer(GL_FRAMEBUFFER, g_blurFBO);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, g_blurSmallTex, 0);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            Log("  !! blur: FBO incompleto, sin desenfoque");
            glDeleteFramebuffers(1, &g_blurFBO); g_blurFBO = 0;
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            return;
        }
        // v1.6: textura intermedia (mitad de la escena) para bajar el blur en
        // 2 pasos (escena -> mitad -> pequena): cada paso promedia bien y la
        // imagen final sale suave, sin los bloques del bajado en un solo paso.
        glGenTextures(1, &g_blurMidTex);
        glBindTexture(GL_TEXTURE_2D, g_blurMidTex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, mw, mh, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glGenFramebuffers(1, &g_blurMidFBO);
        glBindFramebuffer(GL_FRAMEBUFFER, g_blurMidFBO);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, g_blurMidTex, 0);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            Log("  !! blur: FBO intermedio incompleto, sin desenfoque");
            glDeleteFramebuffers(1, &g_blurMidFBO); g_blurMidFBO = 0;
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            return;
        }
        // v1.8: textura intermedia (cuarto de la escena) para la piramide
        // por mitades (blur gaussiano): cada paso baja solo 2x y promedia
        // bien, asi el resultado es suave como un gaussiano y no se ven
        // los bloques del bajado en un solo salto grande.
        glGenTextures(1, &g_blurQtrTex);
        glBindTexture(GL_TEXTURE_2D, g_blurQtrTex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, qw, qh, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glGenFramebuffers(1, &g_blurQtrFBO);
        glBindFramebuffer(GL_FRAMEBUFFER, g_blurQtrFBO);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, g_blurQtrTex, 0);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            Log("  !! blur: FBO cuarto incompleto, sin desenfoque");
            glDeleteFramebuffers(1, &g_blurQtrFBO); g_blurQtrFBO = 0;
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            return;
        }
        // v1.4: FBO para resolver el framebuffer del juego (MSAA) a la
        // textura de copia. Sin esto, glCopyTexImage2D falla con 0x502.
        glGenFramebuffers(1, &g_blurResolveFBO);
        glBindFramebuffer(GL_FRAMEBUFFER, g_blurResolveFBO);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, g_blurCopyTex, 0);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            Log("  !! blur: FBO de resolucion incompleto (se usa copia directa)");
            glDeleteFramebuffers(1, &g_blurResolveFBO); g_blurResolveFBO = 0;
        }
        // v1.9: textura temporal (mismo tamano que la pequena) para el pase
        // gaussiano: horizontal a la temporal, vertical de vuelta a la pequena.
        glGenTextures(1, &g_blurTmpTex);
        glBindTexture(GL_TEXTURE_2D, g_blurTmpTex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, sw, sh, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glGenFramebuffers(1, &g_blurTmpFBO);
        glBindFramebuffer(GL_FRAMEBUFFER, g_blurTmpFBO);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, g_blurTmpTex, 0);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            Log("  !! blur: FBO temporal incompleto, sin desenfoque gaussiano");
            glDeleteFramebuffers(1, &g_blurTmpFBO); g_blurTmpFBO = 0;
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            return;
        }
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        g_blurCW = w; g_blurCH = h; g_blurSW = sw; g_blurSH = sh; g_blurFactor = factor; g_blurMW = mw; g_blurMH = mh; g_blurQW = qw; g_blurQH = qh;
        Log("  blur: FBO listo factor=%dx (%dx%d -> %dx%d)%s", factor, w, h, sw, sh, g_pBlitFB ? " (blit MSAA)" : " (copia directa)");
    }
    // guardar estado GL del juego
    GLint prevProg = 0, prevTex = 0, prevArr = 0, prevElem = 0, prevActive = 0, prevFB = 0, prevVAO = 0;
    glGetIntegerv(GL_CURRENT_PROGRAM, &prevProg);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &prevTex);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &prevArr);
    glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING, &prevElem);
    glGetIntegerv(GL_ACTIVE_TEXTURE, &prevActive);
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFB);
    if (g_glBtnUseVAO) glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &prevVAO);
    GLboolean prevBlend = glIsEnabled(GL_BLEND);
    GLboolean prevScissor = glIsEnabled(GL_SCISSOR_TEST);
    GLboolean prevDepth = glIsEnabled(GL_DEPTH_TEST);
    GLint vp[4]; glGetIntegerv(GL_VIEWPORT, vp);
    // 1. copiar la escena del juego a g_blurCopyTex (framebuffer por defecto).
    // v1.4: con glBlitFramebuffer se resuelve el MSAA del juego (la copia
    // directa fallaba con GL 0x502). Si no hay blit, se intenta la copia.
    // v1.9.50 (Daniel): fondo EN VIVO: la escena se copia CADA cuadro
    // mientras la rueda esta abierta, asi el fondo se mueve en camara lenta
    // con el juego (la rueda ya pone 0.3x). Antes (v1.9.14, Rehan) se
    // congelaba a los 15 cuadros porque manejando el blur en vivo se veia
    // peor que parado; Daniel lo quiere en vivo ahora. El gaussiano y la
    // piramide ya corrian cada cuadro, solo la copia estaba congelada, asi
    // que el unico cambio es copiar siempre. Si la copia falla, se registra
    // el error y se sigue sin desenfoque (como antes).
    // v1.9.69 (optimizar): la copia a pantalla completa es la operacion mas
    // cara con la rueda abierta; el juego va a 0.3x, asi que el fondo apenas
    // se mueve: copiar 1 de cada 2 cuadros es invisible y baja el costo a la
    // mitad. La piramide y el gaussiano siguen cada cuadro sobre la copia
    // (de 1 cuadro de antiguedad). Tras recrear texturas o al abrir la rueda
    // (mas de 100 ms sin llamar), la copia es obligatoria.
    static int s_blurCopyTick = 0;
    static auto s_blurLastCall = std::chrono::steady_clock::now();
    auto blurNow = std::chrono::steady_clock::now();
    bool blurGap = std::chrono::duration_cast<std::chrono::milliseconds>(blurNow - s_blurLastCall).count() > 100;
    s_blurLastCall = blurNow;
    bool doCopy = blurRecreated || blurGap || ((s_blurCopyTick++ & 1) == 0);
    if (doCopy) {
        glActiveTexture(GL_TEXTURE0);  // v1.3: la copia va a la unidad 0 (antes dependia de la que dejara el juego)
        if (g_pBlitFB && g_blurResolveFBO) {
            glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, g_blurResolveFBO);
            g_pBlitFB(0, 0, w, h, 0, 0, w, h, GL_COLOR_BUFFER_BIT, GL_NEAREST);
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
        } else {
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            glBindTexture(GL_TEXTURE_2D, g_blurCopyTex);
            glCopyTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 0, 0, w, h, 0);
        }
        { GLenum e = glGetError(); if (e != GL_NO_ERROR) { Log("  !! blur: no se pudo copiar la escena (GL 0x%X): sin desenfoque", (unsigned)e); goto blur_restore; } }
    }
    // preparar el programa del boton (proyeccion ortografica, Y hacia abajo)
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_SCISSOR_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    if (g_glBtnUseVAO) g_pBindVA(g_glBtnVAO);
    else {
        glBindBuffer(GL_ARRAY_BUFFER, g_glBtnVBO);
        glEnableVertexAttribArray(0); glEnableVertexAttribArray(1); glEnableVertexAttribArray(2);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(GLBtnVert), (void*)0);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(GLBtnVert), (void*)8);
        glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(GLBtnVert), (void*)16);
    }
    glActiveTexture(GL_TEXTURE0);
    glUseProgram(g_glBtnProg);
    glUniform1i(g_glBtnUTex, 0);
    // quad a pantalla completa (x0,y0)-(x1,y1), UV invertida en Y, blanco opaco
    {
        auto drawQuad = [&](GLuint tex, float x0, float y0, float x1, float y1, const float* proj) {
            GLBtnVert q[6] = {
                {x0, y0, 0.0f, 1.0f, 1,1,1,1}, {x1, y0, 1.0f, 1.0f, 1,1,1,1}, {x1, y1, 1.0f, 0.0f, 1,1,1,1},
                {x0, y0, 0.0f, 1.0f, 1,1,1,1}, {x1, y1, 1.0f, 0.0f, 1,1,1,1}, {x0, y1, 0.0f, 0.0f, 1,1,1,1},
            };
            glUniformMatrix4fv(g_glBtnUProj, 1, GL_FALSE, proj);
            glBindTexture(GL_TEXTURE_2D, tex);
            glBindBuffer(GL_ARRAY_BUFFER, g_glBtnVBO);
            glBufferData(GL_ARRAY_BUFFER, sizeof(q), q, GL_STREAM_DRAW);
            glDrawArrays(GL_TRIANGLES, 0, 6);
        };
        // v1.9: dibuja un quad con el programa del blur gaussiano (uDir = un
        // texel en la direccion del pase). Reusa el mismo VBO/VAO del boton.
        auto drawGauss = [&](GLuint tex, float x0, float y0, float x1, float y1, const float* proj, float dx, float dy) {
            if (!g_blurOk || !g_blurProg) return;
            glUseProgram(g_blurProg);
            GLBtnVert q[6] = {
                {x0, y0, 0.0f, 1.0f, 1,1,1,1}, {x1, y0, 1.0f, 1.0f, 1,1,1,1}, {x1, y1, 1.0f, 0.0f, 1,1,1,1},
                {x0, y0, 0.0f, 1.0f, 1,1,1,1}, {x1, y1, 1.0f, 0.0f, 1,1,1,1}, {x0, y1, 0.0f, 0.0f, 1,1,1,1},
            };
            glUniformMatrix4fv(g_blurUProj, 1, GL_FALSE, proj);
            glUniform1i(g_blurUTex, 0);
            glUniform2f(g_blurUDir, dx, dy);
            glBindTexture(GL_TEXTURE_2D, tex);
            glBindBuffer(GL_ARRAY_BUFFER, g_glBtnVBO);
            glBufferData(GL_ARRAY_BUFFER, sizeof(q), q, GL_STREAM_DRAW);
            glDrawArrays(GL_TRIANGLES, 0, 6);
            glUseProgram(g_glBtnProg);  // el boton usa su propio programa; se restaura para el quad de ampliar
        };
        float projS[16] = {0}, projF[16] = {0}, projM[16] = {0}, projQ[16] = {0};
        projS[0] = 2.0f / sw; projS[5] = -2.0f / sh; projS[10] = -1.0f; projS[12] = -1.0f; projS[13] = 1.0f; projS[15] = 1.0f;
        projF[0] = 2.0f / w; projF[5] = -2.0f / h; projF[10] = -1.0f; projF[12] = -1.0f; projF[13] = 1.0f; projF[15] = 1.0f;
        projM[0] = 2.0f / mw; projM[5] = -2.0f / mh; projM[10] = -1.0f; projM[12] = -1.0f; projM[13] = 1.0f; projM[15] = 1.0f;
        projQ[0] = 2.0f / qw; projQ[5] = -2.0f / qh; projQ[10] = -1.0f; projQ[12] = -1.0f; projQ[13] = 1.0f; projQ[15] = 1.0f;
        // 2. reducir en piramide por mitades (v1.8, blur gaussiano): cada
        // paso baja solo 2x y promedia bien, asi el resultado es suave y
        // no se ven los bloques. 2x = 1 paso, 4x = 2 pasos, 8x = 3 pasos.
        if (factor <= 2) {
            glBindFramebuffer(GL_FRAMEBUFFER, g_blurFBO);
            glViewport(0, 0, sw, sh);
            drawQuad(g_blurCopyTex, 0, 0, (float)sw, (float)sh, projS);
        } else if (factor <= 4) {
            glBindFramebuffer(GL_FRAMEBUFFER, g_blurMidFBO);
            glViewport(0, 0, mw, mh);
            drawQuad(g_blurCopyTex, 0, 0, (float)mw, (float)mh, projM);
            glBindFramebuffer(GL_FRAMEBUFFER, g_blurFBO);
            glViewport(0, 0, sw, sh);
            drawQuad(g_blurMidTex, 0, 0, (float)sw, (float)sh, projS);
        } else {
            glBindFramebuffer(GL_FRAMEBUFFER, g_blurMidFBO);
            glViewport(0, 0, mw, mh);
            drawQuad(g_blurCopyTex, 0, 0, (float)mw, (float)mh, projM);
            glBindFramebuffer(GL_FRAMEBUFFER, g_blurQtrFBO);
            glViewport(0, 0, qw, qh);
            drawQuad(g_blurMidTex, 0, 0, (float)qw, (float)qh, projQ);
            glBindFramebuffer(GL_FRAMEBUFFER, g_blurFBO);
            glViewport(0, 0, sw, sh);
            drawQuad(g_blurQtrTex, 0, 0, (float)sw, (float)sh, projS);
        }
        // 2b. blur gaussiano (v1.9): 2 pases separables sobre la textura
        // pequena (horizontal a la temporal, vertical de vuelta a la
        // pequena). Esto es lo que quita los cuadros al 100%: el gaussiano
        // promedia con pesos de campana, no solo 2x2 como el lineal.
        // v1.9.16/1.9.18: varias iteraciones H+V (segun el % de blur) para
        // que ni al 100% se vean los cuadros: una sola pasada no suaviza
        // bastante. La fuerza viene de las iteraciones, no del 8x.
        if (GLBlurInit() && g_blurTmpFBO) {
            for (int gi = 0; gi < BlurGaussIters(); gi++) {
                glBindFramebuffer(GL_FRAMEBUFFER, g_blurTmpFBO);
                glViewport(0, 0, sw, sh);
                drawGauss(g_blurSmallTex, 0, 0, (float)sw, (float)sh, projS, 1.0f / sw, 0.0f);
                glBindFramebuffer(GL_FRAMEBUFFER, g_blurFBO);
                glViewport(0, 0, sw, sh);
                drawGauss(g_blurTmpTex, 0, 0, (float)sw, (float)sh, projS, 0.0f, 1.0f / sh);
            }
        }
        // 3. ampliar: dibujar la textura pequena en el framebuffer por defecto
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, w, h);
        drawQuad(g_blurSmallTex, 0, 0, (float)w, (float)h, projF);
    }
blur_restore:
    // restaurar estado
    if (g_glBtnUseVAO) g_pBindVA((GLuint)prevVAO);
    else { glDisableVertexAttribArray(0); glDisableVertexAttribArray(1); glDisableVertexAttribArray(2); }
    glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)prevFB);
    glBindBuffer(GL_ARRAY_BUFFER, (GLuint)prevArr);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, (GLuint)prevElem);
    glBindTexture(GL_TEXTURE_2D, (GLuint)prevTex);
    glActiveTexture((GLenum)prevActive);
    glUseProgram((GLuint)prevProg);
    if (prevBlend) glEnable(GL_BLEND); else glDisable(GL_BLEND);
    if (prevScissor) glEnable(GL_SCISSOR_TEST); else glDisable(GL_SCISSOR_TEST);
    if (prevDepth) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
    glViewport(vp[0], vp[1], vp[2], vp[3]);
}

static void DrawWheel(float W, float H) {
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    ImFont* font = g_fontTitle ? g_fontTitle : ImGui::GetFont();
    // v2.53: animacion de apertura (solo si esta activa): la rueda crece de
    // 0.85 a 1.0 y se funde en 150 ms. Se mide desde que se abrio (cualquier
    // via: claxon, boton, vista previa); si esta apagada, anim = 1 siempre.
    // v2.54: el inicio se guarda en g_wheelAnimStart al abrir (ToggleWheel /
    // vista previa), no aqui: DrawWheel NO se llama cuando solo esta el boton
    // (DrawFrame usa DrawButtonGL y sale antes), asi que el static de antes
    // nunca se reseteaba al cerrar y la animacion solo se veia "entre veces".
    // Dura 250 ms y crece desde 0.75 para que siempre se note.
    // v1.9.55: animacion de CIERRE: al cerrar, la rueda encoge de 1.0 a 0.75
    // y se funde en 250 ms (anim va de 1 a 0); si esta apagada, anim = 1.
    float anim = 1.0f;
    if (g_cfg.animacion && g_open) {
        float t = std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - g_wheelAnimStart).count() / 250.0f;
        if (t < 0.0f) t = 0.0f; if (t > 1.0f) t = 1.0f;
        anim = 1.0f - (1.0f - t) * (1.0f - t);  // ease-out
    } else if (g_cfg.animacion && g_wheelClosing) {
        float t = std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - g_wheelCloseStart).count() / 250.0f;
        if (t < 0.0f) t = 0.0f;
        if (t >= 1.0f) { g_wheelClosing = false; anim = 0.0f; }
        else anim = (1.0f - t) * (1.0f - t);  // ease-in: de 1 a 0 (encoge y se funde)
    }
    // v1.9.60: el boton flotante + etiqueta (lo que esta sonando) va en una
    // funcion local: con la rueda CERRADA se dibuja aqui (como antes, para
    // el panel/lista); con la rueda ABIERTA se dibuja AL FINAL, encima del
    // fondo y los segmentos, para que lo que suena NO se quite ni lo tape
    // el fondo oscuro o el desenfoque.
    auto drawFloatBtn = [&]() {
        // v2.2: boton flotante + etiqueta con estacion y genero; v2.16: sin radio
        // el boton no se ve (y el panel tampoco se puede abrir); con opacidad ajustable
        if (g_hasRadio && g_cfg.botonVisible) {
            ImVec2 bp, lp, ls; float bs;
            FloatBtnGeom(W, H, bp, lp, ls, bs);
            float ns, ts;
            BtnLabelSizes(H, ns, ts);  // v1.5: la etiqueta escala con el tamano del boton
            int op = g_cfg.btnOpac * 255 / 100;
            // v2.54: el recuadro ENTERO (fondo + estacion + genero) se oculta si
            // esta activa "Ocultar ventana de emisora".
            // v1.7: "Ocultar genero" ya no oculta la ventana (solo la 2a linea).
            if (!g_cfg.ocultarNombre) {
                dl->AddRectFilled(lp, ImVec2(lp.x + ls.x, lp.y + ls.y), IM_COL32(0, 0, 0, 140 * op / 255), H * 0.01f);
                dl->AddText(font, ns, ImVec2(lp.x + H * 0.01f, lp.y + H * 0.008f), IM_COL32(255,255,255,op), kStationName[g_current]);
                // v1.3: en la 12, el nombre de la cancion se desplaza si no cabe en la etiqueta.
                // v1.5: el nombre va ENTRE las flechas (dentro de la etiqueta).
                // v1.7: la 2a linea (genero/cancion) solo si mostrarGenero.
                // v1.8: en la 12 la 2a linea es el nombre de la cancion: siempre.
                if (g_cfg.mostrarGenero || (g_current == 12 && ut_active)) {
                const char* sub = BtnSubLine();
                float subX = lp.x + H * 0.01f;
                float subY = lp.y + H * 0.008f + ns;
                if (g_current == 12 && ut_active) {
                    float subW = font->CalcTextSizeA(ts, FLT_MAX, 0.0f, sub, nullptr).x;
                    float availW = g_lblClipR - g_lblClipL;
                    float off = MarqueeOff(sub, subW, availW);
                    dl->PushClipRect(ImVec2(g_lblClipL, subY), ImVec2(g_lblClipR, subY + ts * 1.2f), true);
                    if (off <= 0.0f) {
                        dl->AddText(font, ts, ImVec2(g_lblClipL, subY), IM_COL32(200,225,235,230 * op / 255), sub);
                    } else {
                        dl->AddText(font, ts, ImVec2(g_lblClipL - off, subY), IM_COL32(200,225,235,230 * op / 255), sub);
                        dl->AddText(font, ts, ImVec2(g_lblClipL - off + subW + 60.0f, subY), IM_COL32(200,225,235,230 * op / 255), sub);
                    }
                    dl->PopClipRect();
                } else {
                    dl->AddText(font, ts, ImVec2(subX, subY), IM_COL32(200,225,235,230 * op / 255), sub);
                }
                }
            }
            // v2.55: disco negro detras del icono de la emisora (el fondo negro
            // que siempre tuvo el boton; con el fondo de color encendido el icono
            // queda transparente y sin esto se veria el fondo del boton).
            { GLuint it = IconTex(g_current); if (it) { dl->AddCircleFilled(ImVec2(bp.x + bs/2, bp.y + bs/2), bs/2, IM_COL32(0, 0, 0, op)); dl->AddImageRounded((ImTextureID)(uintptr_t)it, bp, ImVec2(bp.x + bs, bp.y + bs), ImVec2(0, 0), ImVec2(1, 1), IM_COL32(255, 255, 255, op), bs/2); } else dl->AddCircleFilled(ImVec2(bp.x + bs/2, bp.y + bs/2), bs/2, IM_COL32(30,30,40,220 * op / 255)); }
            dl->AddCircle(ImVec2(bp.x + bs/2, bp.y + bs/2), bs/2, IM_COL32(255,255,255,200 * op / 255), 0, 2.0f);
            // v1.5: flechas anterior/siguiente DENTRO de la etiqueta (solo en la 12),
            // a los lados del nombre de la cancion.
            if (g_lblArrows) {
                ImVec2 ap = g_lblPrevC, an = g_lblNextC;
                float ar = g_lblArrowR;
                dl->AddCircleFilled(ap, ar, IM_COL32(255,255,255,(int)(60 * op / 255)), 24);
                dl->AddCircleFilled(an, ar, IM_COL32(255,255,255,(int)(60 * op / 255)), 24);
                float tr = ar * 0.55f;
                dl->AddTriangleFilled(ImVec2(ap.x - tr*0.4f, ap.y - tr), ImVec2(ap.x - tr*0.4f, ap.y + tr), ImVec2(ap.x + tr*0.6f, ap.y), IM_COL32(255,255,255,(int)(230 * op / 255)));
                dl->AddTriangleFilled(ImVec2(an.x + tr*0.4f, an.y - tr), ImVec2(an.x + tr*0.4f, an.y + tr), ImVec2(an.x - tr*0.6f, an.y), IM_COL32(255,255,255,(int)(230 * op / 255)));
            }
        }

    };
    if (!g_open && !g_wheelClosing) drawFloatBtn();
    if (!g_open && !g_wheelClosing) return;  // v1.9.55: durante el cierre con animacion la rueda se sigue dibujando
    // v2.2: rueda estilo GTA V - 12 logos en circulo + apagar la radio en el centro
    ImVec2 c(W * 0.5f, H * 0.5f);
    float R = WheelR(H);
    float Ri = WheelRi(H) * (0.75f + 0.25f * anim);
    float s = WheelIconS(H) * (0.75f + 0.25f * anim);
    BlurDrawBackground(W, H);  // v2.56: escena borrosa detras (si el blur esta activo)
    dl->AddRectFilled(ImVec2(0, 0), ImVec2(W, H), IM_COL32(g_cfg.fondoR, g_cfg.fondoG, g_cfg.fondoB, (int)(g_cfg.fondo * 255 / 100 * anim)));  // v2.17: color de fondo ajustable (oscuridad = alfa)
    dl->AddCircle(c, Ri, IM_COL32(255, 255, 255, (int)(70 * anim)), 64, 2.0f);  // anillo fino (v1.9.60: con anim, antes era fijo 70 y era lo ultimo en cerrarse)
    for (int i = 1; i <= 13; i++) {
        float a0 = -3.14159265f/2 + (i-1) * 2*3.14159265f/13.0f;
        float a1 = a0 + 2*3.14159265f/13.0f;
        float am = (a0 + a1) * 0.5f;
        ImVec2 p(c.x + cosf(am) * Ri, c.y + sinf(am) * Ri);
        bool sel = (i == g_selected);
        float si = sel ? s * 1.12f : s;
        GLuint it = IconTex(i);
        if (g_cfg.fondoIcono) dl->AddCircleFilled(p, si, IM_COL32(g_cfg.fondoIconoR, g_cfg.fondoIconoG, g_cfg.fondoIconoB, (int)(255 * anim)));  // v2.53: fondo de color del icono
        if (it) {
            ImVec4 tint = sel ? ImVec4(1, 1, 1, anim) : ImVec4(0.55f, 0.55f, 0.55f, 0.75f * anim);
            // v1.9.14 (Daniel): las emisoras 8 (CSR) y 12 (User tracks) quedan
            // NORMALES, sin transparencia: su centro negro (User Track) y
            // morado (CSR) no se transparenta, y detras va el color de
            // fondoIcono. Se dibujan un poco mas chicas para que el color se
            // vea como anillo detras del icono opaco. (La 1.9.13 las teñia;
            // eso era lo contrario: el logo debe quedar con sus colores.)
            float siIcon = (g_cfg.fondoIcono && g_cfg.iconosColor && (i == 8 || i == 12)) ? si * 0.85f : si;
            // v2.56: icono en CIRCULO (redondeo = mitad del lado): antes era
            // un cuadrado y se notaba el cuadrado de la imagen.
            dl->AddImageRounded((ImTextureID)(uintptr_t)it, ImVec2(p.x-siIcon, p.y-siIcon), ImVec2(p.x+siIcon, p.y+siIcon), ImVec2(0, 0), ImVec2(1, 1), ImColor(tint), siIcon);
        } else dl->AddCircleFilled(p, si*0.5f, IM_COL32(255,255,255,(int)(120 * anim)));
        if (sel) dl->AddCircle(p, si, RingColor(anim), 48, 3.5f);  // anillo de seleccion (color ajustable, v1.9.60: con anim)
        else if (i == g_current) dl->AddCircle(p, si, IM_COL32(255, 255, 255, (int)(220 * anim)), 48, 2.0f);  // v1.9.60: con anim
    }
    // centro: nombre de la estacion seleccionada + genero (estilo GTA V)
    float nameSize = H * 0.05f, tagSize = H * 0.024f;
    const char* name = kStationName[g_selected];
    const char* tag = Genre(g_selected);
    ImVec2 nsz = font->CalcTextSizeA(nameSize, FLT_MAX, 0.0f, name, nullptr);
    ImVec2 tsz = font->CalcTextSizeA(tagSize, FLT_MAX, 0.0f, tag, nullptr);
    // v2.6: el texto del centro usa el color elegido (por defecto blanco)
    // v2.53: genero opcional; sin genero el nombre queda centrado (antes iba
    // desplazado arriba para dejar hueco al genero)
    ImU32 txtCol = IM_COL32(g_cfg.textoR, g_cfg.textoG, g_cfg.textoB, (int)(255 * anim));
    ImU32 tagCol = IM_COL32((int)(g_cfg.textoR * 0.8f), (int)(g_cfg.textoG * 0.8f), (int)(g_cfg.textoB * 0.8f), (int)(230 * anim));
    // v1.3: en la 12, la cancion que suena debajo del genero, con flechas
    // anterior/siguiente en posicion FIJA (antes dependian del ancho del
    // nombre y con nombres largos se juntaban al texto). El nombre se
    // desplaza (marquesina) entre las flechas si no cabe.
    // v1.4: en las demas emisoras, el programa/locutor que se esta
    // transmitiendo (como en la radio de GTA V).
    bool showTrack = (g_selected == 12 && ut_active);
    bool showProg = (g_selected >= 1 && g_selected <= 11 && g_cfg.mostrarGenero);
    float nameDy = 0.0f;
    if (g_cfg.mostrarGenero) nameDy -= H*0.015f;
    if (showProg) nameDy -= H*0.012f;
    if (showTrack) nameDy -= H*0.015f;
    dl->AddText(font, nameSize, ImVec2(c.x - nsz.x*0.5f, c.y - nsz.y*0.5f + nameDy), txtCol, name);
    if (g_cfg.mostrarGenero) dl->AddText(font, tagSize, ImVec2(c.x - tsz.x*0.5f, c.y + nsz.y*0.5f + H*0.005f), tagCol, tag);
    if (showProg) {
        const char* prog = kStationProgram[g_selected];
        float progSize = H * 0.02f;
        ImVec2 psz = font->CalcTextSizeA(progSize, FLT_MAX, 0.0f, prog, nullptr);
        float py = c.y + nsz.y*0.5f + H*0.005f + tsz.y + H*0.004f;
        dl->AddText(font, progSize, ImVec2(c.x - psz.x*0.5f, py), tagCol, prog);
    }
    g_trkArrows = false;
    if (showTrack) {
        const char* trk = UT_TrackDisplayName();
        float trkSize = H * 0.022f;
        float ty = c.y + nsz.y*0.5f + H*0.005f;
        if (g_cfg.mostrarGenero) ty += tsz.y + H*0.008f;
        // Medir el nombre una vez por cancion (no cada cuadro).
        static std::string trkMeas;
        static ImVec2 trkSz;
        if (trkMeas != trk) { trkMeas = trk; trkSz = font->CalcTextSizeA(trkSize, FLT_MAX, 0.0f, trk, nullptr); }
        ImVec2 ksz = trkSz;
        float ar = H * 0.022f;
        float ax = H * 0.16f;  // flechas a distancia fija del centro
        float ay = ty + ksz.y * 0.5f;
        g_trkPrevC = ImVec2(c.x - ax, ay); g_trkNextC = ImVec2(c.x + ax, ay); g_trkArrowR = ar; g_trkArrows = true;
        dl->AddCircleFilled(g_trkPrevC, ar, IM_COL32(255,255,255,(int)(60 * anim)), 24);
        dl->AddCircleFilled(g_trkNextC, ar, IM_COL32(255,255,255,(int)(60 * anim)), 24);
        float tr = ar * 0.55f;
        dl->AddTriangleFilled(ImVec2(g_trkPrevC.x - tr*0.4f, ay - tr), ImVec2(g_trkPrevC.x - tr*0.4f, ay + tr), ImVec2(g_trkPrevC.x + tr*0.6f, ay), IM_COL32(255,255,255,(int)(230 * anim)));
        dl->AddTriangleFilled(ImVec2(g_trkNextC.x + tr*0.4f, ay - tr), ImVec2(g_trkNextC.x + tr*0.4f, ay + tr), ImVec2(g_trkNextC.x - tr*0.6f, ay), IM_COL32(255,255,255,(int)(230 * anim)));
        // Nombre: centrado si cabe entre las flechas; si no, marquesina.
        float clipL = c.x - ax + ar + H*0.01f;
        float clipR = c.x + ax - ar - H*0.01f;
        float off = MarqueeOff(trk, ksz.x, clipR - clipL);
        dl->PushClipRect(ImVec2(clipL, ty), ImVec2(clipR, ty + ksz.y), true);
        if (off <= 0.0f) {
            dl->AddText(font, trkSize, ImVec2(c.x - ksz.x*0.5f, ty), tagCol, trk);
        } else {
            float x0 = clipL - off;
            dl->AddText(font, trkSize, ImVec2(x0, ty), tagCol, trk);
            dl->AddText(font, trkSize, ImVec2(x0 + ksz.x + 60.0f, ty), tagCol, trk);
        }
        dl->PopClipRect();
        // v1.4: boton "Lista de canciones" debajo del nombre (abre la lista
        // como la de GTA V para elegir la pista).
        {
            const char* lbl = S(STR_TL_BTN);  // v1.6: traducido segun el idioma
            float lsz = H * 0.02f;
            ImVec2 ltsz = font->CalcTextSizeA(lsz, FLT_MAX, 0.0f, lbl, nullptr);
            float bw = ltsz.x + H * 0.03f, bh = ltsz.y + H * 0.012f;
            float bx = c.x - bw * 0.5f, by = ty + ksz.y + H * 0.012f;
            g_listBtn = true; g_listBtnRect = ImVec4(bx, by, bw, bh);
            dl->AddRectFilled(ImVec2(bx, by), ImVec2(bx + bw, by + bh), IM_COL32(255,255,255,(int)(40 * anim)), H * 0.008f);
            dl->AddRect(ImVec2(bx, by), ImVec2(bx + bw, by + bh), IM_COL32(255,255,255,(int)(150 * anim)), H * 0.008f, 0, 1.5f);
            dl->AddText(font, lsz, ImVec2(c.x - ltsz.x * 0.5f, by + H * 0.006f), IM_COL32(255,255,255,(int)(230 * anim)), lbl);
        }
    } else {
        g_listBtn = false;
    }
    // v2.2: marca de vista previa
    if (g_preview) {
        const char* pv = S(STR_PREVIEW_LABEL);
        ImVec2 ps = font->CalcTextSizeA(H * 0.03f, FLT_MAX, 0.0f, pv, nullptr);
        dl->AddText(font, H * 0.03f, ImVec2(c.x - ps.x*0.5f, H * 0.03f), IM_COL32(255,255,255,230), pv);
    }
    if (g_open || g_wheelClosing) drawFloatBtn();  // v1.9.60: lo que suena, encima de la rueda
}

// ---- diseno del panel (azul casi oscuro + degradado azul -> azul claro) --------
static ImVec4 Mix(const ImVec4& a, const ImVec4& b, float t) {
    return ImVec4(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, a.w);
}
static float Clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
static const ImVec4 kPanelAccent(0.10f, 0.45f, 1.0f, 1.0f);   // azul (v2.6: el panel ya no es violeta)
static const ImVec4 kPanelGradBlue(0.05f, 0.55f, 1.00f, 1.0f); // azul del degradado del logo
static const ImVec4 kPanelGradCyan(0.10f, 0.65f, 1.0f, 1.0f); // azul claro del degradado
static ImVec4 PanelGradL() { return Mix(kPanelAccent, kPanelGradBlue, 0.55f); }
static ImVec4 PanelGradR() { return Mix(kPanelAccent, kPanelGradCyan, 0.55f); }
// Borde con degradado (izquierda -> derecha) siguiendo un rectangulo redondeado.
// Se dibuja como segmentos de linea gruesa con el color interpolado por la x;
// un circulo en cada union evita huecos en las esquinas (igual que en ARCX).
static void AddGradientBorder(ImDrawList* dl, const ImVec2& p0, const ImVec2& p1, float radius, float thick, const ImVec4& colL, const ImVec4& colR) {
    const float w = p1.x - p0.x, h = p1.y - p0.y;
    const float r = fminf(radius, fminf(w, h) * 0.5f);
    const int ES = 8, AS = 5;
    ImVec2 pts[64];
    int n = 0;
    auto push = [&](float x, float y) { if (n < 64) pts[n++] = ImVec2(x, y); };
    for (int i = 0; i <= ES; i++) push(p0.x + r + (w - 2.0f * r) * i / (float)ES, p0.y);
    for (int i = 1; i <= AS; i++) { float a = -1.5707963f + 1.5707963f * i / (float)AS; push(p1.x - r + cosf(a) * r, p0.y + r + sinf(a) * r); }
    for (int i = 1; i <= ES; i++) push(p1.x, p0.y + r + (h - 2.0f * r) * i / (float)ES);
    for (int i = 1; i <= AS; i++) { float a = 1.5707963f * i / (float)AS; push(p1.x - r + cosf(a) * r, p1.y - r + sinf(a) * r); }
    for (int i = 1; i <= ES; i++) push(p1.x - r - (w - 2.0f * r) * i / (float)ES, p1.y);
    for (int i = 1; i <= AS; i++) { float a = 1.5707963f + 1.5707963f * i / (float)AS; push(p0.x + r + cosf(a) * r, p1.y - r + sinf(a) * r); }
    for (int i = 1; i <= ES; i++) push(p0.x, p1.y - r - (h - 2.0f * r) * i / (float)ES);
    for (int i = 1; i <= AS; i++) { float a = 3.14159265f + 1.5707963f * i / (float)AS; push(p0.x + r + cosf(a) * r, p0.y + r + sinf(a) * r); }
    for (int i = 0; i < n; i++) {
        ImVec2 a = pts[i], b = pts[(i + 1) % n];
        float f = ((a.x + b.x) * 0.5f - p0.x) / w;
        if (f < 0.0f) f = 0.0f; if (f > 1.0f) f = 1.0f;
        ImU32 col = ImGui::GetColorU32(ImVec4(colL.x + (colR.x - colL.x) * f, colL.y + (colR.y - colL.y) * f,
                                              colL.z + (colR.z - colL.z) * f, colL.w + (colR.w - colL.w) * f));
        dl->AddLine(a, b, col, thick);
        dl->AddCircleFilled(a, thick * 0.5f, col, 6);
    }
}
// ---- panel RGB tactil (como el selector de ARCX 3.1.38) ----------------------
// El anillo se guarda en RGB (g_cfg), pero el panel trabaja en HSV: el cuadro
// grande es saturacion (X) / valor (Y) para el tono actual, y la barra vertical
// es el tono. El HSV se guarda aparte; cuando no se esta arrastrando, se
// sincroniza desde g_cfg (asi refleja tambien el restablecer).
static float g_pickH = 0.52f, g_pickS = 1.0f, g_pickV = 1.0f;
static bool g_cfgDirty = false;  // v2.5: algo cambio y aun no se guardo en el .ini
static int g_pickTarget = -1;  // v2.6: -1 = selector cerrado, 0 = anillo, 1 = texto del centro
static void PickerSyncFromRGB() {
    int r, g, b;
    if (g_pickTarget == 1) { r = g_cfg.textoR; g = g_cfg.textoG; b = g_cfg.textoB; }
    else if (g_pickTarget == 2) { r = g_cfg.fondoR; g = g_cfg.fondoG; b = g_cfg.fondoB; }  // v2.17: color del fondo
    else if (g_pickTarget == 3) { r = g_cfg.fondoIconoR; g = g_cfg.fondoIconoG; b = g_cfg.fondoIconoB; }  // v2.53: fondo del icono
    else { r = g_cfg.ringR; g = g_cfg.ringG; b = g_cfg.ringB; }
    float h, s, v;
    ImGui::ColorConvertRGBtoHSV(r / 255.0f, g / 255.0f, b / 255.0f, h, s, v);
    // v2.24: un color gris/blanco no tiene tono (S~0); no se borra el tono
    // elegido por el usuario (si no, la barra de tono volvia sola a rojo)
    if (s > 0.02f) g_pickH = h;
    g_pickS = s; g_pickV = v;
}
// v2.24: rect del selector (cuadro SV + barra de tono) del frame anterior;
// el desplazamiento lo usa para no robar los arrastres del color
static ImVec2 g_pickRect0, g_pickRect1;
static void PickerApplyToRGB() {
    float r, g, b;
    ImGui::ColorConvertHSVtoRGB(g_pickH, g_pickS, g_pickV, r, g, b);
    int ri = (int)(r * 255.0f + 0.5f), gi = (int)(g * 255.0f + 0.5f), bi = (int)(b * 255.0f + 0.5f);
    if (g_pickTarget == 1) { g_cfg.textoR = ri; g_cfg.textoG = gi; g_cfg.textoB = bi; }
    else if (g_pickTarget == 2) { g_cfg.fondoR = ri; g_cfg.fondoG = gi; g_cfg.fondoB = bi; }  // v2.17: color del fondo
    else if (g_pickTarget == 3) { g_cfg.fondoIconoR = ri; g_cfg.fondoIconoG = gi; g_cfg.fondoIconoB = bi; }  // v2.53: fondo del icono
    else { g_cfg.ringR = ri; g_cfg.ringG = gi; g_cfg.ringB = bi; }
}
// ---- controles propios del panel (no los de ImGui por defecto) ----------------
// Deslizador propio: etiqueta a la izquierda, valor a la derecha, pista
// redondeada con relleno azul y tirador blanco. Se arrastra con el dedo.
// v2.23: iconos de la imagen de referencia (sol, expandir, globo, opacidad,
// candado, restablecer, ojo) - se dibujan con primitivas de ImGui
enum PanelIcon { IC_NONE = 0, IC_SUN, IC_SIZE, IC_GLOBE, IC_OPAC, IC_LOCK, IC_RESET, IC_EYE };
static void DrawPanelIcon(ImDrawList* dl, float cx, float cy, float r, int kind, ImU32 col, float H) {
    float th = H * 0.002f;  // grosor de las lineas
    switch (kind) {
    case IC_SUN: {  // sol: circulo + 8 rayos
        dl->AddCircle(ImVec2(cx, cy), r * 0.45f, col, 16, th);
        for (int k = 0; k < 8; k++) {
            float a = (float)k * 3.14159f / 4.0f;
            dl->AddLine(ImVec2(cx + cosf(a) * r * 0.62f, cy + sinf(a) * r * 0.62f), ImVec2(cx + cosf(a) * r, cy + sinf(a) * r), col, th);
        }
        break; }
    case IC_SIZE: {  // expandir: flecha doble en diagonal (arriba-derecha y abajo-izquierda)
        float o = r * 0.55f, a = r * 0.35f;
        dl->AddLine(ImVec2(cx - o, cy + o), ImVec2(cx + o, cy - o), col, th);
        dl->AddLine(ImVec2(cx + o, cy - o), ImVec2(cx + o - a, cy - o), col, th);
        dl->AddLine(ImVec2(cx + o, cy - o), ImVec2(cx + o, cy - o + a), col, th);
        dl->AddLine(ImVec2(cx - o, cy + o), ImVec2(cx - o + a, cy + o), col, th);
        dl->AddLine(ImVec2(cx - o, cy + o), ImVec2(cx - o, cy + o - a), col, th);
        break; }
    case IC_GLOBE: {  // globo: circulo + meridiano + ecuador
        dl->AddCircle(ImVec2(cx, cy), r * 0.8f, col, 24, th);
        dl->AddLine(ImVec2(cx, cy - r * 0.8f), ImVec2(cx, cy + r * 0.8f), col, th);
        dl->AddLine(ImVec2(cx - r * 0.8f, cy), ImVec2(cx + r * 0.8f, cy), col, th);
        break; }
    case IC_OPAC: {  // opacidad: cuadro 2x2 (dos cuadros llenos en diagonal)
        float s = r * 0.7f;
        dl->AddRectFilled(ImVec2(cx - s, cy - s), ImVec2(cx, cy), col, 0.0f);
        dl->AddRectFilled(ImVec2(cx, cy), ImVec2(cx + s, cy + s), col, 0.0f);
        dl->AddRect(ImVec2(cx - s, cy - s), ImVec2(cx + s, cy + s), col, 0.0f, 0, th);
        break; }
    case IC_LOCK: {  // candado: arco + cuerpo
        dl->PathArcTo(ImVec2(cx, cy - r * 0.05f), r * 0.40f, 3.14159f, 6.28318f);
        dl->PathStroke(col, 0, th);
        dl->AddRectFilled(ImVec2(cx - r * 0.55f, cy - r * 0.05f), ImVec2(cx + r * 0.55f, cy + r * 0.70f), col, r * 0.15f);
        break; }
    case IC_RESET: {  // restablecer: arco casi completo + punta de flecha
        float r0 = r * 0.70f, a0 = 0.6f, a1 = 5.6f;
        dl->PathArcTo(ImVec2(cx, cy), r0, a0, a1);
        dl->PathStroke(col, 0, th);
        float ex = cx + cosf(a1) * r0, ey = cy + sinf(a1) * r0;
        float tx = -sinf(a1), ty = cosf(a1), ah = r * 0.40f;
        dl->AddLine(ImVec2(ex, ey), ImVec2(ex - tx * ah + ty * ah * 0.5f, ey - ty * ah - tx * ah * 0.5f), col, th);
        dl->AddLine(ImVec2(ex, ey), ImVec2(ex - tx * ah - ty * ah * 0.5f, ey - ty * ah + tx * ah * 0.5f), col, th);
        break; }
    case IC_EYE: {  // ojo: circulo + pupila
        dl->AddCircle(ImVec2(cx, cy), r * 0.70f, col, 24, th);
        dl->AddCircleFilled(ImVec2(cx, cy), r * 0.30f, col, 16);
        break; }
    default: break;
    }
}
static bool PanelSlider(const char* label, int* v, int vmin, int vmax, const char* fmt, float H, int icon) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float w = ImGui::GetContentRegionAvail().x;
    ImVec2 lp = ImGui::GetCursorScreenPos();
    // v2.23: icono a la izquierda + valor en pastilla a la derecha (como la imagen)
    float ir = H * 0.013f;
    float lx = lp.x;
    if (icon != IC_NONE) {
        DrawPanelIcon(dl, lp.x + ir, lp.y + ImGui::GetTextLineHeight() * 0.5f, ir, icon, IM_COL32(150, 190, 255, 255), H);
        lx = lp.x + ir * 2.0f + H*0.008f;
    }
    dl->AddText(ImVec2(lx, lp.y), IM_COL32(150, 190, 255, 255), label);
    char vb[16]; snprintf(vb, sizeof vb, fmt, *v);
    ImVec2 vs = ImGui::CalcTextSize(vb);
    dl->AddRectFilled(ImVec2(lp.x + w - vs.x - H*0.016f, lp.y - H*0.002f), ImVec2(lp.x + w, lp.y + vs.y + H*0.002f), IM_COL32(10, 18, 40, 255), H*0.006f);
    dl->AddRect(ImVec2(lp.x + w - vs.x - H*0.016f, lp.y - H*0.002f), ImVec2(lp.x + w, lp.y + vs.y + H*0.002f), IM_COL32(120, 180, 255, 90), H*0.006f, 0, H*0.001f);
    dl->AddText(ImVec2(lp.x + w - vs.x - H*0.008f, lp.y), IM_COL32(255, 255, 255, 255), vb);
    ImGui::Dummy(ImVec2(w, ImGui::GetTextLineHeight()));
    ImVec2 p0 = ImGui::GetCursorScreenPos();
    float zoneH = H * 0.050f;  // v2.50: barra mas alta al tacto
    ImGui::InvisibleButton(label, ImVec2(w, zoneH));
    bool active = ImGui::IsItemActive();
    ImGuiIO& io = ImGui::GetIO();
    bool changed = false;
    // v2.20: valor antes de pulsar (si el arrastre acaba siendo un
    // desplazamiento del panel, se restaura para que la barra no se mueva)
    static int* savedPtr = nullptr; static int savedVal = 0;
    if (g_panelJustOpened) savedPtr = nullptr;  // v2.48: al reabrir, olvidar el valor guardado de la sesion anterior
    if (ImGui::IsItemActivated()) { savedPtr = v; savedVal = *v; }
    if (g_panelScrolling && savedPtr == v) *v = savedVal;
    // v2.25: al soltar se olvida el valor guardado; si no, un desplazamiento
    // posterior (tocando otra parte del panel) restauraba la ultima barra
    // ajustada a su valor anterior y se regresaba a su mismo lugar
    if (!io.MouseDown[0]) savedPtr = nullptr;
    if (active && io.MouseDown[0] && !g_panelScrolling) {
        float f = (io.MousePos.x - p0.x) / w;
        int nv = vmin + (int)(f * (float)(vmax - vmin) + 0.5f);
        if (nv < vmin) nv = vmin; if (nv > vmax) nv = vmax;
        if (nv != *v) { *v = nv; changed = true; }
    }
    float cy = p0.y + zoneH * 0.5f;
    float r = H * 0.013f;
    dl->AddRectFilled(ImVec2(p0.x, cy - r), ImVec2(p0.x + w, cy + r), IM_COL32(8, 14, 32, 255), r);
    float f = (float)(*v - vmin) / (float)(vmax - vmin);
    if (f > 0.0f) dl->AddRectFilled(ImVec2(p0.x, cy - r), ImVec2(p0.x + w * f, cy + r), IM_COL32(20, 110, 255, 255), r);
    float hx = p0.x + w * f;
    dl->AddCircleFilled(ImVec2(hx, cy), r + H*0.006f, IM_COL32(255, 255, 255, 255), 24);
    dl->AddCircle(ImVec2(hx, cy), r + H*0.006f, IM_COL32(20, 110, 255, 255), 24, H*0.002f);
    return changed;
}
// Boton propio: rectangulo redondeado con degradado azul y texto centrado.
// Devuelve true al soltar el dedo sobre el (como un boton de ImGui).
// v2.23: interruptor (pastilla con circulo, como los de ARCX) - fila con el
// texto a la izquierda y el interruptor a la derecha; tocar la fila cambia
static bool PanelSwitch(const char* label, bool* v, float H) {
    ImVec2 p0 = ImGui::GetCursorScreenPos();
    float w = ImGui::GetContentRegionAvail().x;
    float bh = H * 0.056f;  // v2.50: interruptor mas alto
    float rd = H * 0.012f;
    bool clicked = ImGui::InvisibleButton(label, ImVec2(w, bh));
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p0, ImVec2(p0.x + w, p0.y + bh), IM_COL32(16, 90, 220, 255), rd);
    dl->AddRectFilled(p0, ImVec2(p0.x + w, p0.y + bh * 0.45f), IM_COL32(90, 170, 255, 110), rd, ImDrawFlags_RoundCornersTop);
    dl->AddRect(p0, ImVec2(p0.x + w, p0.y + bh), IM_COL32(120, 180, 255, 130), rd, 0, H*0.0015f);
    ImVec2 ts = ImGui::CalcTextSize(label);
    float tx = p0.x + H*0.016f;
    DrawPanelIcon(dl, tx + H*0.013f, p0.y + bh * 0.5f, H*0.013f, IC_LOCK, IM_COL32(255, 255, 255, 255), H);
    tx += H*0.013f * 2.0f + H*0.008f;
    dl->AddText(ImVec2(tx, p0.y + (bh - ts.y) * 0.5f), IM_COL32(255, 255, 255, 255), label);
    float sw = H * 0.075f, sh = H * 0.038f;  // pastilla del interruptor
    float sx = p0.x + w - H*0.016f - sw, sy = p0.y + (bh - sh) * 0.5f;
    dl->AddRectFilled(ImVec2(sx, sy), ImVec2(sx + sw, sy + sh), *v ? IM_COL32(20, 110, 255, 255) : IM_COL32(74, 76, 88, 255), sh * 0.5f);
    dl->AddCircleFilled(ImVec2(*v ? sx + sw - sh * 0.5f : sx + sh * 0.5f, sy + sh * 0.5f), sh * 0.5f - H*0.004f, IM_COL32(255, 255, 255, 255), 24);
    // v2.54: solo se activa tocando la pastilla del interruptor (derecha, con
    // un poco de margen tactil). Antes el InvisibleButton cubria toda la fila
    // azul y al desplazar el panel o tocar la etiqueta se cambiaban solos.
    ImVec2 mp = ImGui::GetIO().MousePos;
    float m = H * 0.012f;
    bool onSwitch = (mp.x >= sx - m && mp.x <= sx + sw + m && mp.y >= sy - m && mp.y <= sy + sh + m);
    if (clicked && onSwitch) *v = !*v;
    return clicked && onSwitch;
}
static bool PanelButtonIcon(const char* label, float H, float w, int icon) {
    ImVec2 p0 = ImGui::GetCursorScreenPos();
    if (w <= 0.0f) w = ImGui::GetContentRegionAvail().x;  // 0 = todo el ancho
    float bh = H * 0.056f;  // v2.50: boton mas alto
    float rd = H * 0.012f;  // v2.22: esquinas redondeadas (como las filas de ARCX)
    bool clicked = ImGui::InvisibleButton(label, ImVec2(w, bh));
    bool active = ImGui::IsItemActive();
    bool hovered = ImGui::IsItemHovered();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    // v2.22: boton estilo ARCX - fila redondeada con brillo superior, borde
    // claro y estado al pulsar; ya no es un rectangulo plano de ImGui
    ImU32 base = active ? IM_COL32(10, 70, 190, 255) : (hovered ? IM_COL32(24, 110, 235, 255) : IM_COL32(16, 90, 220, 255));
    ImU32 hi = active ? IM_COL32(40, 120, 240, 120) : IM_COL32(90, 170, 255, 110);
    dl->AddRectFilled(p0, ImVec2(p0.x + w, p0.y + bh), base, rd);
    dl->AddRectFilled(p0, ImVec2(p0.x + w, p0.y + bh * 0.45f), hi, rd, ImDrawFlags_RoundCornersTop);
    dl->AddRect(p0, ImVec2(p0.x + w, p0.y + bh), active ? IM_COL32(150, 205, 255, 210) : IM_COL32(120, 180, 255, 130), rd, 0, H*0.0015f);
    // v2.23: icono a la izquierda + texto centrado en lo que queda (como la imagen)
    float ix = p0.x + H*0.016f;
    float cy = p0.y + bh * 0.5f;
    float ir = H * 0.013f;
    if (icon != IC_NONE) {
        DrawPanelIcon(dl, ix + ir, cy, ir, icon, IM_COL32(255, 255, 255, 255), H);
        ix += ir * 2.0f + H*0.008f;
    }
    ImVec2 ts = ImGui::CalcTextSize(label);
    float avail = (p0.x + w) - ix;
    dl->AddText(ImVec2(ix + (avail - ts.x) * 0.5f, p0.y + (bh - ts.y) * 0.5f), IM_COL32(255, 255, 255, 255), label);
    return clicked;
}
static bool PanelButton(const char* label, float H) { return PanelButtonIcon(label, H, 0.0f, IC_NONE); }
// Recuadro pequeno de color: al tocarlo se abre/cierra el panel RGB para ese color.
static bool PanelSwatch(const char* label, ImVec4 col, bool active, float H) {
    float s = H * 0.055f;
    ImVec2 p0 = ImGui::GetCursorScreenPos();
    bool clicked = ImGui::InvisibleButton(label, ImVec2(s, s));
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p0, ImVec2(p0.x + s, p0.y + s), ImGui::GetColorU32(col), H*0.008f);
    if (active) dl->AddRect(p0, ImVec2(p0.x + s, p0.y + s), IM_COL32(255, 255, 255, 255), H*0.008f, 0, H*0.003f);
    else dl->AddRect(p0, ImVec2(p0.x + s, p0.y + s), IM_COL32(255, 255, 255, 90), H*0.008f, 0, H*0.0015f);
    return clicked;
}

// v2.2: panel de personalizacion (se abre con doble toque al boton flotante)
// v1.6: dividir el nombre de la pista en artista y titulo ("Artista -
// Titulo", como en la imagen de GTA V). Si no hay " - ", todo es titulo.
static void SplitArtistTitle(const char* name, char* artist, size_t aCap, char* title, size_t tCap) {
    const char* sep = strstr(name, " - ");
    if (sep) {
        size_t alen = (size_t)(sep - name);
        if (alen >= aCap) alen = aCap - 1;
        memcpy(artist, name, alen); artist[alen] = 0;
        snprintf(title, tCap, "%s", sep + 3);
    } else {
        artist[0] = 0;
        snprintf(title, tCap, "%s", name);
    }
}
// v1.4: lista de canciones de User Tracks (como la de GTA V): se abre desde
// la rueda y muestra todas las pistas; la que suena va marcada con PLAYING.
// Tocar una pista la pone a sonar. La X de arriba a la derecha la cierra.
// v1.5: UI mejorada (icono de User Track en la cabecera, fila actual con
// fondo azul y pastilla amarilla PLAYING como en GTA V, filas mas altas).
// v1.6: cada cancion tiene DIVISION (linea separadora), titulo + artista
// debajo (como en la imagen), y todo traducido segun el idioma.
static void DrawTrackList(float W, float H) {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, H * 0.012f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(H * 0.016f, H * 0.014f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(H * 0.008f, H * 0.006f));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.03f, 0.04f, 0.10f, 0.98f));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, 1));
    ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.10f, 0.35f, 0.75f, 0.55f));
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.10f, 0.45f, 1.0f, 0.65f));
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.10f, 0.45f, 1.0f, 0.85f));
    ImGui::PushStyleColor(ImGuiCol_Separator, ImVec4(0.10f, 0.45f, 1.0f, 0.4f));
    if (g_fontUI) ImGui::PushFont(g_fontUI);
    float pw = W * 0.46f, ph = H * 0.72f;
    ImGui::SetNextWindowSize(ImVec2(pw, ph), ImGuiCond_Always);
    ImGui::SetNextWindowPos(ImVec2((W - pw) * 0.5f, (H - ph) * 0.5f), ImGuiCond_Always);
    bool opened = ImGui::Begin("##vhudtracklist", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse);
    if (!opened) { ImGui::End(); if (g_fontUI) ImGui::PopFont(); ImGui::PopStyleColor(6); ImGui::PopStyleVar(4); return; }
    ImVec2 wp = ImGui::GetWindowPos(); ImVec2 ws = ImGui::GetWindowSize();
    g_trackListRectX = wp.x; g_trackListRectY = wp.y; g_trackListRectW = ws.x; g_trackListRectH = ws.y;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    // Cabecera: icono de User Track + TRACK LIST + X.
    float iconS = H * 0.045f;
    GLuint it = IconTex(12);
    if (it) {
        ImVec2 ip0 = ImGui::GetCursorScreenPos();
        dl->AddImageRounded((ImTextureID)(uintptr_t)it, ip0, ImVec2(ip0.x + iconS, ip0.y + iconS), ImVec2(0, 0), ImVec2(1, 1), IM_COL32(255,255,255,255), H * 0.008f);
        ImGui::Dummy(ImVec2(iconS, iconS));
        ImGui::SameLine();
    }
    ImGui::BeginGroup();
    ImGui::Text("%s", S(STR_TL_TITLE));  // v1.6: traducido segun el idioma
    ImGui::Text("User Track Player");
    ImGui::EndGroup();
    ImGui::SameLine(ImGui::GetWindowWidth() - H * 0.05f);
    if (ImGui::Button("X", ImVec2(H * 0.04f, H * 0.04f))) { g_trackList = false; Log("lista de canciones cerrada"); }
    ImGui::Separator();
    ImGui::BeginChild("##tracks", ImVec2(0, 0), false);
    ImDrawList* dlc = ImGui::GetWindowDrawList();  // v1.9.8: la del HIJO, con recorte; la de fuera (dl) no recorta y las filas traspasaban el encabezado
    // v1.9.6 (Daniel): la lista se desliza con el dedo si hay muchas canciones.
    // El scroll propio de ImGui no responde al toque (como pasaba en el panel),
    // asi que es manual, igual que el del panel: arrastre vertical para
    // desplazar, con inercia al soltar. Un toque (sin arrastre) salta a la
    // cancion; un arrastre solo desplaza, no salta.
    ImGuiIO& lio = ImGui::GetIO();
    static bool listScrolling = false; static bool listScrollDecided = false;
    static ImVec2 listScrollStart; static float listScrollStartY = 0.0f; static float listScrollVel = 0.0f;
    static bool listScrolled = false;
    if (lio.MouseClicked[0]) {
        listScrollStart = lio.MousePos; listScrollDecided = false; listScrolling = false; listScrollVel = 0.0f; listScrolled = false;
        listScrollStartY = ImGui::GetScrollY();
    }
    if (lio.MouseDown[0] && !listScrollDecided) {
        float ldx = lio.MousePos.x - listScrollStart.x, ldy = lio.MousePos.y - listScrollStart.y;
        if (fabsf(ldy) > H * 0.02f && fabsf(ldy) > fabsf(ldx)) { listScrolling = true; listScrollDecided = true; listScrolled = true; }
        else if (fabsf(ldx) > H * 0.02f) { listScrollDecided = true; }
    }
    if (!lio.MouseDown[0]) { listScrolling = false; listScrollDecided = false; }
    if (listScrolling) {
        ImGui::SetScrollY(listScrollStartY - (lio.MousePos.y - listScrollStart.y));
        listScrollVel = -lio.MouseDelta.y;
    }
    if (!lio.MouseDown[0] && fabsf(listScrollVel) > 0.4f) {
        ImGui::SetScrollY(ImGui::GetScrollY() + listScrollVel);
        listScrollVel *= 0.93f;
    } else if (!lio.MouseDown[0]) {
        listScrollVel = 0.0f;
    }
    int cur = UT_CurrentTrackIdx();
    // v1.6: cada fila es titulo + artista debajo, con linea divisora entre
    // canciones (como en la imagen de GTA V). Se dibuja a mano para poder
    // poner el titulo en blanco y el artista en gris.
    float rowH = H * 0.085f;
    float numW = H * 0.045f;
    for (size_t i = 0; i < ut_tracks.size(); i++) {
        const char* nm = UT_TrackNameFor((int)i);
        bool isCur = ((int)i == cur);
        char artist[256], title[256];
        SplitArtistTitle(nm, artist, sizeof artist, title, sizeof title);
        char id[32];
        snprintf(id, sizeof id, "##trk%zu", i);
        ImGui::InvisibleButton(id, ImVec2(ImGui::GetContentRegionAvail().x, rowH));
        bool clicked = ImGui::IsItemClicked();
        ImVec2 rMin = ImGui::GetItemRectMin(), rMax = ImGui::GetItemRectMax();
        if (isCur) dlc->AddRectFilled(rMin, rMax, IM_COL32(10, 60, 140, 255));
        else if (ImGui::IsItemHovered()) dlc->AddRectFilled(rMin, rMax, IM_COL32(255, 255, 255, 18));
        char num[8];
        snprintf(num, sizeof num, "%d", (int)i + 1);
        float tx = rMin.x + H * 0.012f;
        float ty = rMin.y + H * 0.010f;
        dlc->AddText(ImVec2(tx, ty), isCur ? IM_COL32(255, 200, 40, 255) : IM_COL32(150, 150, 150, 255), num);
        dlc->AddText(ImVec2(tx + numW, ty), IM_COL32(255, 255, 255, 255), title);
        if (artist[0]) dlc->AddText(ImVec2(tx + numW, ty + H * 0.030f), IM_COL32(165, 165, 165, 255), artist);
        if (isCur) {
            // Pastilla amarilla PLAYING a la derecha de la fila (como en GTA V).
            const char* pill = S(STR_TL_PLAYING);  // v1.6: traducido segun el idioma
            ImVec2 psz = ImGui::CalcTextSize(pill);
            float pw2 = psz.x + H * 0.02f, ph2 = psz.y + H * 0.008f;
            ImVec2 p0(rMax.x - pw2 - H * 0.008f, rMin.y + (rMax.y - rMin.y - ph2) * 0.5f);
            dlc->AddRectFilled(p0, ImVec2(p0.x + pw2, p0.y + ph2), IM_COL32(255, 200, 40, 255), ph2 * 0.5f);
            dlc->AddText(ImVec2(p0.x + H * 0.01f, p0.y + H * 0.004f), IM_COL32(20, 20, 20, 255), pill);
        }
        // Division entre canciones (linea fina, como en la imagen).
        dlc->AddLine(ImVec2(rMin.x + H * 0.004f, rMax.y), ImVec2(rMax.x - H * 0.004f, rMax.y), IM_COL32(255, 255, 255, 35));
        if (clicked && !listScrolled && std::chrono::steady_clock::now() - g_listOpenedAt > std::chrono::milliseconds(300)) {  // v1.9.7: el primer toque (el que abre la lista) no cambia la cancion
            if (UT_JumpToTrack((int)i)) Log("lista de canciones: saltar a %s", nm);
        }
    }
    ImGui::EndChild();
    ImGui::End();
    if (g_fontUI) ImGui::PopFont();
    ImGui::PopStyleColor(6);
    ImGui::PopStyleVar(4);
}

static void DrawSettings(float W, float H) {
    // v2.6: panel propio (no la ventana de ImGui por defecto) - azul casi oscuro,
    // sin barra de titulo, con deslizadores, recuadros y botones dibujados a mano
    const ImVec4 titleCol(0.75f, 0.85f, 1.0f, 1.0f);  // azul claro
    const ImVec4 subCol(0.55f, 0.65f, 0.85f, 1.0f);   // azul grisaceo
    // v2.17: el panel se puede mover y cambiar de ancho (como el menu de ARCX);
    // la posicion y el ancho se guardan en el .ini (panelX/panelY/panelAncho)
    if (g_cfg.panelX >= 0.0f && g_cfg.panelY >= 0.0f) ImGui::SetNextWindowPos(ImVec2(g_cfg.panelX * W, g_cfg.panelY * H), ImGuiCond_Always);
    else ImGui::SetNextWindowPos(ImVec2(W * 0.5f, H * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(W * g_cfg.panelAncho, g_cfg.panelAlto >= 0.0f ? H * g_cfg.panelAlto : 0), ImGuiCond_Always);  // v2.18: alto ajustable (-1 = automatico)
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(H*0.014f, H*0.014f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(H*0.010f, H*0.016f));  // v2.50: mas aire entre controles (antes todo iba muy junto)
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, H*0.015f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.02f, 0.05f, 0.13f, 0.97f));  // azul casi oscuro
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, 1));
    ImGui::PushStyleColor(ImGuiCol_Separator, ImVec4(0.10f, 0.45f, 1.0f, 0.5f));
    if (g_fontUI) ImGui::PushFont(g_fontUI);
    bool opened = ImGui::Begin("##vhudpanel", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse);
    if (!opened) { ImGui::End(); if (g_fontUI) ImGui::PopFont(); ImGui::PopStyleColor(3); ImGui::PopStyleVar(4); return; }
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImGuiIO& io = ImGui::GetIO();
    // v2.22: cabecera fija (como ARCX) - el titulo, el subtitulo y la X se
    // dibujan en primer plano y NO se desplazan con el contenido; arrastrar
    // la cabecera mueve el panel y la X de arriba a la derecha lo cierra (ya
    // no hay que bajar hasta "Cerrar"). La cabecera se comprueba a mano (como
    // la esquina de redimensionado) porque esta en primer plano.
    const char* title = S(STR_TITLE);
    ImVec2 ts = ImGui::CalcTextSize(title);
    float titleW = ImGui::GetContentRegionAvail().x;
    ImVec2 wp = ImGui::GetWindowPos(); ImVec2 ws = ImGui::GetWindowSize();  // v2.22: se necesitan arriba (cabecera fija y desplazamiento)
    g_panelRectX = wp.x; g_panelRectY = wp.y; g_panelRectW = ws.x; g_panelRectH = ws.y;  // v2.49: rect real del panel para repartir los toques
    float pad = H * 0.014f;  // relleno de la ventana
    float xr = H * 0.020f;   // v2.23: radio de la X (mas chica que en la v2.22)
    float titleY = wp.y + pad;  // titulo
    ImVec2 xc(wp.x + ws.x - pad - xr, titleY + ts.y * 0.5f);  // v2.23: centro de la X en la linea del titulo
    float gradY = titleY + ts.y + H*0.004f;  // linea en degradado
    float subY = gradY + H*0.003f + H*0.010f;  // subtitulo
    float headerH = (subY + ImGui::GetTextLineHeight() + H*0.012f) - wp.y;  // alto total de la cabecera
    static bool panelMoved = false;
    static bool panelMoving = false;  // v2.22: se esta arrastrando la cabecera
    static bool xPressed = false;     // v2.22: se pulso la X (se cierra al soltar sobre ella)
    static bool panelResizing = false; static ImVec2 gripOff;  // v2.19: se declaran aqui (los usa el desplazamiento tactil de abajo)
    ImGui::Dummy(ImVec2(titleW, headerH - pad));  // reserva el hueco de la cabecera fija
    // X de cerrar (arriba a la derecha, como ARCX): circulo oscuro con
    // anillo azul y X blanca; se cierra al soltar el dedo sobre ella
    bool inX = io.MousePos.x >= xc.x - xr && io.MousePos.x <= xc.x + xr && io.MousePos.y >= xc.y - xr && io.MousePos.y <= xc.y + xr;
    if (io.MouseClicked[0] && inX) xPressed = true;
    if (io.MouseReleased[0]) {
        if (xPressed && inX) { g_settings = false; g_cfgDirty = false; SaveCfg(); Log("panel cerrado (ajustes guardados)"); }
        xPressed = false;
    }
    // arrastrar la cabecera mueve el panel (como ARCX); si la posicion es
    // -1 (centrado) toma la posicion real de la ventana al empezar
    bool inHeader = io.MousePos.x >= wp.x && io.MousePos.x <= wp.x + ws.x && io.MousePos.y >= wp.y && io.MousePos.y <= wp.y + headerH;
    if (io.MouseClicked[0] && inHeader && !inX) {
        panelMoving = true;
        if (g_cfg.panelX < 0.0f || g_cfg.panelY < 0.0f) { g_cfg.panelX = wp.x / W; g_cfg.panelY = wp.y / H; }
    }
    if (panelMoving) {
        if (io.MouseDown[0] && ImGui::IsMouseDragging(0, 6.0f)) {
            g_cfg.panelX += io.MouseDelta.x / W;
            g_cfg.panelY += io.MouseDelta.y / H;
            if (g_cfg.panelX < 0.0f) g_cfg.panelX = 0.0f; if (g_cfg.panelX > 1.0f - ws.x / W) g_cfg.panelX = 1.0f - ws.x / W;
            if (g_cfg.panelY < 0.0f) g_cfg.panelY = 0.0f; if (g_cfg.panelY > 1.0f - ws.y / H) g_cfg.panelY = 1.0f - ws.y / H;
            panelMoved = true; g_cfgDirty = true;
        } else if (!io.MouseDown[0]) {
            if (panelMoved) SaveCfg();
            panelMoved = false; panelMoving = false;
        }
    }
    // v2.21: desplazamiento tactil estilo ARCX - el menu de ARCX (ListView)
    // solo responde a toques DENTRO de su area (inView al pulsar) y el
    // desplazamiento sigue al dedo desde donde se pulso (scroll = scroll al
    // pulsar - dy), con inercia al soltar (velocidad x0.93 por frame). La
    // v2.20 no comprobaba donde empezaba el arrastre y un arrastre vertical
    // en el juego (fuera del panel) tambien desplazaba el panel. La
    // direccion sigue como en la v2.20: vertical = desplaza, horizontal =
    // deslizador. Con el selector de color abierto SI hay desplazamiento,
    // salvo si el arrastre empieza sobre el cuadro o la barra de tono (esos
    // arrastres son del color). Tampoco si el arrastre es la cabecera (que
    // mueve el panel), la X o la esquina (que lo redimensiona).
    static bool panelScrolling = false; static bool scrollDecided = false; static ImVec2 scrollStart;
    static bool scrollStartInPanel = false; static bool scrollStartOnPicker = false; static float scrollStartY = 0.0f; static float scrollVel = 0.0f;
    if (g_panelJustOpened) { panelScrolling = false; scrollDecided = false; g_panelScrolling = false; scrollVel = 0.0f; panelMoving = false; panelMoved = false; xPressed = false; panelResizing = false; }  // v2.48: al reabrir, olvidar el arrastre/desplazamiento de la sesion anterior
    if (io.MouseClicked[0]) {
        scrollStart = io.MousePos; scrollDecided = false; panelScrolling = false; g_panelScrolling = false; scrollVel = 0.0f;
        scrollStartY = ImGui::GetScrollY();
        ImVec2 wp0 = ImGui::GetWindowPos(); ImVec2 ws0 = ImGui::GetWindowSize();
        scrollStartInPanel = io.MousePos.x >= wp0.x && io.MousePos.x <= wp0.x + ws0.x && io.MousePos.y >= wp0.y && io.MousePos.y <= wp0.y + ws0.y;
        // v2.24: el arrastre empezo sobre el selector de color (cuadro o barra)
        scrollStartOnPicker = g_pickTarget >= 0 && io.MousePos.x >= g_pickRect0.x && io.MousePos.x <= g_pickRect1.x && io.MousePos.y >= g_pickRect0.y && io.MousePos.y <= g_pickRect1.y;
    }
    if (io.MouseDown[0] && !scrollDecided && !panelMoving && !xPressed && !panelResizing && !scrollStartOnPicker) {
        if (!scrollStartInPanel) { scrollDecided = true; }  // el arrastre empezo fuera del panel: nunca desplaza
        else {
            float dx = io.MousePos.x - scrollStart.x, dy = io.MousePos.y - scrollStart.y;
            if (fabsf(dy) > H*0.02f && fabsf(dy) > fabsf(dx)) { panelScrolling = true; scrollDecided = true; g_panelScrolling = true; }
            else if (fabsf(dx) > H*0.02f) { scrollDecided = true; }
        }
    }
    if (!io.MouseDown[0]) { panelScrolling = false; scrollDecided = false; g_panelScrolling = false; }
    if (panelScrolling) {
        ImGui::SetScrollY(scrollStartY - (io.MousePos.y - scrollStart.y));  // sigue al dedo desde donde se pulso (como ARCX)
        scrollVel = -io.MouseDelta.y;  // velocidad para la inercia al soltar
    }
    // v2.21: inercia al soltar (como ARCX) - al soltar el dedo el contenido
    // sigue deslizandose y frena poco a poco (x0.93 por frame)
    if (!io.MouseDown[0] && fabsf(scrollVel) > 0.4f) {
        ImGui::SetScrollY(ImGui::GetScrollY() + scrollVel);
        scrollVel *= 0.93f;
    } else if (!io.MouseDown[0]) {
        scrollVel = 0.0f;
    }
    // v2.22: el titulo, el subtitulo y la linea van en primer plano (cabecera fija, abajo)
    // v2.50: secciones con titulo (como ARCX) - Rueda de radio / Boton flotante
    ImGui::TextColored(titleCol, "%s", S(STR_SEC_RUEDA));
    ImGui::Separator();
    if (PanelSlider(S(STR_FONDO), &g_cfg.fondo, 0, 100, "%d%%", H, IC_SUN)) g_cfgDirty = true;
    // v2.56: barra del blur de fondo (0 = apagado)
    if (PanelSlider(S(STR_BLUR), &g_cfg.blur, 0, 100, "%d%%", H, IC_SUN)) { g_cfgDirty = true; Log("desenfoque de fondo: %d%%", g_cfg.blur); }
    if (PanelSlider(S(STR_TAMANO), &g_cfg.tamano, 25, 45, "%d%%", H, IC_SIZE)) g_cfgDirty = true;
    // recuadros de color: al tocar uno se abre el panel RGB para ese color
    ImGui::BeginGroup();
    if (PanelSwatch("##swAnillo", ImVec4(g_cfg.ringR / 255.0f, g_cfg.ringG / 255.0f, g_cfg.ringB / 255.0f, 1.0f), g_pickTarget == 0, H)) { g_pickTarget = (g_pickTarget == 0) ? -1 : 0; PlayClick(false); }
    ImGui::SameLine(0, H*0.012f);
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (H*0.055f - ImGui::GetTextLineHeight()) * 0.5f);
    ImGui::Text("%s", S(STR_ANILLO));
    ImGui::EndGroup();
    ImGui::SameLine(0, H*0.05f);
    ImGui::BeginGroup();
    if (PanelSwatch("##swTexto", ImVec4(g_cfg.textoR / 255.0f, g_cfg.textoG / 255.0f, g_cfg.textoB / 255.0f, 1.0f), g_pickTarget == 1, H)) { g_pickTarget = (g_pickTarget == 1) ? -1 : 1; PlayClick(false); }
    ImGui::SameLine(0, H*0.012f);
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (H*0.055f - ImGui::GetTextLineHeight()) * 0.5f);
    ImGui::Text("%s", S(STR_TEXTO));
    ImGui::EndGroup();
    ImGui::SameLine(0, H*0.05f);
    ImGui::BeginGroup();
    if (PanelSwatch("##swFondo", ImVec4(g_cfg.fondoR / 255.0f, g_cfg.fondoG / 255.0f, g_cfg.fondoB / 255.0f, 1.0f), g_pickTarget == 2, H)) { g_pickTarget = (g_pickTarget == 2) ? -1 : 2; PlayClick(false); }
    ImGui::SameLine(0, H*0.012f);
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (H*0.055f - ImGui::GetTextLineHeight()) * 0.5f);
    ImGui::Text("%s", S(STR_FONDO_LBL));
    ImGui::EndGroup();
    ImGui::SameLine(0, H*0.05f);
    ImGui::BeginGroup();
    if (PanelSwatch("##swFondoIcono", ImVec4(g_cfg.fondoIconoR / 255.0f, g_cfg.fondoIconoG / 255.0f, g_cfg.fondoIconoB / 255.0f, 1.0f), g_pickTarget == 3, H)) { g_pickTarget = (g_pickTarget == 3) ? -1 : 3; PlayClick(false); }
    ImGui::SameLine(0, H*0.012f);
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (H*0.055f - ImGui::GetTextLineHeight()) * 0.5f);
    ImGui::Text("%s", S(STR_FONDOICONO));
    ImGui::EndGroup();
    if (g_pickTarget >= 0) {
        ImGui::TextColored(titleCol, "%s", g_pickTarget == 1 ? S(STR_TEXTO) : (g_pickTarget == 2 ? S(STR_FONDO_LBL) : (g_pickTarget == 3 ? S(STR_FONDOICONO) : S(STR_ANILLO))));
        // ---- panel RGB tactil (como ARCX 3.1.38): cuadro saturacion/valor + barra de tono ----
        const float gap = H * 0.012f;
        const float hueW = H * 0.045f;
        float availW = ImGui::GetContentRegionAvail().x;
        float sq = availW - hueW - gap;
        if (sq > H * 0.20f) sq = H * 0.20f;   // que quepa el panel con el selector abierto
        if (sq < H * 0.14f) sq = H * 0.14f;
        float rowW = sq + gap + hueW;
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (availW - rowW) * 0.5f);
        ImVec2 sq0 = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton("##sv", ImVec2(sq, sq));
        bool svActive = ImGui::IsItemActive();
        ImGui::SameLine(0, gap);
        ImVec2 hue0 = ImGui::GetCursorScreenPos();
        g_pickRect0 = sq0; g_pickRect1 = ImVec2(hue0.x + hueW, hue0.y + sq);  // v2.24: rect del selector para el desplazamiento
        ImGui::InvisibleButton("##hue", ImVec2(hueW, sq));
        bool hueActive = ImGui::IsItemActive();
        bool dragging = (svActive || hueActive) && io.MouseDown[0];
        if (!dragging) PickerSyncFromRGB();
        if (svActive && io.MouseDown[0]) {
            g_pickS = Clampf((io.MousePos.x - sq0.x) / sq, 0.0f, 1.0f);
            g_pickV = Clampf(1.0f - (io.MousePos.y - sq0.y) / sq, 0.0f, 1.0f);
            PickerApplyToRGB(); g_cfgDirty = true;
        } else if (hueActive && io.MouseDown[0]) {
            g_pickH = Clampf((io.MousePos.y - hue0.y) / sq, 0.0f, 1.0f);
            // v2.24: con un color gris/blanco el tono no se ve (S=0); al
            // mover la barra se pone saturado para que el color cambie
            if (g_pickS < 0.05f) g_pickS = 1.0f;
            PickerApplyToRGB(); g_cfgDirty = true;
        }
        float hr, hg, hb;
        ImGui::ColorConvertHSVtoRGB(g_pickH, 1.0f, 1.0f, hr, hg, hb);
        const ImU32 hueCol = IM_COL32((int)(hr * 255.0f), (int)(hg * 255.0f), (int)(hb * 255.0f), 255);
        dl->AddRectFilledMultiColor(sq0, ImVec2(sq0.x + sq, sq0.y + sq),
            IM_COL32(255, 255, 255, 255), hueCol, IM_COL32(0, 0, 0, 255), IM_COL32(0, 0, 0, 255));
        dl->AddRect(sq0, ImVec2(sq0.x + sq, sq0.y + sq), IM_COL32(255, 255, 255, 60), H*0.006f, 0, H*0.0015f);
        int cr, cg, cb;
        if (g_pickTarget == 1) { cr = g_cfg.textoR; cg = g_cfg.textoG; cb = g_cfg.textoB; }
        else if (g_pickTarget == 2) { cr = g_cfg.fondoR; cg = g_cfg.fondoG; cb = g_cfg.fondoB; }  // v2.17: color del fondo
        else if (g_pickTarget == 3) { cr = g_cfg.fondoIconoR; cg = g_cfg.fondoIconoG; cb = g_cfg.fondoIconoB; }  // v2.53: fondo del icono
        else { cr = g_cfg.ringR; cg = g_cfg.ringG; cb = g_cfg.ringB; }
        const ImU32 pickU = ImGui::GetColorU32(ImVec4(cr / 255.0f, cg / 255.0f, cb / 255.0f, 1.0f));
        const ImVec2 mk(sq0.x + g_pickS * sq, sq0.y + (1.0f - g_pickV) * sq);
        dl->AddCircleFilled(mk, H*0.011f, pickU, 24);
        dl->AddCircle(mk, H*0.011f, IM_COL32(255, 255, 255, 255), 24, H*0.003f);
        const float seg = sq / 24.0f;
        for (int k = 0; k < 24; k++) {
            float rr, gg, bb;
            ImGui::ColorConvertHSVtoRGB((float)k / 24.0f, 1.0f, 1.0f, rr, gg, bb);
            dl->AddRectFilled(ImVec2(hue0.x, hue0.y + k * seg), ImVec2(hue0.x + hueW, hue0.y + (k + 1) * seg + 0.5f),
                IM_COL32((int)(rr * 255.0f), (int)(gg * 255.0f), (int)(bb * 255.0f), 255));
        }
        dl->AddRect(hue0, ImVec2(hue0.x + hueW, hue0.y + sq), IM_COL32(255, 255, 255, 60), H*0.006f, 0, H*0.0015f);
        const float hy = hue0.y + g_pickH * sq;
        dl->AddLine(ImVec2(hue0.x - H*0.004f, hy), ImVec2(hue0.x + hueW + H*0.004f, hy), IM_COL32(255, 255, 255, 255), H*0.003f);
        // abajo: muestra del color + valores RGB y hexadecimal
        const float readH = H * 0.045f;
        ImVec2 sw0 = ImGui::GetCursorScreenPos();
        ImGui::Dummy(ImVec2(H*0.040f, readH));
        dl->AddRectFilled(sw0, ImVec2(sw0.x + H*0.040f, sw0.y + readH), pickU, H*0.008f);
        dl->AddRect(sw0, ImVec2(sw0.x + H*0.040f, sw0.y + readH), IM_COL32(255, 255, 255, 255), H*0.008f, 0, H*0.002f);
        ImGui::SameLine(0, H*0.016f);
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (readH - ImGui::GetTextLineHeight()) * 0.5f);
        char vals[64];
        snprintf(vals, sizeof vals, "R %d   G %d   B %d    #%02X%02X%02X", cr, cg, cb, cr, cg, cb);
        ImGui::Text("%s", vals);
    }
    // v2.55: interruptores de la rueda en su seccion (antes estaban todos
    // amontonados bajo "Boton flotante").
    {
        bool color = g_cfg.iconosColor != 0;
        if (PanelSwitch(S(STR_ICONOSCOLOR), &color, H)) { g_cfg.iconosColor = color ? 1 : 0; g_cfgDirty = false; SaveCfg(); PlayClick(false); Log("iconos de las emisoras: %s", g_cfg.iconosColor ? "a color" : "normales"); }
        bool lenta = g_cfg.camaraLenta != 0;
        if (PanelSwitch(S(STR_CAMARALENTA), &lenta, H)) { g_cfg.camaraLenta = lenta ? 1 : 0; g_cfgDirty = false; SaveCfg(); PlayClick(false); Log("camara lenta: %s", g_cfg.camaraLenta ? "activada" : "apagada"); }
        bool genero = g_cfg.mostrarGenero != 0;
        if (PanelSwitch(S(STR_GENERO), &genero, H)) { g_cfg.mostrarGenero = genero ? 1 : 0; g_cfgDirty = false; SaveCfg(); PlayClick(false); Log("genero en la rueda: %s", g_cfg.mostrarGenero ? "visible" : "oculto"); }
        bool fIco = g_cfg.fondoIcono != 0;
        if (PanelSwitch(S(STR_FONDOICONO), &fIco, H)) { g_cfg.fondoIcono = fIco ? 1 : 0; g_cfgDirty = false; SaveCfg(); PlayClick(false); ReloadIcons(); Log("fondo del icono: %s", g_cfg.fondoIcono ? "activado" : "apagado"); }
        bool anim = g_cfg.animacion != 0;
        if (PanelSwitch(S(STR_ANIMACION), &anim, H)) { g_cfg.animacion = anim ? 1 : 0; g_cfgDirty = false; SaveCfg(); PlayClick(false); Log("animacion de la rueda: %s", g_cfg.animacion ? "activada" : "apagada"); }
        bool bloqJ = g_cfg.bloquearJuego != 0;
        if (PanelSwitch(S(STR_BLOQUEARJUEGO), &bloqJ, H)) { g_cfg.bloquearJuego = bloqJ ? 1 : 0; g_cfgDirty = false; SaveCfg(); PlayClick(false); Log("bloquear juego con la rueda abierta: %s", g_cfg.bloquearJuego ? "activado" : "apagado"); }
    }
    ImGui::Separator();
    ImGui::TextColored(titleCol, "%s", S(STR_SEC_BOTON));
    ImGui::Separator();
    // v2.16: idioma + boton flotante (tamano, opacidad, bloqueo, posicion)
    // v2.23: globo a la izquierda de "Idioma" (como la imagen)
    {
        ImVec2 ip = ImGui::GetCursorScreenPos();
        float ir = H * 0.013f;
        DrawPanelIcon(dl, ip.x + ir, ip.y + ImGui::GetTextLineHeight() * 0.5f, ir, IC_GLOBE, IM_COL32(150, 190, 255, 255), H);
        ImGui::SetCursorScreenPos(ImVec2(ip.x + ir * 2.0f + H*0.008f, ip.y));
    }
    ImGui::Text("%s", S(STR_LANG));
    ImGui::SameLine(0, H*0.012f);
    {
        int langIdx = g_cfg.idioma;
        if (ImGui::Combo("##idioma", &langIdx, kLangNames, L_COUNT)) {
            g_cfg.idioma = langIdx; g_cfgDirty = false; SaveCfg();
            Log("idioma: %s", kLangCodes[g_cfg.idioma]);
        }
    }
    if (PanelSlider(S(STR_BTN_SIZE), &g_cfg.btnTam, 50, 150, "%d%%", H, IC_SIZE)) g_cfgDirty = true;
    if (PanelSlider(S(STR_BTN_OPAC), &g_cfg.btnOpac, 30, 100, "%d%%", H, IC_OPAC)) g_cfgDirty = true;
    // v1.9.60: controles propios del boton de la rueda (tamano, opacidad y
    // bloqueo de posicion, separados del boton flotante).
    if (PanelSlider(S(STR_BTN_RUEDA_SIZE), &g_cfg.btnRuedaTam, 50, 200, "%d%%", H, IC_SIZE)) g_cfgDirty = true;
    if (PanelSlider(S(STR_BTN_RUEDA_OPAC), &g_cfg.btnRuedaOpac, 10, 100, "%d%%", H, IC_OPAC)) g_cfgDirty = true;
    {
        bool bloqR = g_cfg.btnRuedaBloq != 0;
        if (PanelSwitch(S(STR_BTN_RUEDA_LOCK), &bloqR, H)) { g_cfg.btnRuedaBloq = bloqR ? 1 : 0; g_cfgDirty = false; SaveCfg(); PlayClick(false); Log("boton de la rueda: %s", g_cfg.btnRuedaBloq ? "bloqueado" : "libre"); }
    }
    {
        // v2.23: el bloqueo es un interruptor (no un boton con ON/OFF)
        bool bloq = g_cfg.btnBloq != 0;
        if (PanelSwitch(S(STR_BTN_LOCK), &bloq, H)) { g_cfg.btnBloq = bloq ? 1 : 0; g_cfgDirty = false; SaveCfg(); PlayClick(false); Log("boton flotante: %s", g_cfg.btnBloq ? "bloqueado" : "libre"); }
    }
    {
        // v2.55: solo los interruptores del boton (los de la rueda estan en
        // su seccion; el de iconos propios se quito).
        // v2.56: "Ocultar ventana de emisora" va aqui (la ventana es la
        // etiqueta junto al boton chiquito, no de la rueda).
        bool btnVis = g_cfg.botonVisible != 0;
        if (PanelSwitch(S(STR_BOTONVIS), &btnVis, H)) { g_cfg.botonVisible = btnVis ? 1 : 0; g_cfgDirty = false; SaveCfg(); PlayClick(false); Log("boton flotante: %s", g_cfg.botonVisible ? "visible" : "oculto"); }
        bool ocNom = g_cfg.ocultarNombre != 0;
        if (PanelSwitch(S(STR_OCULTARNOMBRE), &ocNom, H)) { g_cfg.ocultarNombre = ocNom ? 1 : 0; g_cfgDirty = false; SaveCfg(); PlayClick(false); Log("ventana de emisora: %s", g_cfg.ocultarNombre ? "oculta" : "visible"); }
        bool popIco = g_cfg.popupIcono != 0;  // v1.9.73: icono en el aviso de emisora
        if (PanelSwitch(S(STR_POPUPICONO), &popIco, H)) { g_cfg.popupIcono = popIco ? 1 : 0; g_cfgDirty = false; SaveCfg(); PlayClick(false); Log("aviso de emisora: icono %s", g_cfg.popupIcono ? "visible" : "oculto"); }
        bool popTxt = g_cfg.popupTexto != 0;  // v1.9.73: texto en el aviso de emisora
        if (PanelSwitch(S(STR_POPUPTEXTO), &popTxt, H)) { g_cfg.popupTexto = popTxt ? 1 : 0; g_cfgDirty = false; SaveCfg(); PlayClick(false); Log("aviso de emisora: texto %s", g_cfg.popupTexto ? "visible" : "oculto"); }
        if (PanelSlider(S(STR_POPUPTAM), &g_cfg.popupTam, 50, 200, "%d%%", H, IC_SIZE)) { g_cfgDirty = true; Log("aviso de emisora: tamano %d%%", g_cfg.popupTam); }  // v1.9.74: barra de tamano (vale con y sin AdvancedSettings)
    }
    // v2.23: botones en rejilla de 2 columnas con icono (como la imagen)
    {
        float bw = (ImGui::GetContentRegionAvail().x - H*0.010f) * 0.5f;
        if (PanelButtonIcon(S(STR_BTN_RESET_POS), H, bw, IC_RESET)) { g_cfg.btnX = -1.0f; g_cfg.btnY = -1.0f; g_cfg.btnRuedaX = -1.0f; g_cfg.btnRuedaY = -1.0f; g_cfgDirty = false; SaveCfg(); PlayClick(false); Log("posicion del boton restablecida"); }
        ImGui::SameLine(0, H*0.010f);
        if (PanelButtonIcon(S(STR_PREVIEW), H, bw, IC_EYE)) {
            g_preview = true; g_open = true; g_wheelAnimStart = std::chrono::steady_clock::now(); g_selected = g_current; g_settings = false;
            PlayClick(false);
            Log("vista previa de la rueda");
        }
    }
    ImGui::Separator();
    if (PanelButtonIcon(S(STR_RESET), H, 0.0f, IC_RESET)) {
        g_cfg.fondo = 30; g_cfg.tamano = 36; g_cfg.ringR = 0; g_cfg.ringG = 210; g_cfg.ringB = 255; g_cfg.textoR = 255; g_cfg.textoG = 255; g_cfg.textoB = 255;
        g_cfg.idioma = 0; g_cfg.btnX = -1.0f; g_cfg.btnY = -1.0f; g_cfg.btnTam = 100; g_cfg.btnOpac = 100; g_cfg.btnBloq = 0;
        g_cfg.fondoR = 0; g_cfg.fondoG = 0; g_cfg.fondoB = 0; g_cfg.panelX = -1.0f; g_cfg.panelY = -1.0f; g_cfg.panelAncho = 0.44f; g_cfg.panelAlto = -1.0f; g_cfg.iconosColor = 0; g_cfg.camaraLenta = 1; g_cfg.botonVisible = 1; g_cfg.mostrarGenero = 1; g_cfg.ocultarNombre = 0; g_cfg.popupIcono = 1; g_cfg.popupTexto = 1; g_cfg.popupTam = 100; g_cfg.fondoIcono = 0; g_cfg.fondoIconoR = 20; g_cfg.fondoIconoG = 40; g_cfg.fondoIconoB = 80; g_cfg.animacion = 1; g_cfg.blur = 0; g_cfg.bloquearJuego = 1; g_cfg.modoCambio = 0;
        g_cfgDirty = false; SaveCfg();
        Log("ajustes restablecidos");
    }
    // v2.24: credito al final del contenido (abajo del todo, como el
    // "By Hunter09" del menu de ARCX) - v2.26: cambia con el idioma
    ImGui::Dummy(ImVec2(0, H*0.008f));
    {
        const char* credit = S(STR_CREDIT);
        float cw = ImGui::CalcTextSize(credit).x;
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (ImGui::GetContentRegionAvail().x - cw) * 0.5f);
        ImGui::TextColored(ImVec4(0.55f, 0.60f, 0.75f, 1.0f), "%s", credit);
    }
    // v2.23: sin boton Cerrar abajo (se cierra con la X de arriba)
    // guardar al soltar (por si se arrastro el color o un deslizador y no se pulso Cerrar)
    if (g_cfgDirty && io.MouseReleased[0]) { SaveCfg(); g_cfgDirty = false; }
    ImGui::Dummy(ImVec2(0, H*0.035f));  // v2.18: espacio para la esquina de redimensionado (el grip se dibuja en primer plano, en la esquina de la ventana)
    // borde en degradado azul alrededor de toda la ventana
    ImDrawList* fg = ImGui::GetForegroundDrawList();
    AddGradientBorder(fg, wp, ImVec2(wp.x + ws.x, wp.y + ws.y), H*0.015f, H*0.004f, PanelGradL(), PanelGradR());
    // v2.22: cabecera fija en primer plano (titulo + subtitulo + X, como ARCX):
    // no se desplaza con el contenido; el contenido se desliza por debajo
    // v2.23: fondo opaco de la cabecera (tapa el contenido que se desplaza
    // por debajo; sin esto las funciones se veian detras del titulo)
    fg->AddRectFilled(ImVec2(wp.x, wp.y), ImVec2(wp.x + ws.x, wp.y + headerH), IM_COL32(5, 13, 33, 255), H*0.015f, ImDrawFlags_RoundCornersTop);
    fg->AddText(ImVec2(wp.x + pad + (titleW - ts.x) * 0.5f, titleY), ImGui::GetColorU32(titleCol), title);
    fg->AddRectFilledMultiColor(ImVec2(wp.x + pad, gradY), ImVec2(wp.x + ws.x - pad, gradY + H*0.003f), IM_COL32(20, 110, 255, 255), IM_COL32(10, 200, 255, 255), IM_COL32(10, 200, 255, 255), IM_COL32(20, 110, 255, 255));
    fg->AddText(ImVec2(wp.x + pad, subY), ImGui::GetColorU32(subCol), S(STR_HINT));
    fg->AddLine(ImVec2(wp.x + pad, subY + ImGui::GetTextLineHeight() + H*0.006f), ImVec2(wp.x + ws.x - pad, subY + ImGui::GetTextLineHeight() + H*0.006f), ImGui::GetColorU32(ImVec4(0.10f, 0.45f, 1.0f, 0.5f)), H*0.0015f);
    // X de cerrar (circulo oscuro con anillo azul y X blanca, como ARCX)
    {
        bool xAct = xPressed && io.MouseDown[0];
        fg->AddCircleFilled(xc, xr, xAct ? IM_COL32(60, 60, 90, 255) : IM_COL32(22, 22, 30, 255), 32);
        fg->AddCircle(xc, xr, IM_COL32(20, 110, 255, 255), 32, H*0.003f);
        fg->AddLine(ImVec2(xc.x - xr * 0.34f, xc.y - xr * 0.34f), ImVec2(xc.x + xr * 0.34f, xc.y + xr * 0.34f), IM_COL32(255, 255, 255, 255), H*0.003f);
        fg->AddLine(ImVec2(xc.x + xr * 0.34f, xc.y - xr * 0.34f), ImVec2(xc.x - xr * 0.34f, xc.y + xr * 0.34f), IM_COL32(255, 255, 255, 255), H*0.003f);
    }
    // v2.18: esquina de redimensionado en la esquina de la ventana (como en ARCX):
    // se dibuja en primer plano y se arrastra a mano para que nunca se pierda
    // al hacer scroll (el alto es ajustable y el contenido puede ser mas alto
    // que el panel). Cambia el ancho Y el alto (la v2.17 solo cambiaba el ancho).
    {
        float gripS = H * 0.035f;
        ImVec2 gp(wp.x + ws.x - gripS, wp.y + ws.y - gripS);
        if (!panelResizing && io.MouseClicked[0] && io.MousePos.x >= gp.x && io.MousePos.x <= gp.x + gripS && io.MousePos.y >= gp.y && io.MousePos.y <= gp.y + gripS) {
            panelResizing = true;
            gripOff = ImVec2(wp.x + ws.x - io.MousePos.x, wp.y + ws.y - io.MousePos.y);
        }
        if (panelResizing) {
            if (io.MouseDown[0]) {
                g_cfg.panelAncho = Clampf((io.MousePos.x + gripOff.x - wp.x) / W, 0.36f, 0.70f);
                g_cfg.panelAlto = Clampf((io.MousePos.y + gripOff.y - wp.y) / H, 0.30f, 0.95f);
            } else {
                panelResizing = false; SaveCfg(); Log("panel: ancho=%.2f alto=%.2f", g_cfg.panelAncho, g_cfg.panelAlto);
            }
        }
        const ImU32 gc = panelResizing ? IM_COL32(10, 200, 255, 255) : IM_COL32(190, 192, 205, 200);
        const float e = gripS * 0.82f;
        for (int k = 0; k < 3; k++) {
            const float o = e * (0.30f + 0.28f * (float)k);
            fg->AddLine(ImVec2(gp.x + gripS - H*0.006f, gp.y + gripS - o), ImVec2(gp.x + gripS - o, gp.y + gripS - H*0.006f), gc, H*0.003f);
        }
    }
    // v2.21: "Desliza para ver mas" (como ARCX) - si el contenido no cabe y
    // queda mas abajo, se avisa en la parte inferior del panel (solo arriba;
    // al empezar a desplazar se quita para no tapar los ajustes)
    if (ImGui::GetScrollMaxY() > 0.0f && ImGui::GetScrollY() < 1.0f) {
        ImVec2 ts2 = ImGui::CalcTextSize(S(STR_DESLIZA));
        float hx = wp.x + (ws.x - ts2.x) * 0.5f;
        float hy = wp.y + ws.y - ts2.y - H*0.012f;
        fg->AddText(ImVec2(hx, hy), IM_COL32(150, 190, 255, 220), S(STR_DESLIZA));
    }
    ImGui::End();
    if (g_fontUI) ImGui::PopFont();
    ImGui::PopStyleColor(3);
    ImGui::PopStyleVar(4);
}

// v2.51: PlausiblePtr eliminado (solo lo usaba el escaneo ptrCand, que ya se quito).
// v2.9: true si la pagina de p esta mapeada (mincore devuelve 0); quita el
// byte de etiqueta (0xb4...) antes de preguntar al kernel. Sin esto, leer un
// valor que "parece" puntero pero no esta mapeado cierra el juego (v2.8).
static bool IsMapped(const void* p) {
    uintptr_t a = ((uintptr_t)p & 0x00FFFFFFFFFFFFFFULL) & ~0xFFFULL;
    unsigned char vec = 0;
    return mincore((void*)a, 0x1000, &vec) == 0;
}
// v2.15: el vehiculo tiene radio civil? Por LISTA DE MODELOS (ver header).
// La funcion del juego cerraba el juego (SIGILL en su primera instruccion),
// asi que no se llama a nada del juego: solo se lee el modelo en 0x32
// (verificado por el log v2.13: 445 Admiral / 509 Bike) y se compara con
// la lista. Fail-open: modelo fuera de rango -> la rueda ABRE.
// v2.17: el bloqueo se registra como mucho una vez cada 2 s (antes salia
// dos veces por cada doble toque al claxon: por el boton 7 y por la zona)
// v1.9.54: LogBlocked eliminado (el claxon ya no abre la rueda).
static bool VehicleHasRadio(void* veh) {
    if (!veh || !IsMapped(veh)) { Log("  radio: sin vehiculo, asumo radio"); return true; }
    if (!IsMapped((char*)veh + 0x32)) { Log("  radio: modelo no mapeado, asumo radio"); return true; }
    uint16_t modelId = *(uint16_t*)((char*)veh + 0x32);
    if (modelId < 400 || modelId > 611) { Log("  radio: modelo=%u fuera de rango, asumo radio", (unsigned)modelId); return true; }
    bool noRadio = false;
    switch (modelId) {
        case 481: case 509: case 510:  // bicis: BMX, Bike, Mountain Bike
        case 407: case 416: case 427: case 432: case 433: case 490: case 497: case 523: case 528: case 544:  // emergencia: bomberos, ambulancia, Enforcer, Rhino, Barracks, FBI Rancher, Police Maverick, HPV1000, FBI Truck, Firetruck LA
        case 596: case 597: case 598: case 599: case 601:  // patrullas LSPD/SFPD/LVPD/Ranger + S.W.A.T.
        case 441: case 449: case 457: case 464: case 465: case 485: case 501: case 530: case 531: case 532:  // especiales/trabajo: RC Bandit, Tram, Caddy, RC Baron, RC Raider, Baggage, RC Goblin, Forklift, Tractor, Combine
        case 537: case 538: case 539: case 564: case 568: case 571: case 572: case 583: case 594:  // especiales/RC: Freight, Streak, Vortex, RC Tiger, Bandito, Kart, Mower, Tug, RC Cam
            noRadio = true; break;
        default: break;
    }
    Log("  vehiculo: modelo=%u tieneRadio=%d", (unsigned)modelId, noRadio ? 0 : 1);
    return !noRadio;
}
// v2.51: HexDump eliminado (solo lo usaban el volcado del vehiculo y el escaneo ptrCand).
// v1.9.73: aviso de emisora en el cuadro ImGui (rueda/panel/lista abiertos).
// El camino GL (solo boton) lo dibuja DrawButtonGL con los mismos datos.
static void DrawPopup(float W, float H) {
    ImFont* font = g_fontTitle ? g_fontTitle : ImGui::GetFont();
    if (!font) return;
    float ns, ts; BtnLabelSizes(H, ns, ts);
    float pns = ns * g_cfg.popupTam / 100.0f;  // v1.9.74: tamano del aviso
    const char* name = kStationName[g_current];
    float textW = font->CalcTextSizeA(pns, FLT_MAX, 0.0f, name, nullptr).x;
    ImVec2 i0, i1, tPos; bool showIcon, showText;
    if (!PopupGeom(W, H, pns, textW, i0, i1, tPos, showIcon, showText)) return;
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    if (showIcon) {
        GLuint it = IconTex(g_current);
        if (it) dl->AddImage((ImTextureID)(uintptr_t)it, i0, i1);
    }
    if (showText) {
        // Sombra negra como la del texto del juego (SetDropColor negro).
        dl->AddText(font, pns, ImVec2(tPos.x + 2.0f, tPos.y + 2.0f), IM_COL32(0, 0, 0, 255), name);
        dl->AddText(font, pns, tPos, IM_COL32(255, 255, 255, 255), name);
    }
}

static void DrawFrame(EGLDisplay dpy, EGLSurface surf) {
    if (g_failed) return;
    EGLContext ctx = eglGetCurrentContext();
    if (ctx == EGL_NO_CONTEXT) return;
    // v1: durante la pantalla de carga no hacer nada (ni leer el vehiculo ni
    // dibujar): el juego esta cargando texturas y la rueda/boton no se ven.
    // ut_loadingActive es CLoadingScreen::m_bActive (resuelto en UT_InstallHooks).
    // Va ANTES del cambio aplazado para que este espere a que termine la carga.
    if (ut_loadingActive && *ut_loadingActive) return;
    // v2.59: cambio de estacion aplazado (modoCambio=1): 3 cuadros despues
    // de cerrar la rueda, sin UI ni camara lenta.
    if (g_pendingStation != 0 && --g_pendingFrames <= 0) {
        int pid = g_pendingStation;
        g_pendingStation = 0; g_pendingFrames = 0;
        DoStartRadio(pid);
    }
    // v2.51: el tamano de pantalla se consulta una vez y cada 120 cuadros
    // (antes 2 eglQuerySurface en cada cuadro)
    static EGLint cw = 0, ch = 0; static int qTick = 0;
    if (cw <= 0 || ch <= 0 || (++qTick % 120) == 0) { eglQuerySurface(dpy, surf, EGL_WIDTH, &cw); eglQuerySurface(dpy, surf, EGL_HEIGHT, &ch); }
    EGLint w = cw, h = ch;
    if (w < 200 || h < 200) return;
    void* veh = FindPlayerVehicle ? FindPlayerVehicle(-1, false) : nullptr;
    bool wasInVehicle = g_inVehicle;
    g_inVehicle = veh != nullptr;
    // v2.14: radio por modelo, UNA VEZ por vehiculo (no cada frame: asi no
    // parpadea como el rid en la v2.11). Fail-open dentro de VehicleHasRadio.
    {
        static void* lastVehRadio = nullptr;
        if (!g_inVehicle) { lastVehRadio = nullptr; g_hasRadio = true; }
        else if (veh != lastVehRadio) {
            lastVehRadio = veh;
            g_hasRadio = VehicleHasRadio(veh);
            if (!g_hasRadio) g_open = false;  // rueda cerrada si no tiene radio
        }
    }
    // v2.51: el escaneo ptrCand y el volcado hex del vehiculo ya cumplieron
    // su funcion (encontraron la entidad de audio y m_nModelIndex en 0x32);
    // el bloqueo usa la lista de modelos, no el escaneo. Se quitan para no
    // hacer mincore + fopen/fclose en cada entrada a un vehiculo.
    if (g_inVehicle && !wasInVehicle && veh) {
        Log("  vehiculo: %p estacion=%d", veh, g_current);
    }
    // v2.51: diagnostico de audio (rt/rid) eliminado: el rid se probo
    // inestable en la v2.11 y el bloqueo no lo usa (usa la lista de modelos).
    // v2.6: pausa por varias senales (el byte 0x5C solo no basto en la v2.5):
    // CMenuManager::m_bMenuActive, CTimer::m_UserPause / m_CodePause, y si
    // CMenuManager::DrawFrontEnd se llamo en este frame (menu dibujado)
    bool fe = g_menuDrawn; g_menuDrawn = false;
    bool wasPaused = g_paused;
    g_paused = MenuActive() || (g_userPause && *g_userPause) || (g_codePause && *g_codePause) || fe;
    if (g_paused != wasPaused) { Log("menu de pausa: %s (byte=%d user=%d code=%d frontEnd=%d)", g_paused ? "abierto (UI oculta)" : "cerrado", MenuActive() ? 1 : 0, (g_userPause && *g_userPause) ? 1 : 0, (g_codePause && *g_codePause) ? 1 : 0, fe ? 1 : 0); if (g_menuCheck == 0) g_menuCheck = 1; }  // v2.4: el byte cambio: el offset es valido
    // v2.7: ralentizacion estilo GTA V - la v2.6 solo escribia los valores al
    // abrir/cerrar y el juego los borraba en el siguiente frame (el log decia
    // "activada" pero no se veia nada). Ahora se escriben EN CADA FRAME
    // mientras la rueda esta abierta: bandera + escala del slow-motion del
    // juego + la escala de tiempo directamente. Al cerrar se restauran los
    // valores que habia antes de abrir.
    if (g_slowActive || g_timeScale) {
        static bool slowOn = false;
        static float savedTimeScale = 1.0f, savedSlowScale = 0.3f;
        static bool savedSlowActive = false;
        if ((g_open || g_wheelClosing) && !slowOn && g_cfg.camaraLenta) {
            slowOn = true;
            if (g_timeScale) savedTimeScale = *g_timeScale;
            if (g_slowScale) savedSlowScale = *g_slowScale;
            if (g_slowActive) savedSlowActive = *g_slowActive;
            Log("ralentizacion: activada (guardada escala=%.2f)", savedTimeScale);
        } else if (!g_open && !g_wheelClosing && slowOn) {
            slowOn = false;
            if (g_slowActive) *g_slowActive = savedSlowActive;
            if (g_slowScale) *g_slowScale = savedSlowScale;
            if (g_timeScale) *g_timeScale = savedTimeScale;
            Log("ralentizacion: desactivada (restaurada escala=%.2f)", savedTimeScale);
        }
        if (slowOn) {
            if (g_slowActive) *g_slowActive = true;
            if (g_slowScale) *g_slowScale = 0.3f;
            // v1.9.62: se vuelve a escribir ms_fTimeScale (revertir 1.9.61):
            // sin el, la camara lenta visual NO se activa en este telefono
            // (Daniel 2026-11-22: "la camara lenta se desactivo"). La 1.9.61
            // lo quito para que la radio sonara con la rueda abierta, pero
            // el slow-motion propio solo no frena el juego; ms_fTimeScale es
            // lo que da el efecto. Se acepta que con la rueda abierta la
            // radio del juego queda muda y User Tracks sigue sonando.
            if (g_timeScale) *g_timeScale = 0.3f;
        }
    }
    if (!g_inited) {
        // v2.48: mientras carga la partida no iniciar ImGui ni cargar los
        // iconos: las subidas de textura en plena carga ensucian el estado GL
        // del juego y se ven texturas del juego a pantalla completa al reanudar.
        if (g_loadingActive && *g_loadingActive) return;
        if (!FindPlayerPed || !FindPlayerPed(-1)) return;
        ImGui::CreateContext(); ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr; io.LogFilename = nullptr;
        io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
        ImGui::StyleColorsDark();
        // v2.1: letra mas parecida a GTA V - Roboto Condensed negrita del sistema
        // v1.9.52: rango de glifos ampliado (latin-1 + latin extendido-A +
        // cirilico): sin esto ImGui solo dibuja U+0020-U+00FF y los nombres
        // con ş/ı (turco, "Işık") o cirilico salen en blanco. La fuente del
        // sistema SI tiene esos glifos; solo habia que pedirlos.
        static const ImWchar kGlyphRanges[] = { 0x0020, 0x00FF, 0x0100, 0x017F, 0x0400, 0x04FF, 0 };
        io.Fonts->AddFontDefault();
        const char* titlePath = nullptr;
        for (const char* fp : {"/system/fonts/RobotoCondensed-Bold.ttf", "/system/fonts/Roboto-Bold.ttf", "/system/fonts/DroidSans-Bold.ttf"}) {
            g_fontTitle = io.Fonts->AddFontFromFileTTF(fp, 64.0f, nullptr, kGlyphRanges);
            if (g_fontTitle) { titlePath = fp; Log("fuente del titulo: %s", fp); break; }
        }
        if (!g_fontTitle) Log("  !! sin fuente del sistema: el titulo usa la letra de ImGui");
        // v2.3: segunda talla de la misma fuente para el panel (letra legible al tacto)
        if (titlePath) g_fontUI = io.Fonts->AddFontFromFileTTF(titlePath, 32.0f, nullptr, kGlyphRanges);
        if (!ImGui_ImplOpenGL3_Init(nullptr)) { Log("ImGui_ImplOpenGL3_Init fallo"); g_failed = true; return; }
        g_inited = true; LoadRadioTextures(); RefreshCurrent();
        Log("ImGui listo (%.0fx%.0f), estacion actual: %d (%s)", (float)w, (float)h, g_current, kStationName[g_current]);
    }
    LoadRadioTextures();  // v2.54: recargar si ReloadIcons marco g_texTried=false (antes solo se cargaba una vez al inicio; al apagar/encender los iconos propios desaparecian y no volvian). Con el guarda es un no-op si ya estan.
    ImGuiIO& io = ImGui::GetIO();
    static auto last = std::chrono::steady_clock::now();
    auto now = std::chrono::steady_clock::now();
    float dt = std::chrono::duration<float>(now - last).count(); last = now;
    io.DeltaTime = dt > 0.0001f ? dt : 1.0f/60.0f;
    io.DisplaySize = ImVec2((float)w, (float)h);
    // eventos de toque -> raton de ImGui; el boton y la rueda se resuelven por posicion
    std::vector<TouchEv> evs; { std::lock_guard<std::mutex> lk(g_inMtx); evs.swap(g_evs); }
    static bool mouseDown = false;
    for (auto& e : evs) {
        if (e.kind == 0) {
            if (g_open || g_preview || g_settings || g_trackList) io.AddMousePosEvent(e.x, e.y);  // v2.50: con solo el boton, el toque no llega a ImGui
            g_touchX = e.x; g_touchY = e.y;  // v2.18: posicion propia del toque
            if (g_open && !g_settings && !g_paused) {
                int seg = SegmentAt(e.x, e.y, (float)w, (float)h);
                if (seg >= 1) { g_wheelTouch = true; if (seg != g_selected) { g_selected = seg; PlayClick(true); Log("rueda: segmento %d (%s)", seg, kStationName[seg]); } }
            }
            // v2.16: arrastrar el boton flotante (si no esta bloqueado)
            if (g_btnDrag && !g_cfg.btnBloq) {
                float dx = e.x - g_dragSX, dy = e.y - g_dragSY;
                float thr = (float)h * 0.02f;  // v2.17: antes 0.012H - el temblor del dedo movia el boton en vez de tocarlo
                if (!g_btnMoved && dx*dx + dy*dy > thr*thr) g_btnMoved = true;
                if (g_btnMoved) {
                    float bs = BtnSize((float)h);
                    float nx = g_dragBX + dx / (float)w;
                    float ny = g_dragBY + dy / (float)h;
                    if (nx < 0.0f) nx = 0.0f; if (nx > 1.0f - bs / (float)w) nx = 1.0f - bs / (float)w;
                    if (ny < 0.0f) ny = 0.0f; if (ny > 1.0f - bs / (float)h) ny = 1.0f - bs / (float)h;
                    g_cfg.btnX = nx; g_cfg.btnY = ny;
                }
            }
            // v1.9.55: arrastrar el boton propio de la rueda (separado del
            // flotante). v1.9.56: SIN bloqueo de posicion: el candado (btnBloq)
            // es del boton flotante; con bloq=1 el boton de la rueda no se
            // podia mover y solo abria/cerraba la rueda al intentar
            // arrastrarlo. El flotante sigue respetando el candado.
            // v1.9.57: umbral 0.008H (antes 0.02H): con 0.02H el boton no se
            // movia hasta haber arrastrado ~18 px y se sentia con retraso;
            // con ~7 px empieza casi de inmediato y se siente fluido. Un
            // toque normal mueve el dedo menos de eso, asi que sigue abriendo
            // la rueda.
            if (g_wheelBtnDown && !g_cfg.btnRuedaBloq) {  // v1.9.60: si el bloqueo esta activo no se mueve (solo abre la rueda)
                float dx = e.x - g_wheelBtnSX, dy = e.y - g_wheelBtnSY;
                float thr = (float)h * 0.008f;
                if (!g_wheelBtnMoved && dx*dx + dy*dy > thr*thr) g_wheelBtnMoved = true;
                if (g_wheelBtnMoved) {
                    float bs = BtnSize((float)h);
                    float nx = g_wheelDragBX + dx / (float)w;
                    float ny = g_wheelDragBY + dy / (float)h;
                    if (nx < 0.0f) nx = 0.0f; if (nx > 1.0f - bs / (float)w) nx = 1.0f - bs / (float)w;
                    if (ny < 0.0f) ny = 0.0f; if (ny > 1.0f - bs / (float)h) ny = 1.0f - bs / (float)h;
                    g_cfg.btnRuedaX = nx; g_cfg.btnRuedaY = ny;
                }
            }
        } else if (e.kind == 1) { if (g_open || g_preview || g_settings || g_trackList) io.AddMouseButtonEvent(0, true); mouseDown = true;  // v2.50: con solo el boton, el toque no llega a ImGui
            // v2.3: en el menu de pausa los toques son del juego; con el panel abierto, del panel (ImGui)
            if (g_paused) {
                // nada: el menu de pausa procesa el toque
            } else if (g_settings) {
                // nada: ImGui procesa el toque en NewFrame/Render
            } else if (g_preview) {
                // vista previa: solo se mira - tocar un segmento lo resalta, tocar fuera cierra la vista previa y vuelve el panel
                int seg = SegmentAt(g_touchX, g_touchY, (float)w, (float)h);
                if (seg >= 1) { g_selected = seg; g_wheelTouch = true; }
                else { g_preview = false; g_open = false; g_settings = true; Log("vista previa cerrada"); }
            } else if (g_open) {
                // v1.5: tocar un segmento lo elige al instante (antes solo el arrastre elegia)
                g_trackBtn = 0;  // un toque nuevo limpia cualquier flecha anterior
                int seg = SegmentAt(g_touchX, g_touchY, (float)w, (float)h);
                int ta = 0;
                if (seg >= 1) { g_selected = seg; g_wheelTouch = true; g_wheelCenterTap = false; PlayClick(true); Log("rueda: segmento %d (%s) [toque]", seg, kStationName[seg]); }
                else if ((ta = TrackArrowAt(g_touchX, g_touchY)) != 0) { g_trackBtn = ta; g_wheelTouch = false; g_wheelCenterTap = false; }  // v1.2: flechas anterior/siguiente de la cancion
                else if (g_listBtn && g_touchX >= g_listBtnRect.x && g_touchX <= g_listBtnRect.x + g_listBtnRect.z && g_touchY >= g_listBtnRect.y && g_touchY <= g_listBtnRect.y + g_listBtnRect.w) { g_listBtnTap = true; g_wheelTouch = false; g_wheelCenterTap = false; }  // v1.4: boton "Lista de canciones"
                else { g_wheelTouch = false; g_wheelCenterTap = CenterAt(g_touchX, g_touchY, (float)w, (float)h); }  // v1.9.71: solo el centro cierra la rueda; fuera del anillo no hace nada (antes cerraba tambien)
            }
            // v1.9.54: boton propio de la rueda (encima del flotante): un toque
            // abre/cierra la rueda. El claxon ya NO abre la rueda (a peticion
            // de Daniel: la rueda tiene su propio boton). Se resuelve al
            // soltar (si el dedo no se movio), como el boton flotante.
            if (!g_open && !g_preview && !g_settings && !g_trackList && !g_paused && g_inVehicle && g_hasRadio && g_cfg.botonVisible) {
                if (WheelBtnHit(g_touchX, g_touchY, (float)w, (float)h)) {
                    g_wheelBtnDown = true; g_wheelBtnMoved = false; g_wheelBtnSX = g_touchX; g_wheelBtnSY = g_touchY;
                    ImVec2 wp; float ws;
                    WheelBtnGeom((float)w, (float)h, wp, ws);
                    g_wheelDragBX = wp.x / (float)w; g_wheelDragBY = wp.y / (float)h;
                }
            }
            // v2.2: boton flotante - un toque = siguiente estacion; dos toques = panel de personalizacion
            // v2.16: sin radio no hay boton ni panel; el toque se resuelve al SOLTAR
            // (si el dedo se movio, fue un arrastre para mover el boton, no un toque)
            // v1.4: las flechas de cancion junto al boton se resuelven AL TOCAR
            // (cambian de cancion sin abrir la rueda ni tocar la emisora).
            // v1.5: las flechas van DENTRO de la etiqueta (a los lados del nombre).
            bool arrowTap = false;
            if (!g_open && !g_preview && !g_settings && !g_trackList && !g_paused && g_inVehicle && g_hasRadio && g_cfg.botonVisible && g_lblArrows) {
                ImVec2 bp, lp, ls; float bs;
                FloatBtnGeom((float)w, (float)h, bp, lp, ls, bs);  // refresca g_lblPrevC/g_lblNextC
                float ar = g_lblArrowR;
                float dx = g_touchX - g_lblPrevC.x, dy = g_touchY - g_lblPrevC.y;
                if (dx*dx + dy*dy <= ar*ar*1.44f) {
                    UT_PrevTrack();
                    PlayClick(true);
                    Log("boton flotante: anterior cancion (flecha)");
                    arrowTap = true;
                } else {
                    dx = g_touchX - g_lblNextC.x; dy = g_touchY - g_lblNextC.y;
                    if (dx*dx + dy*dy <= ar*ar*1.44f) {
                        UT_NextTrack();
                        PlayClick(true);
                        Log("boton flotante: siguiente cancion (flecha)");
                        arrowTap = true;
                    }
                }
            }
            if (!arrowTap && !g_wheelBtnDown && !g_open && !g_preview && !g_settings && !g_trackList && !g_paused && g_inVehicle && g_hasRadio && g_cfg.botonVisible) {
                if (FloatBtnHit(g_touchX, g_touchY, (float)w, (float)h)) {
                    g_btnDrag = true; g_btnMoved = false;
                    g_dragSX = g_touchX; g_dragSY = g_touchY;
                    ImVec2 ip, lp, ls; float bs;
                    FloatBtnGeom((float)w, (float)h, ip, lp, ls, bs);
                    g_dragBX = (g_cfg.btnX >= 0.0f ? g_cfg.btnX : ip.x / (float)w);
                    g_dragBY = (g_cfg.btnY >= 0.0f ? g_cfg.btnY : ip.y / (float)h);
                }
            }
        } else if (e.kind == 2) { if (g_open || g_preview || g_settings || g_trackList) { io.AddMouseButtonEvent(0, false); io.AddMousePosEvent(-FLT_MAX, -FLT_MAX); }  // v2.50: con solo el boton, el toque no llega a ImGui
            // v1.9.54: el toque al boton propio de la rueda se resuelve al
            // soltar (si el dedo no se movio mas de 0.02H): abre/cierra la rueda.
            // v1.9.55: si se movio, fue un arrastre: se guarda la posicion propia.
            if (g_wheelBtnDown) {
                g_wheelBtnDown = false;
                if (g_wheelBtnMoved) { SaveCfg(); Log("boton rueda movido a (%.2f, %.2f)", g_cfg.btnRuedaX, g_cfg.btnRuedaY); }
                else if (!g_open && !g_preview && !g_settings && !g_trackList && !g_paused && g_inVehicle && g_hasRadio && g_cfg.botonVisible) {
                    ToggleWheel("boton rueda");
                }
            }
            // v2.16: el toque al boton flotante se resuelve al soltar
            if (g_btnDrag) {
                g_btnDrag = false;
                if (g_btnMoved) { SaveCfg(); Log("boton flotante movido a (%.2f, %.2f)", g_cfg.btnX, g_cfg.btnY); }
                else if (!g_open && !g_preview && !g_settings && !g_trackList && !g_paused && g_inVehicle && g_hasRadio && g_cfg.botonVisible) {
                    // v1.5: en la 12, tocar la ETIQUETA ya no cambia de cancion
                    // (para eso estan las flechas dentro de la etiqueta); el
                    // icono sigue cambiando de emisora como siempre.
                    ImVec2 ip, lp, ls; float bs;
                    FloatBtnGeom((float)w, (float)h, ip, lp, ls, bs);
                    bool onLabel = (!g_cfg.ocultarNombre &&
                        g_touchX >= lp.x && g_touchX <= lp.x + ls.x && g_touchY >= lp.y && g_touchY <= lp.y + ls.y);
                    if (g_current == 12 && ut_active && onLabel) {
                        // Nada: el nombre es informativo; las flechas cambian la cancion.
                    } else {
                        static auto lastBtnTap = std::chrono::steady_clock::now() - std::chrono::seconds(5);
                        auto now = std::chrono::steady_clock::now();
                        if (now - lastBtnTap < std::chrono::milliseconds(600)) {  // v2.17: antes 400 ms - a veces no daba tiempo al segundo toque
                            g_btnPending = false;
                            g_settings = true;
                            PlayClick(false);
                            Log("panel de personalizacion abierto");
                        } else {
                            g_btnPending = true; g_btnPendingAt = now;
                        }
                        lastBtnTap = now;
                    }
                }
            }
            if (g_ignoreNextUp) { g_ignoreNextUp = false; mouseDown = false; }  // v1.5: el toque que abrio la rueda no la cierra
            else if (mouseDown && g_open && !g_paused) {
                if (g_preview) { /* v2.2: en vista previa no se aplica ni se cierra */ }
                else if (g_trackBtn != 0) {  // v1.2: flecha anterior/siguiente: cambia la cancion y la rueda sigue abierta
                    if (g_trackBtn < 0) UT_PrevTrack(); else UT_NextTrack();
                    PlayClick(true);
                    Log("rueda: %s cancion", g_trackBtn < 0 ? "anterior" : "siguiente");
                    g_trackBtn = 0;
                }
                else if (g_listBtnTap) { g_listBtnTap = false; g_open = false; g_trackList = true; g_listOpenedAt = std::chrono::steady_clock::now(); PlayClick(false); Log("lista de canciones abierta"); }  // v1.4: boton "Lista de canciones" de la rueda
                else if (g_wheelTouch) { ApplyStation(g_selected); Log("rueda: cambio a %d (%s), sigue abierta (se cierra desde el centro)", g_selected, kStationName[g_selected]); }
                else if (g_wheelCenterTap) { Log("rueda cerrada desde el centro"); g_open = false; }
                else { Log("rueda: toque fuera del anillo, sigue abierta"); }
            }
            g_wheelCenterTap = false;
            mouseDown = false; }
    }
    // v2.1: un solo toque al boton flotante = siguiente estacion (si no llega un segundo toque en 600 ms)
    if (g_btnPending) {
        if (g_open || g_settings || g_trackList || g_paused || !g_hasRadio || !g_cfg.botonVisible) g_btnPending = false;  // rueda, panel, pausa, sin radio o boton oculto: no cambiar de estacion
        else if (std::chrono::steady_clock::now() - g_btnPendingAt > std::chrono::milliseconds(600)) {
            g_btnPending = false;
            int next = g_current % 13 + 1;
            ApplyStation(next);
            PlayClick(true);
            Log("boton: siguiente estacion %d (%s)", next, kStationName[next]);
        }
    }
    // v1.4: el juego actualiza la estacion leida con un poco de retraso tras StartRadio
    // (en el log v1.3 la lectura iba una estacion atrasada); re-leer cada ~0.5 s para
    // que el icono del boton flotante no se quede en la estacion anterior.
    // v2.47 (Google AI Mode): no consultar el motor nativo cuando suena
    // User Tracks (12): la lectura nativa va un cambio por detras y
    // desincronizaria la rueda (ademas ponia g_current=13 cada 0.5 s).
    // v1.9: la nativa queda en 12 (Mixtape), pero la lectura sigue yendo un
    // cambio por detras, asi que la guarda se mantiene.
    if (g_inVehicle && !g_open && g_current != 12) {
        static int frameN = 0;
        if (++frameN % 30 == 0) RefreshCurrent();
    }
    // v1.9: claxon por el boton 7 directo - el byte 84 (0x54) de m_pWidgets[7]
    // es 1 mientras el boton esta pulsado (confirmado por el log de la v1.8).
    // v1.9.54: el claxon ya NO abre la rueda (la rueda tiene su propio boton);
    // este bloque solo valida el offset de pausa (m_bMenuActive).
    if (g_widgets && g_inVehicle) {
        static bool prevHornDown = false;
        void* w7 = g_widgets[kHornWidgetId];
        bool hornDown = w7 && ((const unsigned char*)w7)[84] != 0;
        // v2.4: validar el offset de pausa - el claxon solo se puede pulsar con el juego activo;
        // si el byte de menu dice 1 mientras se pulsa, el offset 0x5C no es el menu: desactivar
        if (hornDown && !prevHornDown && g_menuCheck == 0 && MenuActive()) {
            g_menuCheck = -1;
            Log("  !! m_bMenuActive=1 con el claxon pulsado: el offset 0x5C no es el menu en este telefono; pausa desactivada");
        }
        prevHornDown = hornDown;
    }
    if (!g_inVehicle) { g_open = false; g_preview = false; g_settings = false; g_btnDrag = false; }  // v2.2: fuera del coche se cierra todo
    // v2.30: si no hay nada que dibujar (rueda cerrada, panel cerrado, boton
    // oculto), saltar el frame de ImGui: es el trabajo que la rueda hace en
    // CADA frame y el que le quita CPU al decodificador de MP3 del mod de
    // User Tracks. Con el boton visible se dibuja como siempre.
    // v2.48: al abrir el panel, descartar el estado de arrastre/desplazamiento
    // de la sesion anterior (si no, una barra se restauraba a su valor viejo
    // al reabrir, y el panel podia desplazarse solo por la inercia guardada).
    static bool prevSettings = false;
    if (g_settings && !prevSettings) g_panelJustOpened = true;
    prevSettings = g_settings;
    // v1.9.55: animacion de cierre: si la rueda estaba abierta y ahora esta
    // cerrada volviendo al juego (no a la lista/panel/vista previa, y
    // todavia en el vehiculo), empieza la animacion de cierre (250 ms).
    // Si se vuelve a abrir, el cierre se cancela.
    {
        static bool wasOpen = false;
        if (g_open) { wasOpen = true; g_wheelClosing = false; }
        else if (wasOpen) {
            wasOpen = false;
            if (!g_preview && !g_settings && !g_trackList && g_inVehicle && !g_paused && g_cfg.animacion) {
                g_wheelClosing = true; g_wheelCloseStart = std::chrono::steady_clock::now();
            }
        }
    }
    // v1.9.73: armar el aviso de emisora 2.5 s al cambiar de estacion (lo que
    // dura el texto del juego). g_current lo actualizan los hooks de
    // StartRadio/RetuneRadio y DoStartRadio; solo en cambio real, no al
    // arrancar (la primera vez que se ve, g_popupLastStation es -1).
    // AdvancedSettings se re-chequea aqui por si cargo despues que este mod.
    if (g_current != g_popupLastStation) {
        bool firstSeen = (g_popupLastStation == -1);
        g_popupLastStation = g_current;
        if (!firstSeen && !g_advSettings) g_advSettings = (dlopen("libAdvancedSettings.SA64.so", RTLD_NOLOAD | RTLD_NOW) != nullptr);
        if (!firstSeen && g_current >= 1 && g_current <= 13 && (g_cfg.popupIcono || g_cfg.popupTexto) && !g_cfg.ocultarNombre)
            g_popupUntil = std::chrono::steady_clock::now() + std::chrono::milliseconds(2500);
    }
    // v1.9.63: boton nativo de la rueda (widget del juego via SAUtils):
    // se crea/mueve/muestra aqui, antes de decidir que dibujar, para que
    // el widget este en su sitio cuando el juego dibuje los botones.
    NativeWheelUpdate((float)w, (float)h);
    bool onlyButton = g_inVehicle && !g_paused && !g_open && !g_preview && !g_settings && !g_trackList && !g_wheelClosing && g_hasRadio && g_cfg.botonVisible;
    if (onlyButton && g_inited) {
        // v2.33: solo el boton -> OpenGL directo, sin cuadro ImGui.
        if (!DrawButtonGL((float)w, (float)h)) {
            // Si el dibujo GL fallo, volver a ImGui para que el boton se vea.
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            ImGui_ImplOpenGL3_NewFrame(); ImGui::NewFrame();
            if (g_inVehicle && !g_paused) DrawWheel((float)w, (float)h);
            if (g_inVehicle && !g_paused) DrawPopup((float)w, (float)h);  // v1.9.73: aviso de emisora (DrawButtonGL fallo y no lo dibujo)
            ImGui::Render(); ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        }
        return;
    }
    bool needDraw = (g_inVehicle && !g_paused && (g_open || g_preview || g_cfg.botonVisible)) || ((g_settings || g_trackList) && !g_paused);
    if (needDraw) {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        // v2.49: en el primer cuadro del panel el raton va limpio (sin el clic
        // que lo abrio): asi ese clic no activa una barra o un boton del panel.
        if (g_panelJustOpened) { io.AddMouseButtonEvent(0, false); io.AddMousePosEvent(-FLT_MAX, -FLT_MAX); }
        ImGui_ImplOpenGL3_NewFrame(); ImGui::NewFrame();
        if (g_inVehicle && !g_paused) DrawWheel((float)w, (float)h);
        if (g_inVehicle && !g_paused) DrawPopup((float)w, (float)h);  // v1.9.73: aviso de emisora
        if (g_settings && !g_paused) DrawSettings((float)w, (float)h);
        if (g_trackList && !g_paused) DrawTrackList((float)w, (float)h);
        ImGui::Render(); ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    }
    g_panelJustOpened = false;  // v2.48: el reseteo de apertura solo vale para el primer cuadro
}

static EGLBoolean Swap_hook(EGLDisplay d, EGLSurface s) {
    DrawFrame(d, s);
    UT_Frame();  // v1: reproductor de User Tracks propio (pausa/reanuda/para)
    return Swap_orig(d, s);
}

extern "C" void OnModLoad() {
    // v1.9.14: el paquete se detecta del proceso (Rehan usa uno
    // personalizado, p. ej. com.rockstargames.gtasa2; antes estaba fijo a
    // com.rockstargames.gtasa y su INI se generaba en esa carpeta aunque el
    // juego fuera otro). El proceso principal de una app Android se llama
    // como su paquete; si no se puede leer, queda el original.
    {
        FILE* cf = fopen("/proc/self/cmdline", "rb");
        if (cf) {
            char cmd[256] = {0};
            size_t n = fread(cmd, 1, sizeof(cmd) - 1, cf);
            fclose(cf);
            if (n > 0) {
                std::string c(cmd);  // hasta el primer NUL = nombre del proceso
                if (c.rfind("com.rockstargames.gtasa", 0) == 0 && c.size() < 64) g_pkgName = c;
            }
        }
        Log("  paquete del juego: %s", g_pkgName.c_str());
    }
    std::string kFilesDirs[] = {
        "/storage/emulated/0/Android_unprotected/data/" + g_pkgName + "/files",
        "/storage/emulated/0/Android/data/" + g_pkgName + "/files",
        "/sdcard/Android/data/" + g_pkgName + "/files",
        "/storage/self/primary/Android/data/" + g_pkgName + "/files",
    };
    std::string files;
    for (auto d : kFilesDirs) { struct stat st; if (stat(d.c_str(), &st) == 0) { files = d; break; } }
    if (files.empty()) files = kFilesDirs[1];
    g_gameFilesDir = files;
    // v2.3: el .ini va en data/configs (AML: android_unprotected/... o android/.../configs),
    // NO en files/configs - data es la carpeta padre de files
    g_gameDataDir = files;
    if (g_gameDataDir.size() > 6 && g_gameDataDir.compare(g_gameDataDir.size()-6, 6, "/files") == 0)
        g_gameDataDir.resize(g_gameDataDir.size()-6);
    // v2.36: la carpeta VHudRadio ya no se crea; el log va directo en la
    // carpeta del juego (com.rockstargames.gtasa).
    g_logPath = g_gameDataDir + "/VHudRadioLog.txt";  // v2.36: el log vive en la carpeta del juego, no en VHudRadio/
    { FILE* f = fopen(g_logPath.c_str(), "w"); if (f) { fprintf(f, "# VHudRadioLog - VHud Radio Wheel %s\n", VHUDRADIO_VERSION); fclose(f); } }
    g_vhudDir = "/storage/emulated/0/vhud";  // v2.27: solo se LEE como respaldo (iconos y horn_pos.txt); ya no se crea la carpeta
    // v2.36: la carpeta VHudRadio/radio/ ya no se crea (los iconos van
    // incrustados en el .so); las carpetas vhud/ viejas se siguen leyendo
    // como respaldo si existen, pero tampoco se crean.
    LoadCfg();
    LoadHornPos();
    Log("  centro del claxon: (%.3f, %.3f)", g_hornCx, g_hornCy);
    Log("== VHud Radio Wheel %s cargado ==", VHUDRADIO_VERSION);

    void* h = aml->GetLibHandle("libGTASA.so");
    #define SYM(var, name) do { *(void**)&var = (void*)aml->GetSym(h, name); Log("  %s %s", var ? "ok    " : "FALTA " , name); } while (0)
    SYM(FindPlayerPed, "_Z13FindPlayerPedi");
    SYM(FindPlayerVehicle, "_Z17FindPlayerVehicleib");
    SYM(g_retuneAudio, "_ZN12CAudioEngine11RetuneRadioEa");
    SYM(g_retuneTrack, "_ZN20CAERadioTrackManager11RetuneRadioEa");
    SYM(g_getStationAudio, "_ZN12CAudioEngine24GetCurrentRadioStationIDEv");
    SYM(g_getStationTrack, "_ZN20CAERadioTrackManager24GetCurrentRadioStationIDEv");
    SYM(g_startAudio, "_ZN12CAudioEngine10StartRadioEaa");
    SYM(g_startTrack, "_ZN20CAERadioTrackManager10StartRadioEaafh");
    // v1.9.66: funciones RW para la textura del boton nativo (ver arriba).
    SYM(g_rwImageCreate, "_Z13RwImageCreateiii");
    SYM(g_rwImageAlloc, "_Z21RwImageAllocatePixelsP7RwImage");
    SYM(g_rwImageFindFmt, "_Z23RwImageFindRasterFormatP7RwImageiPiS1_S1_S1_");
    SYM(g_rwRasterCreate, "_Z14RwRasterCreateiiii");
    SYM(g_rwRasterSetFromImage, "_Z20RwRasterSetFromImageP8RwRasterP7RwImage");
    SYM(g_rwTextureCreate, "_Z15RwTextureCreateP8RwRaster");
    SYM(g_rwImageDestroy, "_Z14RwImageDestroyP7RwImage");
    SYM(g_rwRasterDestroy, "_Z15RwRasterDestroyP8RwRaster");
    SYM(g_audioEngine, "AudioEngine");
    SYM(g_trackManager, "AERadioTrackManager");
    SYM(g_widgets, "_ZN15CTouchInterface10m_pWidgetsE");
    if (g_widgets) Log("  widgets: m_pWidgets=%p w0=%p w2=%p w7=%p", (void*)g_widgets, g_widgets[0], g_widgets[2], g_widgets[7]);
    else Log("  !! m_pWidgets no resolvio: no se puede leer el boton 7 directo; el claxon sigue por zona tactil/calibrada");
    SYM(g_menuManager, "FrontEndMenuManager");
    if (!g_menuManager) SYM(g_menuManager, "_ZN13CMenuManager18FrontEndMenuManagerE");
    if (g_menuManager) {
        void* sym = g_menuManager;
        g_menuManager = ResolveMenuManager(g_menuManager);
        Log("  menu de pausa: FrontEndMenuManager sim=%p obj=%p m_bMenuActive(0x5C)=%d", sym, g_menuManager, ((const unsigned char*)g_menuManager)[0x5C]);
    }
    else Log("  !! FrontEndMenuManager no resolvio: la UI no se ocultara en el menu de pausa");
    SYM(g_userPause, "_ZN6CTimer11m_UserPauseE");
    SYM(g_codePause, "_ZN6CTimer11m_CodePauseE");
    SYM(g_slowActive, "_ZN6CTimer17bSlowMotionActiveE");
    SYM(g_slowScale, "_ZN6CTimer19ms_fSlowMotionScaleE");
    SYM(g_timeScale, "_ZN6CTimer13ms_fTimeScaleE");
    SYM(g_loadingActive, "_ZN14CLoadingScreen9m_bActiveE");  // v2.48: true mientras carga la partida
    if (!g_slowActive && !g_timeScale) Log("  !! sin escala de tiempo: no habra ralentizacion al abrir la rueda");
    void* dfe = (void*)aml->GetSym(h, "_ZN12CMenuManager12DrawFrontEndEv");
    Log("  %s CMenuManager::DrawFrontEnd", dfe ? "ok" : "FALTA");
    if (dfe) aml->Hook(dfe, (void*)DrawFrontEnd_hook, (void**)&DrawFrontEnd_orig);
    void* drsn = (void*)aml->GetSym(h, "_ZN20CAERadioTrackManager23DisplayRadioStationNameEv");
    Log("  %s CAERadioTrackManager::DisplayRadioStationName", drsn ? "ok" : "FALTA");
    if (drsn) aml->Hook(drsn, (void*)DisplayRadioStationName_hook, (void**)&DisplayRadioStationName_orig);
    // v1.9.73: AdvancedSettings (mueve el texto de emisora arriba al centro):
    // se detecta por SONAME, sin cargarlo (RTLD_NOLOAD solo lo busca).
    void* adv = dlopen("libAdvancedSettings.SA64.so", RTLD_NOLOAD | RTLD_NOW);
    g_advSettings = (adv != nullptr);
    Log("  AdvancedSettings: %s (aviso de emisora %s)", g_advSettings ? "cargado" : "no cargado", g_advSettings ? "arriba al centro" : "abajo a la izquierda (vainilla)");
    #undef SYM
    Log("  globales: AudioEngine=%p AERadioTrackManager=%p", g_audioEngine, g_trackManager);
    UT_InstallHooks();  // v1: engancha StartRadio para el reproductor de User Tracks propio (y DisplayRadioStationName, despues del hook de la 2.59, para que encadenen)
    if (!g_audioEngine || !g_trackManager) Log("  !! falta un objeto global de radio: no se llamara a ninguna funcion de radio");
    if (!g_retuneAudio && !g_retuneTrack && !g_startAudio && !g_startTrack)
        Log("!! ningun Retune/Start resolvio: la rueda se dibujara pero no cambiara la radio. Ver VHudRadioProbe.txt");

    void* touch = (void*)aml->GetSym(h, "_Z14AND_TouchEventiiii");
    Log("  %s AND_TouchEvent", touch ? "ok" : "FALTA");
    if (touch) aml->Hook(touch, (void*)Touch_hook, (void**)&Touch_orig);
    bool got = PatchGot("libGTASA.so", "eglSwapBuffers", (void*)Swap_hook, (void**)&Swap_orig);
    Log("  eglSwapBuffers por GOT: %s", got ? "ok" : "no encontrado");
    if (!got) {
        void* fn = dlsym(RTLD_DEFAULT, "eglSwapBuffers");
        if (fn) { aml->Hook(fn, (void*)Swap_hook, (void**)&Swap_orig); Log("  eglSwapBuffers por hook directo"); }
        else Log("  !! no se pudo enganchar eglSwapBuffers: la rueda no se dibujara");
    }
}
