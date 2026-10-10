# Tonwerk Synth

Die beiden Browser-Synthesizer aus Tonwerk (YuE UI) als Plugins für Logic Pro: **Tonwerk Analog** und **Tonwerk FM**, jeder mit Delay und Hall und eigenen Werksklängen. Dazu kommen **Tonwerk DX** mit sechs Operatoren für den klassischen FM-Klang der Achtziger, der Wavetable-Synthesizer **Tonwerk Wavetable** für Dubstep-Bässe, die Effekte **Chrome Glitch** für zerstückelte Gesangsspuren und **Tonwerk Distortion**, ein klassischer Gitarren-Verzerrer (siehe unten). Klänge, die in Tonwerk auf der Instrumente-Seite gespeichert sind, lassen sich übernehmen und klingen hier wie dort.

Logic lädt nur Audio Units, deshalb wird das Plugin als **Audio Unit** gebaut, dazu als VST3 (für andere Hosts) und als eigenständige App zum Ausprobieren ohne Logic.

![Tonwerk Analog](docs/analog.png)
![Tonwerk FM](docs/fm.png)

## Zwei Plugins statt einem

Bis 0.3.0 waren beide Engines ein Plugin, **Tonwerk Synth**, mit einem Umschalter oben. Jetzt ist jede Engine ein eigenes Instrument: **Tonwerk Analog** (`aumu TwAn Bvlp`) und **Tonwerk FM** (`aumu TwFm Bvlp`). Jedes hat nur seine eigenen Regler (Logic zeigt bei der Automation also keine fremden mehr) und seine eigenen Werksklänge (siehe [Werksklänge](#werksklänge)).

Die aus Tonwerk oder Dateien übernommenen Klänge teilen sich beide Plugins (dieselbe `presets.json`); jedes zeigt im Menü nur die seiner Engine. Lädst du in Tonwerk Analog eine Datei mit FM-Klängen, landen sie im Menü von Tonwerk FM.

**Alte Projekte:** Tonwerk Synth bleibt installiert und lädt seine Projekte wie bisher, mit beiden Engines und den acht alten Werksklängen auf denselben Programmnummern (die neuen hängen dahinter). Es hat ebenfalls die neue Oberfläche. Für neue Spuren nimmst du Tonwerk Analog oder Tonwerk FM. Eine alte Spur ziehst du um, indem du in Tonwerk Synth **Speichern** drückst und die Datei im neuen Plugin mit **Datei laden** öffnest.

**Oberfläche:** alle Tonwerk-Plugins sehen jetzt aus wie Tonwerk Wavetable: fast schwarz, Gelb, Cyan und Rot im Neon-Look, Paneele mit abgeschnittener Ecke, Bildschirme mit Scanlines. Jede Hüllkurve zeigt ihre Form, Tonwerk FM zeichnet den gewählten Algorithmus (Träger gelb, Modulatoren cyan, die Rückkopplung an Operator 4) und schreibt über jede Operator-Hüllkurve, was der Operator gerade tut („TRÄGER“, „MOD → 1“). Werte stehen als ganze Zahlen mit Einheit da: Zeiten in ms, Pegel und Mengen in %, die Filter-Hüllkurve in Halbtönen (HT), langsame LFO-Raten mit einer Nachkommastelle.

## Was drin ist

**Analog**: zwei Oszillatoren (Sinus, Dreieck, Sägezahn, Rechteck/Puls) mit Oktave, Verstimmung und Pegel, Rauschen, Filter (Tiefpass, Hochpass, Bandpass) mit Resonanz, Keytracking und eigener Hüllkurve, Lautstärke-Hüllkurve, LFO auf Tonhöhe, Filter oder Lautstärke, frei oder im Songtempo. Der Puls hat eine Pulsbreite, die der LFO und beide Hüllkurven bewegen können (PWM).

**Wavefolder und Sample & Hold** gibt es nur im Plugin, nicht in Tonwerk. Der Wavefolder sitzt zwischen Oszillatoren und Filter. **Menge** treibt die Mischung bis zum Achtfachen und faltet alles über dem Vollpegel zurück, statt es abzuschneiden. **Symmetrie** verschiebt das Signal vor dem Falten, das bringt geradzahlige Obertöne. Mit **Hüllkurve** öffnet und schließt die Filter-Hüllkurve den Wavefolder. Sample & Hold nimmt in jedem Schritt einen neuen Zufallswert und hält ihn bis zum nächsten. **Filter** verschiebt damit den Cutoff um bis zu zwei Oktaven nach oben oder unten, **Tonhöhe** den Ton um bis zu eine Oktave. Das Tempo ist frei oder im Songtempo, und jede Note würfelt ihre eigene Folge. Beide stehen auf 0, solange du sie nicht aufdrehst, und Klänge aus Tonwerk laden mit beiden aus.

**FM**: vier Sinus-Operatoren in acht Algorithmen wie bei den 4-Operator-FM-Synths der Achtziger, Feedback auf Operator 4, eine Hüllkurve pro Operator, Anschlagsdynamik pro Operator, LFO auf Tonhöhe, Modulationsindex oder Lautstärke, frei oder im Songtempo.

**Effekte**: Delay mit Tiefpass in der Rückkopplung und Hall, beide als Send. Das Delay läuft frei oder im Songtempo.

**Tempo-Sync**: Mit **Sync** folgen LFO, Sample & Hold und Delay dem Tempo von Logic. Statt Hz oder Sekunden wählst du einen Notenwert von 4/1 bis 1/32, auch punktiert und triolisch. Ohne Host-Tempo (in der App) gelten 120 BPM. Der LFO beginnt wie in Tonwerk mit jeder Note neu. Er läuft also im Tempo, aber nicht auf den Schlag genau.

**Oszilloskop**: unten rechts neben der Tastatur, in beiden Plugins. Es zeigt die Wellenform dessen, was das Plugin gerade spielt, und steht bei einem gehaltenen Ton still.

16 Stimmen, Pitchbend ±2 Halbtöne, Sustain-Pedal. Jeder Regler ist ein Parameter, den Logic automatisieren kann und mit dem Projekt speichert. Eine Tastatur unten im Fenster spielt das Plugin auch ohne MIDI-Keyboard an.

## Chrome Glitch (Effekt)

Ein Audio-Effekt aus demselben Repository, für eine Gesangsspur, deren „Sprach-Chrome“ versagt: Silben stottern („bo-bo-body“), die Stimme setzt kurz aus, wird digital zerbröselt und springt in der Tonhöhe. Er liegt in Logic unter **Audio-FX → Audio Units → b-velop → Chrome Glitch** und wird mit Tonwerk Synth zusammen gebaut, installiert und veröffentlicht.

![Chrome Glitch](docs/chrome-glitch.png)

Oben rechts sitzt ein kleiner **Monitor**: Solange die Stimme sauber ist, zeigt er „CHROME OK“. Bei jedem Glitch flimmert er mit zerrissenen Balken und Rauschen und zeigt „INTERFERENCE“ (Stottern), „NO SIGNAL“ (Aussetzer) oder „FROZEN“ (Einfrieren). Die Texte im Monitor sind bewusst englisch, wie die Anzeigen im Spiel. Auch Glitches, die kürzer als ein Bild sind, lassen ihn aufflackern.

Der Effekt arbeitet ohne Latenz auf dem, was gerade hereinkommt. An jedem Schritt des Rasters (im Songtempo) oder zu zufälligen Momenten (40–250 ms auseinander) würfelt er, ob etwas passiert:

- **Stottern**: Die letzten 10–200 ms (Standard 50 ms) werden mehrmals wiederholt. **Beschleunigen** macht jede Wiederholung kürzer, so wird die Silbe immer schneller. Ab der zweiten Wiederholung kann die Tonhöhe springen (**Pitch-Chance**, **Pitch-Bereich** bis ±24 Halbtöne).
- **Aussetzer**: Die Stimme fällt 20–300 ms ganz weg.
- **Bitcrusher**: Bei jedem Glitch voll (bis 4 Bit und 16-faches Downsampling), dazwischen nur leicht, mit der Stärke wachsend.
- **Einfrieren**: Hält die letzte Silbe in einer Schleife fest, solange es an ist. Für das letzte Wort, das hängen bleibt.
- **Tempo-Sync** und **Raster** (1/8, 1/16, 1/32) legen die Glitches aufs Raster. **Chaos** streut zusätzlich Glitches zwischen die Rasterpunkte und lässt die Stotterlänge schwanken, damit es nicht wie ein sauberer Stutter-Effekt klingt.
- **Glitch-Stärke** ist das Makro für die Automation: Auf 0 geht die Stimme unverändert durch, nach oben werden alle Chancen und der Crusher stärker. Für den letzten Chorus also die Stärke über die vier Zeilen hochziehen und am Ende **Einfrieren** einschalten. Die absolute Stille danach entsteht am einfachsten mit einem Schnitt in der Region.

Alle Übergänge werden über 1,5 ms überblendet, es knackt also nur, wo es soll.

**Seed** (0–999) macht die Glitches wiederholbar: Während Logic spielt, wird an jedem Schlag neu gewürfelt, aus dem Seed und der Nummer des Schlags. Ein Glitch gehört damit zu seiner Stelle im Song. Mit demselben Seed klingt der Chorus gleich, egal ob die Wiedergabe dort startet, am Anfang oder ob der ganze Song gebounct wird (ab dem ersten vollen Schlag nach dem Start; ein Stotterer braucht etwa 200 ms Vorlauf, weil er wiederholt, was davor lief). Passt eine Stelle nicht, gibt ein anderer Seed eine andere, wieder feste Folge. Der Seed lässt sich automatisieren, etwa ein Wert für den Chorus und ein anderer für die Bridge. Ohne laufenden Transport bleibt es zufällig.

**Getestet und ungetestet:** Unter Linux gebaut und getestet (Durchreichen bei Stärke 0, Stotterperiode, klickfreie Aussetzer und Tonhöhensprünge, Raster im Sync, Einfrieren, Crusher, Seed und Songposition, Zustand). `auval` für den Effekt läuft in der GitHub Action. In Logic ist er noch nicht gehört.

## Tonwerk Distortion (Effekt)

Der dritte Baustein aus diesem Repository: die Schaltung eines klassischen Verzerrer-Pedals, Stufe für Stufe nachgerechnet. Er liegt in Logic unter **Audio-FX → Audio Units → b-velop → Tonwerk Distortion** und wird mit den anderen beiden gebaut, installiert und veröffentlicht.

![Tonwerk Distortion](docs/distortion.png)

Oben zeigt ein Oszilloskop den Ausgang (gelb) über dem Eingang (cyan); so siehst du, wie Dist die Welle eckig macht. Die Regler sind die des Pedals, in derselben Reihenfolge:

- **Level**: die Lautstärke danach, −30 bis +12 dB. Bei 0 dB liegt eine voll verzerrte Note bei Tone in der Mitte um −6 dBFS.
- **Tone**: 0 % dunkel, 100 % hell. In der Mitte entsteht die typische Delle um 500 Hz (etwa 8 dB unter Bässen und Höhen), die das Pedal nach „Wand“ klingen lässt.
- **Dist**: 0 bis 100 %, wie das 100-kΩ-Poti im Pedal (Verstärkung der Op-Amp-Stufe 1- bis 22-fach). Auch auf 0 % zerrt das Pedal schon etwas, das macht der Transistor-Booster davor.
- **Eingang** gibt es am Pedal nicht: −24 bis +24 dB vor der Schaltung. Gerechnet wird mit 1,0 im Host = 1 V an der Eingangsbuchse, also etwa einer kräftig angeschlagenen Gitarre. Eine leise aufgenommene Gitarre oder ein Synth verzerrt mit mehr Eingang so, wie man es vom Pedal kennt.

Was nachgebildet ist: Transistor-Booster (35 dB, Hochpass 33 Hz, weich in die Versorgung), Op-Amp-Stufe mit Dist-Poti, 72-Hz-Bassbeschnitt über C8 und 100 pF gegen das Zischeln, Rails eines 9-V-Single-Supply-Op-Amps (asymmetrisch, das gibt geradzahlige Obertöne), dann der Tiefpass 7,2 kHz und die zwei 1N4148 gegen Masse, aus der Shockley-Gleichung als Tabelle. Das ist das harte Clipping um ±0,6 V, das diesen Verzerrer von weicheren Overdrive-Pedalen unterscheidet. Danach die Tone-Blende aus Tiefpass 234 Hz und Hochpass 1063 Hz. Die Bauteilwerte stammen aus einer veröffentlichten Analyse der Originalschaltung. Weggelassen ist die Slew-Rate des Op-Amps: Der 7,2-kHz-Tiefpass verdeckt sie, und digital kostet sie mehr Aliasing, als sie bringt.

**CPU:** Die verzerrenden Stufen laufen vierfach überabgetastet, die Clipper zusätzlich mit Antiderivative-Antialiasing; was zurückfaltet, bleibt selbst bei einer 2,5-kHz-Note mit voller Verzerrung mehr als 60 dB unter dem Ton. Tone und Level laufen auf der normalen Rate. Stereo rechnet das Plugin im Test etwa 50-mal schneller als Echtzeit (rund 2 % eines Kerns), auf einer Mono-Spur nur einen Kanal, also die Hälfte. Die Überabtastung bringt eine Latenz von wenigen Samples, die das Plugin Logic meldet.

**Getestet und ungetestet:** Unter Linux gebaut und getestet (Diodenkennlinie, Tone-Delle, Obertöne mit Dist, Pegel, Aliasing, Stille nach dem Ton, Mono und Stereo, Zustand, Anzeige in ganzen Zahlen) und die Oberfläche dort angesehen. `auval` läuft in der GitHub Action. In Logic ist er noch nicht gehört, und ob er wie das Pedal in deinem Kopf klingt, entscheiden die Ohren.

## Tonwerk Wavetable (Instrument)

Ein Wavetable-Synthesizer für Dubstep-Bässe (Wobble, Growl, Reese, Riddim), Screeches, Laser und breite Leads. Alles ist eigener Code, auch die Wavetables: Sie werden beim Laden aus Formeln berechnet, nichts ist aus einem anderen Synth kopiert. Er liegt in Logic unter **Instrument → AU-Instrumente → b-velop → Tonwerk Wavetable** und wird mit den anderen Plugins gebaut, installiert und veröffentlicht.

![Tonwerk Wavetable](docs/wavetable.png)

- **Zwei Wavetable-Oszillatoren (A und B)** mit je 64 Frames. **Position** fährt durch die Frames, die Anzeige links zeigt die Tabelle als gestapelte Frames und folgt beim Spielen der modulierten Position. Die Tabellen: *Basis* (Sinus → Dreieck → Säge → Rechteck), *Sync*, *PWM*, *Vokal* (Formanten a-e-i-o-u, der „Yoi“-Growl), *FM-Growl*, *Falter*, *Harmonisch*, *Kamm*, *Bitcrush*, *Rauh*.
- **Warp** verbiegt die Phase vor dem Lesen: *Sync*, *Bend*, *PWM* und *FM* (A von B, B von A). Für FM muss der andere Oszillator nicht hörbar sein, er läuft als Modulator auch ausgeschaltet mit.
- **Unison** bis 8 Stimmen pro Oszillator mit **Detune** (Cent zwischen den äußeren Stimmen), **Blend** (wie laut die äußeren Stimmen sind) und **Breite** im Stereobild. Der Pegel bleibt beim Hinzufügen von Stimmen gleich.
- **Sub** (Sinus, Dreieck, Säge, Rechteck, 0 bis −3 Oktaven) und **Rauschen**. Sub *Direkt* führt den Sub am Filter vorbei, damit das Fundament sauber bleibt, während der Filter wobbelt.
- **Filter**: Tiefpass 12 und 24 dB, Hochpass, Bandpass, Kerbfilter und **Kamm** (auf die Cutoff-Frequenz gestimmt, mit Keytracking 100 % auf die Note: metallische Growls). **Drive** sättigt vor dem Filter.
- **Drei Hüllkurven** (1 ist die Lautstärke) und **zwei LFOs** (Sinus, Dreieck, Säge ab/auf, Rechteck, Zufall). Mit **Sync** läuft ein LFO im Raster des Songtempos, von 4/1 bis 1/32 mit punktierten und Triolen. Mit **Neustart** beginnt er bei jeder Note von vorn (der klassische Wobble beim Spielen); ohne läuft er durch und rastet, solange Logic spielt, auf die Songposition ein, sodass der Wobble immer auf dem Beat liegt.
- **Modulation**: acht Zeilen aus Quelle, Ziel und Menge (−100 bis 100 %). Quellen: LFOs, Hüllkurven, Anschlag, Modrad, Aftertouch, Tonhöhe und **vier Makros** (gut zum Automatisieren in Logic). Ziele: Position, Warp, Tonhöhe (100 % = 24 Halbtöne), Pegel und Detune beider Oszillatoren, Sub, Rauschen, Cutoff (100 % = 10 Oktaven), Resonanz, Filter-Drive, Lautstärke. **±** lässt die Quelle um die Mitte schwingen statt nur nach oben, etwa für Vibrato.
- **Stimmen**: Poly (8 Stimmen), Mono oder Legato, **Glide** (in Mono immer, in Legato nur bei überlappend gespielten Noten), Pitchbend-Bereich.
- **Verzerrung** nach den Stimmen (Weich, Hart, Falten, Röhre), zweifach überabgetastet, mit Drive und Mix, dann **Master**.

**Presets** (Logics Programme, oben rechts im Menü und mit den Pfeilen): 33 Klänge von Wobble, Growl und Neuro über Leads und Flächen bis Riser und Downlifter, die Liste steht unter [Werksklänge](#werksklänge). Bei den Bässen öffnet **Makro 1** den Filter oder treibt den Growl weiter, das ist der Regler für Automation im Drop. Die Bässe sind auf Legato gestellt und sind gleich laut, mit Spitzen zwischen etwa −10 und −4 dBFS, Platz für Kompressor oder OTT in Logic.

**CPU:** Die Modulation rechnet alle 32 Samples, Positionen, Pegel und Filterkoeffizienten gleiten dazwischen, damit nichts stuft. Jede Wavetable liegt in einer Kopie pro Oktave vor, die nur die Obertöne unter Nyquist enthält; eine Säge auf C7 hat deshalb kein Aliasing (im Test über 100 dB darunter). Acht Akkordstimmen *Supersaw* (7 + 5 Unison) mit Verzerrung rechnet der Test etwa 13-mal schneller als Echtzeit.

**Getestet und ungetestet:** Unter Linux gebaut und getestet (Tabellen, Aliasing, Unison-Pegel und Stereobreite, Filter, Hüllkurven, Glide, Matrix, Wobble im Takt, LFO auf der Songposition, alle Warp- und Filterarten, alle Presets, CPU, Zustand, Anzeige mit Einheiten). `auval` läuft in der GitHub Action. In Logic ist er noch nicht gehört; ob die Presets nach Dubstep klingen, entscheiden deine Ohren.

## Tonwerk DX (Instrument)

FM mit sechs Operatoren und 32 Algorithmen nach dem Vorbild der klassischen 6-Operator-Synths, für E-Pianos, Glocken, Blech und Bässe aus den Achtzigern. Tonwerk FM bleibt bei vier Operatoren, damit seine Klänge eins zu eins zu Tonwerk im Browser passen; Tonwerk DX ist ein eigenes Instrument (`aumu TwDx Bvlp`) unter **Instrument → AU-Instrumente → b-velop → Tonwerk DX**.

![Tonwerk DX](docs/dx.png)

- **Algorithmus** (1 bis 32) als Diagramm gezeichnet, wie es auf solchen Geräten aufgedruckt ist: Träger gelb, Modulatoren cyan, die Rückkopplungsschleife am Operator mit **Feedback** (0 bis 7). Dazu **Transponieren** in Halbtönen und **Lautstärke**.
- **Sechs Operatoren**, oben jeweils klein mit Hüllkurve, **Ratio** (0,5 und 1 bis 31), **Fein** (plus 0 bis 99 % der Ratio), **Pegel** (0 bis 99, ein Schritt etwa 0,75 dB) und einem Schalter. Über jeder Hüllkurve steht, was der Operator im gewählten Algorithmus tut („TRÄGER“, „MOD → 1“). Ein Klick auf eine Hüllkurve öffnet den Operator unten im Detail.
- **Hüllkurven mit vier Raten und Pegeln**: vier Raten und vier Pegel. Pegel 1 bis 3 werden nacheinander angefahren, auf Pegel 3 bleibt die Note, solange die Taste gedrückt ist, Rate 4 führt nach dem Loslassen zu Pegel 4. Die Kurven laufen in Pegelschritten, also in Dezibel, wie beim Original. Dazu je Operator **Verstimmung** (−7 bis 7), **Anschlag** (0 bis 7, wie viel ein leiser Anschlag wegnimmt), **Tastatur-Rate** (hohe Töne verklingen schneller) und **LFO-Pegel** (wie stark das Tremolo diesen Operator trifft).
- **LFO** mit Dreieck, Sägezahn ab und auf, Rechteck, Sinus und S&H, **Tempo** wie bei den Originalen (35 ist ein Vibrato von etwa 5,6 Hz, 63 etwa 10 Hz, 99 etwa 49 Hz), **Verzögerung** (blendet nach dem Anschlag ein), **Tonhöhe** mit **Empfindlichkeit** (bis eine Oktave) und **Lautstärke**.
- **Effekte**: Delay (frei oder im Songtempo) und Hall wie in den anderen Tonwerk-Synths. 16 Stimmen, Pitchbend ±2 Halbtöne.

**Werksklänge** (eigene, keine Kopien aus Werks-ROMs): eine volle Bank mit 32 Klängen, von E-Piano, Bässen und Glocken über Blech, Holzbläser und Streicher bis Chor und Koto, die Liste steht unter [Werksklänge](#werksklänge). Mit **<** und **>** blätterst du durch das Menü.

**SysEx laden:** SysEx-Bänke klassischer 6-Operator-Synths, die du hast (`.syx` mit 32 Klängen im üblichen 4096-Byte-Format, auch ohne SysEx-Rahmen, oder ein einzelner Klang), lädt **SysEx laden**. Die Datei wird nach `~/Library/Application Support/Tonwerk DX/SysEx` kopiert und steht danach in jeder Instanz als eigene Gruppe im Menü. Was das Plugin nicht hat, fällt weg: die Tonhöhen-Hüllkurve, die Pegelskalierung über die Tastatur und Operatoren mit fester Frequenz (die bekommen die Ratio, die ihrer Frequenz am mittleren C am nächsten kommt). Klänge, die stark davon leben, klingen deshalb anders als am Original.

**CPU:** Hüllkurven und LFO rechnen alle 32 Samples, die Pegel gleiten dazwischen; der Sinus kommt aus einer Tabelle. 16 Stimmen *Blech* mit Delay und Hall rechnet der Test etwa 16-mal schneller als Echtzeit.

**Getestet und ungetestet:** Unter Linux gebaut und getestet (alle 32 Algorithmen, Hüllkurvenzeiten, Pegelschritte, Ratios, Anschlag, LFO und sein Tempo gegen Messungen an den Originalen, SysEx-Bänke und Einzelklänge, jeder Werksklang klingt und übersteuert auch im Akkord nicht, Zustand, CPU) und die Oberfläche als Bild angesehen. `auval` läuft in der GitHub Action. In Logic ist er noch nicht gehört; wie nah die Werksklänge an den Klassikern sind, entscheiden die Ohren.

## Herunterladen

Unter [Releases](https://github.com/Marcel-B/Synth/releases) liegt zu jeder Version ein Zip mit Audio Unit, VST3, App (für Synth und Effekt) und Installationsskript, für Apple Silicon und Intel ab macOS 11. Nach dem Laden im Terminal:

```sh
cd ~/Downloads/Tonwerk-Synth-*-macOS
zsh install.sh
```

Die Plugins sind nur ad hoc signiert, denn eine Signatur mit Apple Developer ID kostet ein Entwicklerkonto. Deshalb versieht macOS alles aus dem Netz mit einer Quarantäne-Markierung, und Logic lädt so markierte Plugins nicht. `install.sh` entfernt die Markierung (`xattr -dr com.apple.quarantine`), kopiert die Dateien an ihren Platz und prüft die Audio Unit mit `auval`. Wer lieber von Hand installiert, ruft vorher selbst `xattr -dr com.apple.quarantine` auf den entpackten Ordner auf.

Zum Aktualisieren lädst du das neue Zip und rufst dessen `install.sh` genauso auf. Logic sollte dabei geschlossen sein. Importierte Klänge bleiben erhalten, und gespeicherte Projekte laden ihre Einstellungen weiter.

Ein neues Release entsteht, wenn ein Tag wie `v0.2.0` gepusht wird, oder von Hand unter Actions → Release → „Run workflow“ mit einer Versionsnummer. Die Action `release.yml` baut dann, testet, prüft mit `auval` und hängt das Zip an. Die Versionsnummer des Plugins kommt aus dem Tag, so erkennt Logic eine neue Version.

## Selbst bauen auf dem Mac

Ein volles Xcode ist nicht nötig, die Command Line Tools reichen:

```sh
xcode-select --install          # einmal, falls noch nicht da
brew install cmake              # einmal
git clone https://github.com/Marcel-B/Synth.git && cd Synth
scripts/install-macos.sh
```

Das Skript baut eine Universal-Binary (Apple Silicon und Intel), signiert sie ad hoc, kopiert die Audio Unit nach `~/Library/Audio/Plug-Ins/Components`, das VST3 nach `~/Library/Audio/Plug-Ins/VST3` und die App nach `~/Applications`, und prüft die Audio Unit mit Apples `auval`. Der erste Lauf lädt JUCE herunter und dauert ein paar Minuten.

In Logic: neue Software-Instrument-Spur, im Kanalzug **Instrument → AU-Instrumente → b-velop → Tonwerk Analog** (oder Tonwerk FM). Taucht es nicht auf, im Plug-in-Manager (Logic Pro → Einstellungen → Plug-in-Manager) „Tonwerk“ suchen und **Erneut scannen** wählen.

Entfernen: `scripts/install-macos.sh --uninstall`.

Zwischenstände ohne Release: Jeder Lauf der Action `build.yml` legt die Dateien auch als Artefakt `tonwerk-synth-macos` ab (Actions → Lauf → Artifacts). Für sie gilt dasselbe wie für Releases, die Quarantäne-Markierung muss weg.

## Werksklänge

Jedes Instrument bringt eigene Klänge mit, keine Kopien aus Werks-ROMs oder fremden Preset-Bänken. Im Menü stehen sie nach Gruppen sortiert (Bässe, Leads, Flächen, Tasten, Plucks, Glocken, Bläser & Streicher, Effekte); die Programmnummern in Logic bleiben dabei, wie sie waren, neue Klänge hängen hinten an. Neu seit 0.4.0 ist alles *kursiv* Gesetzte.

- **Tonwerk Analog**
  - Bässe: Analog Bass (Tonwerk), Acid, Wobble, *Sub*, *Druckbass*, *Pulsbass*, *Reese*, *Gummibass*, *Synthwave-Bass*
  - Leads: Analog Lead und Leitton (Tonwerk), Falt-Lead, *Sägen-Lead*, *Chiptune*, *Pfeife*, *Schrei-Lead*
  - Flächen: Analog Pad (Tonwerk), *Warme Fläche*, *PWM-Fläche*, *Nebel*, *Glasfläche*, *Sweep-Fläche*
  - Tasten: *Combo-Orgel*, *Clavi*, *Polysynth*; Plucks: *Pluck*, *Berlin-Sequenz*; Glocken: *Kristall*
  - Bläser & Streicher: Streicher, *Blechsatz*, *Flöte*; Effekte: Zufall, *Wind*, *Computer*
- **Tonwerk FM**
  - Bässe: FM Bass (Tonwerk), *Slapbass*, *Holzbass*, *Wobble-Bass*, *Synthbass*
  - Leads: FM Leitton (Tonwerk), *Sägezahn-Lead*, *Sync-Lead*
  - Flächen: *Glasfläche*, *Atemfläche*, *Dunkle Fläche*
  - Tasten: FM E-Piano (Tonwerk), Orgel, *Zungen-Piano*, *Cembalo*, *Clavi*; Plucks: Zupf
  - Glocken: Glocke, Marimba, *Spieluhr*, *Vibrafon*, *Kalimba*, *Steeldrum*, *Gong*
  - Bläser & Streicher: FM Blech (Tonwerk), *Panflöte*, *Klarinette*, *Streicher*; Effekte: *Metall*, *Roboter*
- **Tonwerk DX**, jetzt eine volle Bank mit 32 Klängen
  - Bässe: Bass, *Slapbass*, *Holzbass*, *Synthbass*
  - Tasten: E-Piano, Clavi, Orgel, *E-Piano Hell*, *Cembalo*
  - Glocken: Röhrenglocke, Marimba, *Vibrafon*, *Glockenspiel*, *Kalimba*, *Steeldrum*, *Gong*
  - Bläser & Streicher: Blech, Mundharmonika, Flöte, *Akkordeon*, *Streicher*, *Klarinette*, *Oboe*, *Trompete*
  - Flächen: Fläche, *Glasfläche*, *Chor*; Leads: Lead, *Rechteck-Lead*; Plucks: *Harfe*, *Koto*; Effekte: *Datenstrom*
- **Tonwerk Wavetable**
  - Bässe: Wobble 1/8, Wobble Triolen, Reese, Growl Yoi, Growl FM, Riddim, Talking Bass, Sub Bass, *Neuro*, *808*, *Pluck-Bass*
  - Leads: Screech, Supersaw, *Hoover*, *Future-Lead*, *Sync-Lead*, *Vokal-Lead*, *PWM-Lead*
  - Flächen: *Harmonische Fläche*, *Kamm-Fläche*, *Pump-Akkorde* (duckt auf jeder Viertel im Songtempo), *Dunkle Fläche*
  - Plucks: *Pluck*, *Glas-Pluck*, *Bit-Arp*; Tasten: *Bit-Keys*, *Digi-Piano*; Glocken: *Digi-Glocke*
  - Effekte: Laser, *Riser* (vier Sekunden halten), *Downlifter*, *Sirene*

**Gleich laut:** Ein Test spielt jeden Klang mit einer Phrase seiner Gruppe (Basslinie, Akkorde, Arpeggio, Melodie) bei Anschlag 100 und misst die Lautheit nach EBU R128 (momentan, am lautesten Punkt). Tonwerk Analog, FM und Wavetable liegen bei −12 LUFS, Tonwerk DX bei −16 LUFS, jeweils höchstens 2 dB daneben; der DX ist leiser, weil eine Stimme bei voller Lautstärke nur halben Vollpegel erreicht, damit ein hart angeschlagener Akkord nicht übersteuert. Effekte dürfen bis 6 dB leiser sein, ein kurzer Laser wird sonst zu laut. Kein Klang kommt dabei über den Vollpegel, Delay und Hall eingerechnet. Ausgenommen sind Tonwerks eigene Startklänge, die so laut bleiben wie im Browser.

**Geändert an alten Klängen:** Ihre Lautstärke ist angeglichen und der Hall ist bei vielen deutlich zurückgenommen (er ist laut, schon 10 % Mix bei langem Raum sind etwa so laut wie das trockene Signal). Im DX haben Bass, Marimba, Clavi und Lead etwas mehr Pegel im gehaltenen Teil bekommen, damit sie mit den anderen mithalten, und das Vibrato von Blech, Mundharmonika, Flöte und Lead läuft jetzt mit 5 Hz statt 0,6 Hz. Gespeicherte Projekte behalten ihre Einstellungen; das betrifft nur das neue Laden eines Werksklangs.

Der Hall rauscht jetzt bei gleicher Raumgröße immer gleich, ein Klang klingt also bei jedem Laden und jedem Bounce identisch.

## Klänge aus Tonwerk

Das Menü oben listet die **Werksklänge** (Tonwerks Startklänge pro Spurart und die des Plugins) und die übernommenen Klänge der Engine des Plugins.

- **Aus Tonwerk laden** fragt nach Tonwerks Adresse, so wie sie im Browser steht (z. B. `https://<mac>.<tailnet>.ts.net:8443`), und übernimmt alle dort gespeicherten Klänge, die analogen für Tonwerk Analog, die FM-Klänge für Tonwerk FM. Die Adresse wird gemerkt. Gleichnamige Klänge werden ersetzt, ein zweiter Abruf bringt die Liste also auf den Stand von Tonwerk.
- **Datei laden** liest eine JSON-Datei: einen Klang, wie ihn **Speichern** schreibt (`{ "name", "patch" }`), Tonwerks ganze Liste (die Antwort von `GET /api/logic/synths/presets`) oder einen nackten Patch.
- **Speichern** schreibt den aktuellen Klang als JSON im Format von Tonwerk.
- **Löschen** entfernt einen übernommenen Klang aus der Liste der Plugins, in Tonwerk bleibt er.

Die übernommenen Klänge liegen in `~/Library/Application Support/Tonwerk Synth/presets.json` und stehen in jedem Projekt zur Verfügung. Was ein Logic-Projekt spielt, speichert Logic mit dem Projekt; ein späterer Abruf aus Tonwerk ändert es nicht.

## Wie nah am Browser

Die Klangerzeugung rechnet nach, was Tonwerks Web-Audio-Graph tut: dieselben Wellenformen und Pegel (der Puls mit PWM ist dort halb so laut wie das reine Rechteck, hier auch), Web Audios Biquad-Filter mit seiner Eigenheit, dass die Resonanz bei Tief- und Hochpass in Dezibel angegeben ist, dieselbe Hüllkurvenform (linearer Anstieg, exponentieller Abfall mit Zeitkonstante Decay/4), FM als Frequenzmodulation in Hz mit Index bis 8, Feedback nach DX-Art, der Hall aus abklingendem Rauschen bei 0,15. Unterschiede:

- Regler wirken sofort auf klingende Noten, im Browser erst auf die nächste.
- Nach dem Release wird die Stimme in 3 ms ausgeblendet statt hart abgeschnitten.
- Die Oszillatoren sind mit PolyBLEP bandbegrenzt, Web Audio rechnet die Wellen anders; im Klang sollte das nicht auffallen, in den höchsten Lagen vielleicht ein wenig.
- Wavefolder und Sample & Hold hat nur das Plugin. Der Wavefolder rechnet mit einfacher zweifacher Überabtastung, bei hohen Tönen und viel Menge kann er trotzdem etwas Aliasing erzeugen.

**Getestet und ungetestet:** Das Plugin wurde unter Linux gebaut und getestet (Klangerzeugung, Presets, Parameter, Zustand) und die Oberfläche dort angesehen. `auval` läuft in der GitHub Action auf einem Mac. Version 0.2.0 läuft in Logic auf deinem MacBook. Wavefolder und Sample & Hold (ab 0.3.0) sind dort noch nicht gehört, ebenso wenig die Aufteilung in Tonwerk Analog und Tonwerk FM, die Werksklänge von 0.4.0 (im Test: jeder klingt, alle gleich laut, keiner übersteuert; als Spektrogramm angesehen, nicht angehört) und die neue Oberfläche (unter Linux als Bild angesehen). Ob es genau wie der Browser klingt, ist nur mit den Ohren zu prüfen.

## Entwickeln

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --target TonwerkSynthTests && ctest --test-dir build --output-on-failure
cmake --build build --target TonwerkAnalog_Standalone  # die App zum Ausprobieren, ebenso TonwerkFM_Standalone
cmake --build build --target ChromeGlitch_Standalone   # der Effekt als App, mit dem Mikrofon als Eingang
cmake --build build --target TonwerkDistortion_Standalone
cmake --build build --target TonwerkWavetable_Standalone
cmake --build build --target TonwerkDX_Standalone
```

Mit `TONWERK_EDITOR_PNG_DIR=<ordner>` schreiben die Tests ein Bild jeder Oberfläche dorthin, mit `TONWERK_PRESET_WAV_DIR=<ordner>` jeden Werksklang als WAV mit seiner Testphrase. `TONWERK_TEST_CATEGORY=Presets` lässt nur eine Testgruppe laufen, etwa beim Abstimmen von Klängen. Unter Linux braucht JUCE ein paar Pakete, siehe `.github/workflows/build.yml`. Dort entstehen VST3 und App, die Audio Unit nur auf dem Mac.

## Lizenz

Das Plugin nutzt [JUCE](https://juce.com) 8, das unter AGPLv3 oder einer kommerziellen Lizenz steht. Für den eigenen Gebrauch genügt das; wer das Plugin weitergeben will, muss sich vorher für eine der beiden Lizenzen entscheiden.
