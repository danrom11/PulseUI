// Load a form designed in Pulse Layer and wire it to reactive state by id.
//   pulseui_form_app path/to/login.pform
#include <pulseui/pulseui.hpp>

using namespace pulseui;

int main(int argc, char** argv) {
  App app(argc, argv);
  const std::string path = argc > 1 ? argv[1] : "login.pform";

  std::string error;
  auto form = load_form(path, &error);
  if (!form) {
    return run("Form app", 420, 160,
               VStack{Text("Could not load " + path).title(), Text(error).dim().wrap()}.padding(20));
  }

  // State lives here, not in the views.
  pulse::property<std::string> email, password;
  pulse::property<bool> remember{true};
  pulse::computed<bool> valid([&] { return email.get().find('@') != std::string::npos && password.get().size() >= 4; },
                              email, password);
  pulse::property<std::string> status;

  Window win(form->title, form->width, form->height);
  win.content(std::move(form->root));

  // Bind the designed views by their ids. Missing ids are simply skipped.
  if (auto* f = win.find<TextField>("email")) f->model(email);
  if (auto* f = win.find<TextField>("password")) f->password().model(password);
  if (auto* c = win.find<Checkbox>("remember")) c->model(remember);
  if (auto* t = win.find<Text>("status")) t->bind_text(status).dim();
  if (auto* b = win.find<Button>("ok")) {
    b->enabled(valid);
    b->clicked.connect([&] { status = "Signed in as " + email.get() + (remember.get() ? " (remembered)" : ""); });
  }
  return app.run();
}
