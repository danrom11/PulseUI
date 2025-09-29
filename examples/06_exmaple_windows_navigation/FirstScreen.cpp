#include "FirstScreen.hpp"
#include "SecondScreen.hpp"

using namespace pulseui;
using namespace pulseui::ui;

FirstScreen::FirstScreen(SecondScreen* second)
    : second_(second)
{
    win_ = platform::make_window(600, 360, "First Screen");
    ui_  = std::make_unique<Manager>(*win_);

    build_ui();
}

void FirstScreen::build_ui() {
    root_ = &ui_->add<Column>();
    root_->set_rect({0, 0, 600, 360});

    title_   = &ui_->add<Label>(Rect{}, "Screen One");
    openBtn_ = &ui_->add<Button>(Rect{}, "Open second screen");
    closeAppBtn_ = &ui_->add<Button>(Rect{}, "Close App");

    root_->add_child(title_, 48);
    root_->add_child(openBtn_, 56);
    root_->add_child(closeAppBtn_, 56);

    openBtn_->on_click([this] {
        if (second_) second_->show();
    });

    closeAppBtn_->on_click([this] {
        platform::app_quit();
    });
}

void FirstScreen::show() {

}
