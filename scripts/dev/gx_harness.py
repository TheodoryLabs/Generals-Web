#!/usr/bin/env python3
"""CDP harness for GeneralsX WASM crash repro.

Launches disposable Chrome-for-Testing, drives the game via CDP, watches for
renderer crash (Inspector.targetCrashed) vs in-game heartbeat.

Usage: python3 gx_harness.py <url> <outdir> <actions.json> [--headless]
Exit codes: 0=SUCCESS, 2=CRASH, 3=TIMEOUT/other
"""
import base64
import json
import os
import shutil
import signal
import subprocess
import sys
import time
import urllib.request

CHROME = os.environ.get(
    "GX_CHROME",
    "/Users/ralphtheodori/Documents/GeneralsX-WASM/chrome/"
    "mac_arm-149.0.7827.201/chrome-mac-arm64/"
    "Google Chrome for Testing.app/Contents/MacOS/"
    "Google Chrome for Testing")
DEVTOOLS_PORT = 9222

import websocket  # websocket-client


class Driver:
    def __init__(self, url, outdir, headless=False):
        self.url = url
        self.outdir = outdir
        self.headless = headless
        os.makedirs(outdir, exist_ok=True)
        self.console_f = open(os.path.join(outdir, "console.log"), "w")
        self.events_f = open(os.path.join(outdir, "events.log"), "w")
        self.proc = None
        self.ws = None
        self.msg_id = 0
        self.crashed = False
        self.detached = False
        self.last_heartbeat = 0.0
        self.heartbeat_count = 0
        self.canvas_rect = None
        self.console_lines = []  # (t, text)
        self.big_reads = 0
        self.last_big_read = 0.0
        self.t0 = time.time()

    def log(self, msg):
        line = "[%8.2f] %s" % (time.time() - self.t0, msg)
        print(line, flush=True)
        self.events_f.write(line + "\n")
        self.events_f.flush()

    def start_chrome(self):
        profile = os.path.join(self.outdir, "profile")
        shutil.rmtree(profile, ignore_errors=True)
        args = [
            CHROME,
            "--remote-debugging-port=%d" % DEVTOOLS_PORT,
            "--remote-allow-origins=*",
            "--user-data-dir=" + profile,
            "--no-first-run", "--no-default-browser-check",
            "--disable-background-networking",
            "--disable-backgrounding-occluded-windows",
            "--disable-renderer-backgrounding",
            "--disable-background-timer-throttling",
            "--disable-component-update",
            "--disable-sync",
            "--window-size=1100,850",
            "--window-position=-2400,100",
        ]
        if self.headless:
            args.append("--headless=new")
        args.append("about:blank")
        self.proc = subprocess.Popen(
            args,
            stdout=open(os.path.join(self.outdir, "chrome.stdout"), "w"),
            stderr=open(os.path.join(self.outdir, "chrome.stderr"), "w"))
        # wait for devtools
        for _ in range(100):
            try:
                with urllib.request.urlopen(
                        "http://127.0.0.1:%d/json/list" % DEVTOOLS_PORT,
                        timeout=1) as r:
                    targets = json.load(r)
                pages = [t for t in targets if t.get("type") == "page"]
                if pages:
                    self.ws_url = pages[0]["webSocketDebuggerUrl"]
                    self.target_id = pages[0]["id"]
                    self.log("chrome up, target %s" % self.target_id)
                    return
            except Exception:
                pass
            time.sleep(0.3)
        raise RuntimeError("chrome devtools did not come up")

    def connect(self):
        self.ws = websocket.create_connection(self.ws_url, timeout=5)
        self.ws.settimeout(0.5)
        for method, params in [
            ("Page.enable", {}),
            ("Runtime.enable", {}),
            ("Inspector.enable", {}),
            ("Log.enable", {}),
            ("Network.enable", {"maxTotalBufferSize": 1000000,
                                "maxResourceBufferSize": 100000}),
        ]:
            self.cmd(method, params)

    def cmd(self, method, params=None, timeout=20):
        self.msg_id += 1
        mid = self.msg_id
        self.ws.send(json.dumps(
            {"id": mid, "method": method, "params": params or {}}))
        deadline = time.time() + timeout
        while time.time() < deadline:
            msg = self._recv_one()
            if msg is None:
                continue
            if msg.get("id") == mid:
                if "error" in msg:
                    raise RuntimeError("%s -> %s" % (method, msg["error"]))
                return msg.get("result", {})
            if "method" in msg:
                self._handle_event(msg)
        raise TimeoutError("no reply to %s within %ss" % (method, timeout))

    def _recv_one(self):
        try:
            raw = self.ws.recv()
        except websocket.WebSocketTimeoutException:
            return None
        except Exception as e:
            self.log("WS ERROR: %r" % e)
            self.detached = True
            return None
        try:
            return json.loads(raw)
        except Exception:
            return None

    def pump(self, seconds):
        """Process events for `seconds`."""
        end = time.time() + seconds
        while time.time() < end:
            msg = self._recv_one()
            if msg is None:
                if self.detached:
                    return
                continue
            if "method" in msg:
                self._handle_event(msg)

    def _handle_event(self, msg):
        m = msg["method"]
        p = msg.get("params", {})
        if m == "Runtime.consoleAPICalled":
            parts = []
            for a in p.get("args", []):
                v = a.get("value")
                if v is None:
                    v = a.get("description", a.get("type", "?"))
                parts.append(str(v))
            text = " ".join(parts)
            t = time.time() - self.t0
            self.console_lines.append((t, text))
            self.console_f.write("[%8.2f] %s\n" % (t, text))
            self.console_f.flush()
            if text.startswith("GX-HB"):
                self.last_heartbeat = time.time()
                self.heartbeat_count += 1
        elif m == "Log.entryAdded":
            e = p.get("entry", {})
            t = time.time() - self.t0
            self.console_f.write("[%8.2f] LOG(%s/%s) %s\n" % (
                t, e.get("source"), e.get("level"), e.get("text")))
            self.console_f.flush()
        elif m == "Inspector.targetCrashed":
            self.crashed = True
            self.log("*** Inspector.targetCrashed ***")
        elif m == "Inspector.detached":
            self.detached = True
            self.log("*** Inspector.detached: %s ***" % p.get("reason"))
        elif m == "Network.requestWillBeSent":
            url = p.get("request", {}).get("url", "")
            if ".big" in url.lower():
                self.big_reads += 1
                self.last_big_read = time.time()
                hdrs = p.get("request", {}).get("headers", {})
                rng = hdrs.get("Range", hdrs.get("range", ""))
                if self.big_reads <= 30 or self.big_reads % 200 == 0:
                    self.log("BIG read #%d %s %s" % (
                        self.big_reads, url.rsplit("/", 1)[-1], rng))

    def evaluate(self, expr, timeout=20):
        r = self.cmd("Runtime.evaluate",
                     {"expression": expr, "returnByValue": True},
                     timeout=timeout)
        return r.get("result", {}).get("value")

    def get_canvas_rect(self):
        v = self.evaluate(
            "(function(){var r=document.getElementById('canvas')"
            ".getBoundingClientRect();"
            "return JSON.stringify({l:r.left,t:r.top,w:r.width,h:r.height});"
            "})()")
        self.canvas_rect = json.loads(v)
        self.log("canvas rect: %s" % self.canvas_rect)
        return self.canvas_rect

    def to_px(self, x800, y600):
        r = self.canvas_rect
        return (r["l"] + x800 / 800.0 * r["w"],
                r["t"] + y600 / 600.0 * r["h"])

    def cmd_async(self, method, params=None):
        """Fire-and-forget command; reply is discarded later as stale."""
        self.msg_id += 1
        self.ws.send(json.dumps(
            {"id": self.msg_id, "method": method, "params": params or {}}))

    def click_px(self, x, y, label=""):
        self.log("click_px (%.0f, %.0f) %s" % (x, y, label))
        for typ, cc in [("mouseMoved", 0), ("mousePressed", 1),
                        ("mouseReleased", 1)]:
            self.cmd_async("Input.dispatchMouseEvent", {
                "type": typ, "x": x, "y": y,
                "button": "left" if cc else "none",
                "buttons": 1 if typ == "mousePressed" else 0,
                "clickCount": cc})
            self.pump(0.12)

    def wait_idle(self, hb_fresh=2.5, big_quiet=4.0, timeout=150):
        """Wait until heartbeats are fresh and no .big reads for a while."""
        self.log("wait_idle hb_fresh=%s big_quiet=%s timeout=%s"
                 % (hb_fresh, big_quiet, timeout))
        deadline = time.time() + timeout
        while time.time() < deadline:
            self.pump(0.5)
            if self.crashed or self.detached:
                self.log("crash during wait_idle")
                return False
            now = time.time()
            hb_ok = self.last_heartbeat and now - self.last_heartbeat < hb_fresh
            big_ok = now - self.last_big_read > big_quiet
            if hb_ok and big_ok:
                self.log("idle reached (hb gap %.1f, big quiet %.1f)"
                         % (now - self.last_heartbeat,
                            now - self.last_big_read))
                return True
        self.log("wait_idle TIMEOUT")
        return False

    def click800(self, x800, y600, label=""):
        x, y = self.to_px(x800, y600)
        self.click_px(x, y, label + " (800x600 %s,%s)" % (x800, y600))

    def screenshot(self, name):
        try:
            r = self.cmd("Page.captureScreenshot",
                         {"format": "png"}, timeout=15)
            path = os.path.join(self.outdir, name + ".png")
            with open(path, "wb") as f:
                f.write(base64.b64decode(r["data"]))
            self.log("screenshot %s" % path)
        except Exception as e:
            self.log("screenshot %s FAILED: %r" % (name, e))

    def wait_console(self, pattern, timeout):
        self.log("wait_console %r timeout=%s" % (pattern, timeout))
        start_idx = 0
        deadline = time.time() + timeout
        while time.time() < deadline:
            for i in range(start_idx, len(self.console_lines)):
                if pattern in self.console_lines[i][1]:
                    self.log("matched console: %s"
                             % self.console_lines[i][1][:200])
                    return True
            start_idx = len(self.console_lines)
            if self.crashed or self.detached:
                self.log("crash/detach while waiting for %r" % pattern)
                return False
            self.pump(0.5)
        self.log("TIMEOUT waiting console %r" % pattern)
        return False

    def install_heartbeat(self):
        self.evaluate(
            "window.__gxhb||(window.__gxhb=setInterval(function(){"
            "console.log('GX-HB',Date.now())},1000)),'ok'")
        self.log("heartbeat installed")

    def count_console(self, pattern):
        return sum(1 for _, t in self.console_lines if pattern in t)

    def wait_ingame_stable(self, ingame_timeout, stable):
        """Wait for TICK-DIAG inGame=1, then require `stable` more seconds of
        life (fresh inGame=1 ticks keep arriving, no crash)."""
        self.log("wait_ingame_stable ingame_timeout=%s stable=%s"
                 % (ingame_timeout, stable))
        if not self.wait_console("inGame=1", ingame_timeout):
            if self.crashed or self.detached:
                self.log("OUTCOME: CRASH (while waiting for inGame=1, "
                         "big_reads=%d)" % self.big_reads)
                return "CRASH"
            self.log("OUTCOME: TIMEOUT (never saw inGame=1, big_reads=%d)"
                     % self.big_reads)
            return "TIMEOUT"
        t_in = time.time()
        base = self.count_console("inGame=1")
        while time.time() - t_in < stable:
            self.pump(1.0)
            if self.crashed or self.detached:
                self.log("OUTCOME: CRASH (%.1fs after inGame=1, "
                         "big_reads=%d)" % (time.time() - t_in,
                                            self.big_reads))
                return "CRASH"
        ticks = self.count_console("inGame=1") - base
        self.log("OUTCOME: %s (stable %.0fs in-game, %d fresh ticks, "
                 "big_reads=%d)" % ("SUCCESS" if ticks > 3 else "STALLED",
                                    stable, ticks, self.big_reads))
        return "SUCCESS" if ticks > 3 else "STALLED"

    def watch_outcome(self, max_wait, stable):
        """After triggering match start: wait for crash, or heartbeat gap
        (=load freeze) followed by resumed heartbeats stable for `stable`s."""
        self.log("watch_outcome max_wait=%s stable=%s" % (max_wait, stable))
        t_start = time.time()
        saw_freeze = False
        resumed_at = None
        last_hb_before = self.last_heartbeat
        while time.time() - t_start < max_wait:
            self.pump(1.0)
            if self.crashed:
                self.log("OUTCOME: CRASH (targetCrashed) after %.1fs, "
                         "big_reads=%d" % (time.time() - t_start,
                                           self.big_reads))
                return "CRASH"
            if self.detached:
                self.log("OUTCOME: DETACHED after %.1fs" %
                         (time.time() - t_start))
                return "CRASH"
            now = time.time()
            gap = now - self.last_heartbeat if self.last_heartbeat else 999
            if not saw_freeze and gap > 4.0:
                saw_freeze = True
                self.log("main-thread freeze detected (hb gap %.1fs)" % gap)
            if saw_freeze and gap < 2.0 and self.last_heartbeat > last_hb_before:
                if resumed_at is None:
                    resumed_at = now
                    self.log("heartbeats resumed after freeze")
            if resumed_at and now - resumed_at >= stable and gap < 3.0:
                self.log("OUTCOME: SUCCESS (stable %.0fs after load, "
                         "big_reads=%d)" % (now - resumed_at, self.big_reads))
                return "SUCCESS"
        self.log("OUTCOME: TIMEOUT (saw_freeze=%s resumed=%s crashed=%s "
                 "big_reads=%d)" % (saw_freeze, bool(resumed_at),
                                    self.crashed, self.big_reads))
        return "TIMEOUT"

    def shutdown(self):
        try:
            if self.ws:
                self.ws.close()
        except Exception:
            pass
        if self.proc:
            try:
                self.proc.terminate()
                self.proc.wait(timeout=5)
            except Exception:
                try:
                    self.proc.kill()
                except Exception:
                    pass
        self.console_f.close()
        self.events_f.close()


