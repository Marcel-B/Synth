# Tonwerk Synth

Die beiden Browser-Synthesizer aus Tonwerk (YuE UI) als Plugin für Logic Pro: ein Instrument mit einer **Analog**- und einer **FM**-Engine, dazu Delay und Hall. Dazu kommen die Effekte **Chrome Glitch** für zerstückelte Gesangsspuren und **Tonwerk Distortion**, der Klang des BOSS DS-1 (siehe unten). Klänge, die in Tonwerk auf der Instrumente-Seite gespeichert sind, lassen sich übernehmen und klingen hier wie dort.

Logic lädt nur Audio Units, deshalb wird das Plugin als **Audio Unit** gebaut, dazu als VST3 (für andere Hosts) und als eigenständige App zum Ausprobieren ohne Logic.

![Analog-Engine](docs/analog.png)
![FM-Engine](docs/fm.png)

![Oszilloskop](docs/oszilloskop.png)

## Was drin ist

**Analog**: zwei Oszillatoren (Sinus, Dreieck, Sägezahn, Rechteck/Puls) mit Oktave, Verstimmung und Pegel, Rauschen, Filter (Tiefpass, Hochpass, Bandpass) mit Resonanz, Keytracking und eigener Hüllkurve, Lautstärke-Hüllkurve, LFO auf Tonhöhe, Filter oder Lautstärke, frei oder im Songtempo. Der Puls hat eine Pulsbreite, die der LFO und beide Hüllkurven bewegen können (PWM).

**Wavefolder und Sample & Hold** gibt es nur im Plugin, nicht in Tonwerk. Der Wavefolder sitzt zwischen Oszillatoren und Filter. **Menge** treibt die Mischung bis zum Achtfachen und faltet alles über dem Vollpegel zurück, statt es abzuschneiden. **Symmetrie** verschiebt das Signal vor dem Falten, das bringt geradzahlige Obertöne. Mit **Hüllkurve** öffnet und schließt die Filter-Hüllkurve den Wavefolder. Sample & Hold nimmt in jedem Schritt einen neuen Zufallswert und hält ihn bis zum nächsten. **Filter** verschiebt damit den Cutoff um bis zu zwei Oktaven nach oben oder unten, **Tonhöhe** den Ton um bis zu eine Oktave. Das Tempo ist frei oder im Songtempo, und jede Note würfelt ihre eigene Folge. Beide stehen auf 0, solange du sie nicht aufdrehst, und Klänge aus Tonwerk laden mit beiden aus.

**FM**: vier Sinus-Operatoren wie beim Yamaha TX81Z, dessen acht Algorithmen, Feedback auf Operator 4, eine Hüllkurve pro Operator, Anschlagsdynamik pro Operator, LFO auf Tonhöhe, Modulationsindex oder Lautstärke, frei oder im Songtempo.

**Effekte**: Delay mit Tiefpass in der Rückkopplung und Hall, beide als Send. Das Delay läuft frei oder im Songtempo.

**Tempo-Sync**: Mit **Sync** folgen LFO, Sample & Hold und Delay dem Tempo von Logic. Statt Hz oder Sekunden wählst du einen Notenwert von 4/1 bis 1/32, auch punktiert und triolisch. Ohne Host-Tempo (in der App) gelten 120 BPM. Der LFO beginnt wie in Tonwerk mit jeder Note neu. Er läuft also im Tempo, aber nicht auf den Schlag genau.

**Oszilloskop**: unten rechts neben der Tastatur. Es zeigt die Wellenform dessen, was das Plugin gerade spielt, und steht bei einem gehaltenen Ton still.

16 Stimmen, Pitchbend ±2 Halbtöne, Sustain-Pedal. Jeder Regler ist ein Parameter, den Logic automatisieren kann und mit dem Projekt speichert. Eine Tastatur unten im Fenster spielt das Plugin auch ohne MIDI-Keyboard an.

## Chrome Glitch (Effekt)

Ein Audio-Effekt aus demselben Repository, für eine Gesangsspur, deren „Sprach-Chrome“ versagt: Silben stottern („bo-bo-body“), die Stimme setzt kurz aus, wird digital zerbröselt und springt in der Tonhöhe. Er liegt in Logic unter **Audio-FX → Audio Units → b-velop → Chrome Glitch** und wird mit Tonwerk Synth zusammen gebaut, installiert und veröffentlicht.

Der Effekt arbeitet ohne Latenz auf dem, was gerade hereinkommt. An jedem Schritt des Rasters (im Songtempo) oder zu zufälligen Momenten (40–250 ms auseinander) würfelt er, ob etwas passiert:

