"""PROTOTYPE, throwaway: web build ticket 08, runs pages in Chrome and Edge, headless.

    python -I PROTOTYPE_browsers.py <build/web> <soundfont> <work folder> [--runs N]

Standard library only, after midishare-playback ticket 02's browsers.py. Serves on 127.0.0.1,
on a free port:

    /               this folder: preview_timing.html and check_driver.html
    /dist/          <build/web>/dist, the module as a client takes it
    /check/         <build/web>/web-check, the web check page as the build writes it
    /piano.sf2      <soundfont>, read only, never copied

For each of Chrome and Edge, where Windows installs them, it starts the browser headless with
a fresh profile folder under <work folder> for each page load: --runs loads of the timing page
(3 by default), then one of the check page through check_driver.html. Each page posts its
outcome to /result, and the browser is then stopped. Edge relaunches itself under another
parent, so every process whose command line names that load's profile folder is stopped, and
at the end the script checks that no process names any of them. Writes
<work folder>/browser-results.json and prints a summary. Stops the server and every browser it
started.
"""

import json
import os
import queue
import secrets
import shutil
import statistics
import subprocess
import sys
import tempfile
import threading
import time
from functools import partial
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

HERE = Path(__file__).resolve().parent
TIMEOUT = 180


def find_browsers():
    pf, pf86 = os.environ.get("ProgramFiles", ""), os.environ.get("ProgramFiles(x86)", "")
    local = os.environ.get("LOCALAPPDATA", "")
    candidates = {
        "chrome": [rf"{pf}\Google\Chrome\Application\chrome.exe",
                   rf"{pf86}\Google\Chrome\Application\chrome.exe",
                   rf"{local}\Google\Chrome\Application\chrome.exe"],
        "edge": [rf"{pf86}\Microsoft\Edge\Application\msedge.exe",
                 rf"{pf}\Microsoft\Edge\Application\msedge.exe"],
    }
    found = {}
    for name, paths in candidates.items():
        for path in paths:
            if path and Path(path).is_file():
                found[name] = path
                break
    return found


def version(exe):
    out = subprocess.run(["powershell", "-NoProfile", "-Command",
                          f"(Get-Item '{exe}').VersionInfo.ProductVersion"],
                         capture_output=True, text=True)
    return out.stdout.strip()


def command(exe, profile, url):
    return [exe, "--headless", f"--user-data-dir={profile}", "--no-first-run",
            "--no-default-browser-check", "--disable-extensions",
            "--disable-background-networking", "--disable-component-update", "--disable-sync",
            "--metrics-recording-only", url]


def processes_naming(fragment):
    """The ids of every process whose command line holds `fragment`."""
    out = subprocess.run(["powershell", "-NoProfile", "-Command",
                          "Get-CimInstance Win32_Process | Where-Object { $_.ProcessId -ne $PID "
                          f"-and $_.CommandLine -like '*{fragment}*' }} | "
                          "ForEach-Object { $_.ProcessId }"],
                         capture_output=True, text=True)
    return [int(line) for line in out.stdout.split() if line.strip().isdigit()]


def stop(proc, profile):
    subprocess.run(["taskkill", "/T", "/F", "/PID", str(proc.pid)], capture_output=True)
    name = Path(profile).name
    for _ in range(10):
        left = processes_naming(name)
        if not left:
            break
        for pid in left:
            subprocess.run(["taskkill", "/F", "/PID", str(pid)], capture_output=True)
        time.sleep(0.5)
    try:
        proc.wait(timeout=30)
    except subprocess.TimeoutExpired:
        pass


