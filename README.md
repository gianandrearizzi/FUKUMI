# Fukumi

Strumento elettronico: due ingressi, due looper granulari con delay, due risonatori (Rings) e due vocoder modali, mixer, box di modulazione, matrice audio e matrice di controllo, MIDI learn.

## Cartelle

- `src`: il motore in C++, portabile (lo stesso andrà nel plugin iPlug2).
- `test`: prove del motore e rendering di esempio.
- `web`: la pagina dello strumento (`page_template.html`), il worklet e l'interfaccia WebAssembly del motore.
  - `web/dist`: la pagina singola, con il motore incorporato.
  - `web/app`: la stessa pagina come app web per l'iPad (offline, icona), usata anche dall'app nativa.
- `ios`: l'app nativa per iPad (vedi `ios/README.md`).
- `tools`: patch del codice Rings, recupero delle dipendenze e dei caratteri.

## Costruire

```sh
tools/fetch_vendor.sh      # codice Mutable Instruments (MIT) in vendor/eurorack
make && build/test_gestures && build/test_delay
./build_wasm.sh            # motore WebAssembly: clang 17+ e WASI (vedi le prime righe dello script)
python3 build_web.py       # web/dist e web/app
```

`web/app` è già costruita nel repository: l'app iPad si può compilare anche senza questi passi.

## Licenze

Il codice di Rings e stmlib è di Emilie Gillet, licenza MIT: vedi `web/app_assets/THIRD_PARTY_NOTICES.md`.
