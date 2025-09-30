#pragma once
#include <memory>
#include <pulseui/pulseui.hpp>

class SecondScreen {
public:
    SecondScreen();
    void show();

private:
    decltype(pulseui::platform::make_window(0,0,"")) win_;
    std::unique_ptr<pulseui::ui::Manager> ui_;

    pulseui::ui::Column* root_ = nullptr;
    pulseui::ui::Label*  title_ = nullptr;
    pulseui::ui::Label*  text_  = nullptr;
    pulseui::ui::Button* backBtn_ = nullptr;

    void build_ui();
};
