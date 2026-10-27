# Fukumi sull'iPad

Questa cartella è lo strumento come app web: si installa sulla schermata Home dell'iPad, si apre a schermo intero e funziona anche senza rete. Per installarla serve un indirizzo HTTPS. Il modo più semplice e gratuito è GitHub Pages.

## Pubblicare con GitHub Pages

1. Crea un account su github.com, se non ce l'hai.
2. Crea un repository nuovo, pubblico, per esempio `fukumi`.
3. Nella pagina del repository tocca «Add file», poi «Upload files», e trascina tutti i file di questa cartella. Conferma con «Commit changes».
4. Vai in «Settings», poi «Pages». In «Branch» scegli `main` e la cartella `/ (root)`, poi «Save».
5. Dopo un minuto circa lo strumento è su `https://TUO-NOME.github.io/fukumi/`.

## Installare sull'iPad

1. Apri l'indirizzo in Safari.
2. Tocca il pulsante Condividi, poi «Aggiungi alla schermata Home».
3. Apri lo strumento dall'icona, non dal browser: così è a schermo intero.
4. Tocca «Avvia audio» e consenti l'uso del microfono. Ogni ingresso può scegliere il microfono interno o un canale dell'interfaccia collegata.

Con lo Zoom H6 collegato all'iPad usa la modalità Stereo Mix, con l'ingresso 1 tutto a sinistra e il 2 tutto a destra nel mixer dell'H6: arrivano come canale 1 e 2 dello stesso dispositivo.

## Aggiornare

Carica i file nuovi nello stesso repository, sostituendo i vecchi. L'app prende la versione nuova alla prima apertura con la rete; senza rete usa l'ultima che ha salvato.

## Cosa manca in questa forma

- Safari sull'iPad non dà accesso al MIDI alle pagine web: il MIDI learn con l'EC4 non riceve nulla. L'app nativa (cartella `ios`) lo aggiunge.
- Non è un AUv3: non si apre dentro AUM o Loopy Pro. Anche questo arriva con l'app nativa.
- Le impostazioni e i loop registrati restano salvati su questo iPad, dentro l'app.
