#include "EditorWindow.h"
#include <windowsx.h>
#include <commdlg.h>
#include <commctrl.h>

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include "modular/nodes/ManualTriggerNode.h"
#include "modular/nodes/MidiFileNode.h"
#include "modular/nodes/AudioOutputNode.h"
#include "modular/nodes/PrimitivePersistence.h"
#include "modular/nodes/WavRecorderNode.h"

#pragma comment(linker, "/manifestdependency:\"type='win32' " \
    "name='Microsoft.Windows.Common-Controls' version='6.0.0.0' " \
    "processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

namespace modular::editor {
namespace {

constexpr int kSidebar = 252, kTop = 100, kNodeWidth = 300, kPortStep = 28;
constexpr int kValue = 101, kChannels = 102, kInputs = 103, kApply = 104;
constexpr int kStatus = 105, kSummary = 106, kSelection = 107, kDelete = 108;
constexpr int kHelp = 109, kValueLabel = 110, kChannelsLabel = 111, kInputsLabel = 112;
constexpr int kMode = 113, kFilePath = 114, kBrowse = 115, kLoad = 116, kFire = 117;
constexpr int kDirectory = 118, kPrefix = 119, kDirectoryLabel = 120, kPrefixLabel = 121;
constexpr int kRetry = 122, kDiscard = 123;
constexpr int kNew = 201, kOpen = 202, kSave = 203, kSaveAs = 204, kClose = 205;
constexpr COLORREF kInk = RGB(31, 45, 59), kMuted = RGB(94, 112, 129);
constexpr COLORREF kAccent = RGB(0, 122, 117), kBackground = RGB(243, 246, 249);

RECT paletteRowRect(std::size_t index) {
  const LONG y = 120 + static_cast<LONG>(index) * 22;
  return {16, y, kSidebar - 16, y + 21};
}

void fill(HDC dc, RECT rect, COLORREF color) {
  HBRUSH brush = CreateSolidBrush(color);
  FillRect(dc, &rect, brush);
  DeleteObject(brush);
}

void label(HDC dc, RECT rect, const std::wstring& text, COLORREF color = kInk,
           UINT format = DT_LEFT | DT_SINGLELINE | DT_VCENTER) {
  SetTextColor(dc, color);
  DrawTextW(dc, text.c_str(), -1, &rect, format | DT_NOPREFIX);
}

void line(HDC dc, POINT from, POINT to, COLORREF color, int width = 1) {
  HPEN pen = CreatePen(PS_SOLID, width, color);
  auto old = SelectObject(dc, pen);
  MoveToEx(dc, from.x, from.y, nullptr);
  LineTo(dc, to.x, to.y);
  SelectObject(dc, old);
  DeleteObject(pen);
}

void curve(HDC dc, POINT from, POINT to, COLORREF color) {
  const LONG bend = std::max(50L, std::abs(to.x - from.x) / 2);
  POINT points[]{from, {from.x + bend, from.y}, {to.x - bend, to.y}, to};
  HPEN pen = CreatePen(PS_SOLID, 2, color);
  auto old = SelectObject(dc, pen);
  PolyBezier(dc, points, 4);
  SelectObject(dc, old);
  DeleteObject(pen);
}

std::wstring controlText(HWND window, int id) {
  wchar_t buffer[32768]{};
  GetDlgItemTextW(window, id, buffer, 32768);
  std::wstring text(buffer);
  const auto first = text.find_first_not_of(L" \t\r\n");
  if (first == std::wstring::npos) return {};
  return text.substr(first, text.find_last_not_of(L" \t\r\n") - first + 1);
}

std::wstring floatText(float value) {
  std::wostringstream text;
  text << std::setprecision(9) << value;
  return text.str();
}

std::uint32_t countSetting(const std::wstring& text, std::uint32_t limit,
                           const char* message) {
  if (text.empty() || text.find_first_not_of(L"0123456789") != std::wstring::npos)
    throw std::invalid_argument(message);
  try {
    const auto value = std::stoull(text);
    if (value >= 1 && value <= limit) return static_cast<std::uint32_t>(value);
  } catch (const std::exception&) {}
  throw std::invalid_argument(message);
}

} // namespace

int EditorWindow::run(HINSTANCE instance, int show) {
  INITCOMMONCONTROLSEX common{sizeof(common), ICC_STANDARD_CLASSES};
  InitCommonControlsEx(&common);
  WNDCLASSW windowClass{};
  windowClass.lpfnWndProc = windowProc;
  windowClass.hInstance = instance;
  windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  windowClass.lpszClassName = L"ModularGraphEditor";
  if (!RegisterClassW(&windowClass)) return 1;
  RECT size{0, 0, 1240, 800};
  AdjustWindowRect(&size, WS_OVERLAPPEDWINDOW, TRUE);
  window_ = CreateWindowExW(0, windowClass.lpszClassName, L"Modular | Graph editor",
      WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, CW_USEDEFAULT, CW_USEDEFAULT,
      size.right - size.left, size.bottom - size.top, nullptr, nullptr, instance, this);
  if (!window_) return 1;
  ShowWindow(window_, show);
  ACCEL shortcuts[]{{FVIRTKEY | FCONTROL, 'N', kNew},
                    {FVIRTKEY | FCONTROL, 'O', kOpen},
                    {FVIRTKEY | FCONTROL, 'S', kSave}};
  const auto accelerators = CreateAcceleratorTableW(shortcuts, 3);
  MSG message{};
  int result = 0;
  while ((result = GetMessageW(&message, nullptr, 0, 0)) > 0) {
    if (!TranslateAcceleratorW(window_, accelerators, &message) &&
        !IsDialogMessageW(window_, &message)) {
      TranslateMessage(&message);
      DispatchMessageW(&message);
    }
  }
  DeleteObject(font_);
  DeleteObject(title_font_);
  DeleteObject(port_font_);
  DestroyAcceleratorTable(accelerators);
  return result == -1 ? 1 : static_cast<int>(message.wParam);
}

LRESULT CALLBACK EditorWindow::windowProc(HWND window, UINT message,
                                          WPARAM w, LPARAM l) {
  auto* self = reinterpret_cast<EditorWindow*>(GetWindowLongPtrW(window, GWLP_USERDATA));
  if (message == WM_NCCREATE) {
    self = static_cast<EditorWindow*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);
    self->window_ = window;
    SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
  }
  if (!self) return DefWindowProcW(window, message, w, l);
  const auto previousIdentity = self->workspace_.graph().nextIdentity();
  try {
    return self->handle(message, w, l);
  } catch (const std::exception& error) {
    // Construction can consume port identities before node adoption fails.
    // Those identities stay retired, so even this failed edit has saved history.
    if (self->workspace_.graph().nextIdentity() != previousIdentity) self->authoredChange();
    self->cancelGesture();
    self->status(widen(error.what()), true);
    self->updateSummary();
    return message == WM_CREATE ? -1 : 0;
  }
}

