---
tags: [ngb, reverse-engineering, runtime, h6]
updated: 2026-10-21
status: current
---

# Arranque nativo de default.xbe (H6): qué hace el juego al arrancar

`ngb-loader.exe` ejecuta el código real de `default.xbe` en un proceso Win32 de 32 bits, con un kernel HLE propio
([[adr-05-runtime-nativo]]). Esta página recoge lo que **el propio juego hace**, visto en el log del loader.
Fuentes:
- `NGB_DATA\runs\h6-20261021-000838.log` (471 líneas), del analista;
- una ejecución independiente del líder el 2026-10-21, que termina en el mismo punto.

Todo es `VERIFIED` por el log, salvo lo que se indique.

## Secuencia de arranque

1. **Hilo principal → hilo de juego.** El entry `0x0032DA55` crea un hilo de sistema con
   `PsCreateSystemThreadEx`, con la rutina en `0x0032D9E1`, y cierra su handle. Todo lo demás corre en ese hilo
   (T2).
2. **Init de XAPI**, la librería de alto nivel del XDK:
   - secciones críticas;
   - notificación de apagado;
   - DPC y timer;
   - lectura de los ajustes de la EEPROM (`ExQueryNonVolatileSetting` ×8);
   - reserva de memoria (`NtAllocateVirtualMemory`).
3. **Montaje de unidades:**
   - `\??\D:` → `\Device\CdRom0`, el DVD del juego;
   - `T:` → la carpeta TDATA del título `5443000d`;
   - U:, Z: y las particiones del disco duro.
4. **Comprobación de versiones:** XAPI compara las librerías enlazadas (`RtlEqualString` ×45).
5. **Metadatos de guardado:** escribe `TitleMeta.xbx`, `TitleImage.xbx` y `SaveImage.xbx` en U: (`NtWriteFile` ×33).
6. **Caché en el disco duro:** lee la cache DB en crudo (partition0, sector 4) y **formatea la partición caché Z:**
   (Partition5) con IOCTL de geometría y partición. Encaja con las rutas `z:\%s.afs` de [[xbe-default]]: el juego
   copia los AFS a la caché del disco duro.
7. **XInitDevices:** el driver USB OHCI, para los mandos.
8. **Memoria contigua para la GPU:** `MmAllocateContiguousMemory` (0x2472000 + 0x200000 + …).
9. **Init de Direct3D 8**, en la variante LTCG.

**Fin del arranque nativo:** en `EIP 0x003D171B`, código del XBE, el juego ejecuta `mov eax,[0xFD001804]` /
`or eax,4` / `mov [...],eax`, que lee y escribe **NV_PBUS** del NV2A. Es la primera vez que toca la GPU. Ahí
termina H6.

## Llamadas más frecuentes (log del analista, 285 llamadas, 39 funciones distintas)

`RtlInitAnsiString` 57, `RtlEqualString` 45, `NtWriteFile` 33, `RtlEnter/LeaveCriticalSection` 18 cada una,
`RtlInitializeCriticalSection` 15, `NtClose` 13, `NtCreateFile` 8, `ExQueryNonVolatileSetting` 8,
`KeRaiseIrqlToDpcLevel`/`KfLowerIrql` 7 cada una.

En una segunda ejecución, con el disco duro virtual ya formateado, se llaman menos funciones distintas (35), porque
XAPI ya no formatea la caché.

## Más allá de la primera MMIO (exploración con `--mmio-gpu-ram`, `INFERRED`)

Si los registros de la GPU se tratan como RAM, el juego avanza:
- reclama memoria de instancia de la GPU (`MmClaimGpuInstanceMemory`);
- toca PCI y ejecuta WBINVD;
- se queda **sondeando en `0x3CD560`**: escribe el bit 16 en `0xFD100410` (PFB, vaciado del write-combine) y espera
  a que baje.

Con RAM ese bit nunca baja. Es la primera prueba de que hace falta modelar el NV2A, o saltárselo con HLE de D3D8:
esa es la decisión de H7.

## Estado del kernel HLE

- Implementadas **105 de las 128 funciones** que importa `default.xbe`, más las 17 variables de datos (con valores
  aproximados; las claves van a cero).
- Faltan 23:
  - la pila de E/S con IRP (`Io*Packet`, `IofCallDriver`, `IofCompleteRequest`, `IoBuildSynchronousFsdRequest`…);
  - `NtDeleteFile`, `NtQueryDirectoryFile` y `ObReferenceObjectByName`;
  - las funciones `Xc*` de criptografía.
- En total hay 118 exports implementados de los 371 de la tabla.

## Nueva partida: el capítulo 1 se quedaba en negro (`VERIFIED`)

- **Síntoma:** tras la tarjeta "Chapter 1 – Proceed" (botón X), la pantalla se quedaba en negro con el juego
  dibujando a 60 fps.
- **Diagnóstico:**
  - con RenderDoc, la escena del nivel se dibujaba entera, con Ryu y la barra de vida, y el último draw del frame
    era un quad negro opaco a pantalla completa: el fundido de entrada, con el color de vértice `(0,0,0,1)`;
  - las 587 primitivas eran idénticas en cada frame: la lógica del juego no avanzaba;
  - con `--log-cap 20000`, el hilo principal repetía sin fin `DeleteFile("z:\rtm_yasik.afs")`, el borrado de un
    AFS de la caché del disco duro (Z:, partición 5) para hacer sitio al del nivel nuevo.
- **Causa:** `NtSetInformationFile` rechazaba la clase 13 (`FileDispositionInformation`), con la que XAPI
  borra. Estaba en la lista negra para que el juego no pudiera borrar archivos del PC. `DeleteFile` fallaba y el
  juego lo reintentaba en cada frame.
- **Arreglo:** la clase 13 se acepta solo si el handle está dentro del disco duro virtual (`NGB_DATA\runs\hdd`),
  según la ruta final que da Windows (`GetFinalPathNameByHandle`, que resuelve enlaces y `..`). `NtOpenFile` pide
  `DELETE` al host cuando el juego lo pide fuera de D:.
- **Resultado:** el capítulo 1 empieza: cinemática con motor y gameplay en el desfiladero.
- **Herramienta:** el guion de nueva partida (lecturas del mando, sin partida guardada):
  `t=205p:START@12p,t=247p:START@12p,t=314p:START@12p,t=343p:START@12p,t=377p:START@12p,t=521p:A@12p,`
  `t=684p:START@12p,t=718p:START@12p,t=1114p:START@12p,t=7500p:X@12p`. El gameplay empieza hacia el frame 9000.

