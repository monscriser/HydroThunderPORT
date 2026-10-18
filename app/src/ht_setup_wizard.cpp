#include <tuple>
#include "ht_setup_wizard.h"
#include "ht_content_install.h"
#include "ht_save_backup.h"
#include "ht_steam_shortcut.h"
#include "ht_launch.h"
#include "ht_library_paths.h"
#include "ht_shader_cache.h"
#include "ht_lossless.h"
#include "ht_video_policy.h"
#include "ht_config_edit.h"
#include "ht_keyboard_binds.h"
#include <rex/ui/image_decode.h>
#include <rex/ui/imgui_drawer.h>
#include <SDL3/SDL.h>
#include <imgui.h>
#include <array>
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <memory>
#include <mutex>
#include <thread>
#include <rex/cvar.h>
#include <rex/string.h>
#if defined(_WIN32)
REXCVAR_DEFINE_BOOL(ht_import_on_start, false, "Hydro Thunder", "Show the game setup screen on launch");
#else
REXCVAR_DEFINE_BOOL(ht_import_on_start, true, "Hydro Thunder", "Show the game setup screen on launch");
#endif
REXCVAR_DEFINE_BOOL(ht_direct_launch, false, "Hydro Thunder", "Open the game directly, including from Steam");
REXCVAR_DEFINE_BOOL(ht_game_fullscreen, true, "Hydro Thunder", "Use fullscreen after clicking Play");
REXCVAR_DEFINE_INT32(ht_output_width, 1280, "Hydro Thunder", "Selected game output width");
REXCVAR_DEFINE_BOOL(ht_present_tearing, false, "Hydro Thunder",
                    "Allow the immediate (tearing) present mode for the lowest latency");
REXCVAR_DEFINE_BOOL(ht_compat_memexport_readback, false, "Hydro Thunder",
                    "Re-enable memexport CPU readback (slow; only for debugging)");
REXCVAR_DEFINE_BOOL(ht_compat_page_state, false, "Hydro Thunder",
                    "Re-enable per-frame GPU page state refresh (slow; only for debugging)");
REXCVAR_DEFINE_STRING(ht_graphics_profile, "quality", "Hydro Thunder",
                      "quality or performance (lighter GPU settings for Steam Deck/ROG Ally/iGPUs)");
REXCVAR_DEFINE_STRING(ht_ui_language, "", "Hydro Thunder",
                      "Launcher language: pt, en or es (empty = follow the game language)");
REXCVAR_DEFINE_INT32(ht_sharpness, -1, "Hydro Thunder",
                     "Launcher sharpness 0-10 (-1 = default for the chosen upscaler); sets "
                     "present_fsr_sharpness_reduction, present_fsr3_sharpness and present_cas_additional_sharpness");
REXCVAR_DEFINE_INT32(ht_render_quality, 1, "Hydro Thunder",
                     "Internal render size relative to the output: 0 native, 1 quality, 2 balanced, "
                     "3 performance, 4 ultra performance (sets resolution_scale)");