LRESULT EditorWindow::handle(UINT message, WPARAM w, LPARAM l) {
  const POINT point{GET_X_LPARAM(l), GET_Y_LPARAM(l)};
  switch (message) {
    case WM_CREATE:
      createControls();
      SetTimer(window_, 1, 30, nullptr);
      refreshInspector();
      updateSummary();
      updateTitle();
      status(L"Drag a primitive from the palette onto the canvas.");
      return 0;
    case WM_TIMER: pollExecution(); return 0;
    case WM_CLOSE:
      transition(Transition::Close);
      return 0;
    case WM_SIZE: resizeControls(); InvalidateRect(window_, nullptr, FALSE); return 0;
    case WM_GETMINMAXINFO:
      reinterpret_cast<MINMAXINFO*>(l)->ptMinTrackSize = {1080, 840};
      return 0;
    case WM_PAINT: paint(); return 0;
    case WM_PRINTCLIENT: paintContent(reinterpret_cast<HDC>(w)); return 0;
    case WM_ERASEBKGND: return 1;
    case WM_LBUTTONDOWN: SetFocus(window_); leftDown(point); return 0;
    case WM_LBUTTONUP: leftUp(point); return 0;
    case WM_MOUSEMOVE:
      if ((drag_ == Drag::Move || drag_ == Drag::Place) && !(w & MK_LBUTTON))
        cancelGesture();
      else if (drag_ == Drag::Pan && !(w & MK_MBUTTON))
        cancelGesture();
      else mouseMove(point);
      return 0;
    case WM_RBUTTONUP: disconnectAt(point); return 0;
    case WM_MBUTTONDOWN:
      if (const auto canvas = canvasRect(); PtInRect(&canvas, point)) {
        drag_ = Drag::Pan; drag_start_ = point; drag_origin_ = pan_; SetCapture(window_);
      }
      return 0;
    case WM_MBUTTONUP: drag_ = Drag::None; ReleaseCapture(); return 0;
    case WM_MOUSEWHEEL:
      pan_.y += GET_WHEEL_DELTA_WPARAM(w) / 2;
      InvalidateRect(window_, nullptr, FALSE);
      return 0;
    case WM_CAPTURECHANGED:
    case WM_CANCELMODE: cancelGesture(); return 0;
    case WM_KEYDOWN:
      if (w == VK_DELETE) deleteSelected();
      if (w == VK_ESCAPE) { cancelGesture(); status(L"Gesture cancelled."); }
      return 0;
    case WM_COMMAND:
      if (LOWORD(w) == kNew) transition(Transition::New);
      if (LOWORD(w) == kOpen) transition(Transition::Open);
      if (LOWORD(w) == kClose) transition(Transition::Close);
      if (LOWORD(w) == kSave || LOWORD(w) == kSaveAs) {
        if (!dialog_active_ && pending_transition_ == Transition::None) {
          dialog_active_ = true;
          try { cancelGesture(); save(LOWORD(w) == kSaveAs); }
          catch (...) { dialog_active_ = false; throw; }
          dialog_active_ = false;
        }
      }
      if (HIWORD(w) == BN_CLICKED && LOWORD(w) == kMode) toggleMode();
      if (HIWORD(w) == BN_CLICKED && LOWORD(w) == kBrowse) importMidi(true);
      if (HIWORD(w) == BN_CLICKED && LOWORD(w) == kLoad) importMidi(false);
      if (HIWORD(w) == BN_CLICKED && LOWORD(w) == kFire) fireSelected();
      if (HIWORD(w) == BN_CLICKED && (LOWORD(w) == kRetry || LOWORD(w) == kDiscard) &&
          selected_ != kNoNode && execution_.state() == GraphExecution::State::Running) {
        if (auto* recorder = dynamic_cast<nodes::WavRecorderNode*>(
                &workspace_.graph().node(selected_))) {
          if (LOWORD(w) == kRetry) recorder->retry();
          else recorder->discard();
        }
      }
      if (LOWORD(w) == IDCANCEL) { cancelGesture(); status(L"Gesture cancelled."); }
      if (LOWORD(w) == IDOK) applySettings();
      if (HIWORD(w) == BN_CLICKED && LOWORD(w) == kApply) applySettings();
      if (HIWORD(w) == BN_CLICKED && LOWORD(w) == kDelete) deleteSelected();
      return 0;
    case WM_CTLCOLORSTATIC: {
      const HDC dc = reinterpret_cast<HDC>(w);
      SetBkColor(dc, RGB(255, 255, 255));
      SetTextColor(dc, GetDlgCtrlID(reinterpret_cast<HWND>(l)) == kStatus && error_
                          ? RGB(173, 45, 35) : kInk);
      return reinterpret_cast<LRESULT>(GetStockObject(WHITE_BRUSH));
    }
    case WM_DESTROY: PostQuitMessage(0); return 0;
  }
  return DefWindowProcW(window_, message, w, l);
}

