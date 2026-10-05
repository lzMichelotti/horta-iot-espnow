# protocolo

Código compartilhado entre `no_sensor/` e `coordenador/`: a definição do pacote ESP-NOW (`struct` packed) e as funções de montagem e validação. Especificação em [`docs/protocolo.md`](../../docs/protocolo.md).

C++ puro (sem `Arduino.h`), para compilar tanto no ESP32 quanto no PC, onde rodam os testes unitários (`pio test -e native` dentro de `no_sensor/`).

Uso em cada `platformio.ini` (método recomendado pelo PlatformIO para bibliotecas locais, no lugar do `lib_extra_dirs`, que foi descontinuado):

```ini
lib_deps = protocolo=symlink://../comum/protocolo
```

<https://docs.platformio.org/en/latest/projectconf/sections/env/options/library/lib_extra_dirs.html>
