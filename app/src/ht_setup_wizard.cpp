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
#include "ht_user_data.h"
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
  std::atomic<bool> user_data_changed{false};
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
// ---- Look: organized like the UFC3MONS launcher (header art, flag language selector,
// tabs on the art, cards, footer with Steam + PLAY), in Hydro Thunder's blue. ----
const ImVec4 kAccent(.12f,.55f,.88f,1),kAccentHover(.19f,.65f,.96f,1),kAccentActive(.08f,.44f,.74f,1);
const ImVec4 kGreen(.30f,.86f,.62f,1),kMuted(.66f,.73f,.80f,1),kWarn(1,.66f,.38f,1);
ImU32 Col(float r,float g,float b,float a=1){return ImGui::GetColorU32(ImVec4(r,g,b,a));}
void PushSize(float px){ImGui::PushFont(nullptr,px);}
void CheckMark(ImDrawList* draw,ImVec2 c,float r,ImU32 color,float thickness){ImVec2 pts[3]={ImVec2(c.x-r*.45f,c.y+r*.02f),ImVec2(c.x-r*.12f,c.y+r*.36f),ImVec2(c.x+r*.48f,c.y-r*.34f)};draw->AddPolyline(pts,3,color,ImDrawFlags_None,thickness);}
void FilledCheck(ImDrawList* draw,ImVec2 c,float r){draw->AddCircleFilled(c,r,Col(.16f,.70f,.48f),32);CheckMark(draw,c,r,Col(.03f,.08f,.06f),r*.24f);}
void Warning(ImDrawList* draw,ImVec2 c,float r){draw->AddCircleFilled(c,r,Col(.92f,.55f,.12f),32);draw->AddLine(ImVec2(c.x,c.y-r*.5f),ImVec2(c.x,c.y+r*.15f),Col(.08f,.05f,.02f),r*.22f);draw->AddCircleFilled(ImVec2(c.x,c.y+r*.45f),r*.13f,Col(.08f,.05f,.02f));}
void Flag(ImDrawList* draw,ImVec2 p,int language){
  const float w=24,h=16;ImVec2 q(p.x+w,p.y+h);
  if(language==0){draw->AddRectFilled(p,q,IM_COL32(0,151,57,255),2);ImVec2 c(p.x+w/2,p.y+h/2);draw->AddQuadFilled(ImVec2(c.x,p.y+1.5f),ImVec2(q.x-2.5f,c.y),ImVec2(c.x,q.y-1.5f),ImVec2(p.x+2.5f,c.y),IM_COL32(254,221,0,255));draw->AddCircleFilled(c,h*.27f,IM_COL32(1,33,105,255),20);}
  else if(language==1){draw->AddRectFilled(p,q,IM_COL32(255,255,255,255),2);for(int i=0;i<7;i+=2)draw->AddRectFilled(ImVec2(p.x,p.y+i*h/7),ImVec2(q.x,p.y+(i+1)*h/7),IM_COL32(178,34,52,255));draw->AddRectFilled(p,ImVec2(p.x+w*.45f,p.y+h*4/7),IM_COL32(60,59,110,255));}
  else{draw->AddRectFilled(p,q,IM_COL32(170,21,27,255),2);draw->AddRectFilled(ImVec2(p.x,p.y+h/4),ImVec2(q.x,p.y+h*3/4),IM_COL32(241,191,0,255));}
  draw->AddRect(p,q,IM_COL32(0,0,0,90),2);
}
bool StyledButton(const char* label,ImVec2 size,bool primary){
  if(primary){ImGui::PushStyleColor(ImGuiCol_Button,kAccent);ImGui::PushStyleColor(ImGuiCol_ButtonHovered,kAccentHover);ImGui::PushStyleColor(ImGuiCol_ButtonActive,kAccentActive);ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize,0);}
  else{ImGui::PushStyleColor(ImGuiCol_Button,ImVec4(.09f,.13f,.18f,1));ImGui::PushStyleColor(ImGuiCol_ButtonHovered,ImVec4(.14f,.21f,.29f,1));ImGui::PushStyleColor(ImGuiCol_ButtonActive,ImVec4(.18f,.27f,.37f,1));ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize,1);}
  ImGui::PushStyleColor(ImGuiCol_Border,ImVec4(.24f,.33f,.43f,1));
  const bool pressed=ImGui::Button(label,size);ImGui::PopStyleColor(4);ImGui::PopStyleVar();return pressed;
}
void Muted(const char* text){ImGui::PushStyleColor(ImGuiCol_Text,kMuted);ImGui::TextWrapped("%s",text);ImGui::PopStyleColor();}
void Colored(const ImVec4& color,const char* text){ImGui::PushStyleColor(ImGuiCol_Text,color);ImGui::TextWrapped("%s",text);ImGui::PopStyleColor();}
void CardTitle(const char* title,const char* subtitle){PushSize(22);ImGui::TextUnformatted(title);ImGui::PopFont();if(subtitle&&*subtitle)Muted(subtitle);}
void BeginCard(const char* id,ImVec2 size,ImGuiWindowFlags flags=0){
  ImGui::PushStyleColor(ImGuiCol_ChildBg,ImVec4(.055f,.085f,.125f,1));ImGui::PushStyleColor(ImGuiCol_Border,ImVec4(.15f,.23f,.32f,1));
  ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding,8);ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,ImVec2(16,12));ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize,1);
  ImGui::BeginChild(id,size,ImGuiChildFlags_Borders|ImGuiChildFlags_AlwaysUseWindowPadding|(size.y==0?ImGuiChildFlags_AutoResizeY:0),flags);
}
void EndCard(){ImGui::EndChild();ImGui::PopStyleVar(3);ImGui::PopStyleColor(2);}
// Opens a folder in the system file manager (Explorer / the desktop's default).
void OpenFolder(const std::filesystem::path& folder){
  std::error_code ec;std::filesystem::create_directories(folder,ec);
  auto u8=std::filesystem::absolute(folder,ec).u8string();std::string url="file:///";
  for(const auto c8:u8){const unsigned char c=static_cast<unsigned char>(c8);
    if(c=='\\')url+='/';
    else if(c<=0x20||c>=0x7F||c=='%'||c=='#'||c=='?'){char hex[4];std::snprintf(hex,sizeof hex,"%%%02X",c);url+=hex;}
    else url+=char(c);}
  SDL_OpenURL(url.c_str());
}
std::string U8(const std::filesystem::path& p){return rex::path_to_utf8(p);}
const char* kLanguageNames[]={"Português (Brasil)","English","Español"};

