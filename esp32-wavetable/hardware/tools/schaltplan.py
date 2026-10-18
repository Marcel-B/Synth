"""Schaltplan-Skizze des Tonwerk-Wavetable-Moduls: python3 schaltplan.py <Zielordner> (braucht schemdraw, matplotlib).

Eine Skizze, kein Platinen-Schaltplan: jede Stufe einmal gezeichnet, die Wiederholungen (fünf CV-Eingänge, zwei
Ausgänge, zehn Potis) stehen in der Beschriftung. Werte wie in stueckliste.csv.
"""

import sys

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
import schemdraw
import schemdraw.elements as elm
import schemdraw.logic as logic

schemdraw.use("matplotlib")


class Sketch(schemdraw.Drawing):
    """A drawing onto one of the sheet's axes; schemdraw only draws onto a given canvas when asked to."""

    def __init__(self, ax):
        super().__init__(canvas=ax, show=False)

    def __exit__(self, *args):
        self.draw(show=False)
FONT = 9


def cv_input(ax):
    with Sketch(ax) as d:
        d.config(fontsize=FONT, unit=2.2)
        d.here = (0, 0)
        jack = elm.Dot(open=True).label("Buchse\nV/OCT, TABLE,\nPOS, WARP, CUTOFF", loc="left", fontsize=FONT)
        r_in = elm.Resistor().right().label("100k\n0,1 %", loc="top")
        node = elm.Dot()
        elm.Line().right(0.6)
        op = elm.Opamp(leads=True).anchor("in1").label("¼ MCP6004\n3,3 V", loc="center", ofst=(-0.4, 0),
                                                        fontsize=8)
        # Rückkopplung über den Verstärker
        elm.Line().at(node.center).up(1.6)
        elm.Resistor().right().to((op.out.x, node.center[1] + 1.6)).label(
            "37,4k 0,1 % (V/OCT)\n30k (übrige CV)", loc="top")
        elm.Line().down().toy(op.out)
        elm.Dot()
        elm.Resistor().at(op.out).right().label("1k", loc="top")
        adc = elm.Dot()
        elm.Line().right(0.6)
        elm.Dot(open=True).label("MCP3208\nCH0…CH4", loc="right")
        elm.Capacitor().at(adc.center).down().label("10n", loc="bottom")
        elm.Ground()
        # Bias am nicht invertierenden Eingang, unterhalb des Eingangs gezeichnet
        elm.Line().at(op.in2).left(0.6)
        bias = elm.Dot()
        elm.Capacitor().down().label("100n", loc="bottom")
        elm.Ground()
        elm.Line().at(bias.center).left(1.4)
        elm.Line().down(1.6)
        mid = elm.Dot()
        elm.Resistor().left().label("10k (V/OCT)\n16k (CV)", loc="bottom")
        elm.Dot(open=True).label("VREF\n3,0 V", loc="left")
        elm.Resistor().at(mid.center).down().label("17,4k (V/OCT)\n10k (CV)", loc="top")
        elm.Ground()
    return (
            "V/OCT: −1…+7 V → 3,0…0 V (U = 2,618 V − 0,374 · Uin)   CV: −5…+5 V → 3,0…0 V (U = 1,5 V − 0,3 · Uin)\n"
            "Der Verstärker läuft an 3,3 V, sein Ausgang kann den ADC nicht übersteuern. Fünf solche Stufen."
    )


def gate_input(ax):
    with Sketch(ax) as d:
        d.config(fontsize=FONT, unit=2.2)
        d.here = (0, 0)
        elm.Dot(open=True).label("Buchse GATE\n(Schaltkontakt:\n100k an +12 V)", loc="left")
        elm.Resistor().right().label("100k", loc="top")
        base = elm.Dot()
        elm.Line().right(0.5)
        q = elm.BjtNpn(circle=True).anchor("base").label("MMBT3904", loc="right", ofst=(0.1, 0.4))
        elm.Ground().at(q.emitter)
        elm.Resistor().at(base.center).down().label("100k", loc="bottom")
        elm.Ground()
        elm.Line().at(q.collector).up(0.4)
        top = elm.Dot()
        elm.Resistor().up().label("10k", loc="bottom")
        elm.Vdd().label("+3,3 V")
        elm.Line().at(top.center).right(1.5)
        elm.Dot(open=True).label("GPIO18\n(low = Gate an)", loc="right")
    return (
            "Schaltschwelle ca. 1,4 V. Ohne Kabel zieht der Schaltkontakt das Gate hoch:\n"
            "das Modul klingt dann durchgehend (Drone)."
    )