- **Stottern**: Die letzten 10–200 ms (Standard 50 ms) werden mehrmals wiederholt. **Beschleunigen** macht jede Wiederholung kürzer, so wird die Silbe immer schneller. Ab der zweiten Wiederholung kann die Tonhöhe springen (**Pitch-Chance**, **Pitch-Bereich** bis ±24 Halbtöne).
- **Aussetzer**: Die Stimme fällt 20–300 ms ganz weg.
- **Bitcrusher**: Bei jedem Glitch voll (bis 4 Bit und 16-faches Downsampling), dazwischen nur leicht, mit der Stärke wachsend.
- **Einfrieren**: Hält die letzte Silbe in einer Schleife fest, solange es an ist. Für das letzte Wort, das hängen bleibt.
- **Tempo-Sync** und **Raster** (1/8, 1/16, 1/32) legen die Glitches aufs Raster. **Chaos** streut zusätzlich Glitches zwischen die Rasterpunkte und lässt die Stotterlänge schwanken, damit es nicht wie ein sauberer Stutter-Effekt klingt.
- **Glitch-Stärke** ist das Makro für die Automation: Auf 0 geht die Stimme unverändert durch, nach oben werden alle Chancen und der Crusher stärker. Für den letzten Chorus also die Stärke über die vier Zeilen hochziehen und am Ende **Einfrieren** einschalten. Die absolute Stille danach entsteht am einfachsten mit einem Schnitt in der Region.

Alle Übergänge werden über 1,5 ms überblendet, es knackt also nur, wo es soll. Die Würfel beginnen bei jedem Start des Transports mit demselben Startwert: Ein Bounce von derselben Stelle klingt wie die Wiedergabe davor.

**Getestet und ungetestet:** Unter Linux gebaut und getestet (Durchreichen bei Stärke 0, Stotterperiode, klickfreie Aussetzer und Tonhöhensprünge, Raster im Sync, Einfrieren, Crusher, Zustand). `auval` für den Effekt läuft in der GitHub Action. In Logic ist er noch nicht gehört.

## Tonwerk Distortion (Effekt)

Der dritte Baustein aus diesem Repository: die Schaltung des orangen BOSS DS-1, Stufe für Stufe nachgerechnet. Er liegt in Logic unter **Audio-FX → Audio Units → b-velop → Tonwerk Distortion** und wird mit den anderen beiden gebaut, installiert und veröffentlicht.

Die Regler sind die des Pedals, in derselben Reihenfolge:

- **Level**: die Lautstärke danach, −30 bis +12 dB. Bei 0 dB liegt eine voll verzerrte Note bei Tone in der Mitte um −6 dBFS.
- **Tone**: 0 % dunkel, 100 % hell. In der Mitte entsteht die typische Delle um 500 Hz (etwa 8 dB unter Bässen und Höhen), die den DS-1 nach „Wand“ klingen lässt.
- **Dist**: 0 bis 100 %, wie das 100-kΩ-Poti im Pedal (Verstärkung der Op-Amp-Stufe 1- bis 22-fach). Auch auf 0 % zerrt der DS-1 schon etwas, das macht der Transistor-Booster davor.
- **Eingang** gibt es am Pedal nicht: −24 bis +24 dB vor der Schaltung. Gerechnet wird mit 1,0 im Host = 1 V an der Eingangsbuchse, also etwa einer kräftig angeschlagenen Gitarre. Eine leise aufgenommene Gitarre oder ein Synth verzerrt mit mehr Eingang so, wie man es vom Pedal kennt.

Was nachgebildet ist: Transistor-Booster (35 dB, Hochpass 33 Hz, weich in die Versorgung), Op-Amp-Stufe mit Dist-Poti, 72-Hz-Bassbeschnitt über C8 und 100 pF gegen das Zischeln, Rails eines 9-V-Single-Supply-Op-Amps (asymmetrisch, das gibt geradzahlige Obertöne), dann der Tiefpass 7,2 kHz und die zwei 1N4148 gegen Masse, aus der Shockley-Gleichung als Tabelle. Das ist das harte Clipping um ±0,6 V, das den DS-1 vom weicheren Tube Screamer unterscheidet. Danach die Tone-Blende aus Tiefpass 234 Hz und Hochpass 1063 Hz. Die Bauteilwerte stammen aus ElectroSmashs Analyse des DS-1. Weggelassen ist die Slew-Rate des Op-Amps: Der 7,2-kHz-Tiefpass verdeckt sie, und digital kostet sie mehr Aliasing, als sie bringt.

**CPU:** Die verzerrenden Stufen laufen vierfach überabgetastet, die Clipper zusätzlich mit Antiderivative-Antialiasing; was zurückfaltet, bleibt selbst bei einer 2,5-kHz-Note mit voller Verzerrung mehr als 60 dB unter dem Ton. Tone und Level laufen auf der normalen Rate. Stereo rechnet das Plugin im Test etwa 50-mal schneller als Echtzeit (rund 2 % eines Kerns), auf einer Mono-Spur nur einen Kanal, also die Hälfte. Die Überabtastung bringt eine Latenz von wenigen Samples, die das Plugin Logic meldet.