def run_actions(d, actions):
    outcome = None
    for act in actions:
        k = act["op"]
        if d.crashed and k not in ("screenshot",):
            d.log("skipping %s — already crashed" % k)
            break
        if k == "navigate":
            d.cmd("Page.navigate", {"url": d.url})
        elif k == "sleep":
            d.pump(act["s"])
        elif k == "wait_console":
            ok = d.wait_console(act["pattern"], act.get("timeout", 120))
            if not ok and act.get("required", True):
                d.log("required wait failed; aborting actions")
                outcome = "CRASH" if (d.crashed or d.detached) else "TIMEOUT"
                break
        elif k == "canvas_rect":
            d.get_canvas_rect()
        elif k == "heartbeat":
            d.install_heartbeat()
        elif k == "click_begin":
            v = d.evaluate(
                "(function(){var b=document.getElementById('gx-start');"
                "var r=b.getBoundingClientRect();"
                "return JSON.stringify({x:r.left+r.width/2,"
                "y:r.top+r.height/2,cls:b.parentElement.className});})()")
            pt = json.loads(v)
            d.log("begin-button info: %s" % v)
            if pt["x"] > 1 and pt["y"] > 1:
                d.click_px(pt["x"], pt["y"], "begin-button")
            else:
                d.log("begin-button rect degenerate; gesture at (1,1)")
                d.click_px(1, 1, "gesture-fallback")
        elif k == "gesture_click":
            # Exact replica of the original exploratory gesture: 3 events
            # at page (0,0). Off-canvas; only serves as the user gesture.
            d.click_px(0, 0, "gesture(0,0)")
        elif k == "wait_idle":
            ok = d.wait_idle(timeout=act.get("timeout", 150),
                             big_quiet=act.get("big_quiet", 4.0))
            if not ok and (d.crashed or d.detached):
                outcome = "CRASH"
                break
        elif k == "click_retry":
            ok = False
            for attempt in range(act.get("attempts", 3)):
                if d.crashed or d.detached:
                    break
                d.click800(act["x"], act["y"],
                           "%s attempt %d" % (act.get("label", ""), attempt))
                if d.wait_console(act["pattern"], act.get("wait_each", 10)):
                    ok = True
                    break
            if not ok:
                if d.crashed or d.detached:
                    d.log("crash during click_retry %s" % act.get("label"))
                    outcome = "CRASH"
                else:
                    d.log("click_retry %s exhausted; aborting"
                          % act.get("label"))
                    d.screenshot("step_failed_%s" % act.get("label", "x"))
                    outcome = "STEP-FAILED"
                break
        elif k == "wait_ingame_stable":
            outcome = d.wait_ingame_stable(act.get("ingame_timeout", 150),
                                           act.get("stable", 60))
        elif k == "click800":
            d.click800(act["x"], act["y"], act.get("label", ""))
        elif k == "screenshot":
            d.screenshot(act["name"])
        elif k == "eval":
            d.log("eval -> %r" % d.evaluate(act["expr"]))
        elif k == "watch_outcome":
            outcome = d.watch_outcome(act.get("max_wait", 180),
                                      act.get("stable", 60))
        else:
            raise ValueError("unknown op %s" % k)
    return outcome


def main():
    url, outdir, actions_path = sys.argv[1], sys.argv[2], sys.argv[3]
    headless = "--headless" in sys.argv
    with open(actions_path) as f:
        actions = json.load(f)
    d = Driver(url, outdir, headless=headless)
    outcome = "ERROR"
    try:
        d.start_chrome()
        d.connect()
        outcome = run_actions(d, actions) or \
            ("CRASH" if (d.crashed or d.detached) else "NO-OUTCOME")
    except Exception as e:
        d.log("EXCEPTION: %r" % e)
        if d.crashed or d.detached:
            outcome = "CRASH"
    finally:
        d.log("FINAL OUTCOME: %s" % outcome)
        d.shutdown()
    print("FINAL:", outcome)
    sys.exit({"SUCCESS": 0, "CRASH": 2}.get(outcome, 3))


if __name__ == "__main__":
    main()
