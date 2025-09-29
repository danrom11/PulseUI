#import <AppKit/AppKit.h>
namespace pulseui::platform {
  void app_init() {
    [NSApplication sharedApplication];
    [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
    id menubar = [[NSMenu alloc] init];
    id appMenuItem = [[NSMenuItem alloc] init];
    [menubar addItem:appMenuItem];
    [NSApp setMainMenu:menubar];
    id appMenu = [[NSMenu alloc] init];
    id quitMenuItem = [[NSMenuItem alloc] initWithTitle:@"Quit"
                                                 action:@selector(terminate:) keyEquivalent:@"q"];
    [appMenu addItem:quitMenuItem];
    [appMenuItem setSubmenu:appMenu];
    [NSApp activateIgnoringOtherApps:YES];
  }
  void app_run() {
    [NSApp run];
  }
  void app_quit(int /*exit_code*/) {
    [NSApp terminate:nil];
  }
}
