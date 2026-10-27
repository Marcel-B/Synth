# Bauanleitung: Tonwerk Wavetable als Eurorack-Modul (bedrahtete Platinen)

Schritt für Schritt vom Bestellen bis zum ersten Ton, für die bedrahtete Variante (`tonwerk-bedien-tht`, `tonwerk-haupt-tht`, v0.1). Was das Modul kann und wie es bedient wird, steht in der [README](README.md).

> **Stand 18.10.2026: Noch niemand hat das Modul fertig gebaut.** Die Platinen v0.1 sind gefertigt, aber noch nicht bestückt, und die Firmware lief bisher nur als Steckbrett-Test. Diese Anleitung beschreibt den geplanten Aufbau; wer jetzt baut, ist der erste und sollte Zeit für Fehlersuche mitbringen. Was geprüft ist: [README, Was getestet ist](README.md#was-getestet-ist).

## Was man braucht

- Lötkolben (feine Spitze), Lötzinn, Seitenschneider, Multimeter.
- Ein Eurorack-Gehäuse mit ±12 V, besser zum ersten Einschalten ein Labornetzteil mit Strombegrenzung.
- Einen Rechner mit Chrome oder Edge (zum Flashen im Browser) und ein USB-Kabel, das Daten kann.
- Einen 3D-Drucker für die Frontplatte (oder 2-mm-Alu und einen Bohrer).
- Zum Kalibrieren eine Spannungsquelle für genau 1,000 V und 3,000 V (ein Labornetzteil und das Multimeter reichen).

## 1. Bestellen

**Platinen:** Die Gerber-Zips aus [hardware/pcb/tonwerk-haupt-tht/fertigung/](hardware/pcb/tonwerk-haupt-tht/fertigung/) und [hardware/pcb/tonwerk-bedien-tht/fertigung/](hardware/pcb/tonwerk-bedien-tht/fertigung/) bei JLCPCB hochladen, je 5 Stück, 2 Lagen, 1,6 mm, HASL, **ohne Bestückung**. Die übrigen Einstellungen bleiben, wie JLCPCB sie vorschlägt.

**Teile:** [hardware/pcb/einkaufsliste-tht.csv](hardware/pcb/einkaufsliste-tht.csv) (Semikolon-getrennt, mit Preisen geschätzt um 60–80 € plus Platinen). Daneben liegen Listen zum Hochladen in Mousers BOM-Werkzeug (`mouser-bom-tht*.csv`/`.xls`, `-zusatz` sind Ersatzteile) und eine Suchliste für Reichelt (`reichelt-liste-tht.csv`). DevKit, DAC-Modul, Potis, Buchsen, LEDs und Knöpfe gibt es am günstigsten bei AliExpress.

Worauf es ankommt:

- **ESP32-S3-DevKitC-1 N16R8**: N16R8 heißt 16 MB Flash und 8 MB PSRAM. Ohne die 8 MB PSRAM läuft die Firmware nicht (die Tabellen brauchen 5,7 MB). Klone wie das YD-ESP32-S3 gehen, wenn die Pinbelegung gleich ist.
- **PCM5102A-Modul „GY-PCM5102“** (lila) mit der 6-poligen Leiste VIN GND LCK DIN BCK SCK.
- **74HCT125 oder 74AHCT125**, nicht 74HC125 (der erkennt die 3,3 V des ESP32 an 5 V nicht sicher). Ein 74HCT32 passt ohne Änderung in dieselben Löcher.
- **WS2812D-F5** (5 mm, bedrahtet). Die Platine erwartet die Pins 1 DIN, 2 VDD, 3 GND, 4 DOUT; im Datenblatt des Händlers nachsehen. Die abgeflachte Seite der LED liegt bei **DOUT**, nicht bei DIN (siehe Schritt 5). APA106/PL9823 haben dieselbe Pinfolge, aber eine andere Farbreihenfolge (siehe [Fehlersuche](#fehlersuche)).
- **Elkos höchstens 11 mm hoch**, Rastermaß 2 mm.

## 2. Frontplatte drucken

Eine der drei Varianten aus [hardware/](hardware/) drucken, die README erklärt sie unter [Frontplatte / Gehäuse](README.md#frontplatte--gehäuse). Am einfachsten ist [frontplatte.stl](hardware/frontplatte.stl) (Schrift erhaben, Front nach oben, bei 2,5 mm Filament wechseln).

Dann einmal ein Poti, eine Klinkenbuchse, eine LED und einen Taster durchstecken. Die Front dient beim Löten der Bedienplatine als Lehre (Schritt 5).

## 3. Hauptplatine löten

Alles kommt auf die **Rückseite** (die Seite mit dem Bestückungsdruck, sie zeigt im Modul nach hinten), nur die beiden 14-poligen Buchsenleisten J3 und J4 auf die Vorderseite. Welches Teil wohin gehört, zeigt [tonwerk-haupt-tht-bestueckung-hinten.png](hardware/pcb/tonwerk-haupt-tht/tonwerk-haupt-tht-bestueckung-hinten.png) (von hinten gesehen); die Liste nach Wert ist [tonwerk-haupt-tht-bestueckliste.csv](hardware/pcb/tonwerk-haupt-tht/fertigung/tonwerk-haupt-tht-bestueckliste.csv).

Unter dem DevKit und dem DAC-Modul ist nur Platz für flache Teile. Deshalb in dieser Reihenfolge, von flach nach hoch:

1. **Widerstände** (vor dem Einlöten mit dem Multimeter messen, die Farbringe von 1-%-Widerständen sind leicht zu verwechseln):

   | Wert | Bauteile |
   |---|---|
   | 10 Ω | R3 |
   | 330 Ω | R48 |
   | 1k | R11 R14 R22 R25 R28 R31 R34 |
   | 6,8k | R10 R13 |
   | 10k | R9 R12 R40 R43 R46 |
   | 16k | R42 |
   | 17,4k | R41 |
   | 30k | R24 R27 R30 R33 |
   | 37,4k | R21 |
   | 100k | R7 R8 R20 R23 R26 R29 R32 R44 R45 R47 |

2. **Dioden** 1N5817: D1, D2, D3. Der Ring zeigt zur Kathode, wie auf dem Bestückungsdruck.
3. **Keramikkondensatoren**: 100 nF (C10–C19, C27, C28, C35–C40), 10 nF (C30–C34), 1 µF (C4, C6). Sie haben keine Polung.
4. **IC-Fassungen**: DIP-8 (U6), DIP-14 (U7, U8, U10), DIP-16 (U9). Die Kerbe zeigt dorthin, wo sie der Bestückungsdruck zeigt. Die ICs kommen erst in Schritt 7 hinein.
5. **TO-92**: Q1 BC547 (C B E, flache Seite wie auf dem Druck) und U3 MCP1700-3002E (3,0 V; Pin 1 GND, 2 IN, 3 OUT, im Datenblatt nachsehen, bevor er eingelötet wird).
6. **J13**: drei Stifte (L, Masse, R) für die Drähte vom DAC-Modul.
7. **Buchsenleisten** für die Module: zwei 1 × 22 für das DevKit, eine 1 × 6 für das DAC-Modul, alle 8,5 mm hoch. Vorher am eigenen DevKit den Abstand der beiden Stiftreihen messen: Espressifs Board hat 25,4 mm, manche Klone 22,86 mm. Für die engere Reihe hat J3 eine zweite Lochreihe weiter innen (beschriftet „22,86“). Am besten das DevKit in die Leisten stecken, so einsetzen und dann löten, dann stehen sie gerade.
8. **Elkos** 10 µF: C1, C2, C3, C5. **Polung beachten**, sie steht in der Bestückungsliste: C1 + an +12 V, C2 + an GND (er sitzt an −12 V), C3 + an +5 V, C5 + an +3V3A.
9. **Schaltregler** U1 (R-78E5.0-0.5), Beschriftung wie auf dem Druck.
10. **Wannenstecker** J1 (Eurorack-Strom): Pin 1 ist −12 V, dort kommt der rote Streifen des Kabels hin.
11. Zuletzt auf der **Vorderseite**: die Buchsenleisten J3 und J4 (1 × 14, Pin 1 oben). Damit sie zur Bedienplatine passen, die Stiftleisten der Bedienplatine einstecken, beide Platinen mit den Abstandsbolzen (M3 × 11 mm) verschrauben und erst dann löten. Das geht auch nach Schritt 5.

## 4. DAC-Modul vorbereiten

Auf der Rückseite des GY-PCM5102 sind vier Lötbrücken. Jede hat drei Pads; mit einem Zinnklecks das mittlere Pad mit **L** (Low, Masse) oder **H** (High) verbinden:

| Brücke | Funktion | Stellung |
|---|---|---|
| 1 FLT | Filter | **L** |
| 2 DEMP | De-Emphasis | **L** (aus) |
| 3 XSMT | Stummschaltung | **H** (Ton an) |
| 4 FMT | Datenformat | **L** (I2S) |

Manche Module kommen schon so oder ganz ohne Brücken. Den Pin SCK braucht man nicht extra zu verbinden: Die Platine legt ihn an Masse.

Die Stiftleiste so einlöten, dass die Beschriftung des Moduls zu der auf der Hauptplatine passt (VIN GND LCK DIN BCK SCK). Dann drei kurze Drähte vom Ausgang des Moduls (die Lötfahnen der Klinkenbuchse oder die Pads L, G, R) zu J13: L, Masse, R.

## 5. Bedienplatine löten

Auf die Rückseite kommen nur C1 und C2 (100 nF) und die beiden 14-poligen Stiftleisten J3 und J4 (Pin 1 oben). Alles andere sitzt vorn: die zehn Potis (RV1 TUNE … RV10 CUT CV), die acht Buchsen (J5 V/OCT … J12 VOICE), die zwei Taster und die zwei LEDs; siehe [tonwerk-bedien-tht-bestueckung-vorn.png](hardware/pcb/tonwerk-bedien-tht/tonwerk-bedien-tht-bestueckung-vorn.png).

1. C1, C2 und die Stiftleisten zuerst löten.
2. Potis, Buchsen und Taster nur einstecken, die gedruckte Front aufsetzen und die Muttern der Potis und Buchsen leicht anziehen. Erst dann löten, so sitzt alles genau in den Löchern.
3. LEDs: **nicht nach dem Bestückungsdruck richten.** Der Druck der Platine v0.1 zeigt die abgeflachte Seite am eckigen Pad (Pin 1), bei der WS2812D-F5 liegt sie aber bei DOUT (Pin 4). Von vorn gesehen gehört die flache Seite also nach rechts, weg vom eckigen Pad; die beiden kürzeren Beine (DIN, VDD) kommen ans eckige Pad, die beiden längeren (GND, DOUT) nach rechts. Falsch herum liegen +5 V und GND vertauscht an der LED, und sie brennt beim ersten Einschalten durch (so beim ersten Aufbau geschehen). So weit hineinstecken, dass sie mit der Front eingesetzt in den 5-mm-Löchern sitzen, dann löten.

## 6. Erste Prüfung ohne Chips

Noch nichts in die Fassungen stecken, DevKit und DAC-Modul bleiben draußen.

1. Sichtprüfung: Lötbrücken, vergessene Lötstellen, Polung der Elkos und Dioden.
2. Mit dem Multimeter (Durchgang) prüfen, dass +12 V, −12 V und +5 V **nicht** mit Masse verbunden sind. Messpunkte: Fassung U6 Pin 8 (+12 V) und Pin 4 (−12 V), C3 (+5 V).
3. Strom anschließen, am Labornetzteil mit 50 mA Begrenzung je Seite, sonst im Gehäuse (roter Streifen an Pin 1 von J1 und am Bus an −12 V). Messen gegen Masse:

   | Wo | Soll |
   |---|---|
   | U6 Pin 8 | ca. +11,7 V (+12 V minus Diode) |
   | U6 Pin 4 | ca. −11,7 V |
   | U10 Pin 14 und am 5V-Pin der DevKit-Leiste | ca. +4,7 V (5 V minus Diode D3) |
   | U9 Pin 15 (Vref) | 3,00 V |

   3,3 V gibt es noch nicht: sie kommen erst mit dem DevKit aus dessen Regler.

4. Wenn etwas nicht stimmt oder der Strom an die Begrenzung geht: ausschalten und suchen, bevor Chips hineinkommen.

## 7. Chips und Module einsetzen

Strom aus. Dann einsetzen, die Kerbe wie auf dem Druck:

- U6 TL072, U7 und U8 MCP6004, U9 MCP3208, U10 74HCT125.
- Das DAC-Modul auf seine 6-polige Leiste.
- Das DevKit auf seine Leisten, **USB zur Unterkante** (zur Seite mit dem Stromanschluss).

Wieder einschalten. Ohne Firmware zeigen die LEDs nichts; der Strom sollte auf +12 V unter etwa 150 mA bleiben (geschätzt sind 110 mA im Betrieb).

## 8. Firmware flashen

Die fertige Firmware ist [firmware/modul/tonwerk-modul.bin](firmware/modul/tonwerk-modul.bin), eine Datei für Adresse 0x0.

1. In Chrome oder Edge https://espressif.github.io/esptool-js/ öffnen, Baudrate 921600, **Connect** und das DevKit auswählen. Das DevKit hat zwei USB-Buchsen; die mit „UART“ (oder „COM“) beschriftete nehmen.
2. Bei Flash Address `0x0` eintragen, die Datei wählen, **Program**.
3. Unter „Console“ (115200 Baud) sieht man danach, was das Modul tut.

Beim **ersten Start** berechnet das Modul seine Tabellen und legt sie im Flash ab. Das dauert geschätzt bis zu einer Minute (gemessen ist es noch nicht); die LEDs leuchten solange rot und gelb, es kommt kein Ton. Die Konsole meldet `wavetables computed in … ms` und `and kept in flash after … ms`. Jeder weitere Start liest die Tabellen in Bruchteilen einer Sekunde. Blinken beide LEDs rot, fehlt der PSRAM (falsches DevKit, siehe Schritt 1).

Wer selbst bauen will: ESP-IDF 5.3, `idf.py build flash monitor` im Ordner `firmware`, siehe [README, Firmware](README.md#firmware).

Flashen geht mit und ohne Eurorack-Strom; D3 trennt den Regler vom USB.

## 9. Zusammenbauen und erster Ton

1. Die Bedienplatine mit den Muttern der Potis und Buchsen an der Front festziehen, die Knöpfe aufsetzen.
2. Die Hauptplatine mit ihren Buchsenleisten auf die Stiftleisten der Bedienplatine stecken und mit den zwei Abstandsbolzen (Löcher H1, H2) verschrauben.
3. Im Gehäuse anschließen (roter Streifen an −12 V) und mit vier M3-Schrauben an die Schienen.
4. Den **VOICE**-Ausgang an einen Mixer oder Verstärker. Ohne Kabel in GATE ist das Gate dauerhaft an, das Modul spielt als Drone: Es sollte sofort klingen. TUNE, TABLE und CUTOFF drehen, WARP und FILTER kurz drücken (LED 1 und 2 wechseln die Farbe).

Die Ausgänge haben Modulpegel (±5 V), für Kopfhörer viel zu laut.

## 10. V/Oct kalibrieren

Damit V/OCT über die Oktaven stimmt, einmal kalibrieren (die README beschreibt es unter [Bedienung](README.md#bedienung)):

1. Ausschalten, **beide Tasten halten** und einschalten.
2. Genau 1,000 V an V/OCT, sonst nichts gesteckt, **WARP** drücken.
3. Genau 3,000 V anlegen, **FILTER** drücken.
4. Grün: gespeichert. Rot: die Werte waren unplausibel, die alten bleiben; Spannung und Kabel prüfen und wiederholen.

Danach mit einem Stimmgerät über mehrere Oktaven nachprüfen.

## Fehlersuche

- **Kein Ton, LEDs leuchten normal:** Lötbrücken auf dem DAC-Modul (Schritt 4, besonders 3 XSMT auf H), die drei Drähte zu J13, sitzt der TL072 richtig herum? An der Klinkenbuchse des DAC-Moduls hören (Kopfhörer nur leise und nicht aufsetzen, dort kommen 2 Veff): Klingt es dort, liegt der Fehler zwischen J13 und den Ausgangsbuchsen (U6, R9–R14).
- **Kein Ton beim ersten Start:** Die Tabellen werden noch berechnet (Schritt 8). Abwarten, bis die LEDs die Farben der Modi zeigen.
- **LEDs dunkel:** U10 ein 74HC125 statt HCT? Pinfolge der LEDs (Schritt 1)? R48 (330 Ω) gelötet?
- **LEDs in falschen Farben** (z. B. mit APA106/PL9823): Diese LEDs erwarten Rot-Grün-Blau statt Grün-Rot-Blau. In `firmware/main/hw/Hardware.cpp` in `showLeds` je `g` und `r` tauschen (`data[0]`/`data[1]` und `data[3]`/`data[4]`) und die Firmware selbst bauen.
- **Regler tun nichts oder springen:** Ein Regler übernimmt erst, wenn er den gespeicherten Wert erreicht (Pick-up, siehe README). Sonst die Stiftleisten J3 und J4 und die 100-nF-Kondensatoren C10–C19 prüfen.
- **CV-Eingänge tun nichts:** MCP3208 (U9) und die MCP6004 (U7, U8) richtig herum? 3,0 V an U9 Pin 15?
- **Konsole:** Über USB (115200 Baud) meldet das Modul alle 5 s die Last des Audio-Kerns; Fehlermeldungen beim Start stehen dort auch.

Wer das Modul baut: Rückmeldungen, was geklappt hat und was nicht, helfen, diese Anleitung und die Platinen zu verbessern.
