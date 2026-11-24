"""PROTOTYPE, throwaway: midishare-playback ticket 02, runs index.html in installed browsers.

    python browsers.py <out folder>
    python browsers.py <out folder> --only chrome,edge
    python browsers.py <out folder> --serve <port>   (only serve, for a browser run by hand)

<out folder> is build.ps1's -Out, after node_run.mjs wrote the native references there.
Standard library only. Serves this folder at / and <out folder> at /out/ on 127.0.0.1 (a
free port), finds Chrome and Edge where Windows installs them (Firefox too, if present),
and for each module variant starts each browser headless with a fresh, temporary profile:
one full run (renders, comparison, playback, errors) and two instantiate-only runs. The
page posts its results to /result; the browser is then killed. Writes
<out folder>/browser-results.json. Stops the server and every browser it started.
"""

import json
import os
import queue
import shutil
import subprocess
import sys
import tempfile
import threading
import time
from functools import partial
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

HERE = Path(__file__).resolve().parent
VARIANTS = ["noexc", "jsexc", "wasmexc", "exnref"]
RUNS = 3
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
        "firefox": [rf"{pf}\Mozilla Firefox\firefox.exe", rf"{pf86}\Mozilla Firefox\firefox.exe"],
    }
    found = {}
    for name, paths in candidates.items():
        for p in paths:
            if p and Path(p).is_file():
                found[name] = p
                break
    return found


def command(name, exe, profile, url):
    if name == "firefox":
        return [exe, "--headless", "--no-remote", "--profile", profile, url]
    return [exe, "--headless=new", f"--user-data-dir={profile}", "--no-first-run",
            "--no-default-browser-check", "--disable-extensions",
            "--autoplay-policy=no-user-gesture-required", url]


def kill(proc, profile):
    """Edge relaunches itself under a new parent, so a process-tree kill misses it: stop every
    process whose command line names this run's unique profile folder."""
    subprocess.run(["taskkill", "/T", "/F", "/PID", str(proc.pid)], capture_output=True)
    name = Path(profile).name
    subprocess.run(["powershell", "-NoProfile", "-Command",
                    "Get-CimInstance Win32_Process | Where-Object { $_.ProcessId -ne $PID -and "
                    f"$_.CommandLine -like '*{name}*' }} | ForEach-Object {{ Stop-Process "
                    "-Id $_.ProcessId -Force -ErrorAction SilentlyContinue }"], capture_output=True)
    proc.wait(timeout=30)


def version(exe):
    out = subprocess.run(["powershell", "-NoProfile", "-Command",
                          f"(Get-Item '{exe}').VersionInfo.ProductVersion"],
                         capture_output=True, text=True)
    return out.stdout.strip()


def main():
    out = Path(sys.argv[1]).resolve()
    results = queue.Queue()

    class Handler(SimpleHTTPRequestHandler):
        extensions_map = {**SimpleHTTPRequestHandler.extensions_map, ".mjs": "text/javascript",
                          ".js": "text/javascript", ".wasm": "application/wasm",
                          ".mid": "audio/midi", ".f32": "application/octet-stream"}

        def translate_path(self, path):
            clean = path.split("?", 1)[0].split("#", 1)[0]
            if clean.startswith("/out/"):
                return str(out / clean[len("/out/"):])
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

    serve_only = len(sys.argv) > 2 and sys.argv[2] == "--serve"
    server = ThreadingHTTPServer(("127.0.0.1", int(sys.argv[3]) if serve_only else 0),
                                 partial(Handler, directory=str(HERE)))
    port = server.server_address[1]
    if serve_only:  # for a browser driven by hand: serve until stopped, print what is posted
        print(f"serving http://127.0.0.1:{port}/index.html", flush=True)
        threading.Thread(target=server.serve_forever, daemon=True).start()
        try:
            while True:
                print(json.dumps(results.get()), flush=True)
        finally:
            server.shutdown()
    threading.Thread(target=server.serve_forever, daemon=True).start()
    browsers = find_browsers()
    if len(sys.argv) > 3 and sys.argv[2] == "--only":
        browsers = {n: p for n, p in browsers.items() if n in sys.argv[3].split(",")}
    report = {"browsers": {n: version(p) for n, p in browsers.items()},
              "absent": [n for n in ("chrome", "edge", "firefox") if n not in browsers], "runs": []}
    print("browsers:", report["browsers"], "absent:", report["absent"], flush=True)
    try:
        for name, exe in browsers.items():
            for variant in VARIANTS:
                for run in range(RUNS):
                    only = "" if run == 0 else "&only=instantiate"
                    url = f"http://127.0.0.1:{port}/index.html?variant={variant}&report=1{only}"
                    profile = tempfile.mkdtemp(prefix="ms02-profile-")
                    proc = subprocess.Popen(command(name, exe, profile, url),
                                            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
                    started = time.monotonic()
                    try:
                        got = results.get(timeout=TIMEOUT)
                        got.update(browser=name, run=run, wallSeconds=round(time.monotonic() - started, 1))
                    except queue.Empty:
                        got = {"browser": name, "variant": variant, "run": run, "failure": "timeout"}
                    finally:
                        kill(proc, profile)
                        for _ in range(20):
                            try:
                                shutil.rmtree(profile)
                                break
                            except OSError:
                                time.sleep(0.5)
                    report["runs"].append(got)
                    short = {k: got.get(k) for k in ("browser", "variant", "run", "instantiateMs",
                                                    "failure")}
                    print(json.dumps(short), flush=True)
    finally:
        server.shutdown()
        server.server_close()
    suffix = "-" + sys.argv[3].replace(",", "-") if len(sys.argv) > 3 and sys.argv[2] == "--only" else ""
    (out / f"browser-results{suffix}.json").write_text(json.dumps(report, indent=1), encoding="utf-8")
    summarise(report)


def summarise(report):
    print("\nbrowser  variant  instantiate ms (3 runs)   render ms 44.1k/48k: worst, long   "
          "vs native   playback   errors")
    for name in report["browsers"]:
        for variant in VARIANTS:
            runs = [r for r in report["runs"] if r.get("browser") == name and r.get("variant") == variant]
            inst = [round(r["instantiateMs"], 1) for r in runs if "instantiateMs" in r]
            full = next((r for r in runs if r.get("run") == 0), {})
            rend = full.get("renders", {})
            ms = lambda k: rend.get(k, {}).get("renderMs")
            diff = sum(e.get("vsNative", {}).get("differing", 0) for e in rend.values())
            compared = sum(1 for e in rend.values() if "vsNative" in e)
            errs = full.get("errors", {})
            caught = errs.get("parseCaughtInCpp", {}).get("message")
            uncaught = errs.get("parseUncaught", {}).get("type")
            print(f"{name:8} {variant:8} {inst}   {ms('worst@44100')}/{ms('worst@48000')}, "
                  f"{ms('long@44100')}/{ms('long@48000')}   {diff} differ in {compared} renders   "
                  f"{full.get('playback', {}).get('ok')}   caught={caught!r} uncaught={uncaught} "
                  f"after={errs.get('renderAfterErrors', {}).get('sameAsBefore')} "
                  f"{full.get('failure', '')}")


if __name__ == "__main__":
    main()