namespace ht {
namespace {
// Render quality presets, FSR-style ratios of output height to render height.
// The Xenos draw scale is an integer multiple of 720p (EDRAM tiles cannot be
// split), so each preset picks the nearest integer scale; the presenter
// upscaler (bilinear / FSR 1 / FSR 3.1) covers the rest of the way.
constexpr float kRenderRatios[]={1.0f,1.5f,1.7f,2.0f,3.0f};
constexpr int kRenderQualityCount=int(std::size(kRenderRatios));
constexpr int kOutputWidths[]={1280,1920,2560,3840},kOutputHeights[]={720,1080,1440,2160};
// Launcher text language: 0 = Portuguese, 1 = English, 2 = Spanish. Strings stay
// ASCII-only, like the rest of the launcher, so every font range renders them.
int g_ui_lang=0;
inline const char* TR(const char* pt,const char* en,const char* es){
  return g_ui_lang==1 ? en : g_ui_lang==2 ? es : pt;
}
constexpr const char* kUiLanguageCodes[]={"pt","en","es"};
// Sharpness slider 0-10. 0-8 walk RCAS from soft (2.0 stops of reduction) to its
// maximum (0.0); 7 = 0.25, close to the engine default 0.2 that FSR 1 was measured
// with, 8 = the FSR 3.1 setting measured on Asgard. 9-10 add FSR 3.1's own sharpening
// (1.0 overshot in the Asgard captures, so it stops at 0.6). With no upscaling the
// CAS pass is used instead, and it only starts sharpening above the default.
struct Sharpness{float rcas_reduction,fsr3,cas;};
inline Sharpness SharpnessFor(int level){
  level=std::clamp(level,0,10);
  return {std::max(0.0f,2.0f-0.25f*float(level)),level>8 ? 0.3f*float(level-8) : 0.0f,
          level>7 ? float(level-7)/3.0f : 0.0f};
}
inline std::string Fixed(float value){char text[16];std::snprintf(text,sizeof(text),"%.2f",value);return text;}
int RenderScale(int quality,int output_height){
  if(quality<=0) return std::clamp((output_height+719)/720,1,3);  // native: never below output
  const float target=float(output_height)/kRenderRatios[std::clamp(quality,0,kRenderQualityCount-1)]/720.0f;
  return std::clamp(int(target+0.49f),1,3);
}
struct State {
  std::mutex mutex;
  std::array<std::string, 3> sources;
  std::string status;
  std::atomic<bool> busy{false};
  std::atomic<bool> succeeded{false};
  std::string save_folder;
  int save_action=0;
};
struct Picker { std::shared_ptr<State> state; int slot; };
void Selected(void* opaque, const char* const* files, int) {
  std::unique_ptr<Picker> picker(static_cast<Picker*>(opaque));
  if (!files || !files[0]) return;
  std::lock_guard lock(picker->state->mutex);
  if(picker->slot<0){picker->state->save_folder=files[0];picker->state->save_action=-picker->slot;}
  else picker->state->sources[picker->slot] = files[0];
}
// Fixed presets set the console clock and swap limiter together. Unlimited
// removes only the swap limiter; high rates remain experimental.
constexpr auto& kEngineRates=video::Rates;
constexpr int kEngineRateCount=video::RateCount;
inline const char* EngineRateLabel(int i){
  switch(i){
    case 0: return TR("60 FPS (original)","60 FPS (original)","60 FPS (original)");
    case 1: return TR("30 FPS (economia maxima de GPU)","30 FPS (maximum GPU savings)","30 FPS (maximo ahorro de GPU)");
    case 2: return TR("40 FPS (Steam Deck / tela 40 Hz)","40 FPS (Steam Deck / 40 Hz screen)","40 FPS (Steam Deck / pantalla 40 Hz)");
    case 3: return "90 FPS";
    case 4: return "120 FPS";
    default: return TR("Sem limite (experimental)","Unlimited (experimental)","Sin limite (experimental)");
  }
}
inline const char* EngineRateHelp(int i){
  switch(i){
    case 0: return TR("Ritmo original do jogo.","The game's original pace.","Ritmo original del juego.");
    case 1: return TR("O jogo inteiro roda a 30 quadros: metade do trabalho da GPU por segundo. A velocidade da partida nao muda.",
                      "The whole game runs at 30 frames: half the GPU work per second. Race speed does not change.",
                      "Todo el juego corre a 30 cuadros: la mitad del trabajo de la GPU por segundo. La velocidad de la carrera no cambia.");
    case 2: return TR("Bom equilibrio para portateis: 2/3 do trabalho da GPU de 60 FPS. A velocidade da partida nao muda.",
                      "Good balance for handhelds: 2/3 of the GPU work of 60 FPS. Race speed does not change.",
                      "Buen equilibrio para portatiles: 2/3 del trabajo de GPU de 60 FPS. La velocidad de la carrera no cambia.");
    case 3: return TR("O motor desenha ate 90 quadros por segundo. Exige mais da GPU e do processador.",
                      "The engine draws up to 90 frames per second. Needs more GPU and CPU.",
                      "El motor dibuja hasta 90 cuadros por segundo. Exige mas GPU y procesador.");
    case 4: return TR("O motor desenha ate 120 quadros por segundo. O valor real depende do processador; a velocidade da partida nao muda.",
                       "The engine draws up to 120 frames per second. The real rate depends on the CPU; race speed does not change.",
                       "El motor dibuja hasta 120 cuadros por segundo. El valor real depende del procesador; la velocidad de la carrera no cambia.");
    default: return TR("Remove o limitador de quadros do jogo, mantendo o relogio virtual em 60 Hz. O compositor, driver e Lossless Scaling ainda podem limitar a apresentacao. Taxas muito altas precisam de teste de fisica e audio.","Removes the game's frame limiter while keeping the virtual clock at 60 Hz. The compositor, driver and Lossless Scaling may still limit presentation. Very high rates need physics and audio testing.","Quita el limitador de cuadros manteniendo el reloj virtual a 60 Hz. El compositor, driver y Lossless Scaling aun pueden limitar la presentacion. Tasas muy altas requieren pruebas de fisica y audio.");
  }
}
class SetupWizard final : public rex::ui::ImGuiDialog {
 public:
  SetupWizard(rex::ui::ImGuiDrawer* drawer, rex::PathConfig paths,
              std::function<void(rex::PathConfig)> resume)
      : ImGuiDialog(drawer), paths_(std::move(paths)), resume_(std::move(resume)),
        state_(std::make_shared<State>()) {
    root_=paths_.user_data_root.parent_path();
    binary_root_=rex::filesystem::GetExecutableFolder();
    state_->succeeded=ReadyForLaunch(root_);
    async_=rex::cvar::GetFlagByName("async_shader_compilation")=="true";
    game_fullscreen_=rex::cvar::GetFlagByName("ht_game_fullscreen")=="true";
    show_on_start_=rex::cvar::GetFlagByName("ht_import_on_start")=="true";
    language_=std::clamp(std::atoi(rex::cvar::GetFlagByName("user_language").c_str()),1,9)-1;
    // The guest vblank (video_mode_refresh_rate) is the engine clock: the game
    // renders and simulates once per vblank, so it is what the selector sets.
    const auto rate=std::atoi(rex::cvar::GetFlagByName("video_mode_refresh_rate").c_str());
    fps_=0;
    for(int i=0;i<kEngineRateCount;++i) if(kEngineRates[i]==rate) fps_=i;
    if(std::atoi(rex::cvar::GetFlagByName("fps_limit").c_str())<=0)fps_=kEngineRateCount-1;
    performance_=rex::cvar::GetFlagByName("ht_graphics_profile")=="performance";
    mode_=rex::cvar::GetFlagByName("ht_game_fullscreen")=="true" ?
        (rex::cvar::GetFlagByName("fullscreen_mode")=="exclusive" ? 2 : 1) : 0;
    const auto width=std::atoi(rex::cvar::GetFlagByName("ht_output_width").c_str());
    resolution_=width==3840 ? 3 : width==2560 ? 2 : width==1920 ? 1 : 0;
    render_quality_=std::clamp(std::atoi(rex::cvar::GetFlagByName("ht_render_quality").c_str()),
                               0,kRenderQualityCount-1);
    steam_profiles_=steam::Profiles();
    lossless_options_=lossless::Load();
    lossless_detected_=lossless::Detect();
    show_fps_=rex::cvar::GetFlagByName("ht_show_fps")=="true";
    {const auto effect=rex::cvar::GetFlagByName("present_effect");
     upscaler_=effect=="fsr3" ? 2 : effect=="fsr" ? 1 : 0;}
    sharpness_=std::atoi(rex::cvar::GetFlagByName("ht_sharpness").c_str());
    if(sharpness_<0||sharpness_>10)sharpness_=upscaler_==2 ? 8 : 7;
    // FXAA runs on the 720p frame before upscaling and outlines/doubles the thin
    // HUD digits (Asgard captures), so it is opt-in.
    fxaa_=rex::cvar::GetFlagByName("swap_post_effect")=="fxaa";
    {const auto ui=rex::cvar::GetFlagByName("ht_ui_language");
     ui_lang_=ui=="en" ? 1 : ui=="es" ? 2 : ui=="pt" ? 0 :
              language_==8 ? 0 : language_==4 ? 2 : 1;  // follow the game language
     g_ui_lang=ui_lang_;}
  }
  ~SetupWizard() override { if(worker_.joinable()) worker_.join(); }
 protected:
  void OnDraw(ImGuiIO& io) override {
    std::string save_folder; int save_action=0;
    { std::lock_guard lock(state_->mutex);if(!state_->busy){save_action=state_->save_action;save_folder=state_->save_folder;state_->save_action=0;} }
    if(save_action==4){
      auto selected=std::filesystem::u8path(save_folder);
      if(selected.filename()=="game_data" && std::filesystem::is_directory(selected.parent_path()/"user_data"))selected=selected.parent_path();
      try{
        if(!ReadyForLaunch(selected))throw std::runtime_error(TR("Selecione uma pasta com o jogo e o update preparados. Para pacotes originais, use as etapas Jogo/DLC/Update.",
          "Select a folder with the game and update already prepared. For original packages, use the Game/DLC/Update steps.",
          "Selecciona una carpeta con el juego y la actualizacion ya preparados. Para paquetes originales, usa los pasos Juego/DLC/Update."));
        RememberLibrary(selected);root_=selected;paths_.cache_root=ShaderCacheDirectory(root_);state_->succeeded=true;
        std::lock_guard lock(state_->mutex);state_->status=std::string(TR("Pasta registrada, sem copiar os arquivos: ",
          "Folder registered, no files copied: ","Carpeta registrada, sin copiar archivos: "))+rex::path_to_utf8(root_);
      }catch(const std::exception& error){std::lock_guard lock(state_->mutex);state_->status=error.what();}
      save_action=0;
    }
    if(save_action){
      if(worker_.joinable())worker_.join();
      state_->busy=true;
      worker_=std::thread([state=state_,root=root_,binary=binary_root_,save_action,save_folder]{
        std::string message;
        try {
          if(save_action==1){auto path=saves::Export(root,std::filesystem::u8path(save_folder));message=std::string(TR("Backup salvo: ","Backup saved: ","Copia guardada: "))+rex::path_to_utf8(path);}
          else if(save_action==2){saves::Import(root,std::filesystem::u8path(save_folder));message=TR("Saves importados. Os saves anteriores foram preservados em save_backups.",
            "Saves imported. The previous saves were kept in save_backups.","Partidas importadas. Las anteriores se conservaron en save_backups.");}
          else if(save_action==3){steam::Export(binary,std::filesystem::u8path(save_folder));message=TR("Atalho criado. Abra a Steam: o jogo inicia diretamente, sem passar pelo configurador.",
            "Shortcut created. Open Steam: the game starts directly, skipping the launcher.","Acceso directo creado. Abre Steam: el juego inicia directamente, sin pasar por el configurador.");}
          else if(save_action==5){const auto count=ClearShaderCache(std::filesystem::u8path(save_folder));message=std::string(TR("Cache de shaders limpo: ","Shader cache cleared: ","Cache de shaders limpia: "))+std::to_string(count)+
            TR(" arquivos. Sera recompilado na proxima partida; saves preservados."," files. It will be rebuilt in the next race; saves kept."," archivos. Se recompilara en la proxima partida; partidas conservadas.");}
        }catch(const std::exception& error){message=error.what();}
        {std::lock_guard lock(state->mutex);state->status=std::move(message);}
        state->busy=false;
      });
    }
    ImGui::SetNextWindowPos(ImVec2(0,0),ImGuiCond_Always);
    ImGui::SetNextWindowSize(io.DisplaySize,ImGuiCond_Always);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding,0);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,ImVec2(30,18));
    ImGui::Begin("###ht_setup",nullptr,ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
    auto* draw=ImGui::GetWindowDrawList();
    const auto origin=ImGui::GetWindowPos();
    const float header_h=io.DisplaySize.y>700 ? 180.f : 146.f;
    const ImU32 blue=IM_COL32(22,44,66,255);
    draw->AddRectFilled(origin,ImVec2(origin.x+io.DisplaySize.x,origin.y+header_h),blue);
    if(!art_attempted_){
      art_attempted_=true;
      auto load=[&](const char* file){const auto bytes=lossless::Read(binary_root_/file);int w=0,h=0;
        auto pixels=rex::ui::DecodeImageRGBA(reinterpret_cast<const uint8_t*>(bytes.data()),bytes.size(),w,h);
        return pixels.empty()?std::unique_ptr<rex::ui::ImmediateTexture>{}:imgui_drawer()->CreateTextureRGBA(w,h,pixels.data());};
      header_art_=load("steam_art/monscriser/hero.png");
      header_logo_=load("steam_art/logo.png");
    }
    if(header_art_){
      const float left=io.DisplaySize.x*.59f;
      draw->AddImage(reinterpret_cast<ImTextureID>(header_art_.get()),ImVec2(origin.x+left,origin.y),ImVec2(origin.x+io.DisplaySize.x,origin.y+header_h));
      draw->AddRectFilledMultiColor(ImVec2(origin.x+left,origin.y),ImVec2(origin.x+left+110,origin.y+header_h),blue,IM_COL32(22,44,66,0),IM_COL32(22,44,66,0),blue);
    }
    if(header_logo_){
      const float width=std::min(270.f,io.DisplaySize.x*.3f),height=width*float(header_logo_->height)/float(header_logo_->width);
      draw->AddImage(reinterpret_cast<ImTextureID>(header_logo_.get()),ImVec2(origin.x+io.DisplaySize.x-width-18,origin.y+10),ImVec2(origin.x+io.DisplaySize.x-18,origin.y+10+height));
    }
    draw->AddText(ImGui::GetFont(),34,ImVec2(origin.x+30,origin.y+23),IM_COL32(242,247,252,255),"Hydro Thunder");
    draw->AddText(ImGui::GetFont(),34,ImVec2(origin.x+30.7f,origin.y+23),IM_COL32(242,247,252,255),"Hydro Thunder");
    draw->AddText(ImGui::GetFont(),24,ImVec2(origin.x+30,origin.y+65),IM_COL32(105,211,245,255),"Hurricane");
    draw->AddText(ImGui::GetFont(),16,ImVec2(origin.x+30,origin.y+100),IM_COL32(192,214,230,255),TR("Sua proxima corrida comeca aqui.","Your next race starts here.","Tu proxima carrera empieza aqui."));
    ImGui::SetCursorPos(ImVec2(std::max(30.f,io.DisplaySize.x-185),header_h-44));
    {const char* languages[]={"Portugues","English","Espanol"};ImGui::SetNextItemWidth(145);
      if(ImGui::Combo("##ui_language",&ui_lang_,languages,3))g_ui_lang=ui_lang_;}
    ImGui::SetCursorPos(ImVec2(30,header_h+14));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,ImVec2(8,4));
    ImGui::BeginChild("LauncherBody",ImVec2(0,std::max(150.f,ImGui::GetContentRegionAvail().y-85)),false);
    if(save_actions_requested_){ImGui::OpenPopup("###save_steam_actions");save_actions_requested_=false;}
    if(ImGui::BeginPopup("###save_steam_actions")){
    ImGui::BeginDisabled(state_->busy);
    if(ImGui::SmallButton(TR("Exportar saves###export","Export saves###export","Exportar partidas###export")))SDL_ShowOpenFolderDialog(Selected,new Picker{state_,-1},nullptr,nullptr,false);
    ImGui::SameLine();
    if(ImGui::SmallButton(TR("Importar saves###import","Import saves###import","Importar partidas###import")))SDL_ShowOpenFolderDialog(Selected,new Picker{state_,-2},nullptr,nullptr,false);
    ImGui::EndDisabled();
    ImGui::EndPopup();
    }
    ImGui::Spacing();
    std::array<std::string,3> sources;
    std::string status;
    { std::lock_guard lock(state_->mutex); sources=state_->sources; status=state_->status; }
    ImGui::BeginTabBar("Configurador");
    if(ImGui::BeginTabItem(TR("Inicio###tab_game","Home###tab_game","Inicio###tab_game"))){
    ImGui::BeginChild("HomeContents",ImVec2(0,0),false);
    if(state_->succeeded && !manage_install_){
      ImGui::Spacing();ImGui::Spacing();
      ImGui::SetWindowFontScale(1.35f);
      ImGui::TextColored(ImVec4(.78f,.97f,.90f,1),"%s",TR("Tudo configurado","All set","Todo configurado"));
      ImGui::SetWindowFontScale(1);
      ImGui::TextWrapped("%s",TR("Sua instalacao foi reconhecida. E so jogar.","Your installation was recognized. Ready to play.","Se reconocio tu instalacion. Listo para jugar."));
      ImGui::Spacing();ImGui::Spacing();
      ImGui::TextUnformatted(TR("Arquivos do jogo e update","Game files and title update","Archivos del juego y actualizacion"));
      ImGui::SameLine(std::max(260.f,ImGui::GetContentRegionAvail().x-150));
      ImGui::TextColored(ImVec4(.45f,.9f,.73f,1),"%s",TR("Prontos para jogar","Ready to play","Listos para jugar"));
      ImGui::Separator();ImGui::Spacing();
      ImGui::TextUnformatted(TR("Conteudo adicional (DLC)","Additional content (DLC)","Contenido adicional (DLC)"));
      ImGui::SameLine(std::max(260.f,ImGui::GetContentRegionAvail().x-150));
      const bool dlc=std::filesystem::exists(root_/"user_data/0000000000000000/5841096A/00000002/Pack01/Pack01.apf");
      ImGui::TextColored(dlc?ImVec4(.45f,.9f,.73f,1):ImVec4(.7f,.74f,.8f,1),"%s",dlc?TR("Instalado","Installed","Instalado"):TR("Opcional","Optional","Opcional"));
      ImGui::Spacing();
      if(ImGui::TreeNode(TR("Pasta do jogo e saves###paths","Game and save folders###paths","Carpetas del juego y partidas###paths"))){
        ImGui::TextWrapped("%s",rex::path_to_utf8(root_).c_str());
        ImGui::TextWrapped("%s",rex::path_to_utf8(root_/"user_data").c_str());ImGui::TreePop();}
      ImGui::Spacing();
      if(ImGui::Button(TR("Gerenciar instalacao###manage","Manage installation###manage","Administrar instalacion###manage")))manage_install_=true;
      ImGui::SameLine();
      if(ImGui::Button(TR("Saves###save_steam","Saves###save_steam","Partidas###save_steam")))save_actions_requested_=true;
      // Own row under the installation buttons, so players find it (it used to sit
      // inside the Saves menu).
      ImGui::BeginDisabled(state_->busy);
      if(ImGui::Button(TR("Adicionar a Steam###steam","Add to Steam###steam","Agregar a Steam###steam")))AddToSteam();
      ImGui::EndDisabled();
      DrawSteamAccountPopup();
    }else{
      ImGui::TextUnformatted(TR("Vamos preparar sua primeira corrida","Let's prepare your first race","Preparemos tu primera carrera"));
      if(state_->succeeded){if(ImGui::Button(TR("Voltar ao inicio###back_home","Back to Home###back_home","Volver al inicio###back_home")))manage_install_=false;}
      if(ImGui::Button(TR("Usar pasta do jogo existente###existing","Use existing game folder###existing","Usar carpeta del juego existente###existing")))SDL_ShowOpenFolderDialog(Selected,new Picker{state_,-4},nullptr,nullptr,false);
      ImGui::SameLine();if(ImGui::Button(TR("Saves###save_steam","Saves###save_steam","Partidas###save_steam")))save_actions_requested_=true;
    const char* titles[]={TR("1. Jogo","1. Game","1. Juego"),"2. DLC","3. Update"};
    const char* help[]={TR("Pasta com default.xex e Base.apf, ou o pacote original do jogo.",
                           "Folder with default.xex and Base.apf, or the original game package.",
                           "Carpeta con default.xex y Base.apf, o el paquete original del juego."),
                        TR("Pacote LIVE/CON/PIRS, mesmo sem extensao, ou pasta com Pack01.apf. Opcional.",
                           "LIVE/CON/PIRS package, even without an extension, or a folder with Pack01.apf. Optional.",
                           "Paquete LIVE/CON/PIRS, aunque no tenga extension, o carpeta con Pack01.apf. Opcional."),
                        TR("Arquivo UPDATE, default.xexp ou pasta extraida. Esta build usa o update 0.0.2.1.",
                           "UPDATE file, default.xexp or extracted folder. This build uses title update 0.0.2.1.",
                           "Archivo UPDATE, default.xexp o carpeta extraida. Esta build usa la actualizacion 0.0.2.1.")};
    ImGui::BeginDisabled(state_->busy);
    ImGui::BeginChild("Arquivos",ImVec2(0,std::max(120.0f,ImGui::GetContentRegionAvail().y-70)),false);
    for(int slot=0;slot<3;++slot) {
      ImGui::PushID(slot); ImGui::Separator();
      const bool installed=slot==0 ? std::filesystem::exists(GameDirectory(root_)/"Base.apf") :
                           slot==1 ? std::filesystem::exists(root_/"user_data/0000000000000000/5841096A/00000002/Pack01/Pack01.apf") : ReadyForLaunch(root_);
      ImGui::TextUnformatted(titles[slot]); ImGui::SameLine();
      ImGui::TextColored(installed ? ImVec4(.35f,.8f,.6f,1) : ImVec4(.7f,.7f,.7f,1),
                         "%s",installed ? TR("- instalado","- installed","- instalado") :
                                slot==1 ? TR("- opcional","- optional","- opcional") : TR("- necessario","- required","- necesario"));
      ImGui::TextWrapped("%s",help[slot]);
      std::array<char,4096> path{};
      std::strncpy(path.data(),sources[slot].c_str(),path.size()-1);
      ImGui::SetNextItemWidth(std::max(120.0f,ImGui::GetContentRegionAvail().x-190));
      if(ImGui::InputText("##path",path.data(),path.size())) {
        std::lock_guard lock(state_->mutex);state_->sources[slot]=path.data();
      }
      ImGui::SameLine();
      if(ImGui::Button(TR("Arquivo###file","File###file","Archivo###file"))) SDL_ShowOpenFileDialog(Selected,new Picker{state_,slot},nullptr,nullptr,0,nullptr,false);
      ImGui::SameLine();
      if(ImGui::Button(TR("Pasta###folder","Folder###folder","Carpeta###folder"))) SDL_ShowOpenFolderDialog(Selected,new Picker{state_,slot},nullptr,nullptr,false);
      ImGui::PopID();
    }
    ImGui::EndChild();
    ImGui::Spacing();
    if(ImGui::Button(TR("Preparar arquivos###prepare","Prepare files###prepare","Preparar archivos###prepare"),ImVec2(175,34))) {
      if(worker_.joinable()) worker_.join();
      std::array<std::filesystem::path,3> paths;
      { std::lock_guard lock(state_->mutex); for(int i=0;i<3;++i) paths[i]=std::filesystem::u8path(state_->sources[i]); }
      if(!paths[0].empty()){
        auto selected=std::filesystem::is_directory(paths[0]) ? paths[0] : paths[0].parent_path();
        if(selected.filename()=="game_data" && std::filesystem::is_directory(selected.parent_path()/"user_data"))selected=selected.parent_path();
        root_=selected;
        paths_.cache_root=ShaderCacheDirectory(root_);
        if(ReadyForLaunch(root_)){RememberLibrary(root_);paths[0].clear();}
      }
      state_->busy=true;state_->succeeded=false;
      worker_=std::thread([state=state_,root=root_,paths] {
        auto report=[state](std::string message) { std::lock_guard lock(state->mutex);state->status=std::move(message); };
        try {if(!ReadyForLaunch(root) || !paths[0].empty() || !paths[1].empty() || !paths[2].empty())PrepareContent(root,paths,report);RememberLibrary(root);state->succeeded=true;report(TR("Pasta do jogo registrada. Os saves ficam junto do jogo, separados da build.",
          "Game folder registered. Saves stay next to the game, apart from the build.","Carpeta del juego registrada. Las partidas quedan junto al juego, separadas de la build."));}
        catch(const std::exception& error) {report(error.what());}
        state->busy=false;
      });
    }
    ImGui::EndDisabled();
    }
    ImGui::EndChild();
    ImGui::EndTabItem();
    }
    if(ImGui::BeginTabItem(TR("Video e idioma###tab_video","Video and language###tab_video","Video e idioma###tab_video"))){
    ImGui::BeginChild("Preferencias",ImVec2(0,std::max(120.0f,ImGui::GetContentRegionAvail().y)),false);
    ImGui::SetWindowFontScale(1.35f);ImGui::TextUnformatted(TR("Do seu jeito","Make it yours","A tu manera"));ImGui::SetWindowFontScale(1);
    ImGui::TextWrapped("%s",TR("As preferencias serao aplicadas na proxima abertura do jogo.","Preferences apply the next time you start the game.","Las preferencias se aplican al volver a abrir el juego."));
    ImGui::Spacing();ImGui::Spacing();
    if(ImGui::BeginTable("BasicVideo",2,ImGuiTableFlags_SizingStretchSame)){
      ImGui::TableNextColumn();
      ImGui::TextUnformatted(TR("Modo de exibicao","Display mode","Modo de pantalla"));
      const char* modes[]={TR("Janela","Windowed","Ventana"),TR("Sem bordas","Borderless","Sin bordes"),TR("Tela cheia","Fullscreen","Pantalla completa")};
      ImGui::SetNextItemWidth(-12);ImGui::Combo("##display_basic",&mode_,modes,3);
      ImGui::TableNextColumn();ImGui::TextUnformatted(TR("Limite de FPS","FPS limit","Limite de FPS"));
      const char* rates[kEngineRateCount];for(int i=0;i<kEngineRateCount;++i)rates[i]=EngineRateLabel(i);
      ImGui::SetNextItemWidth(-12);ImGui::Combo("##rate_basic",&fps_,rates,kEngineRateCount);
      ImGui::TableNextColumn();ImGui::Spacing();ImGui::TextUnformatted(TR("Resolucao de saida","Output resolution","Resolucion de salida"));
      const char* sizes[]={"1280 x 720","1920 x 1080","2560 x 1440","3840 x 2160"};
      ImGui::SetNextItemWidth(-12);ImGui::Combo("##resolution_basic",&resolution_,sizes,4);
      ImGui::TableNextColumn();ImGui::Spacing();ImGui::TextUnformatted(TR("Filtro de imagem","Image filter","Filtro de imagen"));
      const char* filters[]={"Bilinear","FSR 1","FSR 3.1"};ImGui::SetNextItemWidth(-12);ImGui::Combo("##filter_basic",&upscaler_,filters,3);
      ImGui::TableNextColumn();ImGui::Spacing();ImGui::TextUnformatted(TR("Idioma do jogo","Game language","Idioma del juego"));
      const char* languages[]={"English","Japanese","German","French","Spanish","Italian","Korean","Chinese (Traditional)","Portugues"};
      ImGui::SetNextItemWidth(-12);ImGui::Combo("##language_basic",&language_,languages,9);
      ImGui::TableNextColumn();ImGui::Spacing();
      ImGui::Checkbox(TR("Mostrar contador de FPS###fps_basic","Show FPS counter###fps_basic","Mostrar contador de FPS###fps_basic"),&show_fps_);
      ImGui::Checkbox(TR("Compilar shaders em paralelo###async_basic","Compile shaders in parallel###async_basic","Compilar shaders en paralelo###async_basic"),&async_);
      ImGui::EndTable();
    }
    ImGui::Spacing();ImGui::TextWrapped("%s",EngineRateHelp(fps_));
    if(ImGui::CollapsingHeader(TR("Ajustes avancados###advanced_video","Advanced settings###advanced_video","Ajustes avanzados###advanced_video"))){
    ImGui::PushID("AdvancedVideo");
    ImGui::Checkbox(TR("Compilar shaders em segundo plano (Async)###async","Compile shaders in the background (Async)###async",
                       "Compilar shaders en segundo plano (Async)###async"),&async_);
    ImGui::SameLine();
    const char* languages[]={"English","Japanese","German","French","Spanish","Italian",
      "Korean","Chinese (Traditional)","Portugues"};
    ImGui::SetNextItemWidth(160);
    ImGui::Combo(TR("Idioma do jogo###game_language","Game language###game_language","Idioma del juego###game_language"),&language_,languages,9);
    if(ImGui::IsItemHovered()) ImGui::SetTooltip("%s",TR("Idiomas sem traducao no jogo usam o idioma padrao do proprio jogo.\nPortugues usa os textos em portugues do Brasil.",
      "Languages the game was not translated to fall back to its default language.\nPortugues uses the Brazilian Portuguese texts.",
      "Los idiomas sin traduccion en el juego usan su idioma predeterminado.\nPortugues usa los textos en portugues de Brasil."));
    ImGui::TextWrapped("%s",TR("O cache fica salvo nesta pasta e e reutilizado ao abrir o jogo.",
      "The cache is kept in this folder and reused when the game starts.","La cache se guarda en esta carpeta y se reutiliza al abrir el juego."));
    ImGui::Checkbox(TR("Mostrar contador de FPS###fps","Show FPS counter###fps","Mostrar contador de FPS###fps"),&show_fps_);
    {const char* upscalers[]={TR("Bilinear (mais suave)","Bilinear (softest)","Bilineal (mas suave)"),
                              TR("FSR 1 (recomendado)","FSR 1 (recommended)","FSR 1 (recomendado)"),"FSR 3.1 - AMD FidelityFX"};
     ImGui::SetNextItemWidth(230);
     const int previous_upscaler=upscaler_;
     ImGui::Combo(TR("Ampliacao da imagem###upscaler","Upscaling###upscaler","Escalado de imagen###upscaler"),&upscaler_,upscalers,3);
     // Moving to/from FSR 3.1 keeps the slider on each upscaler's measured default
     // only while the user has not chosen another level.
     if(upscaler_!=previous_upscaler && sharpness_==(previous_upscaler==2 ? 8 : 7))
       sharpness_=upscaler_==2 ? 8 : 7;
     if(ImGui::IsItemHovered())ImGui::SetTooltip("%s",TR(
       "Usado quando a imagem do jogo e menor que a resolucao de saida.\nFSR 1: o mais nitido nos testes, custo muito baixo.\nFSR 3.1: o jogo nao fornece vetores de movimento, entao cada quadro e\nreconstruido sozinho; com nitidez RCAS maxima fica parecido com o FSR 1.\nNenhum dos dois gera quadros extras. Para a imagem mais limpa, use 'Nativa'.",
       "Used when the game image is smaller than the output resolution.\nFSR 1: the sharpest in our tests, very low cost.\nFSR 3.1: the game provides no motion vectors, so each frame is\nrebuilt on its own; with maximum RCAS it looks like FSR 1.\nNeither generates extra frames. For the cleanest image, use 'Native'.",
       "Se usa cuando la imagen del juego es menor que la resolucion de salida.\nFSR 1: el mas nitido en las pruebas, costo muy bajo.\nFSR 3.1: el juego no da vectores de movimiento, asi que cada cuadro se\nreconstruye solo; con nitidez RCAS maxima queda parecido al FSR 1.\nNinguno genera cuadros extra. Para la imagen mas limpia, usa 'Nativa'."));
     ImGui::SetNextItemWidth(230);
     ImGui::SliderInt(TR("Nitidez###sharpness","Sharpness###sharpness","Nitidez###sharpness"),&sharpness_,0,10);
     if(ImGui::IsItemHovered())ImGui::SetTooltip("%s",TR(
       "Realca as bordas depois da ampliacao (padrao: 7 no FSR 1, 8 no FSR 3.1).\n0-8: filtro RCAS do FSR, do mais suave ao maximo.\n9-10: tambem liga a nitidez propria do FSR 3.1 (sem efeito extra no FSR 1).\nTextos e contornos de contraste muito alto sao protegidos da nitidez.\nO jogo desenha em 720p: nitidez realca bordas, nao cria detalhes.",
       "Enhances edges after upscaling (default: 7 with FSR 1, 8 with FSR 3.1).\n0-8: FSR's RCAS filter, from softest to maximum.\n9-10: also enables FSR 3.1's own sharpening (no extra effect with FSR 1).\nText and very high-contrast outlines are protected from sharpening.\nThe game draws at 720p: sharpening enhances edges, it does not add detail.",
       "Realza los bordes despues del escalado (por defecto: 7 en FSR 1, 8 en FSR 3.1).\n0-8: filtro RCAS de FSR, del mas suave al maximo.\n9-10: tambien activa la nitidez propia de FSR 3.1 (sin efecto extra en FSR 1).\nLos textos y contornos de contraste muy alto quedan protegidos de la nitidez.\nEl juego dibuja en 720p: la nitidez realza bordes, no crea detalles."));
     ImGui::Checkbox(TR("Anti-serrilhado FXAA###fxaa","FXAA anti-aliasing###fxaa","Antialiasing FXAA###fxaa"),&fxaa_);
     if(ImGui::IsItemHovered())ImGui::SetTooltip("%s",TR(
       "Suaviza o serrilhado das bordas, mas roda na imagem de 720p antes da ampliacao\ne deixa textos pequenos do HUD (numeros do velocimetro) com contorno duplo.\nDesligado por padrao.",
       "Smooths jagged edges, but it runs on the 720p image before upscaling\nand gives small HUD text (speedometer digits) a doubled outline.\nOff by default.",
       "Suaviza los dientes de sierra, pero corre sobre la imagen de 720p antes del escalado\ny deja los textos pequenos del HUD (numeros del velocimetro) con doble contorno.\nDesactivado por defecto."));}
    if(ImGui::Button(TR("Limpar shadercache###clear_cache","Clear shader cache###clear_cache","Limpiar cache de shaders###clear_cache")))ImGui::OpenPopup("###confirm_cache");
    const std::string confirm_title=std::string(TR("Confirmar limpeza de shaders","Confirm shader cache cleanup","Confirmar limpieza de shaders"))+"###confirm_cache";
    if(ImGui::BeginPopupModal(confirm_title.c_str(),nullptr,ImGuiWindowFlags_AlwaysAutoResize)){
      ImGui::TextUnformatted(TR("Feche outras janelas do jogo antes de continuar.\nOs shaders serao recompilados e podem causar engasgos iniciais.\nSaves, jogo, DLC e update ficam preservados.",
        "Close other game windows before continuing.\nShaders will be rebuilt and may cause some stutter at first.\nSaves, game, DLC and update are kept.",
        "Cierra otras ventanas del juego antes de continuar.\nLos shaders se recompilaran y pueden causar tirones al principio.\nPartidas, juego, DLC y actualizacion se conservan."));
      if(ImGui::Button(TR("Limpar agora###clear_now","Clear now###clear_now","Limpiar ahora###clear_now"))){std::lock_guard lock(state_->mutex);state_->save_folder=rex::path_to_utf8(paths_.cache_root);state_->save_action=5;ImGui::CloseCurrentPopup();}
      ImGui::SameLine();if(ImGui::Button(TR("Cancelar###cancel","Cancel###cancel","Cancelar###cancel")))ImGui::CloseCurrentPopup();ImGui::EndPopup();
    }
    const char* profiles[]={TR("Qualidade (padrao)","Quality (default)","Calidad (predeterminado)"),
                            TR("Desempenho - GPU fraca (Steam Deck, Ally, integrada)","Performance - weak GPU (Steam Deck, Ally, integrated)",
                               "Rendimiento - GPU debil (Steam Deck, Ally, integrada)")};
    int profile=performance_?1:0;
    ImGui::SetNextItemWidth(260);ImGui::Combo(TR("Perfil grafico###profile","Graphics profile###profile","Perfil grafico###profile"),&profile,profiles,2);performance_=profile==1;
    if(ImGui::IsItemHovered())ImGui::SetTooltip("%s",TR(
      "Desempenho: filtragem de texturas definida pelo proprio jogo (sem anisotropico forcado),\nalvos de gamma em 8 bits, menos threads de compilacao e cache de texturas menor.\nReduz a carga na GPU e na memoria compartilhada; diferenca visual pequena.",
      "Performance: texture filtering as set by the game (no forced anisotropic),\n8-bit gamma targets, fewer compile threads and a smaller texture cache.\nLowers GPU and shared-memory load; small visual difference.",
      "Rendimiento: filtrado de texturas definido por el juego (sin anisotropico forzado),\nobjetivos gamma de 8 bits, menos hilos de compilacion y cache de texturas menor.\nReduce la carga de GPU y de memoria compartida; diferencia visual pequena."));
    const char* rate_labels[kEngineRateCount];
    for(int i=0;i<kEngineRateCount;++i)rate_labels[i]=EngineRateLabel(i);
    ImGui::SetNextItemWidth(220);ImGui::Combo(TR("Taxa de quadros do jogo###rate","Game frame rate###rate","Cuadros por segundo del juego###rate"),&fps_,rate_labels,kEngineRateCount);
    ImGui::TextWrapped("%s",EngineRateHelp(fps_));
    const char* sizes[]={"1280 x 720 (720p)","1920 x 1080 (1080p)","2560 x 1440 (1440p)","3840 x 2160 (2160p)"};
    ImGui::SetNextItemWidth(220);ImGui::Combo(TR("Resolucao de saida###output","Output resolution###output","Resolucion de salida###output"),&resolution_,sizes,4);
    ImGui::TextWrapped("%s",TR("A imagem nativa de 720p e ajustada para a resolucao de saida. Sem bordas usa a resolucao da area de trabalho.",
      "The native 720p image is fitted to the output resolution. Borderless uses the desktop resolution.",
      "La imagen nativa de 720p se ajusta a la resolucion de salida. Sin bordes usa la resolucion del escritorio."));
    const char* render_labels[]={TR("Nativa (maxima qualidade)","Native (maximum quality)","Nativa (maxima calidad)"),
                                 TR("Qualidade","Quality","Calidad"),TR("Equilibrado","Balanced","Equilibrado"),
                                 TR("Desempenho","Performance","Rendimiento"),TR("Ultra desempenho","Ultra performance","Ultra rendimiento")};
    ImGui::SetNextItemWidth(220);
    ImGui::Combo(TR("Qualidade da imagem###render_quality","Image quality###render_quality","Calidad de imagen###render_quality"),&render_quality_,render_labels,kRenderQualityCount);
    if(ImGui::IsItemHovered())ImGui::SetTooltip("%s",TR(
      "Define o tamanho em que o jogo desenha cada quadro, acompanhando a resolucao de saida.\nNativa: desenha no tamanho da saida ou maior (bordas mais lisas, GPU mais carregada).\nQualidade/Equilibrado/Desempenho/Ultra: desenha menor e a ampliacao (FSR) completa a imagem.\nO motor do Xbox 360 so desenha em multiplos inteiros de 720p (1x, 2x, 3x).",
      "Sets the size the game draws each frame at, following the output resolution.\nNative: draws at the output size or larger (smoother edges, heavier GPU load).\nQuality/Balanced/Performance/Ultra: draws smaller and the upscaler (FSR) completes the image.\nThe Xbox 360 engine only draws at whole multiples of 720p (1x, 2x, 3x).",
      "Define el tamano en que el juego dibuja cada cuadro, segun la resolucion de salida.\nNativa: dibuja al tamano de salida o mayor (bordes mas suaves, GPU mas cargada).\nCalidad/Equilibrado/Rendimiento/Ultra: dibuja menor y el escalado (FSR) completa la imagen.\nEl motor de Xbox 360 solo dibuja en multiplos enteros de 720p (1x, 2x, 3x)."));
    {
      const int scale=RenderScale(render_quality_,kOutputHeights[resolution_]);
      const int render_w=1280*scale,render_h=720*scale;
      const int out_w=kOutputWidths[resolution_],out_h=kOutputHeights[resolution_];
      if(render_h<out_h)
        ImGui::TextWrapped(TR("Desenha em %dx%d (escala %dx) e amplia para %dx%d com \"%s\".",
                              "Draws at %dx%d (%dx scale) and upscales to %dx%d with \"%s\".",
                              "Dibuja en %dx%d (escala %dx) y escala a %dx%d con \"%s\"."),render_w,render_h,
                           scale,out_w,out_h,upscaler_==2 ? "FSR 3.1" : upscaler_==1 ? "FSR 1" : "Bilinear");
      else if(render_h>out_h)
        ImGui::TextWrapped(TR("Desenha em %dx%d (escala %dx) e reduz para %dx%d: bordas mais lisas, GPU bem mais carregada.",
                              "Draws at %dx%d (%dx scale) and downscales to %dx%d: smoother edges, much heavier GPU load.",
                              "Dibuja en %dx%d (escala %dx) y reduce a %dx%d: bordes mas suaves, GPU mucho mas cargada."),
                           render_w,render_h,scale,out_w,out_h);
      else
        ImGui::TextWrapped(TR("Desenha em %dx%d (escala %dx), igual a saida: sem ampliacao.",
                              "Draws at %dx%d (%dx scale), same as the output: no upscaling.",
                              "Dibuja en %dx%d (escala %dx), igual a la salida: sin escalado."),render_w,render_h,scale);
    }
    const char* modes[]={TR("Janela (Windowed)","Windowed","Ventana (Windowed)"),TR("Sem bordas (Borderless)","Borderless","Sin bordes (Borderless)"),
                         TR("Tela cheia (Fullscreen)","Fullscreen","Pantalla completa (Fullscreen)")};
    ImGui::SetNextItemWidth(220);ImGui::Combo(TR("Modo de exibicao###display","Display mode###display","Modo de pantalla###display"),&mode_,modes,3);
#if defined(_WIN32)
    ImGui::Checkbox(TR("Abrir configurador por padrao###show_on_start","Open the launcher by default###show_on_start",
                       "Abrir el configurador por defecto###show_on_start"),&show_on_start_);
#else
    ImGui::TextWrapped("%s",TR("No Linux, o configurador sempre abre primeiro. O atalho Steam abre o jogo diretamente.",
      "On Linux the launcher always opens first. The Steam shortcut starts the game directly.",
      "En Linux el configurador siempre abre primero. El acceso directo de Steam abre el juego directamente."));
#endif
    ImGui::PopID();
    }
    ImGui::EndChild();
    ImGui::EndTabItem();
    }
    if(ImGui::BeginTabItem(TR("Controles###tab_keys","Controls###tab_keys","Controles###tab_keys"))){
      DrawKeyboardTab();
      ImGui::EndTabItem();
    }else if(capture_.action>=0){
      // Left the tab while waiting for a key: drop the capture and give keyboard
      // navigation back, instead of binding a key pressed later on another tab.
      capture_.action=-1;
      if(nav_saved_){if(nav_was_on_)ImGui::GetIO().ConfigFlags|=ImGuiConfigFlags_NavEnableKeyboard;nav_saved_=false;}
    }
    if(ImGui::BeginTabItem(TR("Geracao de quadros###lsfg","Frame generation###lsfg","Generacion de cuadros###lsfg"))){
      ImGui::BeginChild("LosslessOptions",ImVec2(0,std::max(120.0f,ImGui::GetContentRegionAvail().y)),false);
      ImGui::TextWrapped("%s",TR("Lossless Scaling usa a sua instalacao da Steam. Nenhum arquivo comprado e copiado para a build.",
        "Lossless Scaling uses your Steam installation. Purchased files are not copied into this build.",
        "Lossless Scaling usa tu instalacion de Steam. No se copian archivos comprados a esta build."));
      if(ImGui::Button(TR("Procurar novamente###scan_ls","Scan again###scan_ls","Buscar de nuevo###scan_ls")))lossless_detected_=lossless::Detect();
      if(lossless_detected_.purchased_files){
        ImGui::TextWrapped(TR("Instalacao encontrada: %s","Installation found: %s","Instalacion encontrada: %s"),lossless::Utf8(lossless_detected_.folder).c_str());
      }else{
        ImGui::TextWrapped("%s",TR("Lossless Scaling nao encontrado nas bibliotecas da Steam.","Lossless Scaling was not found in your Steam libraries.","No se encontro Lossless Scaling en las bibliotecas de Steam."));
        if(ImGui::Button(TR("Comprar / ver na Steam###buy_ls","Buy / view on Steam###buy_ls","Comprar / ver en Steam###buy_ls")))SDL_OpenURL(lossless::StoreUrl);
      }
      if(ImGui::Button(TR("Instalar pela Steam###install_ls","Install through Steam###install_ls","Instalar desde Steam###install_ls")))SDL_OpenURL(lossless::InstallUrl);
#if !defined(_WIN32)
      ImGui::SeparatorText(TR("Instalacao no Linux - etapas obrigatorias","Linux installation - required steps","Instalacion en Linux - pasos necesarios"));
      ImGui::TextWrapped("%s",TR("1. Instale sua copia do Lossless Scaling na Steam. Isso sozinho nao instala a camada Vulkan.","1. Install your Steam copy of Lossless Scaling. This alone does not install the Vulkan layer.","1. Instala tu copia de Lossless Scaling desde Steam. Esto no instala la capa Vulkan."));
      ImGui::TextWrapped("%s",TR("2. Na Steam: Lossless Scaling > Propriedades > Betas > selecione lsfg-vk. Aguarde o download terminar.","2. Steam: Lossless Scaling > Properties > Betas > select lsfg-vk. Wait for the download to finish.","2. Steam: Lossless Scaling > Propiedades > Betas > selecciona lsfg-vk. Espera a que termine la descarga."));
      ImGui::TextWrapped("%s",TR("3. Instale lsfg-vk 2.x no Linux. No CachyOS/Arch, use o pacote lsfg-vk ou lsfg-vk-bin do AUR. Em outras distribuicoes, siga o guia abaixo. O AppImage nao instala pacotes do sistema.","3. Install lsfg-vk 2.x on Linux. On CachyOS/Arch, use the AUR lsfg-vk or lsfg-vk-bin package. On other distributions, follow the guide below. This AppImage does not install system packages.","3. Instala lsfg-vk 2.x en Linux. En CachyOS/Arch usa el paquete AUR lsfg-vk o lsfg-vk-bin. Para otras distribuciones sigue la guia. Este AppImage no instala paquetes del sistema."));
      if(ImGui::Button(TR("Copiar comando CachyOS/Arch###ls_install_command","Copy CachyOS/Arch command###ls_install_command","Copiar comando CachyOS/Arch###ls_install_command")))SDL_SetClipboardText("yay -S lsfg-vk-bin");
      ImGui::TextWrapped("%s",TR("4. Execute lsfg-vk-cli healthcheck para conferir a camada. Volte aqui e clique em Procurar novamente. Arquivos detectados nao garantem que o driver carregou a camada.","4. Run lsfg-vk-cli healthcheck to check the layer. Return here and Scan again. Detected files do not guarantee that the driver loaded the layer.","4. Ejecuta lsfg-vk-cli healthcheck para comprobar la capa. Vuelve aqui y busca de nuevo. Detectar archivos no garantiza que el driver cargo la capa."));
      ImGui::TextWrapped("%s",TR("Voce nao precisa abrir o aplicativo Lossless Scaling. As preferencias ficam salvas no configurador e o launcher aplica o perfil lsfg-vk ao iniciar o jogo.","You do not need to open the Lossless Scaling app. Preferences are saved by the configurator, and the launcher applies the lsfg-vk profile when the game starts.","No necesitas abrir la aplicacion Lossless Scaling. El configurador guarda las preferencias y el launcher aplica el perfil lsfg-vk al iniciar el juego."));
      ImGui::TextWrapped("%s",lossless_detected_.layer.empty() ? TR("lsfg-vk: camada Vulkan nao encontrada.","lsfg-vk: Vulkan layer not found.","lsfg-vk: capa Vulkan no encontrada.") : TR("lsfg-vk: manifesto Vulkan encontrado.","lsfg-vk: Vulkan manifest found.","lsfg-vk: manifiesto Vulkan encontrado."));
      if(lossless_detected_.purchased_files&&lossless_detected_.dll.empty())ImGui::TextWrapped("%s",TR("Na Steam, abra Propriedades > Betas do Lossless Scaling e escolha lsfg-vk. Depois procure novamente.","In Steam, open Lossless Scaling Properties > Betas and select lsfg-vk. Then scan again.","En Steam, abre Propiedades > Betas de Lossless Scaling y elige lsfg-vk. Luego busca de nuevo."));
      if(ImGui::Button(TR("Como instalar lsfg-vk###help_ls","How to install lsfg-vk###help_ls","Como instalar lsfg-vk###help_ls")))SDL_OpenURL(lossless::LinuxHelp);
#else
      if(ImGui::Button(TR("Abrir Lossless Scaling###open_ls","Open Lossless Scaling###open_ls","Abrir Lossless Scaling###open_ls")))SDL_OpenURL("steam://rungameid/993090");
      ImGui::TextWrapped("%s",TR("Voce nao precisa abrir o Lossless Scaling nem a Steam. Ao clicar em Jogar, o launcher ajusta o perfil do Hydro Thunder e roda o Lossless Scaling escondido, em segundo plano. Ele fecha junto com o jogo.",
        "You never need to open Lossless Scaling or Steam. When you press Play, the launcher sets up the Hydro Thunder profile and runs Lossless Scaling hidden in the background. It closes together with the game.",
        "No necesitas abrir Lossless Scaling ni Steam. Al pulsar Jugar, el launcher ajusta el perfil de Hydro Thunder y ejecuta Lossless Scaling oculto en segundo plano. Se cierra junto con el juego."));
#endif
      bool enabled=lossless_options_.enabled;
      if(ImGui::Checkbox(TR("Ativar Lossless Scaling###enable_ls","Enable Lossless Scaling###enable_ls","Activar Lossless Scaling###enable_ls"),&enabled)){
        if(enabled&&!lossless_detected_.Ready()){
          // A request never silently succeeds while required components are missing.
          SDL_OpenURL(!lossless_detected_.purchased_files ? lossless::StoreUrl :
#if defined(_WIN32)
            lossless::InstallUrl
#else
            lossless::LinuxHelp
#endif
          );
        }else{lossless_options_.enabled=enabled;lossless::Save(lossless_options_);}
      }
      int multiplier=lossless_options_.multiplier-2;
      const char* multipliers[]={"2x","3x","4x"};
      ImGui::SetNextItemWidth(160);
      if(ImGui::Combo(TR("Multiplicador###ls_mult","Multiplier###ls_mult","Multiplicador###ls_mult"),&multiplier,multipliers,3)){lossless_options_.multiplier=multiplier+2;lossless::Save(lossless_options_);}
      if(ImGui::Checkbox(TR("Permitir acima dos Hz (experimental, sem forcar cortes)###ls_above","Allow above display Hz (experimental, no forced tearing)###ls_above","Permitir superar Hz (experimental, sin forzar cortes)###ls_above"),&lossless_options_.above_refresh))lossless::Save(lossless_options_);
#if defined(_WIN32)
      ImGui::TextWrapped("%s",TR("Sem esta opcao, o total fica preso nos Hz da tela: em 2x numa tela de 75 Hz o jogo cai para ~37 quadros reais. Com ela, os 60 quadros do jogo continuam e o total vai a ~120 (medido). A tela continua mostrando no maximo os seus Hz e a imagem pode rasgar.","Without this option the total is held to the display Hz: at 2x on a 75 Hz screen the game drops to ~37 real frames. With it, the game keeps its 60 frames and the total reaches ~120 (measured). The screen still shows at most its Hz and the image may tear.","Sin esta opcion el total queda limitado a los Hz de la pantalla: en 2x a 75 Hz el juego baja a ~37 cuadros reales. Con ella, el juego mantiene sus 60 cuadros y el total llega a ~120 (medido). La pantalla sigue mostrando como maximo sus Hz y la imagen puede cortarse."));
#else
      ImGui::TextWrapped("%s",TR("Uma TV de 60 Hz mostra no maximo 60 atualizacoes por segundo. Este modo tenta retirar a espera pelo V-Sync do gerador, sem forcar tearing. O compositor pode manter a imagem sem cortes e descartar quadros, ou ainda limitar a saida. Nao garante 100 imagens diferentes por segundo numa tela de 60 Hz.","A 60 Hz TV displays at most 60 updates per second. This mode tries to remove the generator V-Sync wait without forcing tearing. The compositor may discard frames without tearing, or still limit output. It cannot guarantee 100 distinct displayed images per second on a 60 Hz panel.","Una TV de 60 Hz muestra como maximo 60 actualizaciones por segundo. Este modo intenta quitar la espera V-Sync del generador sin forzar tearing. El compositor puede descartar cuadros sin cortes o limitar la salida. No garantiza 100 imagenes distintas en una pantalla de 60 Hz."));
#endif
#if !defined(_WIN32)
      ImGui::TextWrapped("%s",TR("No lsfg-vk 2.x, este teste desliga a substituicao por FIFO. O unico pacing documentado ainda e vsync: se houver quadros pulados ou piora, desligue esta opcao.","On lsfg-vk 2.x, this test disables the FIFO override. The only documented pacing mode is still vsync: turn this option off if frames skip or pacing gets worse.","En lsfg-vk 2.x se desactiva la sustitucion por FIFO. El unico pacing documentado sigue siendo vsync: desactiva esta opcion si se saltan cuadros o empeora el ritmo."));
      if(ImGui::Checkbox(TR("Modelo leve LSFG (pode aumentar artefatos)###ls_performance","Lighter LSFG model (may increase artifacts)###ls_performance","Modelo LSFG ligero (puede aumentar artefactos)###ls_performance"),&lossless_options_.performance))lossless::Save(lossless_options_);
#endif
      ImGui::TextWrapped("%s",TR("Os FPS do motor continuam sendo definidos em Video. Quadros gerados nao aumentam a velocidade do jogo e exigem folga de GPU. No Windows, o overlay do jogo (F6) mostra \"Lossless ATIVO\" quando a geracao esta funcionando; use Janela ou Sem bordas. No Linux, confira no MangoHud.",
        "Engine FPS are still selected in Video. Generated frames do not speed up the game and need GPU headroom. On Windows the game overlay (F6) shows \"Lossless ATIVO\" while generation is working; use Windowed or Borderless. On Linux, check MangoHud.",
        "Los FPS del motor se eligen en Video. Los cuadros generados no aceleran el juego y necesitan margen de GPU. En Windows el overlay del juego (F6) muestra \"Lossless ATIVO\" cuando la generacion funciona; usa Ventana o Sin bordes. En Linux, revisa MangoHud."));
      ImGui::EndChild();
      ImGui::EndTabItem();
    }
    ImGui::EndTabBar();
    ImGui::EndChild();
    {
      // Save every change immediately, so starting the game directly (Steam
      // shortcut, desktop shortcut, AppImage) uses what was chosen here even
      // if "Jogar" is never pressed.
      const auto snapshot=std::make_tuple(async_,show_fps_,upscaler_,show_on_start_,mode_,language_,
                                          resolution_,fps_,performance_,render_quality_,sharpness_,fxaa_,ui_lang_);
      if(!snapshot_valid_){last_saved_=snapshot;snapshot_valid_=true;}
      else if(snapshot!=last_saved_){SaveAll();last_saved_=snapshot;}
    }
    ImGui::Separator();
    if(!status.empty())ImGui::TextWrapped("%s",status.c_str());
    else ImGui::Dummy(ImVec2(0,15));
    ImGui::BeginDisabled(state_->busy || !state_->succeeded);
    ImGui::PushStyleColor(ImGuiCol_Button,ImVec4(.28f,.77f,.95f,1));
    ImGui::PushStyleColor(ImGuiCol_Text,ImVec4(.025f,.075f,.12f,1));
    if(ImGui::Button(TR("Jogar###play","Play###play","Jugar###play"),ImVec2(185,46))) {
      SaveAll();
      try{
#if defined(_WIN32)
        // Surface setup errors in the launcher instead of closing it first.
        lossless::ConfigureWindows(lossless_options_);
#endif
        LaunchGame(binary_root_);
        SDL_Event quit{};quit.type=SDL_EVENT_QUIT;SDL_PushEvent(&quit);
      }catch(const std::exception& error){std::lock_guard lock(state_->mutex);state_->status=error.what();}
    }
    ImGui::PopStyleColor(2);
    ImGui::EndDisabled();
    // Every race key with the player's current choice, including jump (X button) and
    // brake (LT), which players could not find. Changed in the Controls tab.
    {auto key=[&](int action){return keys::Label(binds_[action][0],g_ui_lang);};
     const std::string hint=key(0)+TR(": acelerar | ",": throttle | ",": acelerar | ")+key(1)+TR(": freio | ",": brake | ",": freno | ")+
       key(2)+TR(": impulso | ",": boost | ",": impulso | ")+key(3)+TR(": pulo | ",": jump | ",": salto | ")+key(10)+": Start";
     ImGui::SameLine();ImGui::TextDisabled("%s",hint.c_str());}
    ImGui::PopStyleVar();
    ImGui::End();
    ImGui::PopStyleVar(2);
  }
  void SaveAll() {
    ConfigEdit edit(paths_.config_path);
    auto SaveValue=[&](const std::filesystem::path&,const std::string& key,const std::string& value){edit.Set(key,value);};
    auto SaveBool=[&](const std::filesystem::path&,const std::string& key,bool value){edit.Set(key,value?"true":"false");};
    rex::cvar::SetFlagByName("async_shader_compilation",async_ ? "true" : "false");
    SaveBool(paths_.config_path,"async_shader_compilation",async_);
    SaveBool(paths_.config_path,"ht_show_fps",show_fps_);
    SaveValue(paths_.config_path,"present_effect",
              upscaler_==2 ? "\"fsr3\"" : upscaler_==1 ? "\"fsr\"" : "\"bilinear\"");
    const Sharpness sharp=SharpnessFor(sharpness_);
    SaveValue(paths_.config_path,"ht_sharpness",std::to_string(sharpness_));
    SaveValue(paths_.config_path,"present_fsr_sharpness_reduction",Fixed(sharp.rcas_reduction));
    SaveValue(paths_.config_path,"present_fsr3_sharpness",Fixed(upscaler_==2 ? sharp.fsr3 : 0.0f));
    SaveValue(paths_.config_path,"present_cas_additional_sharpness",Fixed(sharp.cas));
    SaveBool(paths_.config_path,"ht_import_on_start",
#if defined(_WIN32)
        show_on_start_
#else
        true
#endif
    );
    game_fullscreen_=mode_!=0;
    SaveBool(paths_.config_path,"ht_game_fullscreen",game_fullscreen_);
    SaveBool(paths_.config_path,"fullscreen",game_fullscreen_);
    rex::cvar::SetFlagByName("user_language",std::to_string(language_+1));
    SaveValue(paths_.config_path,"user_language",std::to_string(language_+1));
    SaveValue(paths_.config_path,"fullscreen_mode",mode_==2 ? "\"exclusive\"" : "\"borderless\"");
    SaveValue(paths_.config_path,"window_width",std::to_string(kOutputWidths[resolution_]));
    SaveValue(paths_.config_path,"ht_output_width",std::to_string(kOutputWidths[resolution_]));
    SaveValue(paths_.config_path,"window_height",std::to_string(kOutputHeights[resolution_]));
    SaveValue(paths_.config_path,"ht_render_quality",std::to_string(render_quality_));
    SaveValue(paths_.config_path,"resolution_scale",
              std::to_string(RenderScale(render_quality_,kOutputHeights[resolution_])));
    // Engine clock and swap limiter move together: the game draws one frame
    // per guest vblank, and the limiter only guards against bursts.
    SaveValue(paths_.config_path,"video_mode_refresh_rate",std::to_string(video::ClockRate(fps_))+".0");
    SaveValue(paths_.config_path,"fps_limit",std::to_string(video::SwapLimit(fps_)));
    SaveBool(paths_.config_path,"vsync",true);
    SaveValue(paths_.config_path,"ht_graphics_profile",performance_ ? "\"performance\"" : "\"quality\"");
    SaveValue(paths_.config_path,"anisotropic_override",performance_ ? "-1" : "4");
    SaveBool(paths_.config_path,"gamma_render_target_as_unorm16",!performance_);
    // -1 = automatic (75% of logical cores), as in Xenia.
    SaveValue(paths_.config_path,"vulkan_pipeline_creation_threads",performance_ ? "3" : "-1");
    SaveValue(paths_.config_path,"texture_cache_memory_limit_soft",performance_ ? "256" : "384");
    SaveValue(paths_.config_path,"texture_cache_memory_limit_hard",performance_ ? "512" : "768");
    // FXAA is opt-in: on the 720p frame it softens stair-steps but doubles the
    // outline of small HUD digits once the frame is upscaled.
    SaveValue(paths_.config_path,"swap_post_effect",fxaa_ ? "\"fxaa\"" : "\"\"");
    SaveValue(paths_.config_path,"ht_ui_language",std::string("\"")+kUiLanguageCodes[std::clamp(ui_lang_,0,2)]+"\"");
    // Keyboard is always a controller in this game; the binds are the player's.
    SaveBool(paths_.config_path,"mnk_mode",true);
    for(int i=0;i<keys::kActionCount;++i)SaveValue(paths_.config_path,keys::kActions[i].cvar,keys::Value(binds_,i));
    edit.Commit();
  }
  // Steam shortcut: with known accounts, pick one in a popup; none found asks for the
  // Steam userdata folder.
  void AddToSteam(){
    if(steam_profiles_.empty())SDL_ShowOpenFolderDialog(Selected,new Picker{state_,-3},nullptr,nullptr,false);
    else ImGui::OpenPopup("###steam_account");
  }
  void DrawSteamAccountPopup(){
    if(!ImGui::BeginPopup("###steam_account"))return;
    ImGui::TextUnformatted(TR("Feche a Steam antes de exportar. Escolha a conta:","Close Steam before exporting. Pick the account:",
                              "Cierra Steam antes de exportar. Elige la cuenta:"));
    for(const auto& profile:steam_profiles_){
      const auto label=std::string(TR("Conta ","Account ","Cuenta "))+profile.parent_path().filename().string();
      if(ImGui::Selectable(label.c_str())){std::lock_guard lock(state_->mutex);state_->save_folder=steam::Utf8(profile);state_->save_action=3;}
    }
    ImGui::EndPopup();
  }
  void DrawKeyboardTab(){
    ImGuiIO& io=ImGui::GetIO();
    // While waiting for a key, keyboard navigation is off: otherwise Space/Enter would
    // also press the focused button and start a new capture.
    if(capture_.action>=0){
      if(!nav_saved_){nav_was_on_=(io.ConfigFlags&ImGuiConfigFlags_NavEnableKeyboard)!=0;nav_saved_=true;}
      io.ConfigFlags&=~ImGuiConfigFlags_NavEnableKeyboard;
      bool cancel=false;
      if(const char* key=keys::PressedKey(cancel)){keys::Assign(binds_,capture_.action,capture_.slot,key);capture_.action=-1;SaveAll();}
      else if(cancel)capture_.action=-1;
    }else if(nav_saved_){
      if(nav_was_on_)io.ConfigFlags|=ImGuiConfigFlags_NavEnableKeyboard;
      nav_saved_=false;
    }
    ImGui::BeginChild("KeyboardBinds",ImVec2(0,std::max(120.0f,ImGui::GetContentRegionAvail().y)),false);
    ImGui::TextWrapped("%s",TR("Clique numa tecla e aperte a nova. Cada acao aceita duas teclas. Se a tecla ja estiver em outra acao, as duas trocam. Esc cancela. Um controle (Xbox/PlayStation) continua funcionando junto.",
      "Click a key and press the new one. Each action takes two keys. If the key is already used by another action, the two swap. Esc cancels. A controller (Xbox/PlayStation) keeps working too.",
      "Haz clic en una tecla y pulsa la nueva. Cada accion admite dos teclas. Si la tecla ya esta en otra accion, se intercambian. Esc cancela. Un mando (Xbox/PlayStation) sigue funcionando."));
    ImGui::Spacing();
    if(ImGui::BeginTable("##binds",3,ImGuiTableFlags_RowBg|ImGuiTableFlags_SizingStretchProp)){
      ImGui::TableSetupColumn(TR("Acao","Action","Accion"),ImGuiTableColumnFlags_WidthStretch,1.6f);
      ImGui::TableSetupColumn(TR("Tecla","Key","Tecla"),ImGuiTableColumnFlags_WidthStretch,1.0f);
      ImGui::TableSetupColumn(TR("Alternativa","Alternative","Alternativa"),ImGuiTableColumnFlags_WidthStretch,1.0f);
      ImGui::TableHeadersRow();
      for(int i=0;i<keys::kActionCount;++i){
        const auto& action=keys::kActions[i];
        ImGui::TableNextRow();ImGui::TableNextColumn();
        ImGui::AlignTextToFramePadding();ImGui::TextUnformatted(TR(action.pt,action.en,action.es));
        for(int slot=0;slot<2;++slot){
          ImGui::TableNextColumn();ImGui::PushID(i*2+slot);
          const bool waiting=capture_.action==i&&capture_.slot==slot;
          const std::string label=waiting ? TR("Aperte uma tecla...","Press a key...","Pulsa una tecla...") :
            binds_[i][slot].empty() ? "-" : keys::Label(binds_[i][slot],g_ui_lang);
          if(waiting)ImGui::PushStyleColor(ImGuiCol_Button,ImVec4(.28f,.77f,.95f,1));
          if(ImGui::Button(label.c_str(),ImVec2(slot==0 ? -1 : -40,0))&&capture_.action<0)capture_={i,slot};
          if(waiting)ImGui::PopStyleColor();
          if(slot==1&&!binds_[i][1].empty()){
            ImGui::SameLine();
            if(ImGui::SmallButton("x")&&capture_.action<0){binds_[i][1].clear();SaveAll();}
          }
          ImGui::PopID();
        }
      }
      ImGui::EndTable();
    }
    ImGui::Spacing();
    if(ImGui::Button(TR("Restaurar padrao###keys_default","Restore defaults###keys_default","Restaurar valores###keys_default"))&&capture_.action<0){binds_=keys::Defaults();SaveAll();}
    ImGui::EndChild();
  }
 private:
  std::tuple<bool,bool,int,bool,int,int,int,int,bool,int,int,bool,int> last_saved_{};
  bool snapshot_valid_=false;
  bool manage_install_=false,art_attempted_=false,save_actions_requested_=false;
  std::unique_ptr<rex::ui::ImmediateTexture> header_art_,header_logo_;
  rex::PathConfig paths_;
  std::filesystem::path root_;
  std::filesystem::path binary_root_;
  std::function<void(rex::PathConfig)> resume_;
  std::shared_ptr<State> state_;
  std::thread worker_;
  bool async_=true;
  bool game_fullscreen_=true;
  bool show_on_start_=true;
  int language_=0;
  int fps_=0,mode_=1,resolution_=0,render_quality_=1,sharpness_=7,ui_lang_=0;
  bool fxaa_=false;
  bool show_fps_=false;
  keys::Binds binds_=keys::Load();
  keys::Capture capture_;
  bool nav_saved_=false,nav_was_on_=false;
  int upscaler_=0;
  bool performance_=false;
  lossless::Options lossless_options_;
  lossless::Detection lossless_detected_;
  std::vector<std::filesystem::path> steam_profiles_;
};
}
void ShowSetupWizard(rex::ui::ImGuiDrawer* drawer,const rex::PathConfig& paths,
                     std::function<void(rex::PathConfig)> resume) {
  new SetupWizard(drawer,paths,std::move(resume));
}
}
