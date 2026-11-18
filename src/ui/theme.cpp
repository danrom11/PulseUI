#include <pulseui/ui/events.hpp>
#include <pulseui/ui/theme.hpp>

#include <cctype>
#include <string>

namespace pulseui {

Theme Theme::dark() {
  Theme t;
  t.name = "dark";
  t.window = Color::hex(0x16181D);
  t.surface = Color::hex(0x1E2128);
  t.surface_alt = Color::hex(0x252932);
  t.control = Color::hex(0x2B303A);
  t.control_hover = Color::hex(0x343A46);
  t.control_pressed = Color::hex(0x232730);
  t.input = Color::hex(0x12141A);
  t.border = Color::hex(0x353B47);
  t.border_focus = Color::hex(0x5B8CFF);
  t.text = Color::hex(0xE6E9EF);
  t.text_dim = Color::hex(0x9AA3B2);
  t.text_disabled = Color::hex(0x5D6573);
  t.accent = Color::hex(0x4F7DF3);
  t.accent_hover = Color::hex(0x6A92F7);
  t.accent_pressed = Color::hex(0x3E69D6);
  t.on_accent = Color::white();
  t.selection = Color::hex(0x4F7DF3, 0.35f);
  t.danger = Color::hex(0xE5534B);
  t.success = Color::hex(0x3FB950);
  t.warning = Color::hex(0xD29922);
  t.shadow = Color(0, 0, 0, 0.45f);
  t.tooltip = Color::hex(0x0E1014);
  t.scrollbar = Color(1, 1, 1, 0.18f);
  t.font_mono.family = "monospace";
  return t;
}

Theme Theme::light() {
  Theme t = dark();
  t.name = "light";
  t.window = Color::hex(0xF3F4F7);
  t.surface = Color::hex(0xFFFFFF);
  t.surface_alt = Color::hex(0xF7F8FA);
  t.control = Color::hex(0xEDEFF3);
  t.control_hover = Color::hex(0xE2E5EB);
  t.control_pressed = Color::hex(0xD6DAE1);
  t.input = Color::hex(0xFFFFFF);
  t.border = Color::hex(0xD3D7DF);
  t.border_focus = Color::hex(0x3D6FE8);
  t.text = Color::hex(0x1C2027);
  t.text_dim = Color::hex(0x5F6877);
  t.text_disabled = Color::hex(0xA4ABB6);
  t.accent = Color::hex(0x3D6FE8);
  t.accent_hover = Color::hex(0x5583F0);
  t.accent_pressed = Color::hex(0x2F5BC9);
  t.selection = Color::hex(0x3D6FE8, 0.25f);
  t.shadow = Color(0, 0, 0, 0.18f);
  t.tooltip = Color::hex(0x2A2F38);
  t.scrollbar = Color(0, 0, 0, 0.22f);
  return t;
}

pulse::property<Theme>& theme() {
  static pulse::property<Theme> t{Theme::dark()};
  return t;
}

const Theme& th() {
  // Snapshot updated through the property's change signal (cheap reads while painting).
  static Theme snapshot = theme().get();
  static auto conn = theme().changed.connect([](const Theme& t) { snapshot = t; });
  (void)conn;
  return snapshot;
}

Shortcut Shortcut::parse(std::string_view s) {
  Shortcut sc;
  std::size_t start = 0;
  while (start <= s.size()) {
    std::size_t plus = s.find('+', start);
    if (plus == start && plus + 1 == s.size()) plus = std::string_view::npos; // "Ctrl++"
    std::string tok(s.substr(start, plus == std::string_view::npos ? std::string_view::npos : plus - start));
    for (auto& c : tok) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (tok == "ctrl" || tok == "cmd" || tok == "primary") sc.mods |= Mod::Primary;
    else if (tok == "meta" || tok == "super" || tok == "win") sc.mods |= Mod::Super;
    else if (tok == "shift") sc.mods |= Mod::Shift;
    else if (tok == "alt" || tok == "option") sc.mods |= Mod::Alt;
    else if (tok.size() == 1 && tok[0] >= 'a' && tok[0] <= 'z')
      sc.key = static_cast<Key>(static_cast<int>(Key::A) + (tok[0] - 'a'));
    else if (tok.size() == 1 && tok[0] >= '0' && tok[0] <= '9')
      sc.key = static_cast<Key>(static_cast<int>(Key::Num0) + (tok[0] - '0'));
    else if (tok.size() >= 2 && tok[0] == 'f' && std::isdigit(static_cast<unsigned char>(tok[1])))
      sc.key = static_cast<Key>(static_cast<int>(Key::F1) + std::stoi(tok.substr(1)) - 1);
    else if (tok == "esc" || tok == "escape") sc.key = Key::Escape;
    else if (tok == "enter" || tok == "return") sc.key = Key::Enter;
    else if (tok == "del" || tok == "delete") sc.key = Key::Delete;
    else if (tok == "backspace") sc.key = Key::Backspace;
    else if (tok == "tab") sc.key = Key::Tab;
    else if (tok == "space") sc.key = Key::Space;
    else if (tok == "up") sc.key = Key::Up;
    else if (tok == "down") sc.key = Key::Down;
    else if (tok == "left") sc.key = Key::Left;
    else if (tok == "right") sc.key = Key::Right;
    else if (tok == "home") sc.key = Key::Home;
    else if (tok == "end") sc.key = Key::End;
    if (plus == std::string_view::npos) break;
    start = plus + 1;
  }
  return sc;
}

} // namespace pulseui
