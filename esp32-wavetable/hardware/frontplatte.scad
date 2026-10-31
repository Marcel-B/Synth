// Tonwerk Wavetable, Eurorack-Frontplatte 14 TE (3U), zum 3D-Druck. Mit engine = "physical", "pd", "granular" oder
// "additiv"
// dieselbe Platte mit der Beschriftung der anderen Firmwares (Löcher an denselben Stellen).
// Maße nach Doepfer A-100: Höhe 128,5 mm, Breite 14 x 5,08 mm - 0,3 mm = 70,8 mm, Befestigungslöcher 3 mm vom
// oberen/unteren Rand, 7,5 mm vom linken Rand und (TE - 3) x 5,08 mm weiter rechts.
//
//   openscad -o frontplatte.stl frontplatte.scad                       3D-Druckteil (Schrift erhaben)
//   openscad -D 'style="graviert"' -o frontplatte-graviert.stl frontplatte.scad   Schrift flach vertieft
//   openscad -D 'style="einlage"' -D 'part="platte"' -o ...           Schrift bündig, Mehrfarbdruck (zwei Teile)
//   openscad -D 'mode="2d"' -o frontplatte-bohrplan.svg frontplatte.scad   Bohrplan 1:1 (z. B. für Alu)
//   openscad -D 'engine="physical"' -o frontplatte-physical.stl frontplatte.scad   Physical, Schrift erhaben
//   openscad -D 'engine="pd"' -o frontplatte-pd.stl frontplatte.scad               PD, Schrift erhaben
//   openscad -D 'engine="granular"' -o frontplatte-granular.stl frontplatte.scad   Granular, Schrift erhaben
//   openscad -D 'engine="additiv"' -o frontplatte-additiv.stl frontplatte.scad     Additiv, Schrift erhaben
//
// Schrift (style):
//   "erhaben"  0,4 mm über der Front (zwei Schichten). Front nach oben drucken, bei 2,5 mm Höhe Filament wechseln,
//              dann ist die Schrift farbig.
//   "graviert" 0,4 mm tief. Front nach oben drucken (so werden die Buchstaben sauber, nach unten gedruckt
//              überbrückt der Drucker sie und sie laufen zu); mit Farbe oder Wachs füllen und abwischen.
//   "einlage"  bündig, als eigenes Teil (part = "schrift") für Drucker mit mehreren Farben oder Werkzeugwechsler;
//              Front nach unten, so bekommen Front und Schrift dieselbe Oberfläche vom Druckbett. So tief wie die
//              ersten inlay_layers Schichten (first_layer, dann layer), damit die Schrift an einer Schichtgrenze endet
//              und die nächste Schicht der Platte sie von hinten abdeckt.
// Der erste Druck (0,6 mm tief, Front nach unten, Schrift 2,3-2,5 mm) war schlecht lesbar: die Striche waren
// dünner als zwei Düsenbreiten. Darum ist die Schrift jetzt größer, um bold mm fetter und weiter gesperrt.

mode = "3d";             // "3d", "2d" oder "vorschau" (Front oben, für Bilder)
engine = "wavetable";    // "wavetable", "physical", "pd", "granular" oder "additiv": welche Firmware die Beschriftung meint
hp = 14;
width = hp * 5.08 - 0.3; // 70,8 mm
height = 128.5;
thickness = 2.5;         // Alu-Platten sind 2 mm; gedruckt etwas dicker für die Steifigkeit
style = "erhaben";       // "erhaben", "graviert" oder "einlage"
part = "platte";         // nur für "einlage": "platte" oder "schrift"
first_layer = 0.5;       // Marcels Drucker: erste Schicht 0,5 mm, dann 0,16 mm
layer = 0.16;
inlay_layers = 1;        // 1: nur die erste Schicht (0,5 mm), 2: 0,66 mm
relief = style == "einlage" ? first_layer + (inlay_layers - 1) * layer : 0.4;
bold = 0.12;             // macht jeden Strich 0,24 mm breiter

pot_hole = 7.2;          // Alpha 9 mm, Gewinde M7
jack_hole = 6.2;         // Thonkiconn PJ398SM, Gewinde M6
led_hole = 5.1;          // WS2812D-F5 (5 mm)
button_hole = 4.0;       // Taster 6 x 6 mm, Stößel 3,5 mm, 13 mm hoch
font = "Liberation Sans:style=Bold";

// Spalten der Potis und Reihen, in mm von links und von oben.
cols = [13.0, 35.4, 57.8];
jack_cols = [10.5, 27.2, 43.6, 60.3];

