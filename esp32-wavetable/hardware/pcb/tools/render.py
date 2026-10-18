"""PNG previews of a board: kicad-cli's SVG of the chosen layers, drawn by headless Chromium on a dark background."""

import os
import subprocess
import sys
import tempfile

CHROME = os.environ.get("CHROME", "/opt/pw-browsers/chromium-1194/chrome-linux/chrome")


def render(pcb, layers, png, scale=10, mirror=False, background="#101418"):
    with tempfile.TemporaryDirectory() as tmp:
        svg = os.path.join(tmp, "b.svg")
        subprocess.run(["kicad-cli", "pcb", "export", "svg", "--layers", layers, "--page-size-mode", "2",
                        "--exclude-drawing-sheet"] + (["--mirror"] if mirror else []) + ["-o", svg, pcb],
                       check=True, capture_output=True)
        text = open(svg).read()
        w = float(text.split('width="')[1].split("mm")[0])
        h = float(text.split('height="')[1].split("mm")[0])
        W, H = int(w * scale) + 40, int(h * scale) + 160   # the window loses some height to the browser
        html = os.path.join(tmp, "b.html")
        open(html, "w").write(f'<html><body style="margin:0;background:{background}">'
                              f'<img src="b.svg" style="width:{w * scale}px;margin:20px;display:block"></body></html>')
        subprocess.run([CHROME, "--headless", "--no-sandbox", "--disable-gpu", "--hide-scrollbars",
                        f"--window-size={W},{H}", f"--screenshot={os.path.abspath(png)}", "file://" + html],
                       check=True, capture_output=True)


if __name__ == "__main__":
    render(sys.argv[1], sys.argv[2], sys.argv[3], mirror=len(sys.argv) > 4)
