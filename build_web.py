"""Assembles the web prototype: page with the engine embedded, plus worklet.js."""
import base64, json, os, shutil
root = os.path.dirname(os.path.abspath(__file__))
web = os.path.join(root, 'web')
dist = os.path.join(web, 'dist')
os.makedirs(dist, exist_ok=True)
wasm = open(os.path.join(root, 'build', 'engine.wasm'), 'rb').read()
worklet = open(os.path.join(web, 'worklet.js')).read()
page = open(os.path.join(web, 'page_template.html')).read()
page = page.replace('"__WASM_B64__"', json.dumps(base64.b64encode(wasm).decode()))
page = page.replace('__WORKLET_SRC__', json.dumps(worklet))
open(os.path.join(dist, 'index.html'), 'w').write(page)
shutil.copy(os.path.join(web, 'worklet.js'), os.path.join(dist, 'worklet.js'))
print('index.html', len(page) // 1024, 'KB; engine', len(wasm) // 1024, 'KB')

# The iPad app: the same page as a full-screen web app that works offline
# (manifest, service worker, icons). Publish the folder web/app on any HTTPS host.
import time, zipfile
app = os.path.join(web, 'app')
os.makedirs(app, exist_ok=True)
assets = os.path.join(web, 'app_assets')
version = time.strftime('%Y%m%d%H%M')
head = """<meta name="viewport" content="width=device-width, initial-scale=1, viewport-fit=cover">
<meta name="apple-mobile-web-app-capable" content="yes">
<meta name="mobile-web-app-capable" content="yes">
<meta name="apple-mobile-web-app-status-bar-style" content="black-translucent">
<meta name="apple-mobile-web-app-title" content="Fukumi">
<meta name="theme-color" content="#e4e6e1" media="(prefers-color-scheme: light)">
<meta name="theme-color" content="#1f2327" media="(prefers-color-scheme: dark)">
<link rel="manifest" href="manifest.webmanifest">
<link rel="apple-touch-icon" href="apple-touch-icon.png">
<link rel="icon" href="favicon.png">"""
sw_register = """<script>
if ('serviceWorker' in navigator && !window.fukumiNative) navigator.serviceWorker.register('sw.js').catch(() => {});
</script>"""
app_page = page.replace('<meta charset="utf-8">', '<meta charset="utf-8">\n' + head, 1)
# Fonts kept inside the app (tools/fetch_fonts.py puts them in web/app/fonts): they win over the online ones.
if os.path.exists(os.path.join(app, 'fonts', 'fonts.css')):
    gf = app_page.index('<link rel="stylesheet" href="https://fonts.googleapis.com')
    gf = app_page.index('>', gf) + 1
    app_page = app_page[:gf] + '\n<link rel="stylesheet" href="fonts/fonts.css">' + app_page[gf:]
app_page = '<!doctype html>\n<html lang="it">\n' + app_page + '\n' + sw_register + '\n</html>\n'
open(os.path.join(app, 'index.html'), 'w').write(app_page)
shutil.copy(os.path.join(web, 'worklet.js'), os.path.join(app, 'worklet.js'))
for f in ['manifest.webmanifest', 'icon-192.png', 'icon-512.png', 'apple-touch-icon.png', 'favicon.png', 'LEGGIMI.md', 'THIRD_PARTY_NOTICES.md']:
    if os.path.exists(os.path.join(assets, f)): shutil.copy(os.path.join(assets, f), os.path.join(app, f))
open(os.path.join(app, 'sw.js'), 'w').write(open(os.path.join(assets, 'sw.js')).read().replace('__VERSION__', version))
os.makedirs(os.path.join(root, 'out'), exist_ok=True)
with zipfile.ZipFile(os.path.join(root, 'out', 'fukumi_app_ipad.zip'), 'w', zipfile.ZIP_DEFLATED) as z:
    for d, _, files in sorted(os.walk(app)):
        for f in sorted(files):
            z.write(os.path.join(d, f), os.path.relpath(os.path.join(d, f), app))
print('app: web/app (version', version + '), out/fukumi_app_ipad.zip')