physical = engine == "physical";
pd = engine == "pd";
granular = engine == "granular";
additiv = engine == "additiv";

// [x, y, Beschriftung]
pots = additiv ? [
    [cols[0], 21, "TUNE"],     [cols[1], 21, "MORPH"],   [cols[2], 21, "OBERTÖNE"],
    [cols[0], 39, "FINE"],     [cols[1], 39, "NEIGUNG"], [cols[2], 39, "SPREIZ."],
    [cols[0], 57, "ABKLING."], [cols[1], 57, "OBT CV"],  [cols[2], 57, "VOKAL"],
                               [cols[1], 75, "SPR CV"],
] : granular ? [
    [cols[0], 21, "TUNE"],  [cols[1], 21, "GRÖSSE"],  [cols[2], 21, "POSITION"],
    [cols[0], 39, "FINE"],  [cols[1], 39, "DICHTE"],  [cols[2], 39, "STREUUNG"],
    [cols[0], 57, "SCAN"],  [cols[1], 57, "POS CV"],  [cols[2], 57, "TONSTREU"],
                            [cols[1], 75, "STR CV"],
] : pd ? [
    [cols[0], 21, "TUNE"],  [cols[1], 21, "LINIE 2"], [cols[2], 21, "HÜLLK."],
    [cols[0], 39, "FINE"],  [cols[1], 39, "MIX"],     [cols[2], 39, "DCW"],
    [cols[0], 57, "DECAY"], [cols[1], 57, "POS CV"],  [cols[2], 57, "CHORUS"],
                            [cols[1], 75, "DCW CV"],
] : physical ? [
    [cols[0], 21, "TUNE"],  [cols[1], 21, "HÄRTE"],   [cols[2], 21, "POSITION"],
    [cols[0], 39, "FINE"],  [cols[1], 39, "KRAFT"],   [cols[2], 39, "DÄMPF."],
    [cols[0], 57, "DECAY"], [cols[1], 57, "POS CV"],  [cols[2], 57, "INHARM."],
                            [cols[1], 75, "DMP CV"],
] : [
    [cols[0], 21, "TUNE"],  [cols[1], 21, "TABLE"],   [cols[2], 21, "POSITION"],
    [cols[0], 39, "FINE"],  [cols[1], 39, "WARP"],    [cols[2], 39, "CUTOFF"],
    [cols[0], 57, "DECAY"], [cols[1], 57, "POS CV"],  [cols[2], 57, "RESO"],
                            [cols[1], 75, "CUT CV"],
];
buttons = additiv ? [[17, 75, "KLANG A"], [54, 75, "KLANG B"]]
        : granular ? [[17, 75, "QUELLE"], [54, 75, "FENSTER"]]
        : pd ? [[17, 75, "WELLE 1"], [54, 75, "WELLE 2"]]
        : physical ? [[17, 75, "ERREGER"], [54, 75, "RESON."]] : [[17, 75, "WARP"], [54, 75, "FILTER"]];
leds = [[7.5, 75], [63.5, 75]];
jacks = additiv ? [
    [jack_cols[0], 94, "V/OCT"],    [jack_cols[1], 94, "GATE"],     [jack_cols[2], 94, "MORPH"], [jack_cols[3], 94, "OBERT."],
    [jack_cols[0], 112, "NEIG."],   [jack_cols[1], 112, "SPREIZ."], [jack_cols[2], 112, "L"],    [jack_cols[3], 112, "R"],
] : granular ? [
    [jack_cols[0], 94, "V/OCT"],  [jack_cols[1], 94, "GATE"],    [jack_cols[2], 94, "GRÖSSE"], [jack_cols[3], 94, "POS"],
    [jack_cols[0], 112, "DICHTE"], [jack_cols[1], 112, "STREU."], [jack_cols[2], 112, "L"],      [jack_cols[3], 112, "R"],
] : pd ? [
    [jack_cols[0], 94, "V/OCT"], [jack_cols[1], 94, "GATE"],  [jack_cols[2], 94, "LINIE 2"], [jack_cols[3], 94, "HÜLLK."],
    [jack_cols[0], 112, "MIX"],  [jack_cols[1], 112, "DCW"],  [jack_cols[2], 112, "L"],       [jack_cols[3], 112, "R"],
] : physical ? [
    [jack_cols[0], 94, "V/OCT"], [jack_cols[1], 94, "GATE"],     [jack_cols[2], 94, "HÄRTE"], [jack_cols[3], 94, "POS"],
    [jack_cols[0], 112, "KRAFT"], [jack_cols[1], 112, "DÄMPF."], [jack_cols[2], 112, "ROH"],  [jack_cols[3], 112, "KLANG"],
] : [
    [jack_cols[0], 94, "V/OCT"], [jack_cols[1], 94, "GATE"],   [jack_cols[2], 94, "TABLE"], [jack_cols[3], 94, "POS"],
    [jack_cols[0], 112, "WARP"], [jack_cols[1], 112, "CUTOFF"], [jack_cols[2], 112, "OSC"], [jack_cols[3], 112, "VOICE"],
];

