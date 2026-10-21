# port/: loader nativo de XBE (H6, arranque nativo)

Ejecuta el código x86 de un `default.xbe` dentro de un proceso Win32 de 32 bits, con un
kernel Xbox propio que registra cada llamada. No emula la CPU: el código del juego corre
tal cual. El hito H6 termina cuando el juego llega a la inicialización de D3D (primer
acceso MMIO al NV2A en `0xFD000000`).

Proyecto pedagógico. No incluye nada del juego ni del XDK de Microsoft. Las estructuras y
los prototipos del kernel salen de nxdk (`lib/xboxkrnl/xboxkrnl.h`, CC0) y la tabla de
exports de `src/ngb_tools/data/xboxkrnl_exports.tsv`.

## Compilar y ejecutar

```bat
port\build.cmd
build\port\ngb-loader.exe %USERPROFILE%\ngb-data\game\default.xbe
```

- `build.cmd` llama a `vcvarsall.bat x86` (VS 2022; se puede cambiar con `VCVARSALL`) y
  compila con `/W4 /WX /O2 /Zi`. Deja `ngb-loader.exe`, `ngb-core.dll` y los `.pdb` en
  `build\port\` (ignorado por git).
- El log va por defecto a `%NGB_DATA%\runs\h6-<fecha>.log` (fuera del repo); `--log`
  lo cambia. El disco duro virtual vive en `%NGB_DATA%\runs\hdd\`.
- Opciones: `--max-calls N` (200000), `--timeout S` (60), `--idle S` (10 s sin llamadas al
  kernel), `--lenient` (una llamada sin implementar devuelve `STATUS_SUCCESS`), `--echo`,
  `--mmio-strict` (para en cualquier MMIO o E/S de puertos), `--mmio-gpu-ram` (el NV2A
  también se trata como RAM trazada: sirve para explorar más allá de H6).
- Código de salida: 0 todo terminó, 1 uso/XBE inválido, 2 llamada sin implementar,
  3 excepción (incluido MMIO), 4 `--max-calls`, 5 timeout/inactividad, 6 el juego pidió
  apagar (`HalReturnToFirmware`, `KeBugCheck`).

Regenerar los stubs tras tocar el TSV: `python tools/gen_kernel_stubs.py` (o
`--check` para verificar). Tests: `python -m unittest discover -s tests` con
`PYTHONPATH=src`; `tests/port/test_loader.py` ejecuta el loader compilado sobre XBE
sintéticos y se salta si no existe `build\port\ngb-loader.exe` (o `NGB_LOADER`).

## Archivos

| Archivo | Propósito |
|---|---|
| `build.cmd` | Compilación x86 con MSVC: `ngb-core.dll` y el exe stub. |
| `src/loader_stub.c` | `ngb-loader.exe`: base fija `0x10000` y BSS `.xbox` que reserva la imagen del XBE; carga `ngb-core.dll`. |
| `src/main.c` | `ngb_main`: argumentos, carga, thunk, hilo de arranque y vigilante (timeout, inactividad, fin). |
| `src/xbe.c`, `xbe.h` | Parseo mínimo: cabecera, secciones, TLS y des-XOR (retail/debug) del entry y del thunk. |
| `src/memory.c` | Mapa de la imagen, cabecera PE sombra, arena contigua en `0x80000000`, kernel PE falso, thunk. |
| `src/thread.c` | Hilos Xbox: KPCR/ETHREAD falsos, superposición en el TEB, pila propia con TLS, cadena SEH. |
| `src/crash.c` | Vectored exception handler: informe del crash, MMIO trazado, E/S de puertos. |
| `src/log.c` | Log thread-safe, resumen y parada (`ngb_stop`). |
| `src/kernel/stubs.c` | GENERADO: un stub por export con su convención y número de argumentos. |
| `src/kernel/dispatch.c` | `ngb_kcall`: línea de log por llamada y despacho a la implementación. |
| `src/kernel/data.c` | Las variables exportadas (`KeTickCount`, `XboxKrnlVersion`, `LaunchDataPage`...). |
| `src/kernel/impl_*.c` | Implementaciones: memoria (`mm`), `Rtl`, planificador (`ke`), hilos (`ps`), archivos (`io`), varios (`misc`). |
| `../tools/gen_kernel_stubs.py` | Envoltorio de `python -m ngb_tools.port.kernel_stubs` (genera `stubs.c` desde el TSV). |

Formato del log: `[n] T<hilo> @<dirección de retorno> <ordinal> <nombre>(args) -> ret`.
La línea se escribe al volver de la llamada, así que los detalles que añade la
implementación (`    · ...`) quedan justo encima. Al final van el motivo de la parada,
el estado de cada hilo Xbox (EIP, pila, llamada en curso) y las llamadas por función.

## Decisiones

**Memoria de la imagen.** El XBE no es reubicable y vive en `0x10000`, una zona que Windows
llena enseguida (heaps, pilas, NLS). Igual que el `cxbxr-ldr.exe` de Cxbx-Reloaded, el exe
se enlaza en `0x10000` con `/FIXED` y una BSS enorme: el cargador de Windows reserva
`[0x10000, 0x01010000)` al crear el proceso. El exe no tiene CRT; todo el loader está en
`ngb-core.dll`, que vive en otra dirección. `ngb_main` pisa el código del stub (no vuelve
nunca) y copia cabeceras y secciones (todas, también las no PRELOAD; `XeLoadSection`
solo cuenta referencias). La imagen queda RWX y el proceso sin DEP, como en Xbox.

**Cabecera PE sombra.** Tras pisar la cabecera PE del exe con la del XBE, `CreateThread`
falla (`ERROR_BAD_EXE_FORMAT`) y los hilos nuevos mueren en ntdll: Windows relee la
cabecera desde la base de la imagen (cambiar `PEB.ImageBaseAddress` no basta). Se copian
las NT headers a `0x01000000` (cola de la BSS), se escribe `MZ` en `0x10000` y en `0x1003C`
(`e_lfanew`) el offset a la copia. Los dos campos caen en el magic y en la firma RSA del
XBE, que el código del juego no lee en ejecución (solo el kernel al cargar).

**FS/KPCR (atajo de H6).** En WOW64 no hay LDT (`NtSetLdtEntries` no existe en x64), así
que `fs:` sigue apuntando al TEB de Windows. El código Xbox solo usa unos pocos campos y se
superponen en el TEB de cada hilo Xbox: `fs:[0x1C]` SelfPcr (EnvironmentPointer),
`fs:[0x20]` Prcb (ClientId.UniqueProcess), `fs:[0x24]` Irql (UniqueThread := `tid << 8`:
byte bajo 0 = PASSIVE_LEVEL y único por hilo para los CRITICAL_SECTION) y `fs:[0x28]`
CurrentThread (ActiveRpcHandle). `fs:[0]`, `fs:[4]` y `fs:[8]` (ExceptionList, StackBase,
StackLimit) significan lo mismo en ambos y se usan tal cual. Consecuencia:
`GetCurrentThreadId`/`GetCurrentProcessId` devuelven basura en hilos Xbox; el loader no
los usa. Alternativa futura: parchear los accesos `fs:` como hace Cxbx-Reloaded.

**TLS.** El arranque de XAPI pone `_tls_index` negativo y accede con
`[fs:[4] + _tls_index*4]`: el bloque TLS vive justo debajo de StackBase y
`KTHREAD.TlsData` (offset 0x28) apunta a él. Cada hilo Xbox corre en una pila propia
(VirtualAlloc, mínimo 1 MB) con el bloque en la cima; el TEB se apunta a ella y la cadena
SEH se reinicia con una copia del registro final de ntdll dentro de la pila nueva, para que
la validación de SEH/SEHOP la acepte.

**Hilos y planificador.** Un hilo Xbox es un hilo Win32 (`PsCreateSystemThreadEx` llama a
la SystemRoutine de XAPI igual que el kernel real). Proceso con afinidad a un núcleo. Subir
a DISPATCH_LEVEL toma un lock global recursivo. Los timers y DPC los ejecuta un hilo Xbox
interno. Las interrupciones se aceptan pero nunca se disparan.

**Kernel.** Cada slot del thunk (`0x80000000|ordinal`) se reescribe con el stub (función)
o con la dirección de la variable (data). Los stubs se generan del TSV con la convención
real (stdcall/fastcall/cdecl) y `arg_bytes/4` argumentos, devuelven EDX:EAX y llaman a
`ngb_kcall`. Una llamada a un export sin implementación detiene la ejecución (salvo con `--lenient`). `XAPI` lee la cabecera PE de
`xboxkrnl.exe` en `0x80010000` (busca su sección INIT): hay una cabecera PE falsa sin INIT.

**Archivos.** `\Device\CdRom0` (D:) es la carpeta del XBE y es de solo lectura: nunca se
escribe ahí. `\Device\Harddisk0\PartitionN\...` va a `%NGB_DATA%\runs\hdd\partitionN\`; el
volumen en crudo (`...\PartitionN` sin barra final) es `partitionN.raw`. El tamaño de
volumen imita FATX (clúster de 16 KB): con el de NTFS, XAPI pide formatear y reinicia.

**MMIO.** El primer acceso al NV2A detiene la ejecución (fin de H6). El resto de periféricos
(USB, APU, AC97, NIC, flash) se traza con RAM a cero: cada acceso se registra (los 400
primeros), se abre la página, se ejecuta la instrucción en paso a paso y se vuelve a
cerrar. `IN`/`OUT`, `CLI`/`STI` y `WBINVD` se registran y se saltan.

## Deuda conocida

- 105 de las 128 funciones que importa `default.xbe` tienen implementación (118 exports
  en total). Faltan 23: la pila de E/S por IRP (`IoBuildSynchronousFsdRequest`,
  `IoInvalidDeviceRequest`, `IoStartNextPacket`, `IoStartPacket`,
  `IoSynchronousDeviceIoControlRequest`, `IoSynchronousFsdRequest`, `IofCallDriver`,
  `IofCompleteRequest`, `IoMarkIrpMustComplete`), `NtDeleteFile`, `NtQueryDirectoryFile`,
  `ObReferenceObjectByName` y la criptografía `Xc*` (SHA-1, RC4, HMAC, RSA, DES). Además,
  `ObReferenceObjectByHandle` solo resuelve hilos y renombrar archivos no está hecho.
- E/S síncrona: las rutinas APC de `NtReadFile` no se llaman.
- Interrupciones (USB, vblank del NV2A) no existen; los timers tienen ~50 ms de granularidad.
- Las tablas de secciones críticas y objetos crecen sin liberar; las pilas Xbox no se liberan.
- Atajos frágiles documentados arriba: superposición en el TEB y cabecera PE sombra.