**Getestet und ungetestet:** Unter Linux gebaut und getestet (Diodenkennlinie, Tone-Delle, Obertöne mit Dist, Pegel, Aliasing, Stille nach dem Ton, Mono und Stereo, Zustand, Anzeige in ganzen Zahlen) und die Oberfläche dort angesehen. `auval` läuft in der GitHub Action. In Logic ist er noch nicht gehört, und ob er wie dein DS-1 klingt, entscheiden die Ohren.

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

In Logic: neue Software-Instrument-Spur, im Kanalzug **Instrument → AU-Instrumente → b-velop → Tonwerk Synth**. Taucht es nicht auf, im Plug-in-Manager (Logic Pro → Einstellungen → Plug-in-Manager) „Tonwerk Synth" suchen und **Erneut scannen** wählen.

Entfernen: `scripts/install-macos.sh --uninstall`.

Zwischenstände ohne Release: Jeder Lauf der Action `build.yml` legt die Dateien auch als Artefakt `tonwerk-synth-macos` ab (Actions → Lauf → Artifacts). Für sie gilt dasselbe wie für Releases, die Quarantäne-Markierung muss weg.

## Klänge aus Tonwerk

Das Menü oben listet die **Werksklänge** (Tonwerks Startklänge pro Spurart, für beide Engines) und die übernommenen Klänge.

- **Aus Tonwerk laden** fragt nach Tonwerks Adresse, so wie sie im Browser steht (z. B. `https://<mac>.<tailnet>.ts.net:8443`), und übernimmt alle dort gespeicherten Klänge. Die Adresse wird gemerkt. Gleichnamige Klänge werden ersetzt, ein zweiter Abruf bringt die Liste also auf den Stand von Tonwerk.
- **Datei laden** liest eine JSON-Datei: einen Klang, wie ihn **Speichern** schreibt (`{ "name", "patch" }`), Tonwerks ganze Liste (die Antwort von `GET /api/logic/synths/presets`) oder einen nackten Patch.
- **Speichern** schreibt den aktuellen Klang als JSON im Format von Tonwerk.
- **Löschen** entfernt einen übernommenen Klang aus der Liste des Plugins, in Tonwerk bleibt er.

Die übernommenen Klänge liegen in `~/Library/Application Support/Tonwerk Synth/presets.json` und stehen in jedem Projekt zur Verfügung. Was ein Logic-Projekt spielt, speichert Logic mit dem Projekt; ein späterer Abruf aus Tonwerk ändert es nicht.

## Wie nah am Browser

Die Klangerzeugung rechnet nach, was Tonwerks Web-Audio-Graph tut: dieselben Wellenformen und Pegel (der Puls mit PWM ist dort halb so laut wie das reine Rechteck, hier auch), Web Audios Biquad-Filter mit seiner Eigenheit, dass die Resonanz bei Tief- und Hochpass in Dezibel angegeben ist, dieselbe Hüllkurvenform (linearer Anstieg, exponentieller Abfall mit Zeitkonstante Decay/4), FM als Frequenzmodulation in Hz mit Index bis 8, Feedback nach DX-Art, der Hall aus abklingendem Rauschen bei 0,15. Unterschiede:

- Regler wirken sofort auf klingende Noten, im Browser erst auf die nächste.
- Nach dem Release wird die Stimme in 3 ms ausgeblendet statt hart abgeschnitten.
- Die Oszillatoren sind mit PolyBLEP bandbegrenzt, Web Audio rechnet die Wellen anders; im Klang sollte das nicht auffallen, in den höchsten Lagen vielleicht ein wenig.
- Wavefolder und Sample & Hold hat nur das Plugin. Der Wavefolder rechnet mit einfacher zweifacher Überabtastung, bei hohen Tönen und viel Menge kann er trotzdem etwas Aliasing erzeugen.

**Getestet und ungetestet:** Das Plugin wurde unter Linux gebaut und getestet (Klangerzeugung, Presets, Parameter, Zustand) und die Oberfläche dort angesehen. `auval` läuft in der GitHub Action auf einem Mac. Version 0.2.0 läuft in Logic auf deinem MacBook. Wavefolder und Sample & Hold (ab 0.3.0) sind dort noch nicht gehört. Ob es genau wie der Browser klingt, ist nur mit den Ohren zu prüfen.

## Entwickeln

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --target TonwerkSynthTests && ctest --test-dir build --output-on-failure
cmake --build build --target TonwerkSynth_Standalone   # die App zum Ausprobieren
cmake --build build --target ChromeGlitch_Standalone   # der Effekt als App, mit dem Mikrofon als Eingang
cmake --build build --target TonwerkDistortion_Standalone
```

Unter Linux braucht JUCE ein paar Pakete, siehe `.github/workflows/build.yml`. Dort entstehen VST3 und App, die Audio Unit nur auf dem Mac.

## Lizenz

Das Plugin nutzt [JUCE](https://juce.com) 8, das unter AGPLv3 oder einer kommerziellen Lizenz steht. Für den eigenen Gebrauch genügt das; wer das Plugin weitergeben will, muss sich vorher für eine der beiden Lizenzen entscheiden.
