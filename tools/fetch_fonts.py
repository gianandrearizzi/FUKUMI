"""Puts the two typefaces inside the app, so they are there without a network.

Downloads the packages from npm (Fontsource, SIL Open Font License), keeps the Latin
subset of the weights the page uses plus the pieces with the glyphs of 含み, writes
web/app/fonts/fonts.css and links it from web/app/index.html.
Run it after build_web.py, on a computer with npm:  python3 tools/fetch_fonts.py
"""
import os, re, shutil, subprocess, sys, tarfile, tempfile

FAMILIES = {  # npm package -> weights used by the page
    'shippori-mincho': [500, 600],
    'zen-maru-gothic': [400, 500, 700],
}
EXTRA = [0x542B, 0x307F]  # 含 み, in the title

root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
app = os.path.join(root, 'web', 'app')
out = os.path.join(app, 'fonts')


def covers(ranges, cp):
    for part in ranges.split(','):
        part = part.strip().upper().replace('U+', '')
        if not part:
            continue
        if '?' in part:
            lo, hi = int(part.replace('?', '0'), 16), int(part.replace('?', 'F'), 16)
        elif '-' in part:
            lo, hi = (int(x, 16) for x in part.split('-'))
        else:
            lo = hi = int(part, 16)
        if lo <= cp <= hi:
            return True
    return False


def faces(css):
    """The @font-face blocks of a Fontsource stylesheet, with their comment name."""
    for m in re.finditer(r'(/\*\s*([^*]+?)\s*\*/\s*)?@font-face\s*{([^}]*)}', css):
        body = m.group(3)
        src = re.search(r"url\((?:\./)?(?:files/)?([^)'\"]+\.woff2)\)", body)
        rng = re.search(r'unicode-range:\s*([^;]+);', body)
        yield (m.group(2) or ''), body, src.group(1) if src else None, rng.group(1) if rng else ''


def main():
    if not os.path.exists(os.path.join(app, 'index.html')):
        sys.exit('Prima esegui build_web.py: manca web/app/index.html')
    os.makedirs(out, exist_ok=True)
    rules = []
    with tempfile.TemporaryDirectory() as tmp:
        for pkg, weights in FAMILIES.items():
            name = subprocess.check_output(['npm', 'pack', '@fontsource/' + pkg, '--silent'], cwd=tmp, text=True).strip().splitlines()[-1]
            with tarfile.open(os.path.join(tmp, name)) as t:
                t.extractall(os.path.join(tmp, pkg), filter='data')
            base = os.path.join(tmp, pkg, 'package')
            for w in weights:
                css = open(os.path.join(base, f'{w}.css'), encoding='utf-8').read()
                for label, body, src, rng in faces(css):
                    if not src:
                        continue
                    keep = label.endswith(f'latin-{w}-normal') or any(covers(rng, cp) for cp in EXTRA)
                    if not keep:
                        continue
                    shutil.copy(os.path.join(base, 'files', src), os.path.join(out, src))
                    body = re.sub(r'src:[^;]+;', f"src: url({src}) format('woff2');", body)
                    rules.append('@font-face {' + body + '}')
            lic = os.path.join(base, 'LICENSE')
            if os.path.exists(lic):
                shutil.copy(lic, os.path.join(out, f'LICENSE-{pkg}.txt'))
    open(os.path.join(out, 'fonts.css'), 'w', encoding='utf-8').write('\n'.join(rules) + '\n')
    page_path = os.path.join(app, 'index.html')
    page = open(page_path, encoding='utf-8').read()
    link = '<link rel="stylesheet" href="fonts/fonts.css">'
    if link not in page:
        gf = page.index('<link rel="stylesheet" href="https://fonts.googleapis.com')
        gf = page.index('>', gf) + 1
        page = page[:gf] + '\n' + link + page[gf:]
        open(page_path, 'w', encoding='utf-8').write(page)
    print(len(rules), 'caratteri in web/app/fonts')


if __name__ == '__main__':
    main()