// Von oben gemessen zeichnen, in OpenSCAD-Koordinaten (y nach oben) umrechnen.
function at(p) = [p[0], height - p[1]];

module mounting_slot(x, y) {
    // Langloch 3,2 x 5,2 mm, gibt beim Einbau 1 mm Spiel zu jeder Seite.
    translate([x, y]) hull() {
        translate([-1, 0]) circle(d = 3.2, $fn = 32);
        translate([1, 0]) circle(d = 3.2, $fn = 32);
    }
}

module holes() {
    for (y = [3, height - 3]) {
        mounting_slot(7.5, y);
        mounting_slot(7.5 + (hp - 3) * 5.08, y);
    }
    for (p = pots) translate(at(p)) circle(d = pot_hole, $fn = 48);
    for (b = buttons) translate(at(b)) circle(d = button_hole, $fn = 32);
    for (l = leds) translate(at(l)) circle(d = led_hole, $fn = 32);
    for (j = jacks) translate(at(j)) circle(d = jack_hole, $fn = 48);
}

module label(t, x, y, size = 3.0) {
    // valign = "center" mittelt über die Umlautpunkte; ein Wort mit Ä, Ö oder Ü stünde sonst tiefer als seine Nachbarn.
    umlaut = len([for (c = t) if (c == "Ä" || c == "Ö" || c == "Ü") c]) > 0;
    translate(at([x, y - (umlaut ? 0.17 * size : 0)])) offset(delta = bold)
        text(t, size = size, font = font, spacing = 1.07, halign = "center", valign = "center");
}

module labels() {
    for (p = pots) label(p[2], p[0], p[1] - 9.0);
    for (b = buttons) label(b[2], b[0], b[1] - 5.6, 2.8);
    for (j = jacks) label(j[2], j[0], j[1] - 6.8, 2.6);
    // Die Ausgänge in einem Rahmen, wie bei Eurorack üblich.
    translate(at([35.8, 116.6])) difference() {
        square([33.7, 15.4]);
        translate([0.8, 0.8]) square([32.1, 13.8]);
    }
    label("TONWERK", width / 2, 6.8, 4.0);
    label(additiv ? "ADDITIV" : granular ? "GRANULAR" : pd ? "PD" : physical ? "PHYSICAL" : "WAVETABLE", width / 2, 123.0, 3.0);
}

if (mode == "2d") {
    difference() {
        square([width, height]);
        holes();
    }
} else if (mode == "vorschau") {
    // Für Bilder, in den Farben des Drucks: Wavetable schwarz mit gelber Schrift, die anderen umgekehrt.
    color(physical || pd || granular || additiv ? "#fcee0a" : "#141414") difference() {
        cube([width, height, thickness]);
        translate([0, 0, -1]) linear_extrude(thickness + 2) holes();
    }
    color(physical || pd || granular || additiv ? "#141414" : "#fcee0a") translate([0, 0, thickness]) linear_extrude(relief) labels();
} else if (style == "einlage") {
    // Gebaut mit der Front oben, dann um die y-Achse gedreht (nicht gespiegelt), damit die Front auf dem Druckbett
    // liegt und Löcher und Schrift von vorn gesehen richtig herum stehen.
    translate([width, 0, thickness]) rotate([0, 180, 0])
        if (part == "schrift") inlay(); else plate();
} else {
    plate();   // Front oben
}

module plate() {
    difference() {
        cube([width, height, thickness]);
        translate([0, 0, -1]) linear_extrude(thickness + 2) holes();
        if (style != "erhaben") translate([0, 0, thickness - relief]) linear_extrude(relief + 1) labels();
    }
    if (style == "erhaben") translate([0, 0, thickness]) linear_extrude(relief) labels();
}

module inlay() {
    translate([0, 0, thickness - relief]) linear_extrude(relief) labels();
}
