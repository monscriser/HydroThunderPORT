#pragma once
#include <rex/rex_app.h>
namespace ht {
void ShowSetupWizard(rex::ui::ImGuiDrawer* drawer, const rex::PathConfig& paths,
                     std::function<void(rex::PathConfig)> resume);
}
