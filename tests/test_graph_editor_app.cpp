#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <objidl.h>
#include <commctrl.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <source_location>
#include <string>
#include <thread>
#include <vector>
#include "WavTestFiles.h"

namespace {

// These are visible control identifiers, not a back door into application state.
constexpr int kValue = 101, kChannels = 102, kInputs = 103, kApply = 104;
constexpr int kStatus = 105, kSummary = 106, kSelection = 107, kDelete = 108;
constexpr int kHelp = 109;
constexpr int kMode = 113, kFilePath = 114, kLoad = 116, kFire = 117;
constexpr int kDirectory = 118, kPrefix = 119, kRetry = 122, kDiscard = 123;
constexpr int kNew = 201, kOpen = 202, kSave = 203, kSaveAs = 204;

void require(bool condition, const std::string& message) {
  if (!condition) throw std::runtime_error(message);
}

// A private Win32 desktop that is never the input desktop: the editor's main
// window, file choosers and task dialogs exist and paint there, but nothing
// reaches the user's screen or taskbar. This thread moves there before it
// touches any window, so window enumeration and launch() use the same desktop.
// It must outlive every RunningApp so the editor is closed before it is.
class PrivateDesktop {
public:
  PrivateDesktop() : original_(GetThreadDesktop(GetCurrentThreadId())) {
    const auto name = L"ModularGuiTest-" + std::to_wstring(GetCurrentProcessId());
    desktop_ = CreateDesktopW(name.c_str(), nullptr, nullptr, 0, GENERIC_ALL, nullptr);
    require(desktop_ != nullptr, "could not create a private test desktop");
    if (!SetThreadDesktop(desktop_)) {
      CloseDesktop(desktop_);
      throw std::runtime_error("could not move the test thread to its private desktop");
    }
  }
  PrivateDesktop(const PrivateDesktop&) = delete;
  PrivateDesktop& operator=(const PrivateDesktop&) = delete;
  ~PrivateDesktop() {
    // A desktop cannot be closed while it is this thread's desktop; process
    // exit releases it anyway if switching back ever fails.
    if (SetThreadDesktop(original_)) CloseDesktop(desktop_);
  }
private:
  HDESK original_;
  HDESK desktop_ = nullptr;
};

std::wstring objectName(HANDLE object) {
  wchar_t name[256]{};
  require(GetUserObjectInformationW(object, UOI_NAME, name, sizeof(name), nullptr) != 0,
          "could not name the test desktop");
  return name;
}

struct RunningApp {
  PROCESS_INFORMATION process{};
  HWND window = nullptr;
  ~RunningApp() {
    if (window) PostMessageW(window, WM_CLOSE, 0, 0);
    if (process.hProcess) {
      if (WaitForSingleObject(process.hProcess, 3000) == WAIT_TIMEOUT)
        TerminateProcess(process.hProcess, 1);
      CloseHandle(process.hProcess);
      CloseHandle(process.hThread);
    }
  }
};

BOOL CALLBACK findWindow(HWND window, LPARAM parameter) {
  auto& app = *reinterpret_cast<RunningApp*>(parameter);
  DWORD pid = 0;
  GetWindowThreadProcessId(window, &pid);
  wchar_t name[128]{};
  GetClassNameW(window, name, 128);
  if (pid == app.process.dwProcessId && std::wstring(name) == L"ModularGraphEditor") {
    app.window = window;
    return FALSE;
  }
  return TRUE;
}

// Every editor joins one job whose only handle is held until this process
// ends. Closing it kills the job's processes, so an editor cannot outlive the
// test, even when CTest's timeout terminates the test without unwinding.
HANDLE editorJob() {
  static const HANDLE job = [] {
    const HANDLE created = CreateJobObjectW(nullptr, nullptr);
    require(created != nullptr, "could not create the graph editor job");
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    require(SetInformationJobObject(created, JobObjectExtendedLimitInformation,
                                    &limits, sizeof(limits)) != 0,
            "could not make the graph editor job kill on close");
    return created;
  }();
  return job;
}

void launch(RunningApp& app, const wchar_t* executable) {
  const HANDLE job = editorJob();
  // Start the editor on this thread's desktop, the one EnumWindows searches.
  auto desktop = objectName(GetProcessWindowStation()) + L"\\" +
                 objectName(GetThreadDesktop(GetCurrentThreadId()));
  STARTUPINFOW startup{};
  startup.cb = sizeof(startup);
  startup.lpDesktop = desktop.data();
  startup.dwFlags = STARTF_USESHOWWINDOW;
  startup.wShowWindow = SW_HIDE;
  std::wstring command = L"\"" + std::wstring(executable) + L"\"";
  require(CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, CREATE_SUSPENDED,
                          nullptr, nullptr, &startup, &app.process) != 0,
          "could not launch graph editor");
  // Join the job before the editor runs any code.
  if (!AssignProcessToJobObject(job, app.process.hProcess)) {
    TerminateProcess(app.process.hProcess, 1);
    throw std::runtime_error("could not tie the graph editor to the test process");
  }
  ResumeThread(app.process.hThread);
  for (int attempt = 0; attempt < 100 && !app.window; ++attempt) {
    EnumWindows(findWindow, reinterpret_cast<LPARAM>(&app));
    if (!app.window) std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
  require(app.window != nullptr, "graph editor window did not appear");
  // WM_NCCREATE publishes the top-level HWND before WM_CREATE adds its controls.
  for (int attempt = 0; attempt < 100 && !GetDlgItem(app.window, kSummary); ++attempt) {
    require(WaitForSingleObject(app.process.hProcess, 0) == WAIT_TIMEOUT,
            "graph editor exited before its controls appeared");
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
  require(GetDlgItem(app.window, kSummary) != nullptr,
          "graph editor controls did not appear");
}

LRESULT send(HWND window, UINT message, WPARAM w = 0, LPARAM l = 0) {
  DWORD_PTR result = 0;
  require(SendMessageTimeoutW(window, message, w, l, SMTO_ABORTIFHUNG, 3000,
                              &result) != 0, "app did not respond to UI input");
  return static_cast<LRESULT>(result);
}

std::wstring text(HWND parent, int id) {
  const HWND control = GetDlgItem(parent, id);
  require(control != nullptr, "missing visible control " + std::to_string(id));
  wchar_t buffer[1024]{};
  send(control, WM_GETTEXT, 1024, reinterpret_cast<LPARAM>(buffer));
  return buffer;
}

void contains(HWND window, int id, const std::wstring& expected,
              const std::source_location& location = std::source_location::current()) {
  const auto actual = text(window, id);
  if (actual.find(expected) == std::wstring::npos) {
    std::wcerr << L"Line " << location.line() << L", expected: " << expected
               << L"\nActual: " << actual << L'\n';
    throw std::runtime_error("unexpected visible UI state");
  }
}

void waitContains(HWND window, int id, const std::wstring& expected, int attempts = 200,
                  const std::source_location& location = std::source_location::current()) {
  for (int attempt = 0; attempt < attempts; ++attempt) {
    if (text(window, id).find(expected) != std::wstring::npos) return;
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  contains(window, id, expected, location);
}

void mouse(HWND window, UINT message, int x, int y, WPARAM buttons = 0) {
  send(window, message, buttons, MAKELPARAM(x, y));
}

void click(HWND window, int x, int y) {
  mouse(window, WM_LBUTTONDOWN, x, y, MK_LBUTTON);
  mouse(window, WM_LBUTTONUP, x, y);
}

void drag(HWND window, int fromX, int fromY, int toX, int toY) {
  mouse(window, WM_LBUTTONDOWN, fromX, fromY, MK_LBUTTON);
  mouse(window, WM_MOUSEMOVE, toX, toY, MK_LBUTTON);
  mouse(window, WM_LBUTTONUP, toX, toY);
}

void edit(HWND window, int id, const wchar_t* value) {
  send(GetDlgItem(window, id), WM_SETTEXT, 0, reinterpret_cast<LPARAM>(value));
}

void press(HWND window, int id) { send(GetDlgItem(window, id), BM_CLICK); }

std::string utf8Text(const std::wstring& text) {
  const auto size = WideCharToMultiByte(CP_UTF8, 0, text.data(),
      static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
  std::string result(size, '\0');
  WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
      result.data(), size, nullptr, nullptr);
  return result;
}

HWND waitDialog(const RunningApp& app, const wchar_t* title) {
  struct Search { const wchar_t* title; HWND window = nullptr; } search{title};
  for (int attempt = 0; attempt < 300; ++attempt) {
    EnumThreadWindows(app.process.dwThreadId, [](HWND window, LPARAM parameter) -> BOOL {
      auto& search = *reinterpret_cast<Search*>(parameter);
      wchar_t title[256]{};
      GetWindowTextW(window, title, 256);
      if (std::wstring(title) == search.title && IsWindowVisible(window)) {
        search.window = window;
        return FALSE;
      }
      return TRUE;
    }, reinterpret_cast<LPARAM>(&search));
    if (search.window) return search.window;
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  throw std::runtime_error("expected native dialog did not appear");
}

void chooseFile(const RunningApp& app, const wchar_t* title,
                 const std::filesystem::path& path) {
  const auto dialog = waitDialog(app, title);
  auto field = GetDlgItem(dialog, 1148); // common dialog filename combo
  if (!field) field = GetDlgItem(dialog, 1152); // classic filename edit
  for (int attempt = 0; attempt < 100 && !field; ++attempt) {
    EnumChildWindows(dialog, [](HWND child, LPARAM parameter) -> BOOL {
      wchar_t name[128]{};
      GetClassNameW(child, name, 128);
      if (GetDlgCtrlID(child) == 1001 && std::wstring(name) == L"Edit") {
        *reinterpret_cast<HWND*>(parameter) = child;
        return FALSE;
      }
      return TRUE;
    }, reinterpret_cast<LPARAM>(&field));
    if (!field) std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  require(field != nullptr, "native file chooser has no filename field");
  // Drive the filename edit's normal change notification. A caption update on
  // the surrounding combo need not replace the chooser's preselected filename.
  HWND filenameEdit = nullptr;
  wchar_t fieldClass[64]{};
  GetClassNameW(field, fieldClass, 64);
  if (std::wstring(fieldClass) != L"Edit") {
    EnumChildWindows(field, [](HWND child, LPARAM parameter) -> BOOL {
      wchar_t kind[64]{};
      GetClassNameW(child, kind, 64);
      if (std::wstring(kind) == L"Edit") {
        *reinterpret_cast<HWND*>(parameter) = child;
        return FALSE;
      }
      return TRUE;
    }, reinterpret_cast<LPARAM>(&filenameEdit));
    if (filenameEdit) field = filenameEdit;
  }
  send(field, EM_SETSEL, 0, -1);
  send(field, EM_REPLACESEL, TRUE, reinterpret_cast<LPARAM>(path.c_str()));
  PostMessageW(dialog, WM_COMMAND, IDOK, 0);
  // Selecting the filename is asynchronous native-dialog work. Let it finish
  // before polling app status or sending the next gesture into the owner.
  for (int attempt = 0; attempt < 1000 && IsWindow(dialog); ++attempt)
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  if (IsWindow(dialog)) {
    std::cerr << "File chooser timeout: " << utf8Text(title) << " -> "
              << utf8Text(path.wstring()) << '\n';
    wchar_t value[32768]{}, kind[64]{};
    send(field, WM_GETTEXT, std::size(value), reinterpret_cast<LPARAM>(value));
    GetClassNameW(field, kind, 64);
    std::cerr << "Filename field " << GetDlgCtrlID(field) << " " << utf8Text(kind)
              << ": " << utf8Text(value) << '\n';
  }
  require(!IsWindow(dialog), "native file chooser did not finish selecting the path");
}

void chooseUnsaved(const RunningApp& app, int choice) {
  const auto dialog = waitDialog(app, L"Unsaved graph");
  PostMessageW(dialog, TDM_CLICK_BUTTON, choice, 0);
  for (int attempt = 0; attempt < 200 && IsWindow(dialog); ++attempt)
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
}

void dirty(HWND window, bool expected) {
  wchar_t title[1024]{};
  send(window, WM_GETTEXT, 1024, reinterpret_cast<LPARAM>(title));
  require(std::wstring(title).ends_with(L" *") == expected, "unexpected graph dirty state");
}

void requestTransition(HWND window, int action) {
  if (action == WM_CLOSE) PostMessageW(window, WM_CLOSE, 0, 0);
  else PostMessageW(window, WM_COMMAND, action, 0);
}

std::string fileBytes(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

void persistenceChecks(const RunningApp& app) {
  const auto directory = std::filesystem::temp_directory_path() /
      (L"modular-app-save-" + std::to_wstring(app.process.dwProcessId) + L"-" +
       std::to_wstring(std::chrono::steady_clock::now().time_since_epoch().count()));
  require(std::filesystem::create_directory(directory), "could not create isolated test directory");
  struct Cleanup {
    std::filesystem::path path;
    ~Cleanup() { std::error_code error; std::filesystem::remove_all(path, error); }
  } cleanup{directory};
  const auto path = directory / L"作曲-é.modular";
  const auto window = app.window;
  drag(window, 100, 131, 300, 150);
  edit(window, kValue, L"0.625");
  press(window, kApply);
  PostMessageW(window, WM_COMMAND, kSave, 0);
  chooseFile(app, L"Save graph", path);
  waitContains(window, kStatus, L"Graph saved");
  require(std::filesystem::exists(path), "Save did not create Unicode graph path");
  send(window, WM_COMMAND, kNew);
  contains(window, kSummary, L"0 nodes | 0 wires");
  PostMessageW(window, WM_COMMAND, kOpen, 0);
  chooseFile(app, L"Open graph", path);
  waitContains(window, kStatus, L"Graph opened");
  contains(window, kSummary, L"1 nodes | 0 wires | Edit");
  click(window, 400, 168);
  contains(window, kValue, L"0.625");
  dirty(window, false);
  press(window, kApply);
  dirty(window, false); // applying the current setting is not an authored change
  const auto saved = fileBytes(path);
  press(window, kMode);
  waitContains(window, kStatus, L"processing enabled");
  dirty(window, false);
  send(window, WM_COMMAND, kSave);
  require(fileBytes(path) == saved, "processing changed persisted authored state");
  PostMessageW(window, WM_COMMAND, kOpen, 0);
  chooseFile(app, L"Open graph", path);
  waitContains(window, kStatus, L"Graph opened");
  contains(window, kSummary, L"Edit");
  click(window, 400, 168);
  edit(window, kValue, L"0.875"); press(window, kApply);
  dirty(window, true);
  for (const int action : {kNew, kOpen, static_cast<int>(WM_CLOSE)}) {
    requestTransition(window, action);
    chooseUnsaved(app, IDCANCEL);
    waitContains(window, kStatus, L"Transition cancelled");
    contains(window, kValue, L"0.875");
    dirty(window, true);
    require(fileBytes(path) == saved, "cancel autosaved the graph");
  }

  // A real sharing violation prevents replacement; every pending transition
  // must be cancelled without changing the existing file or authored graph.
  const auto locked = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ,
                                  nullptr, OPEN_EXISTING, 0, nullptr);
  require(locked != INVALID_HANDLE_VALUE, "could not lock test graph");
  for (const int action : {kNew, kOpen, static_cast<int>(WM_CLOSE)}) {
    requestTransition(window, action);
    chooseUnsaved(app, IDYES);
    waitContains(window, kStatus, L"could not replace destination");
    contains(window, kValue, L"0.875");
    dirty(window, true);
  }
  CloseHandle(locked);
  require(fileBytes(path) == saved, "failed Save changed the previous file");

  const auto corrupt = directory / L"corrupt.modular";
  { std::ofstream output(corrupt); output << "MODULAR_GRAPH 99\n"; }
  requestTransition(window, kOpen);
  chooseUnsaved(app, IDNO);
  chooseFile(app, L"Open graph", corrupt);
  waitContains(window, kStatus, L"unsupported graph version");
  contains(window, kValue, L"0.875");
  dirty(window, true);
  // The editor has no presentation for a soundfont node, and its Open passes
  // only the primitive codecs, so it refuses the node's kind.
  const auto soundFont = directory / L"soundfont.modular";
  {
    std::ofstream output(soundFont);
    output << "MODULAR_GRAPH 1\nNEXT 4\nNODES 1\n"
        "NODE 3 \"modular.soundfont/1\"\nSETTINGS \"empty 256\"\nCONTENT \"\"\n"
        "POSITION 48 50\nINPUTS 1\nPORT 1 2 2 \"midi\"\n"
        "OUTPUTS 1\nPORT 2 0 2 \"out\"\nWIRES 0\nEND\n";
  }
  requestTransition(window, kOpen);
  chooseUnsaved(app, IDNO);
  chooseFile(app, L"Open graph", soundFont);
  waitContains(window, kStatus, L"open: unsupported node kind modular.soundfont/1");
  contains(window, kValue, L"0.875");
  dirty(window, true);
  requestTransition(window, kOpen);
  chooseUnsaved(app, IDNO);
  PostMessageW(waitDialog(app, L"Open graph"), WM_COMMAND, IDCANCEL, 0);
  waitContains(window, kStatus, L"Open cancelled");
  dirty(window, true);

  requestTransition(window, kNew);
  chooseUnsaved(app, IDYES);
  waitContains(window, kStatus, L"New graph");
  contains(window, kSummary, L"0 nodes | 0 wires");
  dirty(window, false);
  PostMessageW(window, WM_COMMAND, kOpen, 0);
  chooseFile(app, L"Open graph", path);
  waitContains(window, kStatus, L"Graph opened");
  click(window, 400, 168);
  contains(window, kValue, L"0.875");
  edit(window, kValue, L"1.125"); press(window, kApply);
  requestTransition(window, kOpen);
  chooseUnsaved(app, IDYES);
  chooseFile(app, L"Open graph", path);
  waitContains(window, kStatus, L"Graph opened");
  click(window, 400, 168);
  contains(window, kValue, L"1.125");
  const auto updated = fileBytes(path);
  edit(window, kValue, L"2"); press(window, kApply);
  requestTransition(window, kNew);
  chooseUnsaved(app, IDNO);
  waitContains(window, kStatus, L"New graph");
  require(fileBytes(path) == updated, "Discard autosaved the graph");

  drag(window, 100, 131, 300, 150);
  for (const int action : {kNew, kOpen, static_cast<int>(WM_CLOSE)}) {
    requestTransition(window, action);
    chooseUnsaved(app, IDYES);
    PostMessageW(waitDialog(app, L"Save graph"), WM_COMMAND, IDCANCEL, 0);
    waitContains(window, kStatus, L"Save cancelled");
    contains(window, kSummary, L"1 nodes | 0 wires");
    dirty(window, true);
  }
  // Successful Discard/Open replaces just one graph and clears dirty state.
  requestTransition(window, kOpen);
  chooseUnsaved(app, IDNO);
  chooseFile(app, L"Open graph", path);
  waitContains(window, kStatus, L"Graph opened");
  dirty(window, false);
  drag(window, 100, 197, 660, 150); // Start now participates in ordinary Save/Open
  send(window, WM_COMMAND, kSave);
  contains(window, kStatus, L"Graph saved");
  dirty(window, false);
  require(fileBytes(path).find("modular.manual-trigger/1") != std::string::npos,
          "Start was omitted from Save");
  press(window, kDelete);
  click(window, 400, 168);
  send(window, WM_COMMAND, kSave);
  dirty(window, false);
  drag(window, 400, 168, 431, 205);
  dirty(window, true);
  const auto moved = directory / L"moved.modular";
  PostMessageW(window, WM_COMMAND, kSaveAs, 0);
  chooseFile(app, L"Save graph", moved);
  waitContains(window, kStatus, L"Graph saved");
  dirty(window, false);
  PostMessageW(window, WM_COMMAND, kOpen, 0);
  chooseFile(app, L"Open graph", moved);
  waitContains(window, kStatus, L"Graph opened");
  click(window, 431, 205);
  contains(window, kSelection, L"(79, 87)");
  edit(window, kValue, L"3"); press(window, kApply);
  requestTransition(window, kNew);
  chooseUnsaved(app, IDNO);
  waitContains(window, kStatus, L"New graph");
  contains(window, kSummary, L"0 nodes | 0 wires");

  const auto exhausted = directory / L"nearly-exhausted.modular";
  {
    std::ofstream output(exhausted);
    output << "MODULAR_GRAPH 1\nNEXT 4294967295\nNODES 1\n"
        "NODE 4 \"modular.mix/1\"\nSETTINGS \"1\"\nCONTENT \"\"\nPOSITION 48 50\n"
        "INPUTS 2\nPORT 1 0 1 \"in0\"\nPORT 2 0 1 \"in1\"\n"
        "OUTPUTS 1\nPORT 3 0 1 \"out\"\nWIRES 0\nEND\n";
  }
  PostMessageW(window, WM_COMMAND, kOpen, 0);
  chooseFile(app, L"Open graph", exhausted);
  waitContains(window, kStatus, L"Graph opened");
  click(window, 400, 168);
  edit(window, kInputs, L"4"); edit(window, kChannels, L"2"); press(window, kApply);
  contains(window, kStatus, L"ids exhausted");
  click(window, 400, 168);
  contains(window, kInputs, L"2"); contains(window, kChannels, L"1");
  dirty(window, false);
  // Failed construction consumes its output ID before node adoption fails.
  // That history is authored state even though no extra node survives.
  drag(window, 100, 131, 660, 400);
  contains(window, kStatus, L"ids exhausted");
  contains(window, kSummary, L"1 nodes | 0 wires");
  dirty(window, true);
  requestTransition(window, kNew);
  chooseUnsaved(app, IDCANCEL);
  waitContains(window, kStatus, L"Transition cancelled");
  send(window, WM_COMMAND, kSave);
  require(fileBytes(exhausted).find("NEXT 4294967296") != std::string::npos,
          "consumed identity history was not saved");
  dirty(window, false);
  send(window, WM_COMMAND, kNew);

  // A frame-based block authored by another Modular client has an honest
  // read-only presentation, even though the GUI offers no MIDI event editor.
  const auto block = directory / L"block.modular";
  {
    std::ofstream output(block);
    output << "MODULAR_GRAPH 1\nNEXT 5\nNODES 1\n"
        "NODE 4 \"modular.midi-block/1\"\nSETTINGS \"\"\n"
        "CONTENT \"310a3720313434203630203130300a\"\nPOSITION 48 50\n"
        "INPUTS 2\nPORT 1 2 1 \"start\"\nPORT 2 2 1 \"stop\"\n"
        "OUTPUTS 1\nPORT 3 2 2 \"midi\"\nWIRES 0\nEND\n";
  }
  requestTransition(window, kOpen);
  chooseFile(app, L"Open graph", block);
  waitContains(window, kStatus, L"Graph opened");
  click(window, 400, 168);
  contains(window, kSelection, L"MidiBlockNode");
  contains(window, kHelp, L"1 stored MIDI messages");
  send(window, WM_COMMAND, kSave);
  contains(window, kStatus, L"Graph saved");
  send(window, WM_COMMAND, kNew);
}

void saveOnCloseCheck(const wchar_t* executable) {
  RunningApp app;
  launch(app, executable);
  const auto path = std::filesystem::temp_directory_path() /
      (L"modular-close-" + std::to_wstring(app.process.dwProcessId) + L"-" +
       std::to_wstring(std::chrono::steady_clock::now().time_since_epoch().count()) +
       L"-保存.modular");
  drag(app.window, 100, 131, 300, 150);
  edit(app.window, kValue, L"0.375"); press(app.window, kApply);
  press(app.window, kMode);
  waitContains(app.window, kStatus, L"processing enabled");
  PostMessageW(app.window, WM_CLOSE, 0, 0);
  chooseUnsaved(app, IDYES);
  chooseFile(app, L"Save graph", path);
  require(WaitForSingleObject(app.process.hProcess, 3000) == WAIT_OBJECT_0,
          "Save did not finish closing a running graph");
  DWORD code = 1;
  GetExitCodeProcess(app.process.hProcess, &code);
  require(code == 0, "Save/Close failed");
  app.window = nullptr;
  require(fileBytes(path).find("SETTINGS \"0.375\"") != std::string::npos,
          "Close did not save the authored setting");
  std::filesystem::remove(path);
}

void recorderRecoveryAndCloseCheck(const wchar_t* executable) {
  WavTestFiles files;
  const auto output = files.directory / L"录音-é";
  const auto saved = files.directory / "recorder.modular";
  RunningApp app;
  launch(app, executable);
  const auto window = app.window;
  drag(window, 100, 197, 300, 150);
  drag(window, 100, 219, 300, 330);
  drag(window, 100, 329, 660, 150);
  const auto pastedDirectory = L"\"" + output.wstring() + L"\"";
  edit(window, kDirectory, pastedDirectory.c_str());
  edit(window, kPrefix, L"recovery");
  press(window, kApply);
  require(text(window, kDirectory) == output.wstring(), "pasted directory quotes remain");
  click(window, 588, 208); click(window, 672, 236);
  click(window, 588, 388); click(window, 672, 264);
  requestTransition(window, kSaveAs);
  chooseFile(app, L"Save graph", saved);
  waitContains(window, kStatus, L"Graph saved");
  const auto trigger = [&](bool start) {
    click(window, 400, start ? 168 : 348);
    contains(window, kSelection, start ? L"Start" : L"Stop");
    press(window, kFire);
    // Wait for the worker to consume the trigger before issuing another one.
    click(window, 760, 168);
    waitContains(window, kHelp, start ? L"Recording" : L"Take retained");
  };
  const auto expectRecorder = [&](const wchar_t* state) {
    click(window, 760, 168);
    waitContains(window, kHelp, state);
  };
  press(window, kMode);
  waitContains(window, kStatus, L"processing enabled");
  trigger(true); trigger(false);
  expectRecorder(L"Take retained");
  require(text(window, kHelp) == L"Output folder not found. Take retained; Retry or Discard.",
          "recorder error is garbled or buried in unrelated help");
  require(IsWindowEnabled(GetDlgItem(window, kRetry)), "Retry unavailable on failed take");
  press(window, kRetry);
  waitContains(window, kHelp, L"Take retained");
  dirty(window, false);
  // Saving a failed take still writes only the authored graph.
  const auto authored = fileBytes(saved);
  send(window, WM_COMMAND, kSave);
  require(fileBytes(saved) == authored, "failed take leaked into graph persistence");
  std::filesystem::create_directory(output);
  press(window, kRetry);
  waitContains(window, kHelp, L"Saved");
  const auto first = readTestWav(output / "recovery-1.wav");
  require(!first.samples.empty(), "Retry did not save retained frames");
  std::filesystem::rename(output, files.directory / "completed");
  trigger(true); trigger(false);
  expectRecorder(L"Take retained");
  press(window, kDiscard);
  waitContains(window, kHelp, L"Take discarded");
  dirty(window, false);
  std::filesystem::create_directory(output);
  trigger(true);
  expectRecorder(L"Recording");
  // Layout is authored, capture is transient. Every cancelled or failed
  // abandonment must retain both the graph and its still-running capture.
  drag(window, 400, 168, 410, 168);
  for (const int transition : {kNew, kOpen, WM_CLOSE}) {
    requestTransition(window, transition);
    chooseUnsaved(app, IDCANCEL);
    waitContains(window, kStatus, L"Transition cancelled");
    expectRecorder(L"Recording");
  }
  const auto locked = CreateFileW(saved.c_str(), GENERIC_READ, FILE_SHARE_READ,
                                  nullptr, OPEN_EXISTING, 0, nullptr);
  require(locked != INVALID_HANDLE_VALUE, "cannot lock recording graph");
  for (const int transition : {kNew, kOpen, WM_CLOSE}) {
    requestTransition(window, transition);
    chooseUnsaved(app, IDYES);
    waitContains(window, kStatus, L"could not replace destination");
    expectRecorder(L"Recording");
    dirty(window, true);
  }
  CloseHandle(locked);
  requestTransition(window, kSaveAs);
  PostMessageW(waitDialog(app, L"Save graph"), WM_COMMAND, IDCANCEL, 0);
  waitContains(window, kStatus, L"Save cancelled");
  expectRecorder(L"Recording");
  requestTransition(window, WM_CLOSE);
  chooseUnsaved(app, IDYES);
  require(WaitForSingleObject(app.process.hProcess, 3000) == WAIT_OBJECT_0,
          "Save and Close did not abort active capture");
  DWORD code = 1;
  GetExitCodeProcess(app.process.hProcess, &code);
  require(code == 0, "recording app failed during normal close");
  app.window = nullptr;
  require(std::filesystem::is_empty(output), "close completed or leaked unfinished capture");
  require(readTestWav(files.directory / "completed/recovery-1.wav").samples == first.samples,
          "close changed a completed recording");
}

#include "WindowsPlaybackProbe.h"

void capture(HWND window, const std::filesystem::path& path) {
  // Shown only for the capture; on the private desktop nobody sees it.
  ShowWindow(window, SW_SHOWNOACTIVATE);
  RedrawWindow(window, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
  RECT rect{};
  GetClientRect(window, &rect);
  const HDC screen = GetDC(window);
  const HDC dc = CreateCompatibleDC(screen);
  const HBITMAP bitmap = CreateCompatibleBitmap(screen, rect.right, rect.bottom);
  const auto old = SelectObject(dc, bitmap);
  // PrintWindow marshals the rendering request across the process boundary.
  const BOOL printed = PrintWindow(window, dc, PW_CLIENTONLY);
  ShowWindow(window, SW_HIDE);
  SelectObject(dc, old);
  BITMAPINFO info{};
  info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  info.bmiHeader.biWidth = rect.right;
  info.bmiHeader.biHeight = -rect.bottom;
  info.bmiHeader.biPlanes = 1;
  info.bmiHeader.biBitCount = 32;
  info.bmiHeader.biCompression = BI_RGB;
  std::vector<char> pixels(static_cast<std::size_t>(rect.right) * rect.bottom * 4);
  const int rows = GetDIBits(dc, bitmap, 0, rect.bottom, pixels.data(), &info, DIB_RGB_COLORS);
  DeleteObject(bitmap);
  DeleteDC(dc);
  ReleaseDC(window, screen);
  require(printed && rows == rect.bottom, "could not capture app rendering");
  require(std::any_of(pixels.begin(), pixels.end(), [](char byte) { return byte != 0; }),
            "app capture was blank");
  BITMAPFILEHEADER header{};
  header.bfType = 0x4d42;
  header.bfOffBits = sizeof(header) + sizeof(BITMAPINFOHEADER);
  header.bfSize = header.bfOffBits + static_cast<DWORD>(pixels.size());
  std::ofstream file(path, std::ios::binary);
  file.write(reinterpret_cast<const char*>(&header), sizeof(header));
  file.write(reinterpret_cast<const char*>(&info.bmiHeader), sizeof(BITMAPINFOHEADER));
  file.write(pixels.data(), static_cast<std::streamsize>(pixels.size()));
  require(file.good(), "could not write app capture");
}

} // namespace

int wmain(int argc, wchar_t** argv) {
  try {
    const PrivateDesktop desktop;
    const bool gesture = argc == 3 && std::wstring(argv[2]) == L"--gesture";
    require(argc == 2 || argc == 3 || (argc == 4 && std::wstring(argv[3]) == L"--audio"),
            "usage: graph_editor_ui_tests path-to-graph_editor.exe [capture.bmp [--audio] | --gesture]");
    if (gesture) {
      RunningApp app;
      launch(app, argv[1]);
      const HWND window = app.window;
      drag(window, 100, 153, 660, 150);
      contains(window, kSelection, L"GainNode");
      contains(window, kSelection, L"(408, 50)");
      mouse(window, WM_LBUTTONDOWN, 760, 168, MK_LBUTTON);
      mouse(window, WM_MOUSEMOVE, 61, 752);
      contains(window, kSelection, L"(408, 50)");
      mouse(window, WM_LBUTTONUP, 760, 168);
      drag(window, 760, 168, 810, 248);
      contains(window, kSelection, L"(458, 130)");
      return 0;
    }
    const bool audio = argc == 4;
    // Hardware activation can take longer than ordinary UI work. Poll visible
    // state and responsiveness throughout a bounded ten-second startup window.
    const auto waitForRun = [audio](HWND window) {
      const auto start = std::chrono::steady_clock::now();
      waitContains(window, kStatus, L"processing enabled", audio ? 1000 : 200);
      if (audio) std::cout << "Device preparation completed in " <<
          std::chrono::duration_cast<std::chrono::milliseconds>(
              std::chrono::steady_clock::now() - start).count() << " ms\n";
    };
    RunningApp app;
    launch(app, argv[1]);
    const HWND window = app.window;
    const auto contentDirectory = std::filesystem::temp_directory_path() /
        (L"modular-owned-app-" + std::to_wstring(app.process.dwProcessId) + L"-" +
         std::to_wstring(std::chrono::steady_clock::now().time_since_epoch().count()));
    require(std::filesystem::create_directory(contentDirectory), "cannot create content fixture");
    struct ContentCleanup {
      std::filesystem::path directory;
      ~ContentCleanup() { std::error_code error; std::filesystem::remove_all(directory, error); }
    } contentCleanup{contentDirectory};
    const auto imported = contentDirectory / L"import.mid";
    const auto playable = contentDirectory / L"playable.modular";
    std::filesystem::copy_file(MODULAR_TEST_MIDI, imported);
    if (audio) {
      // The copied fixture's note-off delta occupies bytes 26-27. Use 16383
      // ticks (>17 seconds), longer than the device detection deadline, so EOF
      // cannot stand in for the wired Stop even on a slow cold device.
      std::fstream fixture(imported, std::ios::binary | std::ios::in | std::ios::out);
      const char kHeldDelta[]{static_cast<char>(0xff), static_cast<char>(0x7f)};
      fixture.seekp(26);
      fixture.write(kHeldDelta, sizeof(kHeldDelta));
      require(fixture.good(), "cannot extend hardware fixture note");
    }
    contains(window, kSummary, L"0 nodes | 0 wires");
    contains(window, kMode, L"Run");

    persistenceChecks(app);

    // Palette drag, creation settings, and the actual canvas selection inspector.
    click(window, 100, 131);
    edit(window, kValue, L"0.75");
    drag(window, 100, 131, 300, 150);
    contains(window, kSummary, L"1 nodes | 0 wires");
    contains(window, kSelection, L"ConstFloatNode");
    contains(window, kValue, L"0.75");
    drag(window, 100, 153, 660, 150);
    contains(window, kSelection, L"GainNode");
    contains(window, kSelection, L"(408, 50)");
    drag(window, 100, 175, 660, 420);
    contains(window, kSummary, L"3 nodes | 0 wires");
    contains(window, kSelection, L"MixNode");
    click(window, 760, 168);
    contains(window, kSelection, L"GainNode");
    contains(window, kSelection, L"(408, 50)");
    click(window, 760, 438);
    contains(window, kSelection, L"MixNode");
    edit(window, kInputs, L"3");
    press(window, kApply);
    contains(window, kStatus, L"Settings applied");
    contains(window, kInputs, L"3");

    // Output -> value input; one source can also feed another Gain later.
    click(window, 588, 208);
    click(window, 672, 236);
    contains(window, kSummary, L"3 nodes | 1 wires");
    // A value cannot feed an audio input. The displayed error comes from Graph.
    click(window, 588, 208);
    click(window, 672, 208);
    contains(window, kStatus, L"connect: type mismatch");
    contains(window, kSummary, L"3 nodes | 1 wires");
    click(window, 588, 208);
    click(window, 672, 236);
    contains(window, kStatus, L"connect: input already connected");

    // Wire gain to the mixer, then exercise a rejected settings edit.
    click(window, 948, 208);
    click(window, 672, 478);
    contains(window, kSummary, L"3 nodes | 2 wires");
    click(window, 760, 168);
    contains(window, kSelection, L"GainNode");
    contains(window, kSelection, L"(408, 50)");
    if (argc >= 3) capture(window, argv[2]);
    click(window, 760, 168);
    contains(window, kSelection, L"GainNode");
    edit(window, kChannels, L"2");
    press(window, kApply);
    contains(window, kStatus, L"configure: type change would invalidate a wire");
    click(window, 760, 168);
    contains(window, kSelection, L"GainNode");
    contains(window, kChannels, L"1");

    // Moving a node preserves its wire; disconnect addresses the moved input.
    drag(window, 760, 168, 810, 248);
    contains(window, kSelection, L"(458, 130)");
    contains(window, kSummary, L"3 nodes | 2 wires");
    mouse(window, WM_RBUTTONUP, 722, 316);
    contains(window, kStatus, L"Disconnected");
    contains(window, kSummary, L"3 nodes | 1 wires");
    press(window, kDelete);
    contains(window, kSummary, L"2 nodes | 0 wires");

    // Cancellation of a palette drag does not place a node.
    mouse(window, WM_LBUTTONDOWN, 100, 153, MK_LBUTTON);
    send(window, WM_KEYDOWN, VK_ESCAPE);
    mouse(window, WM_LBUTTONUP, 500, 350);
    contains(window, kSummary, L"2 nodes | 0 wires");
    click(window, 948, 478); click(window, 672, 478);
    press(window, kMode);
    waitContains(window, kStatus, L"cycle");
    mouse(window, WM_RBUTTONUP, 672, 478);
    click(window, 400, 168);
    press(window, kDelete);
    click(window, 760, 438);
    press(window, kDelete);
    contains(window, kSummary, L"0 nodes | 0 wires");

    drag(window, 100, 307, 660, 150);
    contains(window, kSelection, L"AudioOutputNode");
    edit(window, kChannels, L"2"); press(window, kApply);
    contains(window, kStatus, L"Settings applied");
    edit(window, kChannels, L"3"); press(window, kApply);
    contains(window, kStatus, L"requires mono or stereo");
    press(window, kDelete);

    drag(window, 100, 241, 660, 150); // MIDI file
    contains(window, kSelection, L"MidiFileNode");
    press(window, kMode);
    waitContains(window, kStatus, L"select and load");
    waitContains(window, kSummary, L"Edit");
    // Each failed Run must replace an intervening, unrelated UI status.
    for (int attempt = 0; attempt < 3; ++attempt) {
      click(window, 760, 168);
      contains(window, kStatus, L"Moved node");
      press(window, kMode);
      waitContains(window, kStatus, L"select and load");
    }
    edit(window, kFilePath, L"missing-file.mid");
    press(window, kLoad);
    contains(window, kStatus, L"midi file:");
    edit(window, kFilePath, imported.c_str());
    press(window, kLoad);
    contains(window, kStatus, L"MIDI loaded");

    drag(window, 100, 197, 300, 150); // placed Start
    drag(window, 100, 219, 300, 330); // placed Stop
    drag(window, 100, 263, 660, 420); // sine synth
    click(window, 588, 208); click(window, 672, 208);
    click(window, 588, 388); click(window, 672, 236);
    click(window, 948, 208); click(window, 672, 478);
    contains(window, kSummary, L"4 nodes | 3 wires");
    std::unique_ptr<WindowsPlaybackProbe> device;
    if (audio) {
      device = std::make_unique<WindowsPlaybackProbe>();
      click(window, 100, 307);
      edit(window, kChannels, L"1");
      drag(window, 100, 307, 990, 420);
      click(window, 948, 478); click(window, 1002, 478);
    }
    drag(window, 100, 329, 300, 510);
    contains(window, kSelection, L"WavRecorderNode");
    const auto pastedDirectory = L"\"" + contentDirectory.wstring() + L"\"";
    edit(window, kDirectory, pastedDirectory.c_str());
    edit(window, kPrefix, L"recording");
    press(window, kApply);
    contains(window, kStatus, L"Settings applied");
    click(window, 948, 478); click(window, 312, 568);
    click(window, 588, 208); click(window, 312, 596);
    click(window, 588, 388); click(window, 312, 624);
    requestTransition(window, kSaveAs);
    chooseFile(app, L"Save graph", playable);
    waitContains(window, kStatus, L"Graph saved");
    dirty(window, false);
    const auto authoredBytes = fileBytes(playable);
    // Import failure retains owned content and cannot dirty a clean saved graph.
    click(window, 760, 168);
    edit(window, kFilePath, L"missing-replacement.mid");
    press(window, kLoad);
    contains(window, kHelp, L"Ready: 2 MIDI messages");
    dirty(window, false);

    // Invalid node content leaves the actual app's active graph intact.
    auto damagedBytes = authoredBytes;
    const auto midiKind = damagedBytes.find("modular.midi-file/1");
    require(midiKind != std::string::npos, "MIDI node missing from saved graph");
    const auto contentAt = damagedBytes.find("CONTENT \"", midiKind) + 9;
    damagedBytes.replace(contentAt, damagedBytes.find('"', contentAt) - contentAt, "ff");
    const auto damaged = contentDirectory / L"damaged.modular";
    { std::ofstream output(damaged); output << damagedBytes; }
    requestTransition(window, kOpen);
    chooseFile(app, L"Open graph", damaged);
    waitContains(window, kStatus, L"invalid stored node number");
    contains(window, kSummary, audio ? L"6 nodes | 7 wires" : L"5 nodes | 6 wires");
    dirty(window, false);

    drag(window, 400, 168, 410, 168);
    dirty(window, true);
    requestTransition(window, kSaveAs);
    PostMessageW(waitDialog(app, L"Save graph"), WM_COMMAND, IDCANCEL, 0);
    waitContains(window, kStatus, L"Save cancelled");
    dirty(window, true);
    const auto locked = CreateFileW(playable.c_str(), GENERIC_READ, FILE_SHARE_READ,
                                    nullptr, OPEN_EXISTING, 0, nullptr);
    require(locked != INVALID_HANDLE_VALUE, "could not lock playable graph");
    send(window, WM_COMMAND, kSave);
    contains(window, kStatus, L"could not replace destination");
    CloseHandle(locked);
    dirty(window, true);
    require(fileBytes(playable) == authoredBytes, "failed save lost owned MIDI content");
    drag(window, 410, 168, 400, 168);
    send(window, WM_COMMAND, kSave);
    dirty(window, false);
    send(window, WM_COMMAND, kNew);
    contains(window, kSummary, L"0 nodes | 0 wires");
    require(std::filesystem::remove(imported), "import fixture was not removed");
    requestTransition(window, kOpen);
    chooseFile(app, L"Open graph", playable);
    waitContains(window, kStatus, L"Graph opened in Edit");
    click(window, 760, 168);
    contains(window, kHelp, L"Ready: 2 MIDI messages");
    dirty(window, false);
    press(window, kMode);
    waitForRun(window);
    require(!IsWindowEnabled(GetDlgItem(window, kApply)), "settings enabled in Run");
    drag(window, 100, 131, 1000, 600);
    contains(window, kStatus, L"return to Edit");
    send(window, WM_KEYDOWN, VK_DELETE);
    contains(window, kStatus, L"return to Edit");
    contains(window, kSummary, audio ? L"6 nodes | 7 wires" : L"5 nodes | 6 wires");
    mouse(window, WM_RBUTTONUP, 672, 208);
    contains(window, kStatus, L"return to Edit");
    drag(window, 400, 168, 420, 218);
    contains(window, kSelection, L"(68, 100)");
    if (device) require(device->peak(window, 150) < 0.001f,
        "output was not silent before Start; keep other applications quiet");
    press(window, kFire);
    contains(window, kStatus, L"Start trigger queued");
    click(window, 400, 528);
    waitContains(window, kHelp, L"Recording\r\n");
    if (device) {
      const auto level = device->peak(window, 5000, 0.01f);
      std::cout << "Windows default-device peak after wired Start: " << level << '\n';
      require(level > 0.01f, "wired Start did not reach the default output device");
    }
    send(window, WM_COMMAND, kSave);
    contains(window, kStatus, L"Graph saved");
    dirty(window, false);
    const auto runningBytes = fileBytes(playable);
    click(window, 400, 348);
    press(window, kFire);
    contains(window, kStatus, L"Stop trigger queued");
    if (device) {
      device->peak(window, 180); // let queued buffers leave the device
      const auto level = device->peak(window, 150);
      std::cout << "Windows default-device peak after wired Stop: " << level << '\n';
      require(level < 0.001f, "wired Stop did not silence the output device");
    }
    click(window, 400, 528);
    waitContains(window, kHelp, L"Saved");
    const auto firstTake = readTestWav(contentDirectory / "recording-1.wav");
    require(!firstTake.samples.empty(), "recorder saved an empty take");
    require(std::any_of(firstTake.samples.begin(), firstTake.samples.end(),
                        [](auto sample) { return sample != 0; }), "recorded WAV was silent");
    dirty(window, false);
    send(window, WM_COMMAND, kSave);
    require(fileBytes(playable) == runningBytes, "live controls changed persisted content");
    press(window, kMode);
    waitContains(window, kStatus, L"resources released", audio ? 1000 : 200);
    require(IsWindowEnabled(GetDlgItem(window, kDelete)), "Edit did not restore controls");
    press(window, kMode);
    waitForRun(window);
    click(window, 420, 218);
    press(window, kFire);
    contains(window, kStatus, L"Start trigger queued");
    click(window, 400, 528);
    waitContains(window, kHelp, L"Recording\r\n");
    if (device) {
      const auto level = device->peak(window, 5000, 0.01f);
      std::cout << "Second Run default-device peak after Start: " << level << '\n';
      require(level > 0.01f, "second Run did not play");
    }
    if (argc >= 3) capture(window, std::filesystem::path(argv[2]).replace_extension(".playback.bmp"));
    PostMessageW(window, WM_CLOSE, 0, 0);
    require(WaitForSingleObject(app.process.hProcess, audio ? 10000 : 3000) == WAIT_OBJECT_0,
            "app did not close normally");
    DWORD code = 1;
    GetExitCodeProcess(app.process.hProcess, &code);
    require(code == 0, "app exited with an error");
    app.window = nullptr;
    require(!std::filesystem::exists(contentDirectory / "recording-2.wav"),
            "closing active capture silently completed a take");

    // A fresh process reopens the saved graph after the source file was removed.
    RunningApp reopened;
    launch(reopened, argv[1]);
    requestTransition(reopened.window, kOpen);
    chooseFile(reopened, L"Open graph", playable);
    waitContains(reopened.window, kStatus, L"Graph opened in Edit");
    dirty(reopened.window, false);
    press(reopened.window, kMode);
    waitForRun(reopened.window);
    if (device) require(device->peak(reopened.window, 150) < 0.001f,
                        "reopened graph resumed previous notes or queued audio");
    click(reopened.window, 420, 218);
    press(reopened.window, kFire);
    contains(reopened.window, kStatus, L"Start trigger queued");
    click(reopened.window, 400, 528);
    waitContains(reopened.window, kHelp, L"Recording\r\n");
    if (device) {
      const auto level = device->peak(reopened.window, 5000, 0.01f);
      std::cout << "Reopened default-device peak after Start: " << level << '\n';
      require(level > 0.01f, "reopened owned MIDI did not play through the device");
    }
    click(reopened.window, 400, 348);
    press(reopened.window, kFire);
    contains(reopened.window, kStatus, L"Stop trigger queued");
    if (device) {
      device->peak(reopened.window, 180);
      const auto level = device->peak(reopened.window, 150);
      std::cout << "Reopened default-device peak after Stop: " << level << '\n';
      require(level < 0.001f, "reopened Stop did not silence the device");
    }
    click(reopened.window, 400, 528);
    waitContains(reopened.window, kHelp, L"Saved");
    require(!readTestWav(contentDirectory / "recording-2.wav").samples.empty(),
            "reopened graph did not save the next take");
    require(readTestWav(contentDirectory / "recording-1.wav").samples == firstTake.samples,
            "a later take replaced an earlier recording");
    dirty(reopened.window, false);
    requestTransition(reopened.window, WM_CLOSE);
    require(WaitForSingleObject(reopened.process.hProcess, audio ? 10000 : 3000) == WAIT_OBJECT_0,
            "reopened app did not close normally");
    GetExitCodeProcess(reopened.process.hProcess, &code);
    require(code == 0, "reopened app exited with an error");
    reopened.window = nullptr;
    saveOnCloseCheck(argv[1]);
    recorderRecoveryAndCloseCheck(argv[1]);
    std::cout << "Actual-app gestures, settings, validation, movement and deletion passed.\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