def output(ax):
    with Sketch(ax) as d:
        d.config(fontsize=FONT, unit=2.2)
        d.here = (0, 0)
        elm.Dot(open=True).label("PCM5102A\nOUTL / OUTR\n±2,97 V", loc="left")
        elm.Line().right(1.6)
        op = elm.Opamp(leads=True).anchor("in2").label("½ TL072\n±12 V", loc="center", ofst=(-0.4, 0),
                                                       fontsize=8)
        elm.Line().at(op.in1).left(0.6)
        fb = elm.Dot()
        elm.Resistor().left().label("10k", loc="top")
        elm.Line().down(0.5)
        elm.Ground()
        elm.Line().at(fb.center).up(1.4)
        elm.Resistor().right().tox(op.out).label("6,8k", loc="top")
        elm.Line().down().toy(op.out)
        elm.Dot()
        elm.Resistor().at(op.out).right().label("1k", loc="top")
        elm.Dot(open=True).label("Buchse\nOSC (links)\nVOICE (rechts)", loc="right")
    return (
            "Verstärkung 1,68: ±2,97 V → ±5 V (10 Vss, Eurorack-Audiopegel). Zwei solche Stufen.\n"
            "PCM5102A: SCK an GND (Takt aus BCK), FMT, FLT, DEMP an GND, XSMT an 3,3 V."
    )


def pots_and_leds(ax):
    with Sketch(ax) as d:
        d.config(fontsize=FONT, unit=2.0)
        d.here = (0, 0)
        elm.Vdd().label("VREF 3,0 V")
        pot = elm.Potentiometer().down().label("10k lin\n(Alpha 9 mm)", loc="bottom", ofst=(0.2, 0))
        elm.Ground()
        elm.Line().at(pot.tap).right(1.0)
        w = elm.Dot()
        elm.Line().right(1.0)
        elm.Dot(open=True).label("GPIO1…10\n(ADC1)", loc="right")
        elm.Capacitor().at(w.center).down().label("100n", loc="bottom")
        elm.Ground()

        d.here = (9.0, 0.5)
        elm.Dot(open=True).label("GPIO40", loc="left")
        logic.Buf().right().label("74AHCT1G125\nan 5 V", loc="bottom", fontsize=8)
        elm.Resistor().right().label("330", loc="top")
        elm.Dot(open=True).label("DIN\nWS2812D-F5 ×2\n(in Kette, 5 V)", loc="right")

        d.here = (9.0, -2.6)
        elm.Dot(open=True).label("GPIO21 / GPIO47", loc="left")
        elm.Button().right().label("Taster WARP /\nFILTER", loc="bottom")
        elm.Ground().right()
    return (
            "Zehn Potis an der 3,0-V-Referenz, so misst der ESP32-ADC nur bis 3,0 V (12 dB Dämpfung reicht bis 3,1 V).\n"
            "Die LEDs brauchen 5 V Daten: der Puffer hebt die 3,3 V des ESP32 an. Taster mit internem Pull-up."
    )


def power(ax):
    with Sketch(ax) as d:
        d.config(fontsize=FONT, unit=2.0)
        d.here = (0, 0)
        elm.Dot(open=True).label("Eurorack\n+12 V", loc="left")
        elm.Diode().right().label("1N5817", loc="top")
        p12 = elm.Dot().label("+12 V", loc="top")
        elm.Capacitor(polar=True).at(p12.center).down().label("10µ", loc="bottom")
        elm.Ground()
        elm.Line().at(p12.center).right(1.0)
        reg = elm.Ic(pins=[elm.IcPin(name="IN", side="left"), elm.IcPin(name="OUT", side="right"),
                           elm.IcPin(name="GND", side="bottom")],
                     size=(2.2, 1.2)).anchor("IN").label("R-78E5.0-0.5", loc="top")
        elm.Ground().at(reg.GND)
        elm.Diode().at(reg.OUT).right().label("1N5819", loc="top")
        p5 = elm.Dot().label("+5 V", loc="top")
        elm.Line().right(1.0)
        elm.Dot(open=True).label("DevKit 5V,\nLEDs, Puffer", loc="right")

        d.here = (0, -3.4)
        elm.Dot(open=True).label("Eurorack\n−12 V", loc="left")
        # Kathode zur Buchse: der Strom fließt vom Modul in die −12-V-Schiene.
        elm.Diode().right().reverse().label("1N5817", loc="top")
        m12 = elm.Dot().label("−12 V", loc="bottom", ofst=(0, -0.2))
        elm.Line().right(1.0)
        elm.Dot(open=True).label("TL072", loc="right")

        d.here = (0, -6.0)
        elm.Dot(open=True).label("DevKit 3V3", loc="left")
        elm.Resistor().right().label("10 Ω", loc="top")
        f = elm.Dot()
        elm.Capacitor().at(f.center).down().label("10µ", loc="bottom")
        elm.Ground()
        elm.Line().at(f.center).right(1.0)
        ref = elm.Ic(pins=[elm.IcPin(name="IN", side="left"), elm.IcPin(name="OUT", side="right"),
                           elm.IcPin(name="GND", side="bottom")],
                     size=(2.2, 1.2)).anchor("IN").label("REF3030", loc="top")
        elm.Ground().at(ref.GND)
        elm.Line().at(ref.OUT).right(1.0)
        elm.Dot(open=True).label("VREF 3,0 V: Potis,\nMCP3208 VREF, Bias", loc="right")
    return (
            "Stromaufnahme geschätzt: +12 V ca. 110 mA, −12 V ca. 10 mA, kein +5-V-Bus nötig.\n"
            "Die Schottky-Diode hinter dem Wandler hält USB-Strom vom Rack fern, wenn beides steckt."
    )


