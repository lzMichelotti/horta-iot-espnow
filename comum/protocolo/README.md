# protocolo

Código compartilhado entre `no_sensor/` e `coordenador/`: a definição do pacote ESP-NOW (`struct` packed) num único header, usado pelos dois firmwares. O conteúdo será criado na etapa de ESP-NOW, junto com `docs/protocolo.md`.

Uso em cada `platformio.ini` (método recomendado pelo PlatformIO para bibliotecas locais, no lugar do `lib_extra_dirs`, que foi descontinuado):

```ini
lib_deps = protocolo=symlink://../comum/protocolo
```

<https://docs.platformio.org/en/latest/projectconf/sections/env/options/library/lib_extra_dirs.html>
