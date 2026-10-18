from pathlib import Path
p=Path(__file__).resolve().parents[1]/'app/src/ht_setup_wizard.cpp'
s=p.read_text(encoding='utf-8')
start=s.index('    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x*.5f')
end=s.index('    ImGui::BeginDisabled(state_->busy);',start)
s=s[:start]+'''    ImGui::SetNextWindowPos(ImVec2(0,0),ImGuiCond_Always);
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
      const auto bytes=lossless::Read(binary_root_/"steam_art/monscriser/banner.png");
      int w=0,h=0;auto pixels=rex::ui::DecodeImageRGBA(reinterpret_cast<const uint8_t*>(bytes.data()),bytes.size(),w,h);
      if(!pixels.empty())header_art_=imgui_drawer()->CreateTextureRGBA(w,h,pixels.data());
    }
    if(header_art_){
      const float left=io.DisplaySize.x*.59f;
      draw->AddImage(reinterpret_cast<ImTextureID>(header_art_.get()),ImVec2(origin.x+left,origin.y),ImVec2(origin.x+io.DisplaySize.x,origin.y+header_h));
      draw->AddRectFilledMultiColor(ImVec2(origin.x+left,origin.y),ImVec2(origin.x+left+110,origin.y+header_h),blue,IM_COL32(22,44,66,0),IM_COL32(22,44,66,0),blue);
    }
    draw->AddText(ImGui::GetFont(),34,ImVec2(origin.x+30,origin.y+23),IM_COL32(242,247,252,255),"Hydro Thunder");
    draw->AddText(ImGui::GetFont(),24,ImVec2(origin.x+30,origin.y+65),IM_COL32(105,211,245,255),"Hurricane");
    draw->AddText(ImGui::GetFont(),16,ImVec2(origin.x+30,origin.y+100),IM_COL32(192,214,230,255),TR("Sua proxima corrida comeca aqui.","Your next race starts here.","Tu proxima carrera empieza aqui."));
    ImGui::SetCursorPos(ImVec2(std::max(30.f,io.DisplaySize.x-185),header_h-44));
    {const char* languages[]={"Portugues","English","Espanol"};ImGui::SetNextItemWidth(145);
      if(ImGui::Combo("##ui_language",&ui_lang_,languages,3))g_ui_lang=ui_lang_;}
    ImGui::SetCursorPos(ImVec2(30,header_h+14));
    ImGui::BeginChild("LauncherBody",ImVec2(0,std::max(150.f,ImGui::GetContentRegionAvail().y-85)),false);
    ImGui::BeginPopup("###save_steam_actions");
''' +s[end:]
# Existing actions are retained in a popup; no game/save path changes.
old='''    ImGui::EndDisabled();
    ImGui::TextWrapped("%s",TR("Escolha a pasta do jogo uma vez.'''
i=s.index(old,start)
j=s.index('    ImGui::Spacing();',i)
s=s[:i]+'''    ImGui::EndDisabled();
    ImGui::EndPopup();
'''+s[j:]
s=s.replace('    ImGui::BeginPopup("###save_steam_actions");','    if(ImGui::BeginPopup("###save_steam_actions")){',1)
s=s.replace('    ImGui::EndPopup();\n    ImGui::Spacing();','    ImGui::EndPopup();\n    }\n    ImGui::Spacing();',1)
old='''    if(ImGui::BeginTabItem(TR("Jogo, DLC e Update###tab_game","Game, DLC and Update###tab_game","Juego, DLC y Update###tab_game"))){
    const char* titles[]='''
new='''    if(ImGui::BeginTabItem(TR("Inicio###tab_game","Home###tab_game","Inicio###tab_game"))){
    ImGui::BeginChild("HomeContents",ImVec2(0,0),false);
    if(state_->succeeded && !manage_install_){
      ImGui::Spacing();ImGui::Spacing();
      ImGui::TextColored(ImVec4(.45f,.9f,.73f,1),"%s",TR("Tudo configurado","All set","Todo configurado"));
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
      if(ImGui::Button(TR("Saves e Steam###save_steam","Saves and Steam###save_steam","Partidas y Steam###save_steam")))ImGui::OpenPopup("###save_steam_actions");
    }else{
      ImGui::TextUnformatted(TR("Vamos preparar sua primeira corrida","Let's prepare your first race","Preparemos tu primera carrera"));
      if(state_->succeeded){if(ImGui::Button(TR("Voltar ao inicio###back_home","Back to Home###back_home","Volver al inicio###back_home")))manage_install_=false;}
      if(ImGui::Button(TR("Usar pasta do jogo existente###existing","Use existing game folder###existing","Usar carpeta del juego existente###existing")))SDL_ShowOpenFolderDialog(Selected,new Picker{state_,-4},nullptr,nullptr,false);
      ImGui::SameLine();if(ImGui::Button(TR("Saves e Steam###save_steam","Saves and Steam###save_steam","Partidas y Steam###save_steam")))ImGui::OpenPopup("###save_steam_actions");
    const char* titles[]='''
assert old in s
s=s.replace(old,new,1)
old='''    ImGui::EndDisabled();
    ImGui::EndTabItem();
    }
    if(ImGui::BeginTabItem(TR("Video e idioma'''
new='''    ImGui::EndDisabled();
    }
    ImGui::EndChild();
    ImGui::EndTabItem();
    }
    if(ImGui::BeginTabItem(TR("Video e idioma'''
assert old in s
s=s.replace(old,new,1)
# Every tab lives inside the bounded body; tab contents scroll independently.
s=s.replace('ImGui::GetContentRegionAvail().y-105','ImGui::GetContentRegionAvail().y',2)
s=s.replace('ImGui::GetContentRegionAvail().y-170','ImGui::GetContentRegionAvail().y-70',1)
s=s.replace('    ImGui::EndTabBar();','    ImGui::EndTabBar();\n    ImGui::EndChild();',1)
start=s.index('    ImGui::BeginChild("Status",')
end=s.index('    ImGui::BeginDisabled(state_->busy || !state_->succeeded);',start)
s=s[:start]+'''    ImGui::Separator();
    if(!status.empty())ImGui::TextWrapped("%s",status.c_str());
    else ImGui::Dummy(ImVec2(0,15));
'''+s[end:]
s=s.replace('ImVec2(175,38)','ImVec2(185,46)',1)
s=s.replace('    ImGui::PopStyleVar();\n    ImGui::End();','    ImGui::PopStyleVar();\n    ImGui::End();\n    ImGui::PopStyleVar(2);',1)
s=s.replace('  bool snapshot_valid_=false;','  bool snapshot_valid_=false;\n  bool manage_install_=false,art_attempted_=false;\n  std::unique_ptr<rex::ui::ImmediateTexture> header_art_;',1)
p.write_text(s,encoding='utf-8')