def main():
    if len(sys.argv) < 4:
        sys.exit(__doc__)
    build, soundfont, work = (Path(arg).resolve() for arg in sys.argv[1:4])
    runs = int(sys.argv[sys.argv.index("--runs") + 1]) if "--runs" in sys.argv else 3
    work.mkdir(parents=True, exist_ok=True)
    results = queue.Queue()
    roots = {"/dist/": build / "dist", "/check/": build / "web-check"}

    class Handler(SimpleHTTPRequestHandler):
        extensions_map = {**SimpleHTTPRequestHandler.extensions_map, ".mjs": "text/javascript",
                          ".js": "text/javascript", ".wasm": "application/wasm",
                          ".json": "application/json"}

        def translate_path(self, path):
            clean = path.split("?", 1)[0].split("#", 1)[0]
            if clean == "/piano.sf2":
                return str(soundfont)
            for prefix, root in roots.items():
                if clean.startswith(prefix):
                    return str(root / clean[len(prefix):])
            return str(HERE / clean.lstrip("/"))

        def do_POST(self):
            body = self.rfile.read(int(self.headers["Content-Length"]))
            results.put(json.loads(body))
            self.send_response(204)
            self.end_headers()

        def end_headers(self):
            self.send_header("Cache-Control", "no-store")
            super().end_headers()

        def log_message(self, *args):
            pass

    server = ThreadingHTTPServer(("127.0.0.1", 0), partial(Handler, directory=str(HERE)))
    port = server.server_address[1]
    threading.Thread(target=server.serve_forever, daemon=True).start()
    browsers = find_browsers()
    tag = "wb08-" + secrets.token_hex(4)
    report = {"browsers": {name: version(exe) for name, exe in browsers.items()},
              "profileTag": tag, "loads": []}
    print("browsers:", report["browsers"], flush=True)
    pages = [("timing", "/preview_timing.html?report")] * runs + [("check", "/check_driver.html")]
    try:
        for name, exe in browsers.items():
            for run, (page, path) in enumerate(pages):
                profile = tempfile.mkdtemp(prefix=f"{tag}-", dir=work)
                proc = subprocess.Popen(command(exe, profile, f"http://127.0.0.1:{port}{path}"),
                                        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
                started = time.monotonic()
                try:
                    got = results.get(timeout=TIMEOUT)
                except queue.Empty:
                    got = {"page": page, "failure": "timeout"}
                finally:
                    stop(proc, profile)
                    for _ in range(20):
                        try:
                            shutil.rmtree(profile)
                            break
                        except OSError:
                            time.sleep(0.5)
                got.update(browser=name, run=run, wallSeconds=round(time.monotonic() - started, 1))
                report["loads"].append(got)
                print(json.dumps({k: got.get(k) for k in (
                    "browser", "page", "run", "createInstrumentMs", "firstRender", "matched",
                    "failure", "error")}), flush=True)
    finally:
        server.shutdown()
        server.server_close()
        report["processesLeft"] = processes_naming(tag)
        print("processes left naming", tag, ":", report["processesLeft"], flush=True)
    (work / "browser-results.json").write_text(json.dumps(report, indent=1), encoding="utf-8")
    summarise(report)


def summarise(report):
    print("\nbrowser  load  import  loadModular  createInstrument  first render  "
          "later medians (key@rate: ms)")
    for load in report["loads"]:
        if load.get("page") != "timing" or "failure" in load:
            continue
        later = ", ".join(f"{t['key']}@{t['rate']}: {t['medianMs']}" for t in load["later"])
        print(f"{load['browser']:8} {load['run']:4}  {load['importMs']:6}  "
              f"{load['loadModularMs']:11}  {load['createInstrumentMs']:16}  "
              f"{load['firstRender']['ms']:12}  {later}")
    for name in report["browsers"]:
        timing = [l for l in report["loads"]
                  if l.get("browser") == name and l.get("page") == "timing" and "failure" not in l]
        if timing:
            pick = lambda f: statistics.median(f(l) for l in timing)
            print(f"{name}: medians over {len(timing)} loads: loadModular "
                  f"{pick(lambda l: l['loadModularMs'])} ms, createInstrument "
                  f"{pick(lambda l: l['createInstrumentMs'])} ms, first render "
                  f"{pick(lambda l: l['firstRender']['ms'])} ms, later renders (middle C, 48 kHz) "
                  f"{pick(lambda l: l['later'][0]['medianMs'])} ms")
        for check in (l for l in report["loads"]
                      if l.get("browser") == name and l.get("page") == "check"):
            print(f"{name}: check page matched={check.get('matched')} "
                  f"verdict={check.get('verdict')!r} {check.get('error', '')}")


if __name__ == "__main__":
    main()