class SetupWizard final : public rex::ui::ImGuiDialog {
 public:
  SetupWizard(rex::ui::ImGuiDrawer* drawer, rex::PathConfig paths,
              std::function<void(rex::PathConfig)> resume)
      : ImGuiDialog(drawer), paths_(std::move(paths)), resume_(std::move(resume)),
        state_(std::make_shared<State>()) {
    // The library (game) folder; user_data may live elsewhere now, so not its parent.
    root_=paths_.game_data_root.filename()=="game_data" ? paths_.game_data_root.parent_path() : paths_.game_data_root;
    binary_root_=rex::filesystem::GetExecutableFolder();
    user_data_=UserDataRoot(root_);
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
    if(const char* tab=std::getenv("HT_LAUNCHER_TAB"))select_tab_=std::clamp(std::atoi(tab),0,4);  // screenshots only
    // Tests only: act as if a user_data folder was picked (or "Back to default" pressed),
    // and answer the "folder already has data" question with bring/use.
    if(const char* pick=std::getenv("HT_TEST_PICK_USER_DATA")){state_->save_folder=pick;state_->save_action=std::string(pick)=="default"?8:7;select_tab_=3;}
    if(const char* answer=std::getenv("HT_TEST_USER_DATA_ANSWER"))test_answer_=answer;
    if(const char* xenia=std::getenv("HT_TEST_IMPORT_XENIA")){state_->save_folder=xenia;state_->save_action=9;select_tab_=3;}
  }
  ~SetupWizard() override { if(worker_.joinable()) worker_.join(); }
 protected:
  void OnDraw(ImGuiIO& io) override {
    HandlePickedFolders();
    if(state_->user_data_changed.exchange(false))user_data_=UserDataRoot(root_);
    ImGui::SetNextWindowPos(ImVec2(0,0),ImGuiCond_Always);
    ImGui::SetNextWindowSize(io.DisplaySize,ImGuiCond_Always);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding,0);ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,ImVec2(0,0));ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,ImVec2(10,8));
    ImGui::PushStyleColor(ImGuiCol_WindowBg,ImVec4(.035f,.055f,.082f,1));
    ImGui::PushStyleColor(ImGuiCol_FrameBg,ImVec4(.08f,.12f,.17f,1));ImGui::PushStyleColor(ImGuiCol_FrameBgHovered,ImVec4(.12f,.18f,.25f,1));ImGui::PushStyleColor(ImGuiCol_FrameBgActive,ImVec4(.15f,.23f,.32f,1));
    ImGui::PushStyleColor(ImGuiCol_Header,ImVec4(.08f,.30f,.50f,1));ImGui::PushStyleColor(ImGuiCol_HeaderHovered,ImVec4(.11f,.42f,.68f,1));ImGui::PushStyleColor(ImGuiCol_CheckMark,kAccentHover);
    ImGui::PushStyleColor(ImGuiCol_SliderGrab,kAccent);ImGui::PushStyleColor(ImGuiCol_PopupBg,ImVec4(.05f,.075f,.11f,.98f));ImGui::PushStyleColor(ImGuiCol_ModalWindowDimBg,ImVec4(0,0,0,.55f));
    ImGui::Begin("###ht_setup",nullptr,ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoSavedSettings|ImGuiWindowFlags_NoScrollbar|ImGuiWindowFlags_NoScrollWithMouse);
    const float header=DrawHeader(io);
    const float side=std::clamp(io.DisplaySize.x*.025f,14.f,40.f);
    // Tabs sit on the lower edge of the art, as in the UFC layout.
    ImGui::SetCursorPos(ImVec2(side,header-38));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,ImVec2(18,10));ImGui::PushStyleVar(ImGuiStyleVar_TabRounding,7);ImGui::PushStyleVar(ImGuiStyleVar_TabBarBorderSize,0);
    ImGui::PushStyleColor(ImGuiCol_Tab,ImVec4(.06f,.09f,.13f,.92f));ImGui::PushStyleColor(ImGuiCol_TabHovered,ImVec4(.10f,.36f,.58f,1));ImGui::PushStyleColor(ImGuiCol_TabSelected,kAccent);ImGui::PushStyleColor(ImGuiCol_TabSelectedOverline,kAccent);
    ImGui::PushItemWidth(io.DisplaySize.x-2*side);
    if(ImGui::BeginTabBar("Configurador")){
      const char* names[]={TR("Início###home","Home###home","Inicio###home"),TR("Vídeo###video","Video###video","Vídeo###video"),
                           TR("Controles###keys","Controls###keys","Controles###keys"),TR("Saves e registros###saves","Saves and logs###saves","Partidas y registros###saves"),
                           TR("Geração de quadros###lsfg","Frame generation###lsfg","Generación de cuadros###lsfg")};
      for(int i=0;i<5;++i){
        if(ImGui::BeginTabItem(names[i],nullptr,select_tab_==i?ImGuiTabItemFlags_SetSelected:0)){tab_=i;ImGui::EndTabItem();}
      }
      select_tab_=-1;
      ImGui::EndTabBar();
    }
    ImGui::PopItemWidth();ImGui::PopStyleColor(4);ImGui::PopStyleVar(3);
    {auto* draw=ImGui::GetWindowDrawList();auto o=ImGui::GetWindowPos();draw->AddLine(ImVec2(o.x,o.y+header),ImVec2(o.x+io.DisplaySize.x,o.y+header),ImGui::GetColorU32(kAccent),2);}
    if(tab_!=2&&capture_.action>=0){
      // Left the tab while waiting for a key: drop the capture and give keyboard
      // navigation back, instead of binding a key pressed later on another tab.
      capture_.action=-1;
      if(nav_saved_){if(nav_was_on_)ImGui::GetIO().ConfigFlags|=ImGuiConfigFlags_NavEnableKeyboard;nav_saved_=false;}
    }
    ImGui::SetCursorPos(ImVec2(side,header+12));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,ImVec2(10,7));
    ImGui::BeginChild("LauncherBody",ImVec2(io.DisplaySize.x-2*side,std::max(120.f,io.DisplaySize.y-header-12-74-10)),ImGuiChildFlags_None,ImGuiWindowFlags_NoBackground);
    switch(tab_){case 0:Home();break;case 1:Video();break;case 2:Controls();break;case 3:Saves();break;default:FrameGeneration();break;}
    ImGui::EndChild();
    Footer(io);
    UserDataModal();
    ImGui::PopStyleVar();
    ImGui::End();ImGui::PopStyleColor(10);ImGui::PopStyleVar(3);
    {
      // Save every change immediately, so starting the game directly (Steam
      // shortcut, desktop shortcut, AppImage) uses what was chosen here even
      // if "Jogar" is never pressed.
      const auto snapshot=std::make_tuple(async_,show_fps_,upscaler_,show_on_start_,mode_,language_,
                                          resolution_,fps_,performance_,render_quality_,sharpness_,fxaa_,ui_lang_);
      if(!snapshot_valid_){last_saved_=snapshot;snapshot_valid_=true;}
      else if(snapshot!=last_saved_){SaveAll();last_saved_=snapshot;}
    }
  }
  // Folder/file pickers answer on another thread; their choice is acted on here.
  void HandlePickedFolders(){
    std::string save_folder; int save_action=0;
    { std::lock_guard lock(state_->mutex);if(!state_->busy){save_action=state_->save_action;save_folder=state_->save_folder;state_->save_action=0;} }
    if(!save_action)return;
    if(save_action==4){
      auto selected=std::filesystem::u8path(save_folder);
      if(selected.filename()=="game_data" && std::filesystem::is_directory(selected.parent_path()/"user_data"))selected=selected.parent_path();
      try{
        if(!ReadyForLaunch(selected))throw std::runtime_error(TR("Selecione uma pasta com o jogo e o update preparados. Para pacotes originais, use as etapas Jogo/DLC/Update.",
          "Select a folder with the game and update already prepared. For original packages, use the Game/DLC/Update steps.",
          "Selecciona una carpeta con el juego y la actualización ya preparados. Para paquetes originales, usa los pasos Juego/DLC/Update."));
        RememberLibrary(selected);root_=selected;paths_.cache_root=ShaderCacheDirectory(root_);state_->succeeded=true;user_data_=UserDataRoot(root_);
        SetStatus(std::string(TR("Pasta do jogo registrada, sem copiar arquivos: ","Game folder registered, no files copied: ","Carpeta del juego registrada, sin copiar archivos: "))+U8(root_));
      }catch(const std::exception& error){SetStatus(error.what());}
      return;
    }
    if(save_action==8){RequestUserData(DefaultUserDataRoot(root_),true);return;}
    if(save_action==7){
      // A folder for user_data was picked.
      const auto target=userdata::TargetFor(std::filesystem::u8path(save_folder));
      RequestUserData(target,userdata::Same(target,DefaultUserDataRoot(root_)));
      return;
    }
    if(worker_.joinable())worker_.join();
    state_->busy=true;
    worker_=std::thread([state=state_,root=root_,binary=binary_root_,save_action,save_folder]{
      std::string message;
      try {
        if(save_action==1){auto path=saves::Export(root,std::filesystem::u8path(save_folder));message=std::string(TR("Cópia dos saves criada em: ","Saves backup created in: ","Copia de partidas creada en: "))+rex::path_to_utf8(path);}
        else if(save_action==2){saves::Import(root,std::filesystem::u8path(save_folder));message=TR("Saves importados. Os saves anteriores foram guardados em save_backups.",
          "Saves imported. The previous saves were kept in save_backups.","Partidas importadas. Las anteriores se guardaron en save_backups.");}
        else if(save_action==3){steam::Export(binary,std::filesystem::u8path(save_folder));message=TR("Atalho criado. Abra a Steam: o jogo inicia direto, sem passar pelo configurador.",
          "Shortcut created. Open Steam: the game starts directly, skipping the launcher.","Acceso directo creado. Abre Steam: el juego inicia directamente, sin pasar por el configurador.");}
        else if(save_action==5){const auto count=ClearShaderCache(std::filesystem::u8path(save_folder));message=std::string(TR("Cache de shaders limpo: ","Shader cache cleared: ","Caché de shaders limpia: "))+std::to_string(count)+
          TR(" arquivos. Será recompilado na próxima corrida; saves preservados."," files. It will be rebuilt in the next race; saves kept."," archivos. Se recompilará en la próxima carrera; partidas conservadas.");}
        else if(save_action==9){const auto source=saves::ImportXenia(root,std::filesystem::u8path(save_folder));
          (void)source;message=TR("Save do Xenia importado. O save anterior foi guardado em save_backups.",
            "Xenia save imported. The previous save was kept in save_backups.","Partida de Xenia importada. La anterior se guardó en save_backups.");}
        else if(save_action==6){
          const std::array<std::filesystem::path,3> sources{std::filesystem::path{},std::filesystem::u8path(save_folder),std::filesystem::path{}};
          PrepareContent(root,sources,[state](std::string text){std::lock_guard lock(state->mutex);state->status=std::move(text);});
          message=TR("DLC instalada. Ela aparece no jogo na próxima abertura.","DLC installed. It shows up in the game the next time it starts.","DLC instalado. Aparece en el juego la próxima vez que se abra.");
        }
      }catch(const std::exception& error){message=error.what();}
      {std::lock_guard lock(state->mutex);state->status=std::move(message);}
      state->busy=false;
    });
  }
  void SetStatus(std::string text){std::lock_guard lock(state_->mutex);state_->status=std::move(text);}
  // Moving user_data: when both places hold data, the player chooses which to keep.
  void RequestUserData(const std::filesystem::path& target,bool to_default){
    if(userdata::Same(target,user_data_)){SetStatus(TR("Os dados já estão nessa pasta.","The data is already in that folder.","Los datos ya están en esa carpeta."));return;}
    pending_target_=target;pending_default_=to_default;
    if(userdata::HasData(target)&&userdata::HasData(user_data_)){
      if(test_answer_=="bring")StartUserData(userdata::Mode::kCopyCurrent);
      else if(test_answer_=="use")StartUserData(userdata::Mode::kUseExisting);
      else open_user_data_modal_=true;
    }
    else StartUserData(userdata::Mode::kCopyCurrent);
  }
  void StartUserData(userdata::Mode mode){
    if(worker_.joinable())worker_.join();
    state_->busy=true;
    SetStatus(TR("Copiando a pasta de dados... aguarde.","Copying the data folder... please wait.","Copiando la carpeta de datos... espera."));
    worker_=std::thread([state=state_,root=root_,target=pending_target_,to_default=pending_default_,mode]{
      std::string message;
      try{
        const auto r=userdata::Change(root,target,to_default,mode);
        if(r.used_existing)message=TR("Pasta trocada. Os dados que já estavam lá serão usados; a pasta anterior não foi alterada.",
          "Folder changed. The data already there will be used; the previous folder was not changed.",
          "Carpeta cambiada. Se usarán los datos que ya estaban allí; la carpeta anterior no se modificó.");
        else{
          message=std::string(TR("Pasta trocada: ","Folder changed: ","Carpeta cambiada: "))+std::to_string(r.files)+
            TR(" arquivos copiados. A pasta anterior ficou como cópia de segurança."," files copied. The previous folder was kept as a backup."," archivos copiados. La carpeta anterior quedó como copia de seguridad.");
          if(!r.kept_aside.empty())message+=TR(" O que havia no destino virou user_data-anterior."," What was at the destination became user_data-anterior."," Lo que había en el destino pasó a user_data-anterior.");
        }
      }catch(const std::exception& error){message=error.what();}
      {std::lock_guard lock(state->mutex);state->status=std::move(message);}
      state->user_data_changed=true;state->busy=false;
    });
  }
  void UserDataModal(){
    if(open_user_data_modal_){ImGui::OpenPopup("###user_data_choice");open_user_data_modal_=false;}
    ImGui::SetNextWindowSize(ImVec2(560,0));
    if(!ImGui::BeginPopupModal(TR("A pasta escolhida já tem dados###user_data_choice","The chosen folder already has data###user_data_choice","La carpeta elegida ya tiene datos###user_data_choice"),nullptr,ImGuiWindowFlags_NoResize))return;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,ImVec2(16,12));
    ImGui::TextWrapped("%s",U8(pending_target_).c_str());
    ImGui::Spacing();
    ImGui::TextWrapped("%s",TR("Já existe uma pasta user_data com arquivos nesse lugar. O que você quer fazer? Nada será apagado.",
      "There is already a user_data folder with files there. What do you want to do? Nothing will be deleted.",
      "Ya hay una carpeta user_data con archivos allí. ¿Qué quieres hacer? No se borrará nada."));
    ImGui::Spacing();
    if(StyledButton(TR("Levar meus dados atuais###bring","Bring my current data###bring","Llevar mis datos actuales###bring"),ImVec2(250,38),true)){StartUserData(userdata::Mode::kCopyCurrent);ImGui::CloseCurrentPopup();}
    ImGui::SameLine();
    if(StyledButton(TR("Usar os dados de lá###use","Use the data there###use","Usar los datos de allí###use"),ImVec2(250,38),false)){StartUserData(userdata::Mode::kUseExisting);ImGui::CloseCurrentPopup();}
    Muted(TR("Levar: o que está lá vira uma cópia (user_data-anterior-...). Usar: seus dados atuais ficam onde estão.",
      "Bring: what is there becomes a copy (user_data-anterior-...). Use: your current data stays where it is.",
      "Llevar: lo que hay allí pasa a ser una copia (user_data-anterior-...). Usar: tus datos actuales se quedan donde están."));
    if(StyledButton(TR("Cancelar###cancel_ud","Cancel###cancel_ud","Cancelar###cancel_ud"),ImVec2(120,32),false))ImGui::CloseCurrentPopup();
    ImGui::PopStyleVar();
    ImGui::EndPopup();
  }
  void LanguageCombo(){
    ImGui::SetNextItemWidth(230);auto* draw=ImGui::GetWindowDrawList();ImVec2 p=ImGui::GetCursorScreenPos();float h=ImGui::GetFrameHeight();
    ImGui::PushStyleColor(ImGuiCol_FrameBg,ImVec4(.03f,.06f,.10f,.88f));ImGui::PushStyleColor(ImGuiCol_FrameBgHovered,ImVec4(.06f,.12f,.20f,.92f));ImGui::PushStyleColor(ImGuiCol_Border,ImVec4(.16f,.42f,.66f,1));ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize,1);
    if(ImGui::BeginCombo("##ui_language","")){
      for(int i=0;i<3;++i){ImGui::PushID(i);ImVec2 q=ImGui::GetCursorScreenPos();if(ImGui::Selectable("##item",ui_lang_==i,0,ImVec2(0,24))){ui_lang_=i;g_ui_lang=i;}
        auto* popup=ImGui::GetWindowDrawList();Flag(popup,ImVec2(q.x+4,q.y+4),i);popup->AddText(ImVec2(q.x+38,q.y+3),ImGui::GetColorU32(ImGuiCol_Text),kLanguageNames[i]);ImGui::PopID();}
      ImGui::EndCombo();
    }
    ImGui::PopStyleVar();ImGui::PopStyleColor(3);
    Flag(draw,ImVec2(p.x+10,p.y+(h-16)/2),ui_lang_);draw->AddText(ImVec2(p.x+44,p.y+(h-ImGui::GetFontSize())/2),ImGui::GetColorU32(ImGuiCol_Text),kLanguageNames[ui_lang_]);
  }
  // Header: Monscriser's hero art anchored right, the game logo left, language selector under it.
  float DrawHeader(ImGuiIO& io){
    auto* draw=ImGui::GetWindowDrawList();auto origin=ImGui::GetWindowPos();
    if(!art_attempted_){
      art_attempted_=true;
      auto load=[&](const char* file){const auto bytes=lossless::Read(binary_root_/file);int w=0,h=0;
        auto pixels=rex::ui::DecodeImageRGBA(reinterpret_cast<const uint8_t*>(bytes.data()),bytes.size(),w,h);
        return pixels.empty()?std::unique_ptr<rex::ui::ImmediateTexture>{}:imgui_drawer()->CreateTextureRGBA(w,h,pixels.data());};
      header_art_=load("steam_art/monscriser/hero.png");
      header_logo_=load("steam_art/logo.png");
    }
    const float height=std::clamp(io.DisplaySize.y*.27f,150.f,230.f);
    const ImU32 navy=IM_COL32(6,16,28,255);
    draw->AddRectFilled(origin,ImVec2(origin.x+io.DisplaySize.x,origin.y+height),navy);
    if(header_art_){
      // Keep the art's aspect: fill the height, anchored to the right edge, fading into navy.
      const float w=height*float(header_art_->width)/float(header_art_->height);
      const float left=std::max(io.DisplaySize.x*.30f,io.DisplaySize.x-w);
      const float u0=std::max(0.f,1.f-(io.DisplaySize.x-left)/w);
      draw->AddImage(reinterpret_cast<ImTextureID>(header_art_.get()),ImVec2(origin.x+left,origin.y),ImVec2(origin.x+io.DisplaySize.x,origin.y+height),ImVec2(u0,0),ImVec2(1,1));
      draw->AddRectFilledMultiColor(ImVec2(origin.x+left,origin.y),ImVec2(origin.x+left+160,origin.y+height),navy,IM_COL32(6,16,28,0),IM_COL32(6,16,28,0),navy);
    }
    const float lx=std::max(18.f,io.DisplaySize.x*.033f),ly=12;
    float logo_h=0;
    if(header_logo_){
      logo_h=std::min(height-ImGui::GetFrameHeight()-58,92.f);
      const float logo_w=logo_h*float(header_logo_->width)/float(header_logo_->height);
      draw->AddImage(reinterpret_cast<ImTextureID>(header_logo_.get()),ImVec2(origin.x+lx,origin.y+ly),ImVec2(origin.x+lx+logo_w,origin.y+ly+logo_h));
    }else{
      logo_h=60;draw->AddText(ImGui::GetFont(),34,ImVec2(origin.x+lx,origin.y+ly),IM_COL32(242,247,252,255),"Hydro Thunder");
      draw->AddText(ImGui::GetFont(),22,ImVec2(origin.x+lx,origin.y+ly+36),IM_COL32(105,211,245,255),"Hurricane");
    }
    ImGui::SetCursorScreenPos(ImVec2(origin.x+lx,origin.y+ly+logo_h+8));LanguageCombo();
    return height;
  }
  bool DlcInstalled()const{return std::filesystem::exists(user_data_/"0000000000000000/5841096A/00000002/Pack01/Pack01.apf");}
  void ReadyCard(){
    BeginCard("ReadyCard",ImVec2(0,62),ImGuiWindowFlags_NoScrollbar|ImGuiWindowFlags_NoScrollWithMouse);
    auto* draw=ImGui::GetWindowDrawList();ImVec2 p=ImGui::GetCursorScreenPos();
    const bool ready=state_->succeeded;
    if(ready)FilledCheck(draw,ImVec2(p.x+16,p.y+19),15);else Warning(draw,ImVec2(p.x+16,p.y+19),15);
    ImGui::SetCursorScreenPos(ImVec2(p.x+46,p.y-4));
    ImGui::BeginGroup();
    CardTitle(ready?TR("Tudo configurado","All set","Todo configurado"):TR("Vamos preparar sua primeira corrida","Let's prepare your first race","Preparemos tu primera carrera"),nullptr);
    ImGui::TextUnformatted(ready?TR("Sua instalação foi reconhecida. É só jogar.","Your installation was recognized. Just play.","Se reconoció tu instalación. Solo juega."):
                                 TR("Escolha os arquivos do jogo abaixo.","Choose the game files below.","Elige los archivos del juego abajo."));
    ImGui::EndGroup();
    EndCard();
  }
  void GameCard(float height){
    BeginCard("GameCard",ImVec2(0,height),ImGuiWindowFlags_NoScrollbar);
    CardTitle(TR("Jogo e update","Game and title update","Juego y actualización"),nullptr);
    Colored(kGreen,TR("Prontos para jogar (update 0.0.2.1)","Ready to play (title update 0.0.2.1)","Listos para jugar (actualización 0.0.2.1)"));
    Muted(U8(root_).c_str());
    ImGui::Spacing();
    ImGui::BeginDisabled(state_->busy);
    if(StyledButton(TR("Trocar pasta do jogo...###existing","Change game folder...###existing","Cambiar carpeta del juego...###existing"),ImVec2(0,34),false))SDL_ShowOpenFolderDialog(Selected,new Picker{state_,-4},nullptr,nullptr,false);
    ImGui::SameLine();
    if(StyledButton(TR("Gerenciar instalação###manage","Manage installation###manage","Administrar instalación###manage"),ImVec2(0,34),false))manage_install_=true;
    ImGui::EndDisabled();
    EndCard();
  }
  void DlcCard(float height){
    BeginCard("DlcCard",ImVec2(0,height),ImGuiWindowFlags_NoScrollbar);
    CardTitle(TR("Conteúdo adicional (DLC)","Additional content (DLC)","Contenido adicional (DLC)"),nullptr);
    if(DlcInstalled()){
      Colored(kGreen,TR("Instalado: pistas e barcos extras ativos.","Installed: extra tracks and boats active.","Instalado: pistas y barcos extra activos."));
    }else{
      Muted(TR("Opcional: pacote LIVE/CON/PIRS ou pasta com Pack01.apf. Os arquivos escolhidos são apenas lidos.",
               "Optional: LIVE/CON/PIRS package or a folder with Pack01.apf. The chosen files are only read.",
               "Opcional: paquete LIVE/CON/PIRS o carpeta con Pack01.apf. Los archivos elegidos solo se leen."));
      ImGui::Spacing();
      ImGui::BeginDisabled(state_->busy);
      if(StyledButton(TR("Instalar DLC (arquivo)...###dlc_file","Install DLC (file)...###dlc_file","Instalar DLC (archivo)...###dlc_file"),ImVec2(0,34),true))SDL_ShowOpenFileDialog(Selected,new Picker{state_,-6},nullptr,nullptr,0,nullptr,false);
      ImGui::SameLine();
      if(StyledButton(TR("Pasta...###dlc_folder","Folder...###dlc_folder","Carpeta...###dlc_folder"),ImVec2(0,34),false))SDL_ShowOpenFolderDialog(Selected,new Picker{state_,-6},nullptr,nullptr,false);
      ImGui::EndDisabled();
    }
    EndCard();
  }
  void DataCard(){
    BeginCard("DataCard",ImVec2(0,0));
    CardTitle(TR("Seus dados","Your data","Tus datos"),TR("Saves, DLC instalada e conquistas ficam nesta pasta:","Saves, installed DLC and achievements are kept in this folder:","Partidas, DLC instalado y logros quedan en esta carpeta:"));
    ImGui::TextWrapped("%s",U8(user_data_).c_str());
    if(StyledButton(TR("Alterar local...###to_saves","Change location...###to_saves","Cambiar ubicación...###to_saves"),ImVec2(0,32),false))select_tab_=3;
    ImGui::SameLine();
    if(StyledButton(TR("Abrir pasta###open_ud_home","Open folder###open_ud_home","Abrir carpeta###open_ud_home"),ImVec2(0,32),false))OpenFolder(user_data_);
    EndCard();
  }
  void Home(){
    ReadyCard();
    if(state_->succeeded && !manage_install_){
      const float row=170;
      if(ImGui::BeginTable("GameAndDlc",2,ImGuiTableFlags_SizingStretchProp)){
        ImGui::TableSetupColumn("game",ImGuiTableColumnFlags_WidthStretch,.55f);
        ImGui::TableSetupColumn("dlc",ImGuiTableColumnFlags_WidthStretch,.45f);
        ImGui::TableNextRow();ImGui::TableSetColumnIndex(0);GameCard(row);
        ImGui::TableSetColumnIndex(1);DlcCard(row);
        ImGui::EndTable();
      }
      DataCard();
      return;
    }
    PrepareCard();
  }
  void PrepareCard(){
    std::array<std::string,3> sources;
    { std::lock_guard lock(state_->mutex); sources=state_->sources; }
    BeginCard("PrepareCard",ImVec2(0,0));
    CardTitle(TR("Preparar os arquivos","Prepare the files","Preparar los archivos"),
              TR("Use sua cópia do jogo. Os arquivos escolhidos só são lidos; a versão pronta fica numa pasta separada.",
                 "Use your copy of the game. The chosen files are only read; the prepared version goes to a separate folder.",
                 "Usa tu copia del juego. Los archivos elegidos solo se leen; la versión preparada queda en una carpeta aparte."));
    ImGui::BeginDisabled(state_->busy);
    if(state_->succeeded){if(StyledButton(TR("Voltar ao início###back_home","Back to Home###back_home","Volver al inicio###back_home"),ImVec2(0,32),false))manage_install_=false;ImGui::SameLine();}
    if(StyledButton(TR("Usar pasta do jogo já preparada...###existing2","Use an already prepared game folder...###existing2","Usar carpeta del juego ya preparada...###existing2"),ImVec2(0,32),false))SDL_ShowOpenFolderDialog(Selected,new Picker{state_,-4},nullptr,nullptr,false);
    const char* titles[]={TR("1. Jogo","1. Game","1. Juego"),"2. DLC",TR("3. Update","3. Title update","3. Actualización")};
    const char* help[]={TR("Pasta com default.xex e Base.apf, ou o pacote original do jogo.",
                           "Folder with default.xex and Base.apf, or the original game package.",
                           "Carpeta con default.xex y Base.apf, o el paquete original del juego."),
                        TR("Pacote LIVE/CON/PIRS, mesmo sem extensão, ou pasta com Pack01.apf. Opcional.",
                           "LIVE/CON/PIRS package, even without an extension, or a folder with Pack01.apf. Optional.",
                           "Paquete LIVE/CON/PIRS, aunque no tenga extensión, o carpeta con Pack01.apf. Opcional."),
                        TR("Arquivo UPDATE, default.xexp ou pasta extraída. Esta build usa o update 0.0.2.1.",
                           "UPDATE file, default.xexp or extracted folder. This build uses title update 0.0.2.1.",
                           "Archivo UPDATE, default.xexp o carpeta extraída. Esta build usa la actualización 0.0.2.1.")};
    for(int slot=0;slot<3;++slot) {
      ImGui::PushID(slot); ImGui::Separator();
      const bool installed=slot==0 ? std::filesystem::exists(GameDirectory(root_)/"Base.apf") :
                           slot==1 ? DlcInstalled() : ReadyForLaunch(root_);
      PushSize(19);ImGui::TextUnformatted(titles[slot]);ImGui::PopFont();ImGui::SameLine();
      Colored(installed ? kGreen : ImVec4(.7f,.74f,.8f,1),installed ? TR("instalado","installed","instalado") :
              slot==1 ? TR("opcional","optional","opcional") : TR("necessário","required","necesario"));
      Muted(help[slot]);
      std::array<char,4096> path{};
      std::strncpy(path.data(),sources[slot].c_str(),path.size()-1);
      ImGui::SetNextItemWidth(std::max(120.0f,ImGui::GetContentRegionAvail().x-200));
      if(ImGui::InputText("##path",path.data(),path.size())) {
        std::lock_guard lock(state_->mutex);state_->sources[slot]=path.data();
      }
      ImGui::SameLine();
      if(StyledButton(TR("Arquivo###file","File###file","Archivo###file"),ImVec2(90,0),false)) SDL_ShowOpenFileDialog(Selected,new Picker{state_,slot},nullptr,nullptr,0,nullptr,false);
      ImGui::SameLine();
      if(StyledButton(TR("Pasta###folder","Folder###folder","Carpeta###folder"),ImVec2(90,0),false)) SDL_ShowOpenFolderDialog(Selected,new Picker{state_,slot},nullptr,nullptr,false);
      ImGui::PopID();
    }
    ImGui::Spacing();
    if(StyledButton(TR("Preparar arquivos###prepare","Prepare files###prepare","Preparar archivos###prepare"),ImVec2(200,36),true)) {
      if(worker_.joinable()) worker_.join();
      std::array<std::filesystem::path,3> paths;
      { std::lock_guard lock(state_->mutex); for(int i=0;i<3;++i) paths[i]=std::filesystem::u8path(state_->sources[i]); }
      if(!paths[0].empty()){
        auto selected=std::filesystem::is_directory(paths[0]) ? paths[0] : paths[0].parent_path();
        if(selected.filename()=="game_data" && std::filesystem::is_directory(selected.parent_path()/"user_data"))selected=selected.parent_path();
        root_=selected;
        paths_.cache_root=ShaderCacheDirectory(root_);
        user_data_=UserDataRoot(root_);
        if(ReadyForLaunch(root_)){RememberLibrary(root_);paths[0].clear();}
      }
      state_->busy=true;state_->succeeded=false;
      worker_=std::thread([state=state_,root=root_,paths] {
        auto report=[state](std::string message) { std::lock_guard lock(state->mutex);state->status=std::move(message); };
        try {if(!ReadyForLaunch(root) || !paths[0].empty() || !paths[1].empty() || !paths[2].empty())PrepareContent(root,paths,report);RememberLibrary(root);state->succeeded=true;
          report(TR("Pasta do jogo registrada. Os saves ficam junto do jogo (dá para mudar em Saves e registros).",
                    "Game folder registered. Saves stay next to the game (you can change it in Saves and logs).",
                    "Carpeta del juego registrada. Las partidas quedan junto al juego (se puede cambiar en Partidas y registros)."));}
        catch(const std::exception& error) {report(error.what());}
        state->busy=false;
      });
    }
    ImGui::EndDisabled();
    EndCard();
  }
  void Video(){
    if(ImGui::BeginTable("VideoCards",2,ImGuiTableFlags_SizingStretchSame)){
      ImGui::TableNextRow();ImGui::TableSetColumnIndex(0);
      BeginCard("ScreenCard",ImVec2(0,0));
      CardTitle(TR("Tela","Display","Pantalla"),TR("Aplicado na próxima abertura do jogo.","Applied the next time the game starts.","Se aplica al abrir el juego."));
      const char* modes[]={TR("Janela","Windowed","Ventana"),TR("Sem bordas","Borderless","Sin bordes"),TR("Tela cheia","Fullscreen","Pantalla completa")};
      ImGui::TextUnformatted(TR("Modo de exibição","Display mode","Modo de pantalla"));ImGui::SetNextItemWidth(-1);ImGui::Combo("##display",&mode_,modes,3);
      const char* sizes[]={"1280 x 720 (720p)","1920 x 1080 (1080p)","2560 x 1440 (1440p)","3840 x 2160 (2160p)"};
      ImGui::TextUnformatted(TR("Resolução de saída","Output resolution","Resolución de salida"));ImGui::SetNextItemWidth(-1);ImGui::Combo("##output",&resolution_,sizes,4);
      const char* rates[kEngineRateCount];for(int i=0;i<kEngineRateCount;++i)rates[i]=EngineRateLabel(i);
      ImGui::TextUnformatted(TR("Limite de FPS","FPS limit","Límite de FPS"));ImGui::SetNextItemWidth(-1);ImGui::Combo("##rate",&fps_,rates,kEngineRateCount);
      Muted(EngineRateHelp(fps_));
      EndCard();
      ImGui::TableSetColumnIndex(1);
      BeginCard("GameCardVideo",ImVec2(0,0));
      CardTitle(TR("Jogo","Game","Juego"),nullptr);
      const char* languages[]={"English","Japanese","German","French","Spanish","Italian","Korean","Chinese (Traditional)","Português (Brasil)"};
      ImGui::TextUnformatted(TR("Idioma do jogo","Game language","Idioma del juego"));ImGui::SetNextItemWidth(-1);ImGui::Combo("##game_language",&language_,languages,9);
      Muted(TR("Idiomas sem tradução no jogo usam o idioma padrão dele. O idioma do configurador fica no topo da janela.",
               "Languages the game lacks fall back to its default. The launcher language is at the top of the window.",
               "Los idiomas que el juego no tiene usan su idioma predeterminado. El idioma del configurador está arriba."));
      ImGui::Checkbox(TR("Mostrar contador de FPS (F6)###fps","Show FPS counter (F6)###fps","Mostrar contador de FPS (F6)###fps"),&show_fps_);
      ImGui::Checkbox(TR("Compilar shaders em segundo plano###async","Compile shaders in the background###async","Compilar shaders en segundo plano###async"),&async_);
#if defined(_WIN32)
      ImGui::Checkbox(TR("Abrir o configurador ao iniciar###show_on_start","Open the launcher on start###show_on_start","Abrir el configurador al iniciar###show_on_start"),&show_on_start_);
#else
      Muted(TR("No Linux, o configurador sempre abre primeiro. O atalho da Steam abre o jogo direto.",
               "On Linux the launcher always opens first. The Steam shortcut starts the game directly.",
               "En Linux el configurador siempre abre primero. El acceso directo de Steam abre el juego directamente."));
#endif
      EndCard();
      ImGui::EndTable();
    }
    BeginCard("ImageCard",ImVec2(0,0));
    CardTitle(TR("Imagem","Image","Imagen"),nullptr);
    if(ImGui::BeginTable("ImageGrid",2,ImGuiTableFlags_SizingStretchSame)){
      ImGui::TableNextRow();ImGui::TableSetColumnIndex(0);
      const char* render_labels[]={TR("Nativa (máxima qualidade)","Native (maximum quality)","Nativa (máxima calidad)"),
                                   TR("Qualidade","Quality","Calidad"),TR("Equilibrado","Balanced","Equilibrado"),
                                   TR("Desempenho","Performance","Rendimiento"),TR("Ultra desempenho","Ultra performance","Ultra rendimiento")};
      ImGui::TextUnformatted(TR("Qualidade da imagem","Image quality","Calidad de imagen"));ImGui::SetNextItemWidth(-12);
      ImGui::Combo("##render_quality",&render_quality_,render_labels,kRenderQualityCount);
      if(ImGui::IsItemHovered())ImGui::SetTooltip("%s",TR(
        "Tamanho em que o jogo desenha cada quadro.\nNativa: no tamanho da saída ou maior (mais liso, GPU mais carregada).\nAs outras desenham menor e a ampliação (FSR) completa a imagem.\nO motor do Xbox 360 só desenha em múltiplos inteiros de 720p.",
        "Size the game draws each frame at.\nNative: at the output size or larger (smoother, heavier GPU).\nThe others draw smaller and the upscaler (FSR) completes the image.\nThe Xbox 360 engine only draws at whole multiples of 720p.",
        "Tamaño en que el juego dibuja cada cuadro.\nNativa: al tamaño de salida o mayor (más suave, GPU más cargada).\nLas demás dibujan menor y el escalado (FSR) completa la imagen.\nEl motor de Xbox 360 solo dibuja en múltiplos enteros de 720p."));
      const char* upscalers[]={TR("Bilinear (mais suave)","Bilinear (softest)","Bilineal (más suave)"),TR("FSR 1 (recomendado)","FSR 1 (recommended)","FSR 1 (recomendado)"),"FSR 3.1 - AMD FidelityFX"};
      ImGui::TextUnformatted(TR("Ampliação da imagem","Upscaling","Escalado de imagen"));ImGui::SetNextItemWidth(-12);
      const int previous_upscaler=upscaler_;
      ImGui::Combo("##upscaler",&upscaler_,upscalers,3);
      // Moving to/from FSR 3.1 keeps the slider on each upscaler's measured default
      // only while the user has not chosen another level.
      if(upscaler_!=previous_upscaler && sharpness_==(previous_upscaler==2 ? 8 : 7)) sharpness_=upscaler_==2 ? 8 : 7;
      if(ImGui::IsItemHovered())ImGui::SetTooltip("%s",TR(
        "Usado quando a imagem do jogo é menor que a resolução de saída.\nFSR 1: o mais nítido nos testes, custo muito baixo.\nFSR 3.1: sem vetores de movimento do jogo, fica parecido com o FSR 1.",
        "Used when the game image is smaller than the output resolution.\nFSR 1: the sharpest in our tests, very low cost.\nFSR 3.1: without the game's motion vectors it looks like FSR 1.",
        "Se usa cuando la imagen del juego es menor que la resolución de salida.\nFSR 1: el más nítido en las pruebas, costo muy bajo.\nFSR 3.1: sin vectores de movimiento del juego, se parece a FSR 1."));
      ImGui::TableSetColumnIndex(1);
      ImGui::TextUnformatted(TR("Nitidez","Sharpness","Nitidez"));ImGui::SetNextItemWidth(-1);ImGui::SliderInt("##sharpness",&sharpness_,0,10);
      if(ImGui::IsItemHovered())ImGui::SetTooltip("%s",TR(
        "Realça as bordas depois da ampliação (padrão: 7 no FSR 1, 8 no FSR 3.1).\nTextos e contornos de contraste alto ficam protegidos.",
        "Enhances edges after upscaling (default: 7 with FSR 1, 8 with FSR 3.1).\nText and high-contrast outlines are protected.",
        "Realza los bordes después del escalado (por defecto: 7 en FSR 1, 8 en FSR 3.1).\nTextos y contornos de alto contraste quedan protegidos."));
      const char* profiles[]={TR("Qualidade (padrão)","Quality (default)","Calidad (predeterminado)"),
                              TR("Desempenho - GPU fraca (Deck, Ally)","Performance - weak GPU (Deck, Ally)","Rendimiento - GPU débil (Deck, Ally)")};
      int profile=performance_?1:0;
      ImGui::TextUnformatted(TR("Perfil gráfico","Graphics profile","Perfil gráfico"));ImGui::SetNextItemWidth(-1);ImGui::Combo("##profile",&profile,profiles,2);performance_=profile==1;
      if(ImGui::IsItemHovered())ImGui::SetTooltip("%s",TR(
        "Desempenho: filtragem de texturas do próprio jogo, alvos de gamma em 8 bits,\nmenos threads de compilação e cache de texturas menor. Diferença visual pequena.",
        "Performance: the game's own texture filtering, 8-bit gamma targets,\nfewer compile threads and a smaller texture cache. Small visual difference.",
        "Rendimiento: filtrado de texturas del juego, objetivos gamma de 8 bits,\nmenos hilos de compilación y caché de texturas menor. Diferencia visual pequeña."));
      ImGui::Checkbox(TR("Anti-serrilhado FXAA###fxaa","FXAA anti-aliasing###fxaa","Antialiasing FXAA###fxaa"),&fxaa_);
      if(ImGui::IsItemHovered())ImGui::SetTooltip("%s",TR("Suaviza o serrilhado, mas deixa os números pequenos do HUD com contorno duplo. Desligado por padrão.",
        "Smooths jagged edges, but doubles the outline of small HUD digits. Off by default.","Suaviza los bordes, pero duplica el contorno de los números pequeños del HUD. Desactivado por defecto."));
      ImGui::EndTable();
    }
    {
      const int scale=RenderScale(render_quality_,kOutputHeights[resolution_]);
      const int render_w=1280*scale,render_h=720*scale;
      const int out_w=kOutputWidths[resolution_],out_h=kOutputHeights[resolution_];
      char line[256];
      if(render_h<out_h)std::snprintf(line,sizeof line,TR("Desenha em %dx%d (escala %dx) e amplia para %dx%d com \"%s\".","Draws at %dx%d (%dx scale) and upscales to %dx%d with \"%s\".","Dibuja en %dx%d (escala %dx) y escala a %dx%d con \"%s\"."),
                                      render_w,render_h,scale,out_w,out_h,upscaler_==2 ? "FSR 3.1" : upscaler_==1 ? "FSR 1" : "Bilinear");
      else if(render_h>out_h)std::snprintf(line,sizeof line,TR("Desenha em %dx%d (escala %dx) e reduz para %dx%d: bordas mais lisas, GPU bem mais carregada.","Draws at %dx%d (%dx scale) and downscales to %dx%d: smoother edges, much heavier GPU load.","Dibuja en %dx%d (escala %dx) y reduce a %dx%d: bordes más suaves, GPU mucho más cargada."),render_w,render_h,scale,out_w,out_h);
      else std::snprintf(line,sizeof line,TR("Desenha em %dx%d (escala %dx), igual à saída: sem ampliação.","Draws at %dx%d (%dx scale), same as the output: no upscaling.","Dibuja en %dx%d (escala %dx), igual a la salida: sin escalado."),render_w,render_h,scale);
      Muted(line);
    }
    EndCard();
  }
  void Controls(){
    BeginCard("ControlsCard",ImVec2(0,0));
    CardTitle(TR("Teclado","Keyboard","Teclado"),TR("Clique numa tecla e aperte a nova. Cada ação aceita duas teclas; se a tecla já estiver em outra ação, as duas trocam. Esc cancela. Um controle (Xbox/PlayStation) continua funcionando junto.",
      "Click a key and press the new one. Each action takes two keys; if the key is already used by another action, the two swap. Esc cancels. A controller (Xbox/PlayStation) keeps working too.",
      "Haz clic en una tecla y pulsa la nueva. Cada acción admite dos teclas; si la tecla ya está en otra acción, se intercambian. Esc cancela. Un mando (Xbox/PlayStation) sigue funcionando."));
    DrawKeyboardTab();
    EndCard();
  }
  void Saves(){
    const bool custom=!userdata::Same(user_data_,DefaultUserDataRoot(root_));
    BeginCard("UserDataCard",ImVec2(0,0));
    CardTitle(TR("Pasta de dados (user_data)","Data folder (user_data)","Carpeta de datos (user_data)"),
              TR("Guarda os saves, a DLC instalada e as conquistas. Padrão: ao lado do jogo. Você pode escolher qualquer outra pasta; ao trocar, os dados são copiados e a pasta antiga fica como cópia de segurança.",
                 "Holds the saves, the installed DLC and the achievements. Default: next to the game. You can pick any other folder; when you change it the data is copied and the old folder stays as a backup.",
                 "Guarda las partidas, el DLC instalado y los logros. Predeterminado: junto al juego. Puedes elegir cualquier otra carpeta; al cambiar, los datos se copian y la carpeta anterior queda como copia de seguridad."));
    ImGui::Spacing();
    PushSize(19);ImGui::TextWrapped("%s",U8(user_data_).c_str());ImGui::PopFont();
    Colored(custom?kAccentHover:kGreen,custom?TR("Pasta escolhida por você","Folder chosen by you","Carpeta elegida por ti"):TR("Padrão: ao lado do jogo","Default: next to the game","Predeterminado: junto al juego"));
    ImGui::Spacing();
    ImGui::BeginDisabled(state_->busy||!state_->succeeded);
    if(StyledButton(TR("Escolher outra pasta...###pick_ud","Choose another folder...###pick_ud","Elegir otra carpeta...###pick_ud"),ImVec2(0,34),true))SDL_ShowOpenFolderDialog(Selected,new Picker{state_,-7},nullptr,nullptr,false);
    ImGui::SameLine();
    ImGui::BeginDisabled(!custom);
    if(StyledButton(TR("Voltar ao padrão###default_ud","Back to default###default_ud","Volver al predeterminado###default_ud"),ImVec2(0,34),false))RequestUserData(DefaultUserDataRoot(root_),true);
    ImGui::EndDisabled();
    ImGui::SameLine();
    if(StyledButton(TR("Abrir pasta###open_ud","Open folder###open_ud","Abrir carpeta###open_ud"),ImVec2(0,34),false))OpenFolder(user_data_);
    ImGui::EndDisabled();
    EndCard();
    if(ImGui::BeginTable("SaveTools",2,ImGuiTableFlags_SizingStretchSame)){
      ImGui::TableNextRow();ImGui::TableSetColumnIndex(0);
      BeginCard("BackupCard",ImVec2(0,190));
      CardTitle(TR("Cópia dos saves","Saves backup","Copia de partidas"),TR("Exporta só os saves para uma pasta, ou traz de volta um backup.","Exports just the saves to a folder, or brings a backup back.","Exporta solo las partidas a una carpeta, o restaura una copia."));
      ImGui::BeginDisabled(state_->busy||!state_->succeeded);
      if(StyledButton(TR("Exportar...###export","Export...###export","Exportar...###export"),ImVec2(0,34),false))SDL_ShowOpenFolderDialog(Selected,new Picker{state_,-1},nullptr,nullptr,false);
      ImGui::SameLine();
      if(StyledButton(TR("Importar...###import","Import...###import","Importar...###import"),ImVec2(0,34),false))SDL_ShowOpenFolderDialog(Selected,new Picker{state_,-2},nullptr,nullptr,false);
      if(StyledButton(TR("Importar do Xenia...###import_xenia","Import from Xenia...###import_xenia","Importar de Xenia...###import_xenia"),ImVec2(0,34),true))SDL_ShowOpenFolderDialog(Selected,new Picker{state_,-9},nullptr,nullptr,false);
      if(ImGui::IsItemHovered())ImGui::SetTooltip("%s",TR("Escolha a pasta do Xenia (ou a pasta content dele). O ht2save.dat é encontrado sozinho;\no save atual vai antes para save_backups.",
        "Pick the Xenia folder (or its content folder). The ht2save.dat is found automatically;\nthe current save goes to save_backups first.",
        "Elige la carpeta de Xenia (o su carpeta content). El ht2save.dat se encuentra solo;\nla partida actual va antes a save_backups."));
      ImGui::EndDisabled();
      EndCard();
      ImGui::TableSetColumnIndex(1);
      BeginCard("LogsCard",ImVec2(0,190));
      CardTitle(TR("Registros e cache","Logs and cache","Registros y caché"),TR("Para investigar um problema ou recompilar os shaders.","To investigate a problem or rebuild the shaders.","Para investigar un problema o recompilar los shaders."));
      if(StyledButton(TR("Abrir registros###logs","Open logs###logs","Abrir registros###logs"),ImVec2(0,34),false))OpenFolder(SettingsDirectory()/"logs");
      ImGui::SameLine();
      ImGui::BeginDisabled(state_->busy);
      if(StyledButton(TR("Limpar shaders###clear_cache","Clear shaders###clear_cache","Borrar shaders###clear_cache"),ImVec2(0,34),false))ImGui::OpenPopup("###confirm_cache");
      ImGui::EndDisabled();
      ClearCacheModal();
      EndCard();
      ImGui::EndTable();
    }
    Muted((std::string(TR("Preferências: ","Preferences: ","Preferencias: "))+U8(paths_.config_path)).c_str());
  }
  void ClearCacheModal(){
    const std::string title=std::string(TR("Confirmar limpeza de shaders","Confirm shader cache cleanup","Confirmar limpieza de shaders"))+"###confirm_cache";
    if(!ImGui::BeginPopupModal(title.c_str(),nullptr,ImGuiWindowFlags_AlwaysAutoResize))return;
    ImGui::TextUnformatted(TR("Feche outras janelas do jogo antes de continuar.\nOs shaders serão recompilados e podem causar engasgos no começo.\nSaves, jogo, DLC e update ficam preservados.",
      "Close other game windows before continuing.\nShaders will be rebuilt and may cause some stutter at first.\nSaves, game, DLC and update are kept.",
      "Cierra otras ventanas del juego antes de continuar.\nLos shaders se recompilarán y pueden causar tirones al principio.\nPartidas, juego, DLC y actualización se conservan."));
    if(StyledButton(TR("Limpar agora###clear_now","Clear now###clear_now","Limpiar ahora###clear_now"),ImVec2(0,32),true)){std::lock_guard lock(state_->mutex);state_->save_folder=U8(paths_.cache_root);state_->save_action=5;ImGui::CloseCurrentPopup();}
    ImGui::SameLine();if(StyledButton(TR("Cancelar###cancel","Cancel###cancel","Cancelar###cancel"),ImVec2(0,32),false))ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
  }
  void FrameGeneration(){
    BeginCard("LosslessCard",ImVec2(0,0));
    CardTitle("Lossless Scaling",TR("Usa a sua instalação da Steam. Nenhum arquivo comprado é copiado para a build.",
      "Uses your Steam installation. Purchased files are not copied into this build.",
      "Usa tu instalación de Steam. No se copian archivos comprados a esta build."));
    if(StyledButton(TR("Procurar novamente###scan_ls","Scan again###scan_ls","Buscar de nuevo###scan_ls"),ImVec2(0,32),false))lossless_detected_=lossless::Detect();
    if(lossless_detected_.purchased_files){
      ImGui::SameLine();ImGui::AlignTextToFramePadding();
      Colored(kGreen,(std::string(TR("Encontrado: ","Found: ","Encontrado: "))+lossless::Utf8(lossless_detected_.folder)).c_str());
    }else{
      Colored(kWarn,TR("Lossless Scaling não encontrado nas bibliotecas da Steam.","Lossless Scaling was not found in your Steam libraries.","No se encontró Lossless Scaling en las bibliotecas de Steam."));
      if(StyledButton(TR("Comprar / ver na Steam###buy_ls","Buy / view on Steam###buy_ls","Comprar / ver en Steam###buy_ls"),ImVec2(0,32),false))SDL_OpenURL(lossless::StoreUrl);
      ImGui::SameLine();
    }
    if(StyledButton(TR("Instalar pela Steam###install_ls","Install through Steam###install_ls","Instalar desde Steam###install_ls"),ImVec2(0,32),false))SDL_OpenURL(lossless::InstallUrl);
#if !defined(_WIN32)
    ImGui::SeparatorText(TR("Instalação no Linux - etapas obrigatórias","Linux installation - required steps","Instalación en Linux - pasos necesarios"));
    ImGui::TextWrapped("%s",TR("1. Instale sua cópia do Lossless Scaling na Steam. Isso sozinho não instala a camada Vulkan.","1. Install your Steam copy of Lossless Scaling. This alone does not install the Vulkan layer.","1. Instala tu copia de Lossless Scaling desde Steam. Esto no instala la capa Vulkan."));
    ImGui::TextWrapped("%s",TR("2. Na Steam: Lossless Scaling > Propriedades > Betas > selecione lsfg-vk. Aguarde o download terminar.","2. Steam: Lossless Scaling > Properties > Betas > select lsfg-vk. Wait for the download to finish.","2. Steam: Lossless Scaling > Propiedades > Betas > selecciona lsfg-vk. Espera a que termine la descarga."));
    ImGui::TextWrapped("%s",TR("3. Instale lsfg-vk 2.x no Linux. No CachyOS/Arch, use o pacote lsfg-vk ou lsfg-vk-bin do AUR. Em outras distribuições, siga o guia abaixo. O AppImage não instala pacotes do sistema.","3. Install lsfg-vk 2.x on Linux. On CachyOS/Arch, use the AUR lsfg-vk or lsfg-vk-bin package. On other distributions, follow the guide below. This AppImage does not install system packages.","3. Instala lsfg-vk 2.x en Linux. En CachyOS/Arch usa el paquete AUR lsfg-vk o lsfg-vk-bin. Para otras distribuciones sigue la guía. Este AppImage no instala paquetes del sistema."));
    if(StyledButton(TR("Copiar comando CachyOS/Arch###ls_install_command","Copy CachyOS/Arch command###ls_install_command","Copiar comando CachyOS/Arch###ls_install_command"),ImVec2(0,32),false))SDL_SetClipboardText("yay -S lsfg-vk-bin");
    ImGui::TextWrapped("%s",TR("4. Execute lsfg-vk-cli healthcheck para conferir a camada. Volte aqui e clique em Procurar novamente. Arquivos detectados não garantem que o driver carregou a camada.","4. Run lsfg-vk-cli healthcheck to check the layer. Return here and Scan again. Detected files do not guarantee that the driver loaded the layer.","4. Ejecuta lsfg-vk-cli healthcheck para comprobar la capa. Vuelve aquí y busca de nuevo. Detectar archivos no garantiza que el driver cargó la capa."));
    ImGui::TextWrapped("%s",lossless_detected_.layer.empty() ? TR("lsfg-vk: camada Vulkan não encontrada.","lsfg-vk: Vulkan layer not found.","lsfg-vk: capa Vulkan no encontrada.") : TR("lsfg-vk: manifesto Vulkan encontrado.","lsfg-vk: Vulkan manifest found.","lsfg-vk: manifiesto Vulkan encontrado."));
    if(lossless_detected_.purchased_files&&lossless_detected_.dll.empty())ImGui::TextWrapped("%s",TR("Na Steam, abra Propriedades > Betas do Lossless Scaling e escolha lsfg-vk. Depois procure novamente.","In Steam, open Lossless Scaling Properties > Betas and select lsfg-vk. Then scan again.","En Steam, abre Propiedades > Betas de Lossless Scaling y elige lsfg-vk. Luego busca de nuevo."));
    if(StyledButton(TR("Como instalar lsfg-vk###help_ls","How to install lsfg-vk###help_ls","Cómo instalar lsfg-vk###help_ls"),ImVec2(0,32),false))SDL_OpenURL(lossless::LinuxHelp);
#else
    Muted(TR("Você não precisa abrir o Lossless Scaling nem a Steam. Ao clicar em Jogar, o configurador ajusta o perfil do Hydro Thunder e roda o Lossless Scaling escondido, em segundo plano. Ele fecha junto com o jogo.",
      "You never need to open Lossless Scaling or Steam. When you press Play, the launcher sets up the Hydro Thunder profile and runs Lossless Scaling hidden in the background. It closes together with the game.",
      "No necesitas abrir Lossless Scaling ni Steam. Al pulsar Jugar, el configurador ajusta el perfil de Hydro Thunder y ejecuta Lossless Scaling oculto en segundo plano. Se cierra junto con el juego."));
#endif
    EndCard();
    BeginCard("LosslessOptions",ImVec2(0,0));
    CardTitle(TR("Geração de quadros","Frame generation","Generación de cuadros"),nullptr);
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
    if(ImGui::Checkbox(TR("Permitir acima dos Hz (experimental, sem forçar cortes)###ls_above","Allow above display Hz (experimental, no forced tearing)###ls_above","Permitir superar Hz (experimental, sin forzar cortes)###ls_above"),&lossless_options_.above_refresh))lossless::Save(lossless_options_);
#if defined(_WIN32)
    Muted(TR("Sem esta opção, o total fica preso nos Hz da tela: em 2x numa tela de 75 Hz o jogo cai para ~37 quadros reais. Com ela, os 60 quadros do jogo continuam e o total vai a ~120 (medido). A tela continua mostrando no máximo os seus Hz e a imagem pode rasgar.","Without this option the total is held to the display Hz: at 2x on a 75 Hz screen the game drops to ~37 real frames. With it, the game keeps its 60 frames and the total reaches ~120 (measured). The screen still shows at most its Hz and the image may tear.","Sin esta opción el total queda limitado a los Hz de la pantalla: en 2x a 75 Hz el juego baja a ~37 cuadros reales. Con ella, el juego mantiene sus 60 cuadros y el total llega a ~120 (medido). La pantalla sigue mostrando como máximo sus Hz y la imagen puede cortarse."));
#else
    Muted(TR("Uma TV de 60 Hz mostra no máximo 60 atualizações por segundo. Este modo tenta retirar a espera pelo V-Sync do gerador, sem forçar tearing. O compositor pode descartar quadros ou ainda limitar a saída.","A 60 Hz TV displays at most 60 updates per second. This mode tries to remove the generator V-Sync wait without forcing tearing. The compositor may discard frames or still limit output.","Una TV de 60 Hz muestra como máximo 60 actualizaciones por segundo. Este modo intenta quitar la espera V-Sync del generador sin forzar tearing. El compositor puede descartar cuadros o limitar la salida."));
    if(ImGui::Checkbox(TR("Modelo leve LSFG (pode aumentar artefatos)###ls_performance","Lighter LSFG model (may increase artifacts)###ls_performance","Modelo LSFG ligero (puede aumentar artefactos)###ls_performance"),&lossless_options_.performance))lossless::Save(lossless_options_);
#endif
    Muted(TR("Os FPS do motor continuam definidos em Vídeo. Quadros gerados não aceleram o jogo e exigem folga da GPU. No Windows, o overlay do jogo (F6) mostra \"Lossless ATIVO\" quando a geração está funcionando; use Janela ou Sem bordas. No Linux, confira no MangoHud.",
      "Engine FPS are still selected in Video. Generated frames do not speed up the game and need GPU headroom. On Windows the game overlay (F6) shows \"Lossless ATIVO\" while generation is working; use Windowed or Borderless. On Linux, check MangoHud.",
      "Los FPS del motor se eligen en Vídeo. Los cuadros generados no aceleran el juego y necesitan margen de GPU. En Windows el overlay del juego (F6) muestra \"Lossless ATIVO\" cuando la generación funciona; usa Ventana o Sin bordes. En Linux, revisa MangoHud."));
    EndCard();
  }
  void Footer(ImGuiIO& io){
    auto* draw=ImGui::GetWindowDrawList();auto origin=ImGui::GetWindowPos();const float top=io.DisplaySize.y-74;
    draw->AddRectFilled(ImVec2(origin.x,origin.y+top),ImVec2(origin.x+io.DisplaySize.x,origin.y+io.DisplaySize.y),IM_COL32(4,9,16,255));
    draw->AddLine(ImVec2(origin.x,origin.y+top),ImVec2(origin.x+io.DisplaySize.x,origin.y+top),IM_COL32(28,44,62,255));
    std::string status;{std::lock_guard lock(state_->mutex);status=state_->status;}
    const bool ok=state_->succeeded;
    if(ok)FilledCheck(draw,ImVec2(origin.x+34,origin.y+top+37),14);else Warning(draw,ImVec2(origin.x+34,origin.y+top+37),14);
    const float buttons=200+12+200+20;
    ImGui::SetCursorScreenPos(ImVec2(origin.x+58,origin.y+top+10));
    ImGui::BeginChild("Status",ImVec2(std::max(120.f,io.DisplaySize.x-58-buttons-16),56),ImGuiChildFlags_None,ImGuiWindowFlags_NoScrollbar|ImGuiWindowFlags_NoBackground);
    if(!status.empty())ImGui::TextWrapped("%s",status.c_str());
    else if(ok){
      // Every race key with the player's current choice, including jump (X) and brake (LT).
      auto key=[&](int action){return keys::Label(binds_[action][0],g_ui_lang);};
      const std::string hint=key(0)+TR(": acelerar  ",": throttle  ",": acelerar  ")+key(1)+TR(": frear  ",": brake  ",": frenar  ")+
        key(2)+TR(": impulso  ",": boost  ",": impulso  ")+key(3)+TR(": pulo  ",": jump  ",": salto  ")+key(10)+": Start";
      ImGui::AlignTextToFramePadding();Muted(hint.c_str());
    }else ImGui::TextWrapped("%s",TR("Prepare os arquivos do jogo para liberar o botão Jogar.","Prepare the game files to enable Play.","Prepara los archivos del juego para activar Jugar."));
    ImGui::EndChild();
    ImGui::SetCursorScreenPos(ImVec2(origin.x+io.DisplaySize.x-buttons,origin.y+top+14));
    ImGui::BeginDisabled(state_->busy||!state_->succeeded);
    if(StyledButton(TR("Adicionar à Steam###steam","Add to Steam###steam","Añadir a Steam###steam"),ImVec2(200,46),false))AddToSteam();
    DrawSteamAccountPopup();
    ImGui::SameLine(0,12);
    PushSize(22);
    if(StyledButton(TR("JOGAR###play","PLAY###play","JUGAR###play"),ImVec2(200,46),true)) {
      SaveAll();
      try{
#if defined(_WIN32)
        // Surface setup errors in the launcher instead of closing it first.
        lossless::ConfigureWindows(lossless_options_);
#endif
        LaunchGame(binary_root_);
        SDL_Event quit{};quit.type=SDL_EVENT_QUIT;SDL_PushEvent(&quit);
      }catch(const std::exception& error){SetStatus(error.what());}
    }
    ImGui::PopFont();
    ImGui::EndDisabled();
  }
  void SaveAll() {
    ConfigEdit edit(paths_.config_path);
    auto SaveValue=[&](const std::string& key,const std::string& value){edit.Set(key,value);};
    auto SaveBool=[&](const std::string& key,bool value){edit.Set(key,value?"true":"false");};
    rex::cvar::SetFlagByName("async_shader_compilation",async_ ? "true" : "false");
    SaveBool("async_shader_compilation",async_);
    SaveBool("ht_show_fps",show_fps_);
    SaveValue("present_effect",upscaler_==2 ? "\"fsr3\"" : upscaler_==1 ? "\"fsr\"" : "\"bilinear\"");
    const Sharpness sharp=SharpnessFor(sharpness_);
    SaveValue("ht_sharpness",std::to_string(sharpness_));
    SaveValue("present_fsr_sharpness_reduction",Fixed(sharp.rcas_reduction));
    SaveValue("present_fsr3_sharpness",Fixed(upscaler_==2 ? sharp.fsr3 : 0.0f));
    SaveValue("present_cas_additional_sharpness",Fixed(sharp.cas));
    SaveBool("ht_import_on_start",
#if defined(_WIN32)
        show_on_start_
#else
        true
#endif
    );
    game_fullscreen_=mode_!=0;
    SaveBool("ht_game_fullscreen",game_fullscreen_);
    SaveBool("fullscreen",game_fullscreen_);
    rex::cvar::SetFlagByName("user_language",std::to_string(language_+1));
    SaveValue("user_language",std::to_string(language_+1));
    SaveValue("fullscreen_mode",mode_==2 ? "\"exclusive\"" : "\"borderless\"");
    SaveValue("window_width",std::to_string(kOutputWidths[resolution_]));
    SaveValue("ht_output_width",std::to_string(kOutputWidths[resolution_]));
    SaveValue("window_height",std::to_string(kOutputHeights[resolution_]));
    SaveValue("ht_render_quality",std::to_string(render_quality_));
    SaveValue("resolution_scale",std::to_string(RenderScale(render_quality_,kOutputHeights[resolution_])));
    // Engine clock and swap limiter move together: the game draws one frame
    // per guest vblank, and the limiter only guards against bursts.
    SaveValue("video_mode_refresh_rate",std::to_string(video::ClockRate(fps_))+".0");
    SaveValue("fps_limit",std::to_string(video::SwapLimit(fps_)));
    SaveBool("vsync",true);
    SaveValue("ht_graphics_profile",performance_ ? "\"performance\"" : "\"quality\"");
    SaveValue("anisotropic_override",performance_ ? "-1" : "4");
    SaveBool("gamma_render_target_as_unorm16",!performance_);
    // -1 = automatic (75% of logical cores), as in Xenia.
    SaveValue("vulkan_pipeline_creation_threads",performance_ ? "3" : "-1");
    SaveValue("texture_cache_memory_limit_soft",performance_ ? "256" : "384");
    SaveValue("texture_cache_memory_limit_hard",performance_ ? "512" : "768");
    // FXAA is opt-in: on the 720p frame it softens stair-steps but doubles the
    // outline of small HUD digits once the frame is upscaled.
    SaveValue("swap_post_effect",fxaa_ ? "\"fxaa\"" : "\"\"");
    SaveValue("ht_ui_language",std::string("\"")+kUiLanguageCodes[std::clamp(ui_lang_,0,2)]+"\"");
    // Keyboard is always a controller in this game; the binds are the player's.
    SaveBool("mnk_mode",true);
    for(int i=0;i<keys::kActionCount;++i)SaveValue(keys::kActions[i].cvar,keys::Value(binds_,i));
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
    ImGui::TextUnformatted(TR("Feche a Steam antes de adicionar. Escolha a conta:","Close Steam before adding. Pick the account:",
                              "Cierra Steam antes de añadir. Elige la cuenta:"));
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
    ImGui::Spacing();
    if(ImGui::BeginTable("##binds",3,ImGuiTableFlags_RowBg|ImGuiTableFlags_SizingStretchProp)){
      ImGui::TableSetupColumn(TR("Ação","Action","Acción"),ImGuiTableColumnFlags_WidthStretch,1.6f);
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
          if(waiting)ImGui::PushStyleColor(ImGuiCol_Button,kAccent);
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
    if(StyledButton(TR("Restaurar padrão###keys_default","Restore defaults###keys_default","Restaurar valores###keys_default"),ImVec2(0,32),false)&&capture_.action<0){binds_=keys::Defaults();SaveAll();}
  }
 private:
  std::tuple<bool,bool,int,bool,int,int,int,int,bool,int,int,bool,int> last_saved_{};
  bool snapshot_valid_=false;
  bool manage_install_=false,art_attempted_=false,open_user_data_modal_=false,pending_default_=false;
  int tab_=0,select_tab_=-1;
  std::string test_answer_;
  std::unique_ptr<rex::ui::ImmediateTexture> header_art_,header_logo_;
  rex::PathConfig paths_;
  std::filesystem::path root_,user_data_,pending_target_;
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
