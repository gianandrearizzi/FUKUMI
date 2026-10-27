# Fukumi per iPad, app nativa

L'app apre la pagina dello strumento (la stessa di `web/app`) a schermo intero e aggiunge quello che Safari non dà:

- **MIDI dal sistema.** L'app ascolta tutti i dispositivi MIDI dell'iPad con CoreMIDI (l'EC4 via USB, il MIDI di rete, le altre app) e li passa al MIDI learn. Le sorgenti collegate e scollegate si aggiornano da sole.
- **Audio da strumento.** Sessione audio in riproduzione e registrazione a 48 kHz con buffer corto. Dopo una telefonata o un'altra app l'audio riparte da solo.
- **Schermo sempre acceso**, barra di stato e indicatore Home nascosti.
- **Niente rete.** La pagina è dentro l'app e la serve un piccolo server raggiungibile solo dall'iPad (`http://localhost:47816`): lì il browser concede microfono e audio worklet. Impostazioni e loop registrati restano tra un'apertura e l'altra.

Non è ancora un AUv3: non si apre dentro AUM o Loopy Pro. Quello arriva con il plugin iPlug2, dove il motore C++ gira nativo e questa stessa pagina diventa l'interfaccia del plugin.

## Costruirla senza un Mac: GitHub Actions e TestFlight

Serve l'Apple Developer Program (99 USD l'anno). Tutti i passi si fanno dal browser, anche sull'iPad.

1. **Identificativo dell'app.** Su developer.apple.com, in Certificates, Identifiers & Profiles, poi Identifiers, aggiungi un App ID, per esempio `it.tuonome.fukumi`.
2. **App su App Store Connect.** In App, tocca + e poi Nuova app: piattaforma iOS, nome Fukumi, l'identificativo del punto 1.
3. **Chiave API.** In Utenti e accesso, poi Integrazioni, App Store Connect API, crea una chiave del team con ruolo Admin. Scarica il file `.p8` (si scarica una volta sola) e annota Key ID e Issuer ID.
4. **Segreti su GitHub.** Nel repository, Settings, Secrets and variables, Actions, aggiungi:
   - `APPLE_TEAM_ID`: il Team ID (developer.apple.com, Membership)
   - `BUNDLE_ID`: l'identificativo del punto 1
   - `ASC_KEY_ID` e `ASC_ISSUER_ID`: dal punto 3
   - `ASC_KEY_P8`: il contenuto del file `.p8`, tutto, righe BEGIN ed END comprese
5. **Build.** In Actions apri «App iPad», poi Run workflow. Il primo lavoro compila per il simulatore, il secondo firma e carica su TestFlight. A ogni modifica di `ios` o `web/app` il primo lavoro gira da solo, come controllo.
6. **Installazione.** In App Store Connect, TestFlight, aggiungi te stesso al gruppo di test interno. Sull'iPad installa l'app TestFlight: Fukumi compare lì dopo qualche minuto di elaborazione.

## Costruirla con un Mac

```sh
brew install xcodegen
cd ios
xcodegen generate
open Fukumi.xcodeproj
```

In Xcode, nel target Fukumi, Signing & Capabilities: scegli il tuo team e un identificativo tuo. Collega l'iPad e premi Run. Con un account Apple gratuito l'app resta installata 7 giorni.

## Aggiornare la pagina dentro l'app

L'app usa la cartella `web/app` così com'è. Dopo una modifica allo strumento:

```sh
python3 build_web.py          # ricostruisce web/app
python3 tools/fetch_fonts.py  # facoltativo: i caratteri dentro l'app, per l'uso senza rete (serve npm)
```

Poi ricostruisci l'app. Senza `fetch_fonts.py` i caratteri arrivano dalla rete quando c'è; senza rete l'iPad usa i suoi (Hiragino).

## Da sapere

- Il server ascolta solo sull'iPad stesso, non sulla rete locale: non compare la richiesta «trovare dispositivi sulla rete locale».
- La porta 47816 è fissa perché i dati salvati della pagina sono legati all'indirizzo. Cambiarla fa ripartire da impostazioni vuote.
- Quanti canali arrivano da un'interfaccia multicanale (lo Zoom H6) lo decide WebKit. Se ne arrivano meno del previsto, lo risolve il passaggio al motore nativo del plugin.
- Su un Mac, Safari può ispezionare la pagina dell'app (menu Sviluppo, con l'iPad collegato).
- Questo progetto non è stato compilato nell'ambiente in cui è stato scritto, che non ha Xcode: la prima build su GitHub Actions è la verifica.