def wiring(ax):
    """Die Verdrahtung am ESP32-S3-DevKitC-1 als Pin-Tabelle."""
    ax.axis("off")
    rows = [
        ("GPIO1…10", "Potis TUNE, FINE, TABLE, POSITION, WARP, CUTOFF, RESO, DECAY, POS CV, CUT CV (ADC1 Kanal 0…9)"),
        ("GPIO11 / 12 / 13", "PCM5102A BCK / LRCK / DIN (I2S, 48 kHz, 32 bit)"),
        ("GPIO14 / 15 / 16 / 17", "MCP3208 CLK / DIN / DOUT / CS (SPI, 1 MHz, 3,3 V, VREF 3,0 V)"),
        ("GPIO18", "Gate (über Transistor, low = an)"),
        ("GPIO21 / GPIO47", "Taster WARP / FILTER nach GND"),
        ("GPIO40", "WS2812-Kette (über 74AHCT1G125)"),
        ("5V / 3V3 / GND", "aus R-78E5.0 / zu REF3030, MCP6004, MCP3208, PCM5102A / Masse"),
        ("frei", "GPIO19/20 USB (Flashen, später USB-MIDI), GPIO33…37 belegt der PSRAM, 0/3/45/46 Strapping"),
    ]
    ax.text(0, 1.0, "ESP32-S3-DevKitC-1 N16R8: Belegung", fontsize=11, weight="bold", va="top")
    for i, (pin, use) in enumerate(rows):
        y = 0.86 - i * 0.11
        ax.text(0.0, y, pin, fontsize=FONT, family="monospace", va="top")
        ax.text(0.27, y, use, fontsize=FONT, va="top")


def main(out_dir):
    fig = plt.figure(figsize=(16.5, 23.4))  # A2 hochkant, als PDF auch auf A4 lesbar
    fig.suptitle("Tonwerk Wavetable – Eurorack-Modul mit ESP32-S3: Schaltplan-Skizze", fontsize=16, weight="bold",
                 y=0.985)
    grid = fig.add_gridspec(4, 2, height_ratios=[1.15, 1.0, 1.0, 0.8], hspace=0.12, wspace=0.08,
                            left=0.03, right=0.97, top=0.96, bottom=0.02)
    sections = [
        (grid[0, 0], "1  CV-Eingang (5×) zum MCP3208", cv_input),
        (grid[0, 1], "2  Stromversorgung und Referenz", power),
        (grid[1, 0], "3  Audio-Ausgang (2×) hinter dem PCM5102A", output),
        (grid[1, 1], "4  Gate-Eingang", gate_input),
        (grid[2, :], "5  Potis, LEDs, Taster", pots_and_leds),
        (grid[3, :], None, wiring),
    ]
    for spec, heading, draw in sections:
        ax = fig.add_subplot(spec)
        if heading:
            ax.set_title(heading, loc="left", fontsize=12, weight="bold")
        note = draw(ax)
        if note:
            ax.text(0.0, 0.0, note, transform=ax.transAxes, va="top", fontsize=8.5)
        if draw is not wiring:
            ax.set_aspect("equal")
            ax.axis("off")
    for ext in ("svg", "pdf", "png"):
        fig.savefig(f"{out_dir}/schaltplan.{ext}", dpi=110 if ext == "png" else None)


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else ".")
