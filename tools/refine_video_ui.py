from pathlib import Path
p=Path(__file__).resolve().parents[1]/'app/src/ht_setup_wizard.cpp'
s=p.read_text()
a=s.index('    ImGui::Checkbox(TR("Compilar shaders em segundo plano')
b=s.index('    if(ImGui::Button(TR("Limpar shadercache',a)
advanced=s[a:b]
# The basic screen mirrors the supplied reference. Advanced features remain below.
basic='''    ImGui::SetWindowFontScale(1.35f);ImGui::TextUnformatted(TR("Do seu jeito","Make it yours","A tu manera"));ImGui::SetWindowFontScale(1);
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
'''
s=s[:a]+basic+advanced+s[b:]
# Close advanced contents after all their widgets, including the default-start option.
key='''#endif
    ImGui::EndChild();
    ImGui::EndTabItem();
    }
    if(ImGui::BeginTabItem(TR("Geracao de quadros'''
replacement='''#endif
    ImGui::PopID();
    }
    ImGui::EndChild();
    ImGui::EndTabItem();
    }
    if(ImGui::BeginTabItem(TR("Geracao de quadros'''
assert key in s
s=s.replace(key,replacement,1)
s=s.replace('''    if(ImGui::Button(TR("Jogar###play","Play###play","Jugar###play"),ImVec2(185,46))) {''','''    ImGui::PushStyleColor(ImGuiCol_Button,ImVec4(.28f,.77f,.95f,1));
    ImGui::PushStyleColor(ImGuiCol_Text,ImVec4(.025f,.075f,.12f,1));
    if(ImGui::Button(TR("Jogar###play","Play###play","Jugar###play"),ImVec2(185,46))) {''',1)
s=s.replace('''    ImGui::EndDisabled();
    ImGui::SameLine();ImGui::TextDisabled''','''    ImGui::PopStyleColor(2);
    ImGui::EndDisabled();
    ImGui::SameLine();ImGui::TextDisabled''',1)
# SaveAll now commits all settings with a single atomic replacement, not 30 rewrites.
s=s.replace('#include "ht_video_policy.h"','#include "ht_video_policy.h"\n#include "ht_config_edit.h"',1)
a=s.index('  void SaveAll() {')
b=s.index('\n private:',a)
part=s[a:b]
part=part.replace('  void SaveAll() {','''  void SaveAll() {
    ConfigEdit edit(paths_.config_path);
    auto SaveValue=[&](const std::filesystem::path&,const std::string& key,const std::string& value){edit.Set(key,value);};
    auto SaveBool=[&](const std::filesystem::path&,const std::string& key,bool value){edit.Set(key,value?"true":"false");};''',1)
last=part.rfind('  }')
part=part[:last]+'    edit.Commit();\n'+part[last:]
s=s[:a]+part+s[b:]
p.write_text(s)
