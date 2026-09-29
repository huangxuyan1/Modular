#include "EditorWindow.h"

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show) {
  modular::editor::EditorWindow app;
  return app.run(instance, show);
}
