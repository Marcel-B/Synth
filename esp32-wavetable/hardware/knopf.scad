// Tonwerk Wavetable, Drehknopf für die Alpha-9-mm-Potis, zweifarbig zum 3D-Druck.
// Die gedruckte Front (2,5 mm) samt Mutter lässt von der Achse wenig übrig; der Knopf greift darum mit einer
// Aussparung über Mutter und Scheibe und sitzt so fast auf der Front, die Bohrung nimmt die ganze Achse.
//
//   openscad -D 'part="koerper"' -o knopf-koerper.stl knopf.scad      Körper (schwarz)
//   openscad -D 'part="zeiger"' -o knopf-zeiger.stl knopf.scad        Zeiger und Ring (gelb), bündig im Deckel
//   openscad -D 'part="probe"' -o knopf-probe.stl knopf.scad          Passprobe: drei Bohrungen zum Anprobieren
//   python3 tools/make-3mf.py --knoepfe knopf-koerper.stl knopf-zeiger.stl knopf.3mf   zehn Knöpfe, zwei Teile
//
// Gedruckt wird mit dem Deckel nach unten: Zeiger und Ring sind die erste Schicht (first_layer), so bekommen sie
// dieselbe glatte Fläche vom Druckbett wie der Körper, und Bohrung und Aussparung öffnen sich nach oben, ohne
// Stützen. Mit einem Werkzeugwechsler beide Teile aus der 3MF; mit einer Düse die erste Schicht gelb drucken und
// danach auf Schwarz wechseln (dann ist der ganze Deckel gelb, der Zeiger fällt weg; dafür lieber einlage = false).
//
// Vor den zehn Knöpfen einmal die Probe drucken und auf eine Achse stecken: die Bohrung, die stramm sitzt, als
// shaft_d eintragen. Achse und Mutter am eigenen Poti nachmessen (shaft_len ab Front, nut_h ab Front); eine zu lang
// angenommene Achse schadet nicht, der Knopf rutscht dann bis auf die Front, eine zu kurz angenommene hebt ihn an.

part = "koerper";        // "koerper", "zeiger", "probe" oder "vorschau"
shaft = "geriffelt";     // "geriffelt" (18 Zähne, geschlitzt) oder "d" (D-Achse mit Abflachung)
shaft_d = 6.1;           // Bohrung; beim ersten Aufbau passte von 5,9, 6,0 und 6,1 mm nur 6,1 (gedruckte Löcher fallen enger aus)
d_flat = 4.5;            // D-Achse: Maß über die Abflachung
shaft_len = 8.0;         // so weit steht die Achse über die Front hinaus (mit Mutter darauf gemessen)
nut_h = 2.4;             // Mutter mit Scheibe, Höhe über der Front
nut_d = 10.8;            // Aussparung für die Mutter (M7, 9 mm Schlüsselweite = 10,4 mm über Eck)

knob_d = 13.4;           // passt zwischen die Reihen (18 mm) und unter die Beschriftung (9 mm über der Mitte)
knob_h = max(shaft_len + 2.5, 11.0);
gap = 0.4;               // Luft zwischen Knopf und Front
flutes = 18;             // Griffrillen
top_chamfer = 0.6;
first_layer = 0.5;       // Marcels Drucker: erste Schicht 0,5 mm
einlage = true;          // Zeiger und Ring als eigene Farbe
pointer_w = 1.4;
ring_w = 0.9;

$fn = 64;

module bore2d(dd) {
    if (shaft == "d")
        intersection() {
            circle(d = dd);
            translate([-dd / 2, -dd / 2]) square([dd, d_flat]);
        }
    else
        circle(d = dd);
}

// Der Zeiger zeigt zur Abflachung der D-Achse (+y), wie die meisten Potis ausgeliefert werden.
module marks2d() {
    translate([-pointer_w / 2, 1.2]) square([pointer_w, knob_d / 2 - 1.2 - ring_w - 0.5]);
    difference() {
        circle(d = knob_d - 2 * top_chamfer);
        circle(d = knob_d - 2 * top_chamfer - 2 * ring_w);
    }
}

// Außenform mit dem Deckel bei z = 0 (Druckbett) und der Unterseite bei z = knob_h.
module outer() {
    difference() {
        union() {
            cylinder(d1 = knob_d - 2 * top_chamfer, d2 = knob_d, h = top_chamfer);
            translate([0, 0, top_chamfer]) cylinder(d = knob_d, h = knob_h - top_chamfer);
        }
        // Griffrillen nur am unteren Teil, der Deckel bleibt rund.
        for (i = [0 : flutes - 1])
            rotate(i * 360 / flutes)
                translate([knob_d / 2 + 0.5, 0, knob_h * 0.3]) cylinder(d = 1.6, h = knob_h, $fn = 16);
    }
}

module body() {
    difference() {
        outer();
        // Bohrung: bis 0,5 mm über die Achsspitze (der Knopf sitzt gap über der Front), unten angefast, damit die
        // Achse leicht hineinfindet.
        bore_top = knob_h + gap - shaft_len - 0.5;
        translate([0, 0, max(1.6, bore_top)]) linear_extrude(knob_h) bore2d(shaft_d);
        translate([0, 0, knob_h - nut_h - 0.3 - 0.6])
            cylinder(d1 = shaft_d, d2 = shaft_d + 1.2, h = 0.61);
        // Aussparung über Mutter und Scheibe.
        translate([0, 0, knob_h - nut_h - 0.3]) cylinder(d = nut_d, h = nut_h + 0.31);
        if (einlage) translate([0, 0, -0.01]) linear_extrude(first_layer + 0.01) marks2d();
    }
}

module marks() {
    linear_extrude(first_layer) marks2d();
}

module probe() {
    // Drei Ringe mit Bohrungen shaft_d - 0,1, shaft_d und shaft_d + 0,1, nebeneinander, mit Strichen zum Zählen.
    for (i = [0 : 2]) translate([i * 14, 0, 0]) difference() {
        cylinder(d = 11, h = 6);
        translate([0, 0, -1]) linear_extrude(8) bore2d(shaft_d - 0.1 + i * 0.1);
        for (k = [0 : i]) translate([-1.5 + k * 1.5, 4.2, 5.4]) cube([0.8, 1.2, 1], center = true);
    }
}

if (part == "koerper") body();
else if (part == "zeiger") marks();
else if (part == "probe") probe();
else if (part == "vorschau") {
    // Aufrecht, wie er auf dem Poti sitzt.
    translate([0, 0, knob_h]) mirror([0, 0, 1]) {
        color([0.12, 0.12, 0.12]) body();
        if (einlage) color([0.99, 0.93, 0.04]) marks();
    }
}
