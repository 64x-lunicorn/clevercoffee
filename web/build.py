"""Builds the web UI into one gzipped page for LittleFS: data/www/index.html.gz

    python3 web/build.py

index.html gets style.css, the vendored Preact (MIT, web/vendor) and app.js inlined, so the
machine serves a single file and the UI never needs the internet.
"""
import gzip
from pathlib import Path

WEB = Path(__file__).parent
OUT = WEB.parent / "data" / "www" / "index.html.gz"


def main():
    html = (WEB / "index.html").read_text()
    css = (WEB / "style.css").read_text()
    scripts = "\n".join((WEB / name).read_text() for name in ("vendor/preact.min.umd.js", "vendor/hooks.umd.js"))
    app = (WEB / "app.js").read_text()
    html = html.replace("/*STYLE*/", css).replace("/*VENDOR*/", scripts).replace("/*APP*/", app)
    OUT.parent.mkdir(parents=True, exist_ok=True)
    # mtime=0: identical input gives an identical file
    OUT.write_bytes(gzip.compress(html.encode(), compresslevel=9, mtime=0))
    print(f"{OUT.relative_to(WEB.parent)}: {len(html)} bytes, {OUT.stat().st_size} gzipped")


if __name__ == "__main__":
    main()
