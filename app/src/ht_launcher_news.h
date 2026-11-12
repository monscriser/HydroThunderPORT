// "Novidades" (What's new) page of the launcher.
//
// EDIT HERE: this is the only place with the release notes shown in the launcher.
// - Each version is one NewsEntry in kNews (newest first).
// - Each line is {"português", "English", "español"}. Plain UTF-8; accents are fine
//   (Roboto has them). Avoid symbols outside Latin-1 (no emoji, no long dashes).
// - The first entry is shown highlighted (it is the version being released).
#pragma once
#include <cstddef>
#include <iterator>

namespace ht::news {

struct Text { const char* pt; const char* en; const char* es; };
struct Entry {
  Text title;        // e.g. "Próxima versão" or "1.1.0 BETA"
  Text date;         // free text, may be empty ("")
  const Text* items;
  std::size_t count;
};

// ---- Next version (placeholder: final wording still to be decided) ----
inline constexpr Text kNextItems[] = {
  {"Novo launcher: menu lateral com ícones, textos que se ajustam ao tamanho da janela (pequena, tela cheia, Steam Deck e ROG Ally) e esta página de novidades.",
   "New launcher: side menu with icons, text that adapts to the window size (small window, fullscreen, Steam Deck and ROG Ally) and this What's new page.",
   "Nuevo launcher: menú lateral con iconos, textos que se ajustan al tamaño de la ventana (pequeña, pantalla completa, Steam Deck y ROG Ally) y esta página de novedades."},
  {"Corrigido o texto \"Capturing Vulkan / F12\" que ficava preso no canto da tela no Steam Deck / SteamOS.",
   "Fixed the \"Capturing Vulkan / F12\" text stuck in the corner of the screen on Steam Deck / SteamOS.",
   "Corregido el texto \"Capturing Vulkan / F12\" que quedaba fijo en la esquina de la pantalla en Steam Deck / SteamOS."},
  {"Menos uso de CPU no Linux: a thread de áudio gastava 77% de um núcleo girando à toa enquanto esperava; agora dorme de verdade (sobra energia para o jogo em portáteis de 20 W).",
   "Lower CPU use on Linux: the audio thread used 77% of a core spinning while it waited; it now really sleeps (more power left for the game on 20 W handhelds).",
   "Menos uso de CPU en Linux: el hilo de audio gastaba el 77% de un núcleo girando mientras esperaba; ahora duerme de verdad (queda más energía para el juego en portátiles de 20 W)."},
  {"Menos trabalho do driver por desenho: cerca de 96% das texturas de cada desenho deixam de ser reenviadas ao driver (era o que mais pesava nas pistas pesadas, como a Área 51, no ROG Ally).",
   "Less driver work per draw: about 96% of each draw's texture bindings are no longer re-sent to the driver (the biggest cost on heavy tracks like Area 51 on the ROG Ally).",
   "Menos trabajo del driver por dibujo: cerca del 96% de las texturas de cada dibujo dejan de reenviarse al driver (lo que más pesaba en pistas pesadas como Área 51 en la ROG Ally)."},
};

// ---- 1.1.0 BETA ----
inline constexpr Text k110Items[] = {
  {"Editor de save na aba Saves: nomes dos jogadores 1 a 4 e créditos (com cópia de segurança antes de salvar).",
   "Save editor in the Saves tab: names of players 1 to 4 and credits (with a backup before saving).",
   "Editor de partida en la pestaña Partidas: nombres de los jugadores 1 a 4 y créditos (con copia de seguridad antes de guardar)."},
  {"Perfis para os jogadores 3 e 4 na tela dividida.",
   "Profiles for players 3 and 4 in split screen.",
   "Perfiles para los jugadores 3 y 4 en pantalla dividida."},
  {"Filtro anisotrópico escolhível (do jogo, 2x a 16x); padrão 16x, sem custo medido.",
   "Selectable anisotropic filtering (game default, 2x to 16x); 16x by default, no measured cost.",
   "Filtro anisotrópico seleccionable (del juego, 2x a 16x); 16x por defecto, sin costo medido."},
};

// ---- 1.0.0 BETA ----
inline constexpr Text k100Items[] = {
  {"Launcher novo com abas, arte da Steam e seletor de idioma.",
   "New launcher with tabs, Steam art and language selector.",
   "Launcher nuevo con pestañas, arte de Steam y selector de idioma."},
  {"Pasta de dados (user_data) escolhível e importação de saves do Xenia.",
   "Selectable data folder (user_data) and save import from Xenia.",
   "Carpeta de datos (user_data) seleccionable e importación de partidas de Xenia."},
  {"Versão PGO: código do jogo otimizado pelo perfil de uso (até 9% mais FPS no PC de teste).",
   "PGO build: game code optimized from a usage profile (up to 9% more FPS on the test PC).",
   "Versión PGO: código del juego optimizado por perfil de uso (hasta 9% más FPS en el PC de prueba)."},
};

inline constexpr Entry kNews[] = {
  {{"1.2.0 BETA", "1.2.0 BETA", "1.2.0 BETA"}, {"11/11/2026", "2026-11-11", "11/11/2026"},
   kNextItems, std::size(kNextItems)},
  {{"1.1.0 BETA", "1.1.0 BETA", "1.1.0 BETA"}, {"05/11/2026", "2026-11-05", "05/11/2026"},
   k110Items, std::size(k110Items)},
  {{"1.0.0 BETA", "1.0.0 BETA", "1.0.0 BETA"}, {"04/11/2026", "2026-11-04", "04/11/2026"},
   k100Items, std::size(k100Items)},
};

}  // namespace ht::news