void EditorWindow::createControls() {
  const auto menu = CreateMenu();
  const auto file = CreatePopupMenu();
  AppendMenuW(file, MF_STRING, kNew, L"&New\tCtrl+N");
  AppendMenuW(file, MF_STRING, kOpen, L"&Open...\tCtrl+O");
  AppendMenuW(file, MF_STRING, kSave, L"&Save\tCtrl+S");
  AppendMenuW(file, MF_STRING, kSaveAs, L"Save &As...");
  AppendMenuW(file, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(file, MF_STRING, kClose, L"E&xit");
  AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(file), L"&File");
  SetMenu(window_, menu);
  font_ = CreateFontW(-16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
      OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
  title_font_ = CreateFontW(-24, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
      OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
  port_font_ = CreateFontW(-13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
      OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
  const auto control = [&](const wchar_t* kind, const wchar_t* text, int id,
                           int x, int y, int width, int height, DWORD style = 0) {
    HWND child = CreateWindowExW(0, kind, text, WS_CHILD | WS_VISIBLE | style,
        x, y, width, height, window_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
        GetModuleHandleW(nullptr), nullptr);
    if (!child) throw std::runtime_error("window: could not create inspector control");
    SendMessageW(child, WM_SETFONT, reinterpret_cast<WPARAM>(font_), TRUE);
    if (std::wstring(kind) == L"EDIT")
      SendMessageW(child, EM_SETLIMITTEXT,
          id == kFilePath || id == kDirectory ? 32767 : id == kHelp ? 8192 : 64, 0);
  };
  control(L"STATIC", L"", kSelection, 16, 342, 220, 48);
  control(L"STATIC", L"Value", kValueLabel, 16, 400, 100, 24);
  control(L"EDIT", L"1", kValue, 120, 394, 116, 28, WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL);
  control(L"STATIC", L"Channels (1-64)", kChannelsLabel, 16, 452, 105, 42);
  control(L"EDIT", L"1", kChannels, 120, 450, 116, 28, WS_BORDER | WS_TABSTOP);
  control(L"STATIC", L"Inputs (1-32)", kInputsLabel, 16, 512, 100, 24);
  control(L"EDIT", L"2", kInputs, 120, 506, 116, 28, WS_BORDER | WS_TABSTOP);
  control(L"BUTTON", L"Apply settings", kApply, 16, 554, 220, 32, WS_TABSTOP);
  control(L"BUTTON", L"Delete node and wires", kDelete, 16, 598, 220, 32, WS_TABSTOP);
  control(L"EDIT", L"", kHelp, 16, 650, 220, 100,
          ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL);
  control(L"EDIT", L"", kFilePath, 16, 394, 220, 28,
          WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL);
  control(L"BUTTON", L"Browse MIDI...", kBrowse, 16, 438, 220, 32, WS_TABSTOP);
  control(L"BUTTON", L"Load MIDI", kLoad, 16, 482, 220, 32, WS_TABSTOP);
  control(L"BUTTON", L"Start", kFire, 16, 394, 220, 38, WS_TABSTOP);
  control(L"STATIC", L"Output directory", kDirectoryLabel, 16, 394, 220, 22);
  control(L"EDIT", L".", kDirectory, 16, 416, 220, 28,
          WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL);
  control(L"STATIC", L"Filename prefix", kPrefixLabel, 16, 486, 220, 22);
  control(L"EDIT", L"take", kPrefix, 16, 510, 220, 28,
          WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL);
  control(L"BUTTON", L"Retry", kRetry, 16, 554, 106, 32, WS_TABSTOP);
  control(L"BUTTON", L"Discard", kDiscard, 130, 554, 106, 32, WS_TABSTOP);
  control(L"STATIC", L"", kStatus, 16, 764, 1208, 28);
  control(L"STATIC", L"", kSummary, 948, 28, 276, 26, SS_RIGHT);
  control(L"BUTTON", L"Run", kMode, 650, 18, 130, 34, WS_TABSTOP);
  resizeControls();
}

void EditorWindow::resizeControls() {
  RECT client{};
  GetClientRect(window_, &client);
  MoveWindow(GetDlgItem(window_, kStatus), 16, client.bottom - 32,
              client.right - 32, 28, TRUE);
  MoveWindow(GetDlgItem(window_, kSummary), client.right - 292, 28, 276, 26, TRUE);
  MoveWindow(GetDlgItem(window_, kHelp), 16, 650, 220,
              std::max(70L, client.bottom - 700), TRUE);
}

void EditorWindow::updateSelectionLabel() {
  std::wstring text;
  if (selected_ != kNoNode) {
    const auto position = workspace_.position(selected_);
    text = presentation(workspace_.graph().node(selected_)).name;
    text += L" #" + std::to_wstring(selected_) + L"\r\n(" +
        std::to_wstring(static_cast<int>(position.x)) + L", " +
        std::to_wstring(static_cast<int>(position.y)) + L") | " +
        (execution_.active() ? L"Run" : L"Edit settings");
  } else {
    text = L"Create " + std::wstring(palette()[palette_index_].name);
    text += L"\r\nSettings for the next placement";
  }
  SetDlgItemTextW(window_, kSelection, text.c_str());
}

void EditorWindow::refreshInspector() {
  const auto& entry = selected_ == kNoNode ? palette()[palette_index_]
      : presentation(workspace_.graph().node(selected_));
  const auto settings = selected_ == kNoNode ? creation_settings_[palette_index_]
      : entry.settings(workspace_.graph().node(selected_));
  SetDlgItemTextW(window_, kValue, floatText(settings.constant).c_str());
  SetDlgItemTextW(window_, kChannels, std::to_wstring(settings.channels).c_str());
  SetDlgItemTextW(window_, kInputs, std::to_wstring(settings.inputs).c_str());
  for (const auto [id, visible] : {std::pair{kValue, entry.hasConstant},
       {kValueLabel, entry.hasConstant}, {kChannels, entry.hasChannels},
       {kChannelsLabel, entry.hasChannels}, {kInputs, entry.hasInputs},
       {kInputsLabel, entry.hasInputs}})
    ShowWindow(GetDlgItem(window_, id), visible ? SW_SHOW : SW_HIDE);
  const Node* selected = selected_ == kNoNode ? nullptr : &workspace_.graph().node(selected_);
  const auto* midi = dynamic_cast<const nodes::MidiFileNode*>(selected);
  const auto* manual = dynamic_cast<const nodes::ManualTriggerNode*>(selected);
  const bool recorder = entry.hasRecording;
  SetDlgItemTextW(window_, kDirectory, settings.directory.c_str());
  SetDlgItemTextW(window_, kPrefix, widen(settings.prefix).c_str());
  for (const int id : {kDirectory, kPrefix, kDirectoryLabel, kPrefixLabel}) {
    ShowWindow(GetDlgItem(window_, id), recorder ? SW_SHOW : SW_HIDE);
    EnableWindow(GetDlgItem(window_, id), !execution_.active());
  }
  for (const int id : {kRetry, kDiscard})
    ShowWindow(GetDlgItem(window_, id), recorder && execution_.active() ? SW_SHOW : SW_HIDE);
  std::wstring help = entry.help;
  if (midi) {
    help = midi->ready() ? L"Ready: " + std::to_wstring(midi->file().entries().size()) +
        L" MIDI messages; skipped SysEx: " + std::to_wstring(midi->file().skippedSysExEvents()) +
        L".\r\n" + help : L"Not ready: select and load a MIDI file.\r\n" + help;
    if (!midi->importError().empty()) help = widen(midi->importError()) + L"\r\n" + help;
  } else if (const auto* block = dynamic_cast<const nodes::MidiBlockNode*>(selected)) {
    help = std::to_wstring(block->entries().size()) + L" stored MIDI messages.\r\n" + help;
  }
  SetDlgItemTextW(window_, kHelp, help.c_str());
  for (const int id : {kFilePath, kBrowse, kLoad}) {
    ShowWindow(GetDlgItem(window_, id), midi ? SW_SHOW : SW_HIDE);
    EnableWindow(GetDlgItem(window_, id), !execution_.active());
  }
  ShowWindow(GetDlgItem(window_, kFire), manual ? SW_SHOW : SW_HIDE);
  SetDlgItemTextW(window_, kFire, manual && manual->label() == nodes::ManualTriggerNode::Label::Stop
      ? L"Stop" : L"Start");
  EnableWindow(GetDlgItem(window_, kFire), execution_.state() == GraphExecution::State::Running);
  SetDlgItemTextW(window_, kInputsLabel,
      std::wstring(entry.name) == L"VoiceAllocatorNode" ? L"Voices (1-32)" : L"Inputs (1-32)");
  SetDlgItemTextW(window_, kChannelsLabel,
      recorder ? L"Channels" : std::wstring(entry.name) == L"AudioOutputNode"
          ? L"Channels (1-2)" : L"Channels (1-64)");
  SetDlgItemTextW(window_, kApply, selected_ == kNoNode ? L"Use for next placement"
                                                       : L"Apply settings");
  EnableWindow(GetDlgItem(window_, kDelete), selected_ != kNoNode && !execution_.active());
  for (const int id : {kValue, kChannels, kInputs, kApply})
    EnableWindow(GetDlgItem(window_, id), !execution_.active());
  ShowWindow(GetDlgItem(window_, kApply),
      (entry.hasConstant || entry.hasChannels || entry.hasInputs) &&
      !(recorder && execution_.active()) ? SW_SHOW : SW_HIDE);
  refreshRecorderStatus();
  updateSelectionLabel();
  InvalidateRect(window_, nullptr, FALSE);
}

void EditorWindow::refreshRecorderStatus() {
  if (selected_ == kNoNode) return;
  const auto* recorder = dynamic_cast<const nodes::WavRecorderNode*>(
      &workspace_.graph().node(selected_));
  if (!recorder) return;
  auto help = widen(recorder->statusText());
  if (recorder->state() != nodes::WavRecorderNode::State::Failed)
    help += L"\r\n" + std::wstring(presentation(*recorder).help);
  if (controlText(window_, kHelp) != help) SetDlgItemTextW(window_, kHelp, help.c_str());
  for (const int id : {kRetry, kDiscard})
    EnableWindow(GetDlgItem(window_, id),
        execution_.state() == GraphExecution::State::Running &&
        recorder->state() == nodes::WavRecorderNode::State::Failed);
}

CreationSettings EditorWindow::readSettings() const {
  const auto& entry = selected_ == kNoNode ? palette()[palette_index_]
      : presentation(workspace_.graph().node(selected_));
  CreationSettings settings;
  if (entry.hasConstant) {
    const auto text = controlText(window_, kValue);
    std::size_t used = 0;
    try { settings.constant = std::stof(text, &used); }
    catch (const std::exception&) {
      throw std::invalid_argument("constant: enter a finite number");
    }
    if (used != text.size() || !std::isfinite(settings.constant))
      throw std::invalid_argument("constant: enter a finite number");
  }
  if (entry.hasChannels)
    settings.channels = countSetting(controlText(window_, kChannels), 64,
                                      "channels: enter a whole number from 1 to 64");
  if (entry.hasInputs)
    settings.inputs = countSetting(controlText(window_, kInputs), 32,
                                    "inputs: enter a whole number from 1 to 32");
  if (entry.hasRecording) {
    settings.directory = controlText(window_, kDirectory);
    const auto prefix = std::filesystem::path(controlText(window_, kPrefix)).u8string();
    settings.prefix.assign(prefix.begin(), prefix.end());
  }
  return settings;
}

void EditorWindow::applySettings() {
  requireEdit();
  const auto settings = readSettings();
  if (selected_ == kNoNode) creation_settings_[palette_index_] = settings;
  else {
    const auto& entry = presentation(workspace_.graph().node(selected_));
    const auto before = entry.settings(workspace_.graph().node(selected_));
    entry.apply(workspace_.graph(), selected_, settings);
    if (before != entry.settings(workspace_.graph().node(selected_))) authoredChange();
  }
  wire_start_.reset();
  refreshInspector();
  status(L"Settings applied.");
}

void EditorWindow::updateSummary() {
  const auto& graph = workspace_.graph();
  const auto text = std::to_wstring(graph.nodeCount()) + L" nodes | " +
      std::to_wstring(graph.connections().size()) + L" wires | " +
      (execution_.active() ? L"Run" : L"Edit");
  SetDlgItemTextW(window_, kSummary, text.c_str());
  InvalidateRect(window_, nullptr, FALSE);
}

void EditorWindow::status(const std::wstring& message, bool error) {
  error_ = error;
  SetDlgItemTextW(window_, kStatus, message.c_str());
  InvalidateRect(GetDlgItem(window_, kStatus), nullptr, TRUE);
}

void EditorWindow::deleteSelected() {
  requireEdit();
  if (selected_ == kNoNode) return;
  cancelGesture();
  workspace_.deleteNodeWithConnections(selected_);
  authoredChange();
  selected_ = kNoNode;
  refreshInspector();
  updateSummary();
  status(L"Deleted node and its attached wires.");
}

RECT EditorWindow::canvasRect() const {
  RECT rect{};
  GetClientRect(window_, &rect);
  return {kSidebar, kTop, rect.right, rect.bottom - 44};
}

NodePosition EditorWindow::worldPoint(POINT point) const {
  return {point.x - kSidebar - pan_.x, point.y - kTop - pan_.y};
}

RECT EditorWindow::nodeRect(NodeId id) const {
  const auto& node = workspace_.graph().node(id);
  const auto position = workspace_.position(id);
  const LONG x = static_cast<LONG>(position.x + pan_.x) + kSidebar;
  const LONG y = static_cast<LONG>(position.y + pan_.y) + kTop;
  const LONG rows = static_cast<LONG>(std::max(node.inputs().size(), node.outputs().size()));
  return {x, y, x + kNodeWidth, y + 76 + rows * kPortStep};
}

POINT EditorWindow::portPoint(Endpoint endpoint) const {
  const auto& node = workspace_.graph().node(endpoint.node);
  const auto ports = endpoint.input ? node.inputs() : node.outputs();
  const auto rect = nodeRect(endpoint.node);
  for (std::size_t row = 0; row < ports.size(); ++row)
    if (ports[row].id == endpoint.port)
      return {endpoint.input ? rect.left + 12 : rect.right - 12,
               rect.top + 58 + static_cast<LONG>(row) * kPortStep};
  throw std::invalid_argument("canvas: port no longer exists");
}

NodeId EditorWindow::hitNode(POINT point) const {
  const auto canvas = canvasRect();
  if (!PtInRect(&canvas, point)) return kNoNode;
  if (selected_ != kNoNode) {
    const auto rect = nodeRect(selected_);
    if (PtInRect(&rect, point)) return selected_;
  }
  auto ids = workspace_.graph().nodeIds();
  for (auto at = ids.rbegin(); at != ids.rend(); ++at) {
    const auto rect = nodeRect(*at);
    if (PtInRect(&rect, point)) return *at;
  }
  return kNoNode;
}

std::optional<EditorWindow::Endpoint> EditorWindow::hitPort(POINT point) const {
  const auto id = hitNode(point);
  if (id == kNoNode) return {};
  const auto& node = workspace_.graph().node(id);
  for (const bool input : {true, false})
    for (const auto& port : input ? node.inputs() : node.outputs()) {
      const Endpoint endpoint{id, port.id, input};
      const auto center = portPoint(endpoint);
      if (std::abs(point.x - center.x) <= 10 && std::abs(point.y - center.y) <= 10)
        return endpoint;
    }
  return {};
}

void EditorWindow::selectPort(Endpoint endpoint) {
  requireEdit();
  selected_ = endpoint.node;
  refreshInspector();
  if (!wire_start_) {
    wire_start_ = endpoint;
    status(L"Choose the other end of the wire. Esc cancels.");
  } else {
    const auto start = *wire_start_;
    wire_start_.reset();
    const auto output = start.input ? endpoint : start;
    const auto input = start.input ? start : endpoint;
    workspace_.graph().connect(output.node, output.port, input.node, input.port);
    authoredChange();
    updateSummary();
    status(L"Connected.");
  }
}

void EditorWindow::leftDown(POINT point) {
  pointer_ = point;
  drag_start_ = point;
  for (std::size_t index = 0; index < palette().size(); ++index) {
    const auto row = paletteRowRect(index);
    if (!PtInRect(&row, point)) continue;
    requireEdit();
    if (selected_ == kNoNode && palette_index_ == index)
      creation_settings_[index] = readSettings();
    selected_ = kNoNode;
    palette_index_ = index;
    wire_start_.reset();
    refreshInspector();
    drag_ = Drag::Place;
    SetCapture(window_);
    return;
  }
  if (const auto port = hitPort(point)) { selectPort(*port); return; }
  const auto canvas = canvasRect();
  if (!PtInRect(&canvas, point)) return;
  wire_start_.reset();
  selected_ = hitNode(point);
  refreshInspector();
  if (selected_ != kNoNode) {
    drag_ = Drag::Move;
    drag_origin_ = workspace_.position(selected_);
    SetCapture(window_);
  }
}

void EditorWindow::leftUp(POINT point) {
  const auto previous = drag_;
  // Releasing capture sends WM_CAPTURECHANGED synchronously. The completed
  // gesture must no longer be cancellable when that notification arrives.
  drag_ = Drag::None;
  if (GetCapture() == window_) ReleaseCapture();
  const auto canvas = canvasRect();
  if (previous == Drag::Place && PtInRect(&canvas, point)) {
    requireEdit();
    const auto& entry = palette()[palette_index_];
    selected_ = entry.create(workspace_.graph(), creation_settings_[palette_index_]);
    workspace_.setPosition(selected_, worldPoint(point));
    authoredChange();
    refreshInspector();
    updateSummary();
    status(L"Placed " + std::wstring(entry.name) + L". Drag the header to move it.");
  } else if (previous == Drag::Move) {
    if (workspace_.position(selected_) != drag_origin_) authoredChange();
    status(L"Moved node. Its wires follow its ports.");
  } else if (wire_start_ && (std::abs(point.x - drag_start_.x) > 5 ||
                            std::abs(point.y - drag_start_.y) > 5)) {
    if (const auto port = hitPort(point)) selectPort(*port);
  }
}

void EditorWindow::mouseMove(POINT point) {
  pointer_ = point;
  if (drag_ == Drag::Move && selected_ != kNoNode) {
    workspace_.setPosition(selected_, {drag_origin_.x + point.x - drag_start_.x,
                                      drag_origin_.y + point.y - drag_start_.y});
    updateSelectionLabel();
  } else if (drag_ == Drag::Pan) {
    pan_ = {drag_origin_.x + point.x - drag_start_.x,
             drag_origin_.y + point.y - drag_start_.y};
  }
  if (drag_ != Drag::None || wire_start_) InvalidateRect(window_, nullptr, FALSE);
}

void EditorWindow::cancelGesture() {
  if (drag_ == Drag::Move && selected_ != kNoNode)
    workspace_.setPosition(selected_, drag_origin_);
  if (drag_ == Drag::Pan) pan_ = drag_origin_;
  drag_ = Drag::None;
  wire_start_.reset();
  if (GetCapture() == window_) ReleaseCapture();
  updateSelectionLabel();
  InvalidateRect(window_, nullptr, FALSE);
}

void EditorWindow::disconnectAt(POINT point) {
  requireEdit();
  wire_start_.reset();
  const auto port = hitPort(point);
  if (!port || !port->input) {
    status(L"Right-click the input end of a wire to disconnect it.");
    return;
  }
  workspace_.graph().disconnect(port->node, port->port);
  authoredChange();
  selected_ = port->node;
  refreshInspector();
  updateSummary();
  status(L"Disconnected.");
}

void EditorWindow::requireEdit() const {
  if (execution_.active())
    throw std::invalid_argument("edit: return to Edit before changing the graph");
}

void EditorWindow::importMidi(bool browse) {
  requireEdit();
  if (selected_ == kNoNode) return;
  auto* midi = dynamic_cast<nodes::MidiFileNode*>(&workspace_.graph().node(selected_));
  if (!midi) return;
  if (browse) {
    wchar_t path[32768]{};
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = window_;
    dialog.lpstrFilter = L"MIDI files (*.mid;*.midi)\0*.mid;*.midi\0All files\0*.*\0";
    dialog.lpstrFile = path;
    dialog.nMaxFile = 32768;
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetOpenFileNameW(&dialog)) {
      if (CommDlgExtendedError()) status(L"MIDI file chooser failed; enter a path and Load MIDI.", true);
      return;
    }
    SetDlgItemTextW(window_, kFilePath, path);
  }
  try { midi->import(std::filesystem::path(controlText(window_, kFilePath))); }
  catch (...) { refreshInspector(); throw; }
  authoredChange();
  refreshInspector();
  status(L"MIDI loaded into node. Wire Start/Stop and MIDI output, then enter Run.");
}

void EditorWindow::fireSelected() {
  if (execution_.state() != GraphExecution::State::Running || selected_ == kNoNode) return;
  if (auto* manual = dynamic_cast<nodes::ManualTriggerNode*>(&workspace_.graph().node(selected_))) {
    manual->press();
    status(std::wstring(manual->label() == nodes::ManualTriggerNode::Label::Start
        ? L"Start" : L"Stop") + L" trigger queued through this node's output.");
  }
}

void EditorWindow::authoredChange() {
  dirty_ = true;
  updateTitle();
}

void EditorWindow::updateTitle() {
  const auto name = graph_path_.empty() ? L"Untitled" : graph_path_.filename().wstring();
  SetWindowTextW(window_, (L"Modular | Graph editor | " + std::wstring(name) +
                          (dirty_ ? L" *" : L"")).c_str());
}

std::optional<std::filesystem::path> EditorWindow::chooseGraphFile(bool saving) {
  wchar_t path[32768]{};
  if (saving && !graph_path_.empty()) {
    const auto current = graph_path_.wstring();
    if (current.size() < std::size(path)) std::copy(current.begin(), current.end(), path);
  }
  OPENFILENAMEW dialog{};
  dialog.lStructSize = sizeof(dialog);
  dialog.hwndOwner = window_;
  dialog.lpstrTitle = saving ? L"Save graph" : L"Open graph";
  dialog.lpstrFilter = L"Modular graphs (*.modular)\0*.modular\0All files\0*.*\0";
  dialog.lpstrDefExt = L"modular";
  dialog.lpstrFile = path;
  dialog.nMaxFile = static_cast<DWORD>(std::size(path));
  dialog.Flags = OFN_EXPLORER | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR |
      (saving ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST);
  const bool chosen = saving ? GetSaveFileNameW(&dialog) : GetOpenFileNameW(&dialog);
  if (!chosen) {
    if (CommDlgExtendedError())
      throw std::invalid_argument("file chooser: could not select graph path");
    return std::nullopt;
  }
  return std::filesystem::path(path);
}

bool EditorWindow::save(bool choosePath) {
  auto path = graph_path_;
  if (choosePath || path.empty()) {
    const auto chosen = chooseGraphFile(true);
    if (!chosen) { status(L"Save cancelled. Graph retained."); return false; }
    path = *chosen;
  }
  saveGraphFile(workspace_, path, nodes::primitiveCodecs());
  graph_path_ = std::move(path);
  dirty_ = false;
  updateTitle();
  status(L"Graph saved.");
  return true;
}

bool EditorWindow::confirmAbandon() {
  if (!dirty_) return true;
  const TASKDIALOG_BUTTON buttons[]{{IDYES, L"&Save"}, {IDNO, L"&Discard"},
                                   {IDCANCEL, L"&Cancel"}};
  TASKDIALOGCONFIG dialog{};
  dialog.cbSize = sizeof(dialog);
  dialog.hwndParent = window_;
  dialog.dwFlags = TDF_ALLOW_DIALOG_CANCELLATION | TDF_POSITION_RELATIVE_TO_WINDOW;
  dialog.pszWindowTitle = L"Unsaved graph";
  dialog.pszMainInstruction = L"Save changes to this graph?";
  dialog.pszContent = L"Your authored changes have not been saved.";
  dialog.cButtons = static_cast<UINT>(std::size(buttons));
  dialog.pButtons = buttons;
  dialog.nDefaultButton = IDCANCEL;
  int choice = IDCANCEL;
  if (FAILED(TaskDialogIndirect(&dialog, &choice, nullptr, nullptr)))
    throw std::invalid_argument("session: could not show unsaved graph choices");
  if (choice == IDYES) return save();
  if (choice == IDNO) return true;
  status(L"Transition cancelled. Graph retained.");
  return false;
}

void EditorWindow::transition(Transition next) {
  if (dialog_active_ || pending_transition_ != Transition::None) return;
  cancelGesture();
  dialog_active_ = true;
  try {
    if (!confirmAbandon()) { dialog_active_ = false; return; }
    replacement_path_.clear();
    if (next == Transition::Open) {
      const auto path = chooseGraphFile(false);
      if (!path) {
        dialog_active_ = false;
        status(L"Open cancelled. Graph retained.");
        return;
      }
      auto loaded = openGraphFile(*path, nodes::primitiveCodecs());
      replacement_ = std::move(loaded);
      replacement_path_ = *path;
    } else if (next == Transition::New) {
      replacement_ = Workspace{};
    }
    pending_transition_ = next;
    dialog_active_ = false;
    if (execution_.active()) {
      EnableWindow(window_, FALSE);
      execution_.stop();
      status(L"Releasing execution resources...");
    } else finishTransition();
  } catch (...) {
    dialog_active_ = false;
    replacement_.reset();
    throw;
  }
}

void EditorWindow::finishTransition() {
  const auto completed = pending_transition_;
  pending_transition_ = Transition::None;
  if (completed == Transition::Close) { DestroyWindow(window_); return; }
  workspace_ = std::move(*replacement_);
  replacement_.reset();
  graph_path_ = std::move(replacement_path_);
  dirty_ = false;
  selected_ = kNoNode;
  pan_ = {};
  shown_state_ = execution_.state();
  EnableWindow(window_, TRUE);
  SetDlgItemTextW(window_, kMode, L"Run");
  refreshInspector();
  updateTitle();
  updateSummary();
  status(completed == Transition::Open ? L"Graph opened in Edit." : L"New graph in Edit.");
}

void EditorWindow::toggleMode() {
  cancelGesture();
  if (execution_.active()) execution_.stop();
  else execution_.start(workspace_.graph());
  // A fast attempt can end in the same state as the previous one, while other
  // UI actions have replaced its status message. Every attempt must be shown.
  shown_state_.reset();
  pollExecution();
}

void EditorWindow::pollExecution() {
  refreshRecorderStatus();
  if (execution_.active()) {
    const auto canvas = canvasRect();
    InvalidateRect(window_, &canvas, FALSE);
  }
  const auto state = execution_.state();
  if (pending_transition_ != Transition::None && !execution_.active()) {
    finishTransition();
    return;
  }
  if (state == shown_state_) return;
  shown_state_ = state;
  SetDlgItemTextW(window_, kMode, execution_.active() ? L"Return to Edit" : L"Run");
  refreshInspector();
  updateSummary();
  switch (state) {
    case GraphExecution::State::Starting: status(L"Compiling and preparing nodes..."); break;
    case GraphExecution::State::Running:
      status(L"Run: processing enabled. Operate the Start/Stop nodes you wired."); break;
    case GraphExecution::State::Stopping: status(L"Releasing execution resources..."); break;
    case GraphExecution::State::Failed: status(widen(execution_.errorMessage()), true); break;
    case GraphExecution::State::Idle: status(L"Edit: execution resources released."); break;
  }
}

void EditorWindow::paint() {
  PAINTSTRUCT paint{};
  const HDC target = BeginPaint(window_, &paint);
  RECT client{};
  GetClientRect(window_, &client);
  // Render the complete projection in memory so moving nodes does not flicker.
  const HDC dc = CreateCompatibleDC(target);
  const HBITMAP bitmap = CreateCompatibleBitmap(target, client.right, client.bottom);
  auto old = SelectObject(dc, bitmap);
  paintContent(dc);
  BitBlt(target, 0, 0, client.right, client.bottom, dc, 0, 0, SRCCOPY);
  SelectObject(dc, old);
  DeleteObject(bitmap);
  DeleteDC(dc);
  EndPaint(window_, &paint);
}

void EditorWindow::paintContent(HDC dc) {
  RECT client{};
  GetClientRect(window_, &client);
  fill(dc, client, RGB(255, 255, 255));
  SetBkMode(dc, TRANSPARENT);
  SelectObject(dc, title_font_);
  label(dc, {20, 14, 620, 52}, L"Modular / Graph editor");
  SelectObject(dc, font_);
  label(dc, {20, 56, client.right - 20, 88},
        L"Wire in Edit: output, then input.  Disconnect: right-click input.  "
        L"Pan: middle-drag / wheel.  Esc: cancel.", kMuted);
  label(dc, {16, 90, 236, 116}, L"PRIMITIVES  /  drag to place", kMuted);
  for (std::size_t index = 0; index < palette().size(); ++index) {
    const auto row = paletteRowRect(index);
    const LONG y = row.top;
    fill(dc, row, selected_ == kNoNode && palette_index_ == index
        ? RGB(220, 243, 239) : kBackground);
    label(dc, {28, y, 230, y + 21}, palette()[index].name);
  }
  const auto canvas = canvasRect();
  fill(dc, canvas, kBackground);
  const int saved = SaveDC(dc);
  IntersectClipRect(dc, canvas.left, canvas.top, canvas.right, canvas.bottom);
  const LONG gridX = canvas.left + static_cast<LONG>(pan_.x) % 24;
  const LONG gridY = canvas.top + static_cast<LONG>(pan_.y) % 24;
  for (LONG y = gridY; y < canvas.bottom; y += 24)
    for (LONG x = gridX; x < canvas.right; x += 24)
      SetPixelV(dc, x, y, RGB(211, 221, 230));
  const auto& graph = workspace_.graph();
  if (graph.nodeCount() == 0) {
    label(dc, {canvas.left + 40, 150, canvas.right - 20, 185}, L"Build your first graph");
    label(dc, {canvas.left + 40, 185, canvas.right - 20, 245},
          L"Drag a ConstFloatNode, GainNode and MixNode here.\n"
          L"Or place Start, Stop, MIDI file, sine synth and audio output, then wire them.", kMuted,
          DT_LEFT | DT_WORDBREAK);
  }
  for (const auto& wire : graph.connections())
    curve(dc, portPoint({wire.srcNode, wire.srcPort, false}),
          portPoint({wire.dstNode, wire.dstPort, true}), kAccent);
  if (wire_start_) {
    const auto anchor = portPoint(*wire_start_);
    curve(dc, wire_start_->input ? pointer_ : anchor,
          wire_start_->input ? anchor : pointer_, RGB(114, 144, 154));
  }
  const auto drawNode = [&](NodeId id) {
    const auto& node = graph.node(id);
    const auto rect = nodeRect(id);
    RECT overlap{};
    if (!IntersectRect(&overlap, &rect, &canvas)) return;
    fill(dc, {rect.left + 3, rect.top + 4, rect.right + 3, rect.bottom + 4},
          RGB(221, 228, 234));
    fill(dc, rect, selected_ == id ? kAccent : RGB(192, 207, 218));
    fill(dc, {rect.left + 2, rect.top + 2, rect.right - 2, rect.bottom - 2},
          RGB(255, 255, 255));
    fill(dc, {rect.left + 2, rect.top + 2, rect.right - 2, rect.top + 38},
          selected_ == id ? RGB(220, 243, 239) : RGB(234, 240, 245));
    label(dc, {rect.left + 14, rect.top + 4, rect.right - 14, rect.top + 35},
          presentation(node).name);
    for (const bool input : {true, false}) {
      const auto ports = input ? node.inputs() : node.outputs();
      for (const auto& port : ports) {
        const auto center = portPoint({id, port.id, input});
        const COLORREF color = port.type.domain == Domain::Value
            ? RGB(175, 103, 23) : kAccent;
        HBRUSH brush = CreateSolidBrush(color);
        auto oldBrush = SelectObject(dc, brush);
        auto oldPen = SelectObject(dc, GetStockObject(NULL_PEN));
        Ellipse(dc, center.x - 6, center.y - 6, center.x + 6, center.y + 6);
        SelectObject(dc, oldPen);
        SelectObject(dc, oldBrush);
        DeleteObject(brush);
        const auto text = widen(port.name) + L"\n" + portTypeName(port.type);
        const RECT area = input
            ? RECT{rect.left + 24, center.y - 16, rect.left + 148, center.y + 16}
            : RECT{node.inputs().empty() ? rect.left + 24 : rect.right - 146,
                    center.y - 16, rect.right - 24, center.y + 16};
        SelectObject(dc, port_font_);
        label(dc, area, text, color, (input ? DT_LEFT : DT_RIGHT));
        SelectObject(dc, font_);
      }
    }
    if (!node.inputs().empty())
      label(dc, {rect.left + 14, rect.bottom - 26, rect.left + 146, rect.bottom - 5},
            L"Inputs →", kMuted);
    if (!node.outputs().empty())
      label(dc, {rect.right - 146, rect.bottom - 26, rect.right - 14, rect.bottom - 5},
            L"Outputs →", kMuted, DT_RIGHT | DT_SINGLELINE | DT_VCENTER);
    if (const auto* recorder = dynamic_cast<const nodes::WavRecorderNode*>(&node)) {
      const auto state = recorder->state();
      const auto text = state == nodes::WavRecorderNode::State::Failed ? L"Retry / Discard" :
          state == nodes::WavRecorderNode::State::Recording ? L"Recording" :
          state == nodes::WavRecorderNode::State::Writing ? L"Writing WAV" : L"Idle";
      label(dc, {rect.left + 148, rect.bottom - 26, rect.right - 14, rect.bottom - 5},
          text, state == nodes::WavRecorderNode::State::Failed ? RGB(173, 45, 35) : kMuted,
          DT_RIGHT | DT_SINGLELINE | DT_VCENTER);
    }
  };
  for (const auto id : graph.nodeIds()) if (id != selected_) drawNode(id);
  if (selected_ != kNoNode) drawNode(selected_);
  if (drag_ == Drag::Place && PtInRect(&canvas, pointer_)) {
    fill(dc, {pointer_.x, pointer_.y, pointer_.x + 240, pointer_.y + 38},
          RGB(220, 243, 239));
    label(dc, {pointer_.x + 12, pointer_.y, pointer_.x + 230, pointer_.y + 38},
          palette()[palette_index_].name);
  }
  RestoreDC(dc, saved);
  line(dc, {kSidebar - 1, kTop}, {kSidebar - 1, canvas.bottom}, RGB(218, 227, 233));
  line(dc, {0, canvas.bottom}, {client.right, canvas.bottom}, RGB(218, 227, 233));
}

} // namespace modular::editor
