#include "SecondScreen.hpp"

using namespace pulseui;
using namespace pulseui::ui;

SecondScreen::SecondScreen() {
    win_ = platform::make_window(520, 320, "Second Screen");
    ui_  = std::make_unique<Manager>(*win_);
    build_ui();
    win_->hide();
}

void SecondScreen::build_ui() {
    root_ = &ui_->add<Column>();
    root_->set_rect({0,0,520,320});

    title_   = &ui_->add<Label>(Rect{}, "Screen Two");
    text_    = &ui_->add<Label>(Rect{}, "This is the second screen.");
    backBtn_ = &ui_->add<Button>(Rect{}, "Close");

    root_->add_child(title_, 44);
    root_->add_child(text_, 32);
    root_->add_child(backBtn_, 48);

    backBtn_->on_click([this]{
        win_->hide();
    });
}

void SecondScreen::show() {
    win_->show();
}
