# Hydro Thunder Hurricane — Port para PC Windows y Linux | HydroThunderPORT

[English](README.md) · [Português (Brasil)](README.pt-BR.md) · **Español**

[Descargar](https://github.com/monscriser/HydroThunderPORT/releases) · [Sitio del proyecto](https://monscriser.github.io/HydroThunderPORT/) · [Incidencias](https://github.com/monscriser/HydroThunderPORT/issues)

## Estado del proyecto — FINAL BUILD y el futuro de HydroThunderPORT

> [!IMPORTANT]
> La **FINAL BUILD** es la última versión de la generación 1.x, basada en la arquitectura actual de recompilación estática y compatibilidad gráfica con Xbox 360. Sigue siendo la versión estable recomendada para Windows y Linux. **Este es el cierre del ciclo técnico actual, no el final de HydroThunderPORT.**

La próxima generación, **HydroThunderPORT 2.0**, está prevista con un nuevo renderizador Vulkan nativo. A diferencia del backend Vulkan actual, la arquitectura propuesta traducirá las operaciones gráficas de alto nivel del juego directamente a Vulkan siempre que sea viable, reduciendo la dependencia de la ruta de compatibilidad heredada de la GPU Xenos. La recompilación estática de la CPU seguirá formando parte del enfoque.

El objetivo es mejorar la escalabilidad gráfica, la eficiencia y el rendimiento, conservando los efectos visuales, la física de las embarcaciones y la jugabilidad original. Son objetivos de desarrollo, no funciones ni resultados disponibles actualmente.

El trabajo en la 2.0 está previsto después de completar y validar el ciclo actual de desarrollo de [UFC3MONS](https://github.com/monscriser/UFC3MONS). **No hay fecha ni estimación de lanzamiento.** No se prevén más versiones rutinarias de la línea 1.x. La FINAL BUILD seguirá disponible como referencia estable durante el desarrollo y las pruebas de la nueva arquitectura.

Gracias a todos los que han jugado, realizado pruebas, comunicado problemas y apoyado el proyecto. Sus aportaciones han ayudado a hacer posible esta versión.

## Acerca del port

HydroThunderPORT es un port experimental y no oficial de Hydro Thunder Hurricane para Windows y Linux. Utiliza ReXGlue para la recompilación estática y un backend Vulkan con componentes de compatibilidad Xbox 360 basados en el trabajo de Xenia. Parte de [htrecomp](https://github.com/visibou/htrecomp) 1.0 de visibou, incluidas las bases de la aplicación, el launcher/overlay y la recompilación.

**No se incluyen los archivos originales del juego, DLC ni Title Updates.** Utiliza tus propias copias obtenidas legalmente.

| Función | FINAL BUILD actual |
| --- | --- |
| Windows | Ejecutable nativo y launcher de configuración |
| Linux | AppImage con launcher |
| Gráficos | Vulkan (arquitectura heredada actual) |
| Controles | Mando, teclado reasignable y cuatro perfiles locales con nombre |
| Imagen | FSR 1, FSR 3.1 experimental, FXAA, filtrado anisotrópico hasta 16x, resolución y pantalla |
| Rendimiento | Cachés persistentes de shaders/pipelines y límites de FPS |
| Portátiles | ROG Ally probado; validación más amplia de CachyOS/Steam Deck aún limitada |

## Aspectos destacados de las versiones

### FINAL BUILD (PGO)

- Las pruebas en ROG Ally cubrieron los 11 circuitos. En Area 51, el mínimo comunicado pasó de 55 a 70 FPS o más (aproximadamente 27%); se comunicaron mejoras similares en los demás circuitos. Los resultados dependen del hardware y las condiciones.
- Antes del menú, el juego prepara los shaders conocidos de los circuitos para la GPU y el controlador locales, con una barra de progreso. La lista contiene 1.444 pipelines y 586 shaders. Los inicios posteriores reutilizan la caché; un shader no incluido aún puede provocar un tirón al usarse por primera vez.
- Los registros están desactivados de forma predeterminada; se pueden activar en Partidas guardadas y registros → Escribir archivos de registro.
- Incluye las mejoras de la 1.2.0. La versión final se distribuye con PGO en ambas plataformas.

### BETA 1.2 (1.2.0)

- Telemetría obtenida en ROG Ally a 20 W en 8 circuitos base y 3 de DLC, incluidos datos por fotograma, hilos y funciones de CPU, trazas de GPU, memoria, frecuencias y potencia.
- Reutilización de conjuntos de descriptores de textura dentro del fotograma (aproximadamente 96%). La etapa medida anteriormente costaba 3,17 ms por fotograma en el hilo limitante de circuitos exigentes como Area 51, Tsunami Cup y City; la verificación de equivalencia registró cero diferencias.
- En Linux, la espera de audio dejó de consumir CPU continuamente: el hilo pasó del 77% de un núcleo a aproximadamente el 1%, tras corregir una espera redondeada a cero milisegundos.
- Corregida la carga involuntaria de RenderDoc y su overlay de captura en Steam Deck/SteamOS.
- Launcher con navegación por iconos, página de novedades, texto adaptable y navegación por mando/teclado. Desde esta versión solo se publican paquetes PGO. La estimación de FPS de la 1.2.0 se basaba en telemetría, no en un benchmark de esa versión en aquel momento.

### BETA 1.1 (1.1.0)

- Editor de partidas guardadas para nombres de jugadores 1–4 y créditos, con copia de seguridad y recálculo de la suma de comprobación. Los jugadores 3 y 4 recibieron partidas independientes.
- Filtrado anisotrópico: valor del juego, 2x, 4x, 8x y 16x (predeterminado 16x); no se midió coste de GPU en la prueba comunicada.
- Las verificaciones de equivalencia CPU/GPU registraron cero diferencias con las bibliotecas de esta versión en Windows y Linux.

### BETA 1.0 (1.0.0)

- Las builds Standard y PGO tenían las mismas funciones. Pruebas Windows sin límite de FPS comunicaron mejoras PGO del 3,5% en Asgard Invasion y 9% en un circuito de cañón ajeno al perfil inicial. Linux utiliza su propio perfil.
- Launcher basado en la organización de UFC3MONS: selector de idioma, Inicio, Vídeo, Controles, Partidas guardadas y registros, Generación de fotogramas, Añadir a Steam y Jugar.
- Ubicación configurable de `user_data`, copiando los datos y conservando la carpeta anterior como respaldo; la selección persiste entre actualizaciones. La importación desde Xenia localiza `ht2save.dat` y respalda la partida actual.
- Corregido el tamaño excesivo de la ventana con escalado de pantalla Windows al 125%/150%.
- Eliminado un bloqueo global al consultar páginas de memoria en el hilo de comandos de vídeo; la medición comunicada registró una reducción del 2,7% del coste de CPU por draw en ese hilo.

### Revisión 9.1.0

- Las optimizaciones configurables en `hydrothunder.toml` buscan mejorar la eficiencia CPU/GPU sin reducir resolución, MSAA, efectos ni precisión.
- Pruebas informales en los cuatro primeros circuitos en ROG Ally Linux a 25 W comunicaron alrededor de 120 FPS la mayor parte del tiempo y pocas veces menos de 100 FPS; no son un benchmark controlado ni una garantía.
- Corregidos un cierre inesperado al salir por el menú y luces visibles a través de paredes; se añadieron controles de teclado reasignables al launcher.

Consulta las [Releases](https://github.com/monscriser/HydroThunderPORT/releases) y las [notas de FINAL BUILD](NOTAS-FINAL-BUILD.md) para mediciones históricas, descargas y sumas de comprobación.

## Funciones y configuración

- Instalación guiada de contenido Xbox 360 extraído y paquetes compatibles (juego, DLC y Title Update), incluida la configuración de actualización 0.0.2.1. El contenido y las traducciones compatibles se gestionan en ejecución sin modificar los archivos fuente originales del juego.
- Carpeta del juego persistente y `user_data` configurable (partidas, DLC y logros); importación desde Xenia, copias de seguridad y restauración, editor de nombres y créditos para cuatro jugadores, overlay FPS opcional y exportación de accesos directos Steam con ilustraciones.
- Launcher en inglés, portugués brasileño y español, con modo de pantalla, resolución, idioma, escalado, FPS e instalación.
- Mando, teclado reasignable y hasta cuatro perfiles locales; pantalla dividida disponible, pero requiere más validación.

| Acción | Tecla predeterminada |
| --- | --- |
| Iniciar / Pausar | Enter |
| Confirmar / A | Espacio |
| Acelerar / RT | E |
| Frenar / LT | Q |
| Dirección | A/D o flechas |

### Gráficos y rendimiento

El backend actual ofrece modos ventana, sin bordes y pantalla completa, escalado y nitidez FSR 1, reconstrucción FSR 3.1 experimental, FXAA opcional, filtrado anisotrópico hasta 16x, compilación paralela de shaders, cachés persistentes y guardado de caché en segundo plano. Límites FPS: 30, 40, 60, 90 y 120; la presentación ilimitada experimental utiliza un reloj virtual de 60 Hz.

FSR 3.1 carece de datos completos de vectores de movimiento del juego. El escalado no equivale a generación de fotogramas; seleccionar FSR 3.1 no genera fotogramas. La compatibilidad de la caché depende del hardware, controlador y build.

### Integración opcional con Lossless Scaling

El launcher puede detectar una copia de Steam adquirida por separado, configurar perfiles Windows con respaldo y seleccionar un multiplicador de generación de fotogramas. En Linux proporciona instrucciones para la capa externa `lsfg-vk` y muestra los ajustes solicitados en el overlay. Lossless Scaling no está incluido; Linux necesita una capa Vulkan compatible además de la aplicación Steam. La validación integral sigue incompleta.

## Instalación

### Windows

1. Extrae `Hydro-Thunder-FINAL-BUILD-Windows-PGO.zip` de [Releases](https://github.com/monscriser/HydroThunderPORT/releases).
2. Abre `Launcher-Config.exe` y selecciona tus archivos del juego, DLC y actualización.
3. Configura vídeo y controles, y selecciona Jugar.

Tras configurar, puedes abrir `HydroThunder.exe` directamente. Usa el launcher para ajustes, partidas y accesos directos Steam.

### Linux / AppImage

1. Descarga el AppImage Linux de [Releases](https://github.com/monscriser/HydroThunderPORT/releases).
2. Si es necesario, concede permisos: `chmod +x HydroThunder*.AppImage`.
3. Ábrelo, selecciona los archivos del juego obtenidos legalmente, configura y selecciona Jugar.

Exporta un acceso directo Steam desde el launcher para iniciar el juego directamente desde Steam.

### Generación de fotogramas opcional

- **Windows:** instala y abre tu copia adquirida de Lossless Scaling en Steam, ciérrala y activa la integración en el launcher. Empieza con 2x. Cierra Lossless Scaling antes de modificar los ajustes del perfil.
- **Linux:** instala la aplicación adquirida, selecciona la rama Steam `lsfg-vk` apropiada, instala una capa Vulkan 2.x compatible con tu distribución, ejecuta su diagnóstico, vuelve a buscarla y activa la integración. Sigue la documentación de la capa.

## Partidas, actualizaciones y compatibilidad

La selección de la carpeta del juego persiste. Respalda las partidas antes de cambiar de build, extrae en una carpeta independiente, comprueba la ruta del juego y conserva la build anterior hasta terminar las pruebas. Nunca borres la carpeta real del juego al eliminar un launcher antiguo.

El rendimiento depende del hardware, controladores, ajustes, circuito y caché. La recompilación estática no garantiza una ventaja frente a Xenia. La generación de fotogramas no aumenta la frecuencia de simulación subyacente ni elimina la latencia de entrada.

## Limitaciones conocidas

- FINAL BUILD cierra el ciclo técnico 1.x; no se prevén actualizaciones rutinarias. El futuro renderizador 2.0 no está incluido.
- La pantalla dividida con tres o cuatro mandos físicos aún no se ha probado, aunque se comprobaron los cuatro perfiles. Cierra el juego antes de editar créditos de una partida.
- La validación más amplia de Linux/CachyOS/Steam Deck y portátiles sigue siendo limitada; las observaciones en ROG Ally no son garantías universales.
- Lossless Scaling necesita más pruebas integrales. El overlay del juego no verifica independientemente los fotogramas generados; los contadores Linux dependen de overlays externos y del orden de las capas.
- La presentación por encima de la frecuencia de refresco o ilimitada puede producir ritmo irregular, tearing o regresiones; el modo ilimitado no ha pasado pruebas exhaustivas de física/audio.
- Los shaders no listados pueden causar un tirón inicial; pueden persistir defectos gráficos, como luces de puertas de colores visibles a través del escenario.
- La exportación de accesos directos Steam requiere más pruebas en instalaciones reales; algunos errores solo aparecen en portugués.
- FSR 3.1 carece de entradas temporales completas.

## Compilación desde el código fuente

Genera el código recompilado a partir de tu propia copia del juego: `app/generated-updated/` no se distribuye aquí. Scripts: [`tools/`](tools/) y [`tools/wsl/`](tools/wsl/). PGO utiliza CMake `-DHT_PGO=USE -DHT_PGO_PROFILE=<file>` y perfiles en [`pgo/`](pgo/), con `build_pgo.sh` / `build_pgo_linux.sh`; Linux también requiere el runtime de perfil de `build_profile_runtime.sh`.

El proyecto utiliza ReXGlue, componentes de compatibilidad derivados de Xenia, Vulkan y Dear ImGui. Consulta [`licenses/`](licenses/) y [`LICENSE-ReXGlue`](LICENSE-ReXGlue) para conocer los términos aplicables de terceros.

## Incidencias y contribuciones

Al comunicar una [incidencia](https://github.com/monscriser/HydroThunderPORT/issues), incluye versión, sistema, controlador, CPU/GPU/RAM, ajustes de pantalla y FPS, estado de generación de fotogramas, circuito/modo, pasos de reproducción, registros relevantes y capturas/vídeos si ayudan. Compara con Xenia en condiciones equivalentes. Los informes pueden orientar la 2.0, pero no implican una corrección programada para 1.x.

Nunca publiques contenido protegido del juego, DLC, actualizaciones ni datos personales de partidas en incidencias públicas.

## Créditos y aviso legal

Agradecimientos especiales a visibou y [htrecomp](https://github.com/visibou/htrecomp), base de este port y documentación original del formato de suma de comprobación de partidas. Gracias a los colaboradores de ReXGlue, Xenia, Dear ImGui, FidelityFX, lsfg-vk y la comunidad de gráficos/emulación. Las ilustraciones Steam pertenecen a sus respectivos colaboradores de SteamGridDB, incluido Monscriser.

HydroThunderPORT es un proyecto comunitario no oficial, sin afiliación ni respaldo de Microsoft, Xbox o los desarrolladores/editoras originales. Los nombres, ilustraciones y recursos originales pertenecen a sus titulares. No se distribuyen archivos propietarios del juego ni de Lossless Scaling. Respeta las licencias de terceros al reutilizar o redistribuir código.
