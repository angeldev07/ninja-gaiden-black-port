/*
 * strings.h: every user-visible string of the launcher, English first, Spanish second.
 * To add a language, add a column here and a button in the sidebar. Format strings use
 * the same placeholders in both columns.
 */
#pragma once

#define STRING_TABLE(X)                                                                          \
    /* window and sidebar */                                                                     \
    X(WIN_TITLE, L"NINJA GAIDEN BLACK  ·  Native PC port", L"NINJA GAIDEN BLACK  ·  Port nativo a PC") \
    X(SIDE_TAG, L"native PC port  ·  v%ls", L"port nativo a PC  ·  v%ls")                         \
    X(SIDE_SECTIONS, L"SECTIONS", L"SECCIONES")                                                  \
    X(SIDE_LANGUAGE, L"LANGUAGE", L"IDIOMA")                                                     \
    X(LANG_EN_NAME, L"English", L"English")                                                      \
    X(LANG_ES_NAME, L"Español", L"Español")                                                      \
    X(LEGAL, L"Unofficial fan port. Requires your own copy of the game. Not affiliated with Koei Tecmo or Microsoft.", \
      L"Port no oficial de aficionados. Requiere tu propia copia del juego. Sin relación con Koei Tecmo ni Microsoft.") \
    X(NAV_PLAY, L"Play", L"Jugar")                                                               \
    X(NAV_GRAPHICS, L"Graphics", L"Gráficos")                                                    \
    X(NAV_AUDIO, L"Audio", L"Audio")                                                             \
    X(NAV_CONTROLS, L"Controls", L"Controles")                                                   \
    X(NAV_ADVANCED, L"Advanced", L"Avanzado")                                                    \
    X(NAV_LOG, L"Log", L"Registro")                                                              \
    /* footer */                                                                                 \
    X(BTN_PLAY, L"PLAY", L"JUGAR")                                                               \
    X(BTN_STOP, L"Stop", L"Detener")                                                             \
    X(BTN_IMPORT_PLATE, L"IMPORT GAME", L"IMPORTAR")                                             \
    X(BTN_IMPORTING_PLATE, L"IMPORTING…", L"IMPORTANDO…")                                        \
    X(ST_RUNNING, L"Running  ·  PID %lu  ·  %ls", L"En ejecución  ·  PID %lu  ·  %ls")           \
    X(ST_NO_LOADER, L"ngb-loader.exe was not found next to the launcher", L"No se encuentra ngb-loader.exe junto al launcher") \
    X(ST_NO_GAME, L"No game imported yet: use Import", L"Aún no hay juego: usa Importar")        \
    X(ST_READY, L"Ready to play", L"Listo para jugar")                                           \
    X(ST_IMPORTING, L"Importing your game…  %d%%", L"Importando tu juego…  %d%%")                \
    X(SUM_AUTO, L"Auto renderer", L"Render automático")                                          \
    X(SUM_GL, L"OpenGL 4.x", L"OpenGL 4.x")                                                      \
    X(SUM_SOFT, L"Software", L"Software")                                                        \
    X(SUM_FPS60, L"60 fps", L"60 fps")                                                           \
    X(SUM_NOFPS, L"Unlimited fps", L"Sin límite de fps")                                         \
    X(SUM_MUTED, L"Muted", L"Silenciado")                                                        \
    X(SUM_SOUND, L"Sound on", L"Con sonido")                                                     \
    /* play page */                                                                              \
    X(PLAY_TAGLINE, L"NATIVE PC PORT", L"PORT NATIVO A PC")                                      \
    X(CARD_GAME, L"GAME", L"JUEGO")                                                              \
    X(CARD_LAST, L"LAST SESSION", L"ÚLTIMA SESIÓN")                                              \
    X(GAME_VERIFIED, L"Verified: this is Ninja Gaiden Black", L"Verificado: es Ninja Gaiden Black") \
    X(GAME_UNKNOWN, L"Not recognised as Ninja Gaiden Black (%ls)", L"No se reconoce como Ninja Gaiden Black (%ls)") \
    X(GAME_MISSING, L"The game file is missing", L"Falta el archivo del juego")                  \
    X(GAME_BAD_XBE, L"default.xbe is not a valid Xbox executable", L"default.xbe no es un ejecutable de Xbox válido") \
    X(GAME_SIZE, L"default.xbe  ·  %.2f MB", L"default.xbe  ·  %.2f MB")                          \
    X(BTN_IMPORT_ISO, L"Import disc image…", L"Importar imagen…")                       \
    X(BTN_USE_FOLDER, L"Use folder…", L"Usar carpeta…")                       \
    X(CTA_TITLE, L"IMPORT YOUR GAME", L"IMPORTA TU JUEGO")                                       \
    X(CTA_BODY, L"Ninja Gaiden Black for Xbox is not included. Import your own copy: a disc image (.iso) or a folder with the extracted files (it must contain default.xbe).", \
      L"Ninja Gaiden Black de Xbox no se incluye. Importa tu propia copia: una imagen de disco (.iso) o una carpeta con los archivos extraídos (debe contener default.xbe).") \
    X(CTA_BTN_ISO, L"DISC IMAGE (.ISO)", L"IMAGEN DE DISCO (.ISO)")                              \
    X(CTA_BTN_FOLDER, L"EXTRACTED FOLDER", L"CARPETA EXTRAÍDA")                                  \
    X(CTA_STEP1, L"Pick your disc image or folder", L"Elige tu imagen de disco o carpeta")       \
    X(CTA_STEP2, L"Wait while the files are extracted", L"Espera a que se extraiga el juego") \
    X(CTA_STEP3, L"Press PLAY", L"Pulsa JUGAR")                                                  \
    X(CTA_SAVE_IN, L"FILES WILL BE SAVED IN", L"LOS ARCHIVOS SE GUARDARÁN EN")                   \
    X(CTA_DROP, L"Tip: you can also drag an .iso file or a folder onto this window.", L"Consejo: también puedes arrastrar un .iso o una carpeta sobre esta ventana.") \
    X(DATA_FOLDER, L"DATA FOLDER", L"CARPETA DE DATOS")                                          \
    X(BTN_CHANGE, L"Change…", L"Cambiar…")                                                       \
    X(BTN_OPEN, L"Open", L"Abrir")                                                               \
    X(LS_NONE, L"No sessions yet. Press PLAY to start.", L"Todavía no hay sesiones. Pulsa JUGAR para empezar.") \
    X(LS_START, L"Started", L"Inicio")                                                           \
    X(LS_DURATION, L"Duration", L"Duración")                                                     \
    X(LS_RESULT, L"Result", L"Resultado")                                                        \
    X(LS_LOG, L"Log", L"Registro")                                                               \
    X(BTN_VIEW_LOG, L"View log", L"Ver el registro")                                             \
    /* import progress */                                                                        \
    X(IMP_TITLE, L"IMPORTING YOUR GAME", L"IMPORTANDO TU JUEGO")                                 \
    X(IMP_SCAN, L"Reading the disc image…", L"Leyendo la imagen de disco…")                      \
    X(IMP_EXTRACT, L"Extracting  %ls", L"Extrayendo  %ls")                                       \
    X(IMP_FINISH, L"Finishing…", L"Terminando…")                                                 \
    X(IMP_CANCELLING, L"Cancelling…", L"Cancelando…")                                            \
    X(IMP_AMOUNT, L"%d%%   ·   %.2f of %.2f GB", L"%d%%   ·   %.2f de %.2f GB")                  \
    X(IMP_SPEED, L"%.0f MB/s  ·  about %ld min left", L"%.0f MB/s  ·  faltan unos %ld min")      \
    X(IMP_SPEED_SEC, L"%.0f MB/s  ·  less than a minute left", L"%.0f MB/s  ·  falta menos de un minuto") \
    X(IMP_HINT, L"You can keep using the launcher while this runs.", L"Puedes seguir usando el launcher mientras tanto.") \
    X(BTN_CANCEL, L"Cancel", L"Cancelar")                                                        \
    X(NOTE_IMPORTED, L"Game imported and verified.", L"Juego importado y verificado.")           \
    X(NOTE_FOLDER_OK, L"Game folder selected and verified.", L"Carpeta del juego elegida y verificada.") \
    X(ERR_OPEN, L"That file could not be opened.", L"No se pudo abrir ese archivo.")             \
    X(ERR_NOT_XISO, L"That is not an Xbox disc image (Redump or XISO .iso).", L"Eso no es una imagen de disco de Xbox (.iso Redump o XISO).") \
    X(ERR_NO_XBE, L"No default.xbe found in the image: it does not look like a game disc.", L"No hay default.xbe en la imagen: no parece un disco de juego.") \
    X(ERR_NOT_NGB, L"This is \"%ls\", not Ninja Gaiden Black. Please pick your Ninja Gaiden Black disc or folder.", L"Esto es \"%ls\", no Ninja Gaiden Black. Elige tu disco o carpeta de Ninja Gaiden Black.") \
    X(ERR_READ, L"The image could not be read; it may be damaged or incomplete.", L"No se pudo leer la imagen; puede estar dañada o incompleta.") \
    X(ERR_CORRUPT, L"The disc image looks damaged (inconsistent file tables).", L"La imagen de disco parece dañada (tablas de archivos incoherentes).") \
    X(ERR_WRITE, L"Could not write to the data folder. Check permissions or pick another folder.", L"No se pudo escribir en la carpeta de datos. Revisa los permisos o elige otra carpeta.") \
    X(ERR_NOSPACE, L"Not enough free disk space in the data folder (about %.1f GB needed).", L"No hay espacio libre suficiente en la carpeta de datos (hacen falta unos %.1f GB).") \
    X(ERR_CANCELLED, L"Import cancelled.", L"Importación cancelada.")                            \
    X(ERR_XBE_BAD, L"default.xbe is not a valid Xbox executable.", L"default.xbe no es un ejecutable de Xbox válido.") \
    X(ERR_FOLDER_NO_XBE, L"That folder does not contain default.xbe.", L"Esa carpeta no contiene default.xbe.") \
    X(ERR_INTERNAL, L"Something unexpected went wrong.", L"Algo inesperado ha fallado.")         \
    X(DLG_PICK_ISO, L"Select your Ninja Gaiden Black disc image", L"Elige la imagen de disco de Ninja Gaiden Black") \
    X(DLG_FILTER_ISO, L"Xbox disc images (*.iso;*.xiso)\0*.iso;*.xiso\0All files\0*.*\0", L"Imágenes de disco de Xbox (*.iso;*.xiso)\0*.iso;*.xiso\0Todos los archivos\0*.*\0") \
    X(DLG_PICK_FOLDER, L"Select the folder with the extracted game (it contains default.xbe)", L"Elige la carpeta con el juego extraído (contiene default.xbe)") \
    X(DLG_PICK_DATA, L"Select the data folder", L"Elige la carpeta de datos")                    \
    X(DLG_REPLACE, L"A game is already imported in this data folder. Replace it with the new import?", L"Ya hay un juego importado en esta carpeta de datos. ¿Reemplazarlo con la nueva importación?") \
    X(DLG_CLOSE, L"A game import is still running. Cancel it and quit?", L"Aún hay una importación en curso. ¿Cancelarla y salir?") \
    X(DLG_LAUNCH_FAIL, L"Could not start ngb-loader.exe.", L"No se pudo iniciar ngb-loader.exe.") \
    X(DLG_TITLE, L"Ninja Gaiden Black", L"Ninja Gaiden Black")                                   \
    /* session results */                                                                        \
    X(EXIT_STOPPED, L"Stopped from the launcher", L"Detenido desde el launcher")                 \
    X(EXIT_0, L"Normal exit", L"Salida normal")                                                  \
    X(EXIT_1, L"Invalid arguments or XBE (code 1)", L"Argumentos o XBE inválidos (código 1)")    \
    X(EXIT_2, L"Unimplemented kernel call (code 2)", L"Llamada del kernel sin implementar (código 2)") \
    X(EXIT_3, L"The game closed on an exception (code 3)", L"El juego se cerró por una excepción (código 3)") \
    X(EXIT_4, L"Call limit reached (code 4)", L"Límite de llamadas alcanzado (código 4)")        \
    X(EXIT_5, L"Time or idle limit reached (code 5)", L"Tiempo o inactividad agotados (código 5)") \
    X(EXIT_6, L"The game asked to shut down (code 6)", L"El juego pidió apagar (código 6)")      \
    X(EXIT_OTHER, L"Exit code %d", L"Código de salida %d")                                       \
    /* graphics */                                                                               \
    X(GFX_SUB, L"How the game is drawn and at what pace.", L"Cómo se dibuja el juego y a qué ritmo.") \
    X(GFX_SECTION, L"Window and renderer", L"Ventana y render")                                  \
    X(GFX_RENDERER, L"Renderer", L"Renderizador")                                                \
    X(GFX_RENDERER_DESC, L"Automatic uses OpenGL 4.x when your GPU offers it. The software rasteriser is slow and serves as a reference.", \
      L"Automático usa OpenGL 4.x si tu GPU lo ofrece. El rasterizador por software es lento y sirve de referencia.") \
    X(GFX_R_AUTO, L"Automatic", L"Automático")                                                   \
    X(GFX_R_GL, L"OpenGL 4.x", L"OpenGL 4.x")                                                    \
    X(GFX_R_SOFT, L"Software (rasteriser)", L"Software (rasterizador)")                          \
    X(GFX_FPS, L"Limit to 60 fps", L"Limitar a 60 fps")                                          \
    X(GFX_FPS_DESC, L"Each frame waits for the next 1/60 s, like on the Xbox. Turn it off to see the most your PC can do.", \
      L"Cada frame espera al siguiente 1/60 s, como en la Xbox. Desactívalo para ver el máximo que da tu equipo.") \
    X(GFX_NOTE, L"The game window opens centred and can be resized: the picture keeps its 4:3 shape with black bars. F12 saves a screenshot to runs\\shots.", \
      L"La ventana del juego se abre centrada y se puede redimensionar: la imagen mantiene el aspecto 4:3 con bandas negras. F12 guarda una captura en runs\\shots.") \
    /* audio */                                                                                  \
    X(AUD_SUB, L"Sound output of the game.", L"Salida de sonido del juego.")                     \
    X(AUD_SECTION, L"Sound", L"Sonido")                                                          \
    X(AUD_MUTE, L"Mute", L"Silenciar")                                                           \
    X(AUD_MUTE_DESC, L"The emulated APU keeps running; only the speaker output is switched off.", L"La APU emulada sigue avanzando; solo se apaga la salida al altavoz.") \
    X(AUD_WAV, L"Save the audio to a WAV file", L"Guardar el audio en un WAV")                   \
    X(AUD_WAV_DESC, L"Writes what you hear to <log>-audio.wav, next to each session's log.", L"Escribe lo que suena en <registro>-audio.wav, junto al log de cada sesión.") \
    /* controls */                                                                               \
    X(CTL_SUB, L"Keyboard and gamepad mapping as defined by the port (read only).", L"El mapeo de teclado y mando, tal como lo define el port (solo lectura).") \
    X(CTL_KEYBOARD, L"KEYBOARD", L"TECLADO")                                                     \
    X(CTL_PAD, L"PC GAMEPAD (XINPUT)", L"MANDO DE PC (XINPUT)")                                  \
    X(CTL_START_A, L"Start and A", L"Start y A")                                                 \
    X(CTL_BACK_B, L"Back and B", L"Back y B")                                                    \
    X(CTL_DPAD, L"D-pad", L"Cruceta")                                                            \
    X(CTL_LSTICK, L"Left stick", L"Stick izquierdo")                                             \
    X(CTL_RSTICK, L"Right stick", L"Stick derecho")                                              \
    X(CTL_WHITE, L"White", L"Blanco (White)")                                                    \
    X(CTL_BLACK, L"Black", L"Negro (Black)")                                                     \
    X(CTL_LTRIG, L"Left trigger", L"Gatillo izquierdo")                                          \
    X(CTL_RTRIG, L"Right trigger", L"Gatillo derecho")                                           \
    X(CTL_TRIGS, L"Left and right triggers", L"Gatillos izquierdo y derecho")                    \
    X(CTL_CLICK, L"Stick buttons", L"Pulsar los sticks")                                         \
    X(CTL_SHOT, L"Screenshot", L"Captura de pantalla")                                           \
    X(CTL_RDOC, L"RenderDoc capture", L"Captura RenderDoc")                                      \
    X(CTL_KEY_LSTICK, L"L stick", L"Stick izq.")                                                 \
    X(CTL_KEY_RSTICK, L"R stick", L"Stick der.")                                                 \
    X(CTL_BOTH, L"Gamepad and keyboard add up: you can play with both at once.", L"El mando de PC y el teclado se suman: se puede jugar con los dos a la vez.") \
    /* advanced */                                                                               \
    X(ADV_SUB, L"Debugging and logging. Usually best left as it is.", L"Depuración y registro. Lo normal es dejarlo como está.") \
    X(ADV_DEBUG, L"Debugging", L"Depuración")                                                    \
    X(ADV_RDOC, L"RenderDoc capture (F11)", L"Captura con RenderDoc (F11)")                      \
    X(ADV_RDOC_DESC, L"Loads RenderDoc at start and F11 captures the next frame. Needs RenderDoc installed and the OpenGL renderer.", \
      L"Carga RenderDoc al arrancar y F11 captura el frame siguiente. Necesita RenderDoc instalado y el render OpenGL.") \
    X(ADV_LENIENT, L"Tolerate unimplemented calls", L"Tolerar llamadas sin implementar")         \
    X(ADV_LENIENT_DESC, L"An unimplemented kernel call returns STATUS_SUCCESS and the game goes on (it can hide failures).", \
      L"Una llamada del kernel sin implementar devuelve STATUS_SUCCESS y el juego sigue (puede ocultar fallos).") \
    X(ADV_LOGGING, L"Logging", L"Registro")                                                      \
    X(ADV_LOGCAP, L"Log lines per kernel function", L"Líneas de log por función del kernel")     \
    X(ADV_LOGCAP_DESC, L"Past that limit only the calls are counted (--log-cap). Fewer lines, shorter logs.", \
      L"Pasado ese límite solo se cuentan las llamadas (--log-cap). Menos líneas, logs más cortos.") \
    X(ADV_LOGCAP_DEF, L"2000 (default)", L"2000 (por defecto)")                                  \
    X(ADV_CMDLINE, L"PLAY COMMAND LINE", L"LÍNEA DE COMANDOS DE JUGAR")                          \
    /* log */                                                                                    \
    X(LOG_SUB, L"The tail of the latest ngb-loader log (runs folder).", L"La cola del último registro de ngb-loader (carpeta runs).") \
    X(LOG_OPEN, L"Open folder", L"Abrir carpeta")                                                \
    X(LOG_EMPTY, L"No logs yet. Each session writes an h6-<date>.log in the runs folder.", L"Todavía no hay registros. Cada sesión escribe un h6-<fecha>.log en la carpeta runs.") \
    X(LOG_UPDATED, L"%ls  ·  updated %ls", L"%ls  ·  actualizado %ls")

enum {
#define X(id, en, es) S_##id,
    STRING_TABLE(X)
#undef X
    S_COUNT
};
