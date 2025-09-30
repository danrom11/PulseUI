#pragma once
#include <memory>
#include <pulseui/pulseui.hpp>

class SecondScreen;

class FirstScreen {
public:
    explicit FirstScreen(SecondScreen* second);
    void show();

private:
    decltype(pulseui::platform::make_window(0,0,"")) win_;
    std::unique_ptr<pulseui::ui::Manager> ui_;
    SecondScreen* second_;

    pulseui::ui::Column* root_ = nullptr;
    pulseui::ui::Label*  title_ = nullptr;
    pulseui::ui::Button* openBtn_ = nullptr;
    pulseui::ui::Button* closeAppBtn_ = nullptr;

    void build_ui();
};
