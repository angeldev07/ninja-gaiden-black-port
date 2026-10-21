"""Errores tipados de `ngb_tools.xbe`.

Jerarquía:

    XbeError                    base de todo el paquete
    ├── XbeFormatError          el archivo no es un XBE válido o es incoherente
    │   ├── TruncatedDataError  una estructura se sale del archivo
    │   ├── BadMagicError       no empieza por 'XBEH'
    │   ├── UnmappedAddressError  una VA no apunta a datos del archivo
    │   ├── ImageLayoutError    estructura fuera de su región, desborde de 32 bits o cota
    │   └── ThunkTableError     tabla de imports del kernel sin terminador
    └── KernelExportsError      la tabla TSV de exports está mal formada

La CLI traduce `XbeFormatError` a fallo de validación (código 1) y
`KernelExportsError` a error de uso (código 2): el XBE no tiene la culpa.
"""

from __future__ import annotations


class XbeError(Exception):
    """Base de los errores del inspector de XBE."""


class XbeFormatError(XbeError):
    """El archivo no es un XBE válido, está truncado o es incoherente."""


class TruncatedDataError(XbeFormatError):
    """Lectura de `size` bytes en `offset` fuera de los límites del archivo."""

    def __init__(self, offset: int, size: int) -> None:
        super().__init__(f"lectura de {size} bytes fuera del archivo en offset 0x{offset:X}")
        self.offset = offset
        self.size = size


class BadMagicError(XbeFormatError):
    """La firma de los 4 primeros bytes no es 'XBEH'."""


class UnmappedAddressError(XbeFormatError):
    """Una dirección virtual (VA) no está respaldada por bytes del archivo.

    Pasa con punteros a BSS (memoria que el cargador rellena con ceros) o fuera
    de la imagen.
    """

    def __init__(self, va: int, what: str) -> None:
        super().__init__(f"{what}: VA 0x{va:08X} no está respaldada por el archivo")
        self.va = va
        self.what = what


class ImageLayoutError(XbeFormatError):
    """El layout declarado es imposible: una estructura fuera de su región, un rango que
    desborda los 32 bits o se sale de la imagen, o un contador por encima de su cota.

    Comprobarlo antes de recorrer nada es lo que impide que un XBE hostil cuelgue el
    análisis con un `section_count` de miles de millones.
    """


class ThunkTableError(XbeFormatError):
    """La tabla de thunks del kernel no termina en 0 dentro de la cota."""


class KernelExportsError(XbeError):
    """Una línea de la tabla de exports de xboxkrnl no se puede interpretar."""
