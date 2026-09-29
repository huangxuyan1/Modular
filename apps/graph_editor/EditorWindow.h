#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <array>
#include <optional>
#include <string>
#include <filesystem>

#include "PrimitivePalette.h"
#include "modular/Workspace.h"
#include "modular/GraphExecution.h"

namespace modular::editor {

class EditorWindow {
public:
  int run(HINSTANCE instance, int show);

private:
  struct Endpoint {
    NodeId node;
    PortId port;
    bool input;
  };
  enum class Drag { None, Place, Move, Pan };
  enum class Transition { None, New, Open, Close };

  static LRESULT CALLBACK windowProc(HWND, UINT, WPARAM, LPARAM);
  LRESULT handle(UINT, WPARAM, LPARAM);
  void createControls();
  void resizeControls();
  void refreshInspector();
  void refreshRecorderStatus();
  void updateSelectionLabel();
  void updateSummary();
  void status(const std::wstring& message, bool error = false);
  CreationSettings readSettings() const;
  void applySettings();
  void deleteSelected();
  void paint();
  void paintContent(HDC dc);
  RECT canvasRect() const;
  RECT nodeRect(NodeId node) const;
  POINT portPoint(Endpoint port) const;
  NodePosition worldPoint(POINT point) const;
  std::optional<Endpoint> hitPort(POINT point) const;
  NodeId hitNode(POINT point) const;
  void leftDown(POINT point);
  void leftUp(POINT point);
  void mouseMove(POINT point);
  void cancelGesture();
  void selectPort(Endpoint port);
  void disconnectAt(POINT point);
  void toggleMode();
  void pollExecution();
  void requireEdit() const;
  void importMidi(bool browse);
  void fireSelected();
  void authoredChange();
  void updateTitle();
  std::optional<std::filesystem::path> chooseGraphFile(bool save);
  bool save(bool choosePath = false);
  bool confirmAbandon();
  void transition(Transition);
  void finishTransition();

  HWND window_ = nullptr;
  HFONT font_ = nullptr;
  HFONT title_font_ = nullptr;
  HFONT port_font_ = nullptr;
  Workspace workspace_;
  GraphExecution execution_;
  std::optional<GraphExecution::State> shown_state_ = GraphExecution::State::Idle;
  bool dirty_ = false;
  bool dialog_active_ = false;
  std::filesystem::path graph_path_;
  Transition pending_transition_ = Transition::None;
  std::optional<Workspace> replacement_;
  std::filesystem::path replacement_path_;
  NodeId selected_ = kNoNode;
  std::size_t palette_index_ = 0;
  std::array<CreationSettings, kPaletteSize> creation_settings_{};
  std::optional<Endpoint> wire_start_;
  Drag drag_ = Drag::None;
  POINT pointer_{};
  POINT drag_start_{};
  NodePosition drag_origin_{};
  NodePosition pan_{};
  bool error_ = false;
};

} // namespace modular::editor
