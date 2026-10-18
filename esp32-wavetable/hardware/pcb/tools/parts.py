"""Every part of both boards and what each pin connects to: the one source for schematics, boards and BOMs.

Two boards stacked behind the 14 HP panel:
  bedien (controls): pots, jacks, buttons and LEDs on the front side, toward the panel; two 1x14 pin headers on the
         back side reach the main board.
  haupt  (main):     two 1x14 sockets on its front side, everything else on its back side (SMD for JLCPCB assembly,
         plus the Eurorack header, the DC/DC converter and USB-C).

Coordinates are the panel's (hardware/frontplatte.scad): mm from the panel's left and top edge, seen from the front.
"""

from dataclasses import dataclass, field

R0805 = "Resistor_SMD:R_0805_2012Metric"
C0805 = "Capacitor_SMD:C_0805_2012Metric"
C1206 = "Capacitor_SMD:C_1206_3216Metric"

# LCSC numbers only where they are JLCPCB basic parts I am sure of; the rest are found by value in JLCPCB's BOM tool.
LCSC = {
    ("R", "10k"): "C17414", ("R", "100k"): "C149504", ("R", "1k"): "C17513", ("R", "5.1k"): "C27834",
    ("R", "330"): "C17630", ("R", "10"): "C17415", ("R", "470"): "C17710", ("R", "6.8k"): "C17772",
    ("C", "100n"): "C49678", ("C", "10u"): "C15850", ("C", "1u"): "C28323", ("C", "2.2u"): "C377773",
    ("C", "10n"): "C1710", ("R", "30k"): "C17621", ("R", "16k"): "C17490", ("R", "17.4k"): "C2930173",
    ("R", "100k 0.1%"): "C5713386", ("R", "37.4k 0.1%"): "C865436", ("C", "22u"): "C45783",
    ("C", "2.2n"): "C28260", ("C1206", "10u 25V"): "C13585",
}


@dataclass
class Part:
    ref: str
    symbol: str          # library:name
    footprint: str       # library:name
    value: str
    pins: dict           # pin number or pin name -> net
    side: str = "F"      # F or B
    at: tuple = None     # panel coordinates of the part's centre, or None for automatic placement
    rot: float = 0.0
    group: str = ""      # placement region on the main board
    lcsc: str = ""
    note: str = ""
    hand: bool = False   # SMD, but soldered by hand: JLCPCB has no match for it
    fields: dict = field(default_factory=dict)


def resistor(ref, value, a, b, group, note=""):
    return Part(ref, "Device:R", R0805, value, {"1": a, "2": b}, "B", group=group,
                lcsc=LCSC.get(("R", value), ""), note=note)


def capacitor(ref, value, a, b, group, footprint=C0805):
    return Part(ref, "Device:C", footprint, value, {"1": a, "2": b}, "B", group=group,
                lcsc=LCSC.get(("C", value), "") if footprint == C0805 else LCSC.get(("C1206", value), ""))


# Panel positions (frontplatte.scad).
COLS = [13.0, 35.4, 57.8]
JACK_COLS = [10.5, 27.2, 43.6, 60.3]
POTS = [  # ref, net, panel x, y, label; in the firmware's order (twm::Pot), GPIO1..GPIO10
    ("RV1", "POT_TUNE", COLS[0], 21, "TUNE"),
    ("RV2", "POT_FINE", COLS[0], 39, "FINE"),
    ("RV3", "POT_TABLE", COLS[1], 21, "TABLE"),
    ("RV4", "POT_POSITION", COLS[2], 21, "POSITION"),
    ("RV5", "POT_WARP", COLS[1], 39, "WARP"),
    ("RV6", "POT_CUTOFF", COLS[2], 39, "CUTOFF"),
    ("RV7", "POT_RESO", COLS[2], 57, "RESO"),
    ("RV8", "POT_DECAY", COLS[0], 57, "DECAY"),
    ("RV9", "POT_POS_CV", COLS[1], 57, "POS CV"),
    ("RV10", "POT_CUT_CV", COLS[1], 75, "CUT CV"),
]
JACKS = [  # ref, tip net, switch net, panel x, y, label
    ("J5", "JACK_VOCT", "GND", JACK_COLS[0], 94, "V/OCT"),
    ("J6", "JACK_GATE", "GATE_NORM", JACK_COLS[1], 94, "GATE"),
    ("J7", "JACK_TABLE", "GND", JACK_COLS[2], 94, "TABLE"),
    ("J8", "JACK_POS", "GND", JACK_COLS[3], 94, "POS"),
    ("J9", "JACK_WARP", "GND", JACK_COLS[0], 112, "WARP"),
    ("J10", "JACK_CUTOFF", "GND", JACK_COLS[1], 112, "CUTOFF"),
    ("J11", "OUT_OSC", None, JACK_COLS[2], 112, "OSC"),
    ("J12", "OUT_VOICE", None, JACK_COLS[3], 112, "VOICE"),
]

# The two board-to-board connectors, pin by pin; the same on both boards.
J3_PINS = ["GND", "VREF"] + [p[1] for p in POTS] + ["GND", "+5V"]
J4_PINS = ["GND", "JACK_VOCT", "JACK_TABLE", "JACK_POS", "JACK_WARP", "JACK_CUTOFF", "JACK_GATE", "GATE_NORM",
           "OUT_OSC", "OUT_VOICE", "LED_DIN", "BTN_WARP", "BTN_FILTER", "GND"]
HEADER_X = (24.2, 46.6)   # between the pot columns
HEADER_Y = 16.0           # pin 1; the pins run down at 2.54 mm


def connectors(socket):
    fp = ("Connector_PinSocket_2.54mm:PinSocket_1x14_P2.54mm_Vertical" if socket
          else "Connector_PinHeader_2.54mm:PinHeader_1x14_P2.54mm_Vertical")
    side = "F" if socket else "B"
    return [
        Part("J3", "Connector_Generic:Conn_01x14", fp, "Potis", {str(i + 1): n for i, n in enumerate(J3_PINS)},
             side, at=(HEADER_X[0], HEADER_Y), note="Pin 1 oben"),
        Part("J4", "Connector_Generic:Conn_01x14", fp, "Buchsen", {str(i + 1): n for i, n in enumerate(J4_PINS)},
             side, at=(HEADER_X[1], HEADER_Y), note="Pin 1 oben"),
    ]


def control_board():
    parts = connectors(socket=False)
    for ref, net, x, y, label in POTS:
        parts.append(Part(ref, "Device:R_Potentiometer",
                          "Potentiometer_THT:Potentiometer_Alpha_RD901F-40-00D_Single_Vertical",
                          "B10K", {"1": "GND", "2": net, "3": "VREF"}, "F", at=(x, y), note=label))
    for ref, tip, switch, x, y, label in JACKS:
        pins = {"T": tip, "S": "GND"}
        if switch:
            pins["TN"] = switch
        parts.append(Part(ref, "Connector_Audio:AudioJack2_SwitchT",
                          "Connector_Audio:Jack_3.5mm_QingPu_WQP-PJ398SM_Vertical_CircularHoles",
                          "PJ398SM", pins, "F", at=(x, y), note=label))
    parts += [
        Part("SW1", "Switch:SW_Push", "Button_Switch_THT:SW_PUSH_6mm_H13mm", "WARP", {"1": "BTN_WARP", "2": "GND"},
             "F", at=(17.0, 75.0)),
        Part("SW2", "Switch:SW_Push", "Button_Switch_THT:SW_PUSH_6mm_H13mm", "FILTER",
             {"1": "BTN_FILTER", "2": "GND"}, "F", at=(54.0, 75.0)),
        Part("D1", "tonwerk:WS2812D-F5", "LED_THT:LED_D5.0mm-4_RGB", "WS2812D-F5",
             {"1": "LED_DIN", "2": "+5V", "3": "GND", "4": "LED_CHAIN"}, "F", at=(7.5, 75.0)),
        Part("D2", "tonwerk:WS2812D-F5", "LED_THT:LED_D5.0mm-4_RGB", "WS2812D-F5",
             {"1": "LED_CHAIN", "2": "+5V", "3": "GND"}, "F", at=(63.5, 75.0)),
        Part("C1", "Device:C", C0805, "100n", {"1": "+5V", "2": "GND"}, "B", at=(7.5, 80.0), lcsc="C49678"),
        Part("C2", "Device:C", C0805, "100n", {"1": "+5V", "2": "GND"}, "B", at=(63.5, 80.0), lcsc="C49678"),
    ]
    return parts


def main_board():
    p = connectors(socket=True)
    add = p.append

    # Power: Eurorack header, reverse-polarity diodes, 12 V -> 5 V converter, USB-C, 3.3 V regulator, reference.
    idc = {"1": "-12V_IN", "2": "-12V_IN", "9": "+12V_IN", "10": "+12V_IN"}
    idc.update({str(i): "GND" for i in range(3, 9)})
    add(Part("J1", "Connector_Generic:Conn_02x05_Odd_Even", "Connector_IDC:IDC-Header_2x05_P2.54mm_Vertical",
             "Eurorack", idc, "B", at=(60.0, 96.0), note="Pin 1 = -12 V (roter Streifen)"))
    add(Part("D1", "Device:D_Schottky", "Diode_SMD:D_SMA", "SS14", {"1": "+12V", "2": "+12V_IN"}, "B",
             group="power", lcsc="C2480"))
    add(Part("D2", "Device:D_Schottky", "Diode_SMD:D_SMA", "SS14", {"1": "-12V_IN", "2": "-12V"}, "B",
             group="power", lcsc="C2480"))
    add(capacitor("C1", "10u 25V", "+12V", "GND", "power", C1206))
    add(capacitor("C2", "10u 25V", "GND", "-12V", "power", C1206))
    add(Part("U1", "Regulator_Switching:R-78E5.0-0.5", "Converter_DCDC:Converter_DCDC_RECOM_R-78E-0.5_THT",
             "R-78E5.0-0.5", {"1": "+12V", "2": "GND", "3": "+5V_REG"}, "B", at=(46.0, 104.0), rot=0))
    add(Part("D3", "Device:D_Schottky", "Diode_SMD:D_SMA", "SS14", {"1": "+5V", "2": "+5V_REG"}, "B",
             group="power", lcsc="C2480"))
    add(Part("J2", "Connector:USB_C_Receptacle_USB2.0_16P", "Connector_USB:USB_C_Receptacle_HRO_TYPE-C-31-M-12",
             "USB-C", {"A1": "GND", "A12": "GND", "B1": "GND", "B12": "GND", "S1": "GND",
                       "A4": "VBUS", "A9": "VBUS", "B4": "VBUS", "B9": "VBUS", "A5": "CC1", "B5": "CC2",
                       "A6": "USB_DP", "B6": "USB_DP", "A7": "USB_DN", "B7": "USB_DN"},
             "B", at=(22.0, 114.5), rot=0, lcsc="C165948", note="zum Flashen"))
    add(resistor("R1", "5.1k", "CC1", "GND", "power"))
    add(resistor("R2", "5.1k", "CC2", "GND", "power"))
    add(Part("D4", "Device:D_Schottky", "Diode_SMD:D_SMA", "SS14", {"1": "+5V", "2": "VBUS"}, "B",
             group="power", lcsc="C2480"))
    add(capacitor("C3", "10u", "+5V", "GND", "power"))
    add(Part("U2", "Regulator_Linear:AMS1117-3.3", "Package_TO_SOT_SMD:SOT-223-3_TabPin2", "AMS1117-3.3",
             {"1": "GND", "2": "+3V3", "3": "+5V"}, "B", group="power", lcsc="C6186"))
    add(capacitor("C4", "22u", "+3V3", "GND", "power"))
    add(resistor("R3", "10", "+3V3", "+3V3A", "ref", note="Filter vor der Referenz"))
    add(capacitor("C5", "10u", "+3V3A", "GND", "ref"))
    add(Part("U3", "Reference_Voltage:REF3030", "Package_TO_SOT_SMD:SOT-23", "REF3030",
             {"1": "+3V3A", "2": "VREF", "3": "GND"}, "B", group="ref"))
    add(capacitor("C6", "1u", "VREF", "GND", "ref"))

    # ESP32-S3: pots on ADC1 (GPIO1..10), I2S, SPI, gate, buttons, LED, native USB.
    esp = {"GND": "GND", "3V3": "+3V3", "EN": "EN", "IO0": "BOOT",
           "IO11": "I2S_BCK", "IO12": "I2S_LRCK", "IO13": "I2S_DIN",
           "IO14": "SPI_CLK", "IO15": "SPI_MOSI", "IO16": "SPI_MISO", "IO17": "ADC_CS", "IO18": "GATE_IN",
           "IO19": "USB_DN", "IO20": "USB_DP", "IO21": "BTN_WARP", "IO47": "BTN_FILTER", "IO40": "LED_3V3"}
    for i, (_, net, *_rest) in enumerate(POTS):
        esp["IO%d" % (i + 1)] = net
    add(Part("U4", "RF_Module:ESP32-S3-WROOM-1", "RF_Module:ESP32-S3-WROOM-1", "ESP32-S3-WROOM-1-N16R8", esp, "B",
             at=(35.4, 24.5), rot=0, lcsc="C2913202", note="N16R8: 16 MB Flash, 8 MB PSRAM"))
    add(capacitor("C7", "10u", "+3V3", "GND", "esp"))
    add(capacitor("C8", "100n", "+3V3", "GND", "esp"))
    add(resistor("R4", "10k", "+3V3", "EN", "esp"))
    add(capacitor("C9", "1u", "EN", "GND", "esp"))
    add(resistor("R5", "10k", "+3V3", "BOOT", "esp"))
    add(Part("SW1", "Switch:SW_Push", "Button_Switch_SMD:SW_SPST_TL3342", "RESET", {"1": "EN", "2": "GND"}, "B",
             group="esp", hand=True, note="E-Switch TL3342, optional"))
    add(Part("SW2", "Switch:SW_Push", "Button_Switch_SMD:SW_SPST_TL3342", "BOOT", {"1": "BOOT", "2": "GND"}, "B",
             group="esp", hand=True, note="E-Switch TL3342, optional"))
    for i, (_, net, *_rest) in enumerate(POTS):
        add(capacitor("C%d" % (10 + i), "100n", net, "GND", "potcaps"))

    # DAC and output stages.
    dac = {"CPVDD": "+3V3", "CAPP": "CAPP", "CAPM": "CAPM", "CPGND": "GND", "VNEG": "VNEG", "OUTL": "DAC_L_RAW",
           "OUTR": "DAC_R_RAW", "AVDD": "+3V3A", "AGND": "GND", "DEMP": "GND", "FLT": "GND", "SCK": "GND",
           "BCK": "I2S_BCK", "DIN": "I2S_DIN", "LRCK": "I2S_LRCK", "FMT": "GND", "XSMT": "XSMT", "LDOO": "LDOO",
           "DGND": "GND", "DVDD": "+3V3"}
    add(Part("U5", "Audio:PCM5102A", "Package_SO:TSSOP-20_4.4x6.5mm_P0.65mm", "PCM5102APWR", dac, "B",
             group="dac", lcsc="C107671"))
    add(capacitor("C20", "2.2u", "CAPP", "CAPM", "dac"))
    add(capacitor("C21", "2.2u", "VNEG", "GND", "dac"))
    add(capacitor("C22", "1u", "LDOO", "GND", "dac"))
    add(capacitor("C23", "100n", "+3V3", "GND", "dac"))
    add(capacitor("C24", "100n", "+3V3A", "GND", "dac"))
    add(resistor("R6", "10k", "+3V3", "XSMT", "dac"))
    add(resistor("R7", "470", "DAC_L_RAW", "DAC_L", "dac"))
    add(capacitor("C25", "2.2n", "DAC_L", "GND", "dac"))
    add(resistor("R8", "470", "DAC_R_RAW", "DAC_R", "dac"))
    add(capacitor("C26", "2.2n", "DAC_R", "GND", "dac"))
    add(Part("U6", "Amplifier_Operational:TL072", "Package_SO:SOIC-8_3.9x4.9mm_P1.27mm", "TL072",
             {"1": "OA_OUT_L", "2": "OA_FB_L", "3": "DAC_L", "4": "-12V", "5": "DAC_R", "6": "OA_FB_R",
              "7": "OA_OUT_R", "8": "+12V"}, "B", group="out"))
    add(resistor("R9", "10k", "OA_FB_L", "GND", "out"))
    add(resistor("R10", "6.8k", "OA_FB_L", "OA_OUT_L", "out"))
    add(resistor("R11", "1k", "OA_OUT_L", "OUT_OSC", "out"))
    add(resistor("R12", "10k", "OA_FB_R", "GND", "out"))
    add(resistor("R13", "6.8k", "OA_FB_R", "OA_OUT_R", "out"))
    add(resistor("R14", "1k", "OA_OUT_R", "OUT_VOICE", "out"))
    add(capacitor("C27", "100n", "+12V", "GND", "out"))
    add(capacitor("C28", "100n", "GND", "-12V", "out"))

    # CV inputs: five inverting stages into the MCP3208.
    stages = [  # input, ADC net, feedback, bias, amp unit pins (out, -, +)
        ("JACK_VOCT", "ADC_PITCH", "37.4k 0.1%", "BIAS_PITCH", ("U7", "1", "2", "3")),
        ("JACK_TABLE", "ADC_TABLE", "30k", "BIAS_CV", ("U7", "7", "6", "5")),
        ("JACK_POS", "ADC_POS", "30k", "BIAS_CV", ("U7", "8", "9", "10")),
        ("JACK_WARP", "ADC_WARP", "30k", "BIAS_CV", ("U7", "14", "13", "12")),
        ("JACK_CUTOFF", "ADC_CUTOFF", "30k", "BIAS_CV", ("U8", "1", "2", "3")),
    ]
    amps = {"U7": {"4": "+3V3A", "11": "GND"}, "U8": {"4": "+3V3A", "11": "GND"}}
    r = 20
    c = 30
    for k, (jack, adc, rf, bias, (u, out, inv, non)) in enumerate(stages):
        amp_out = "AMP_" + adc[4:]
        node = "INV_" + adc[4:]
        amps[u].update({out: amp_out, inv: node, non: bias})
        add(resistor("R%d" % r, "100k 0.1%" if k == 0 else "100k", jack, node, "cv"))
        add(resistor("R%d" % (r + 1), rf, node, amp_out, "cv"))
        add(resistor("R%d" % (r + 2), "1k", amp_out, adc, "cv"))
        add(capacitor("C%d" % c, "10n", adc, "GND", "cv"))
        r += 3
        c += 1
    # The three spare amplifiers of U8 as followers of the bias, as Microchip recommends for unused ones.
    for out, inv, non, name in (("7", "6", "5", "U8B_OUT"), ("8", "9", "10", "U8C_OUT"), ("14", "13", "12", "U8D_OUT")):
        amps["U8"].update({out: name, inv: name, non: "BIAS_CV"})
    for u in ("U7", "U8"):
        add(Part(u, "Amplifier_Operational:MCP6004", "Package_SO:SOIC-14_3.9x8.7mm_P1.27mm", "MCP6004",
                 amps[u], "B", group="cv"))
    add(capacitor("C35", "100n", "+3V3A", "GND", "cv"))
    add(capacitor("C36", "100n", "+3V3A", "GND", "cv"))
    add(resistor("R40", "10k", "VREF", "BIAS_PITCH", "cv"))
    add(resistor("R41", "17.4k", "BIAS_PITCH", "GND", "cv"))
    add(capacitor("C37", "100n", "BIAS_PITCH", "GND", "cv"))
    add(resistor("R42", "16k", "VREF", "BIAS_CV", "cv"))
    add(resistor("R43", "10k", "BIAS_CV", "GND", "cv"))
    add(capacitor("C38", "100n", "BIAS_CV", "GND", "cv"))
    add(Part("U9", "Analog_ADC:MCP3208", "Package_SO:SOIC-16_3.9x9.9mm_P1.27mm", "MCP3208-CI/SL",
             {"CH0": "ADC_PITCH", "CH1": "ADC_TABLE", "CH2": "ADC_POS", "CH3": "ADC_WARP", "CH4": "ADC_CUTOFF",
              "CH5": "GND", "CH6": "GND", "CH7": "GND", "DGND": "GND", "AGND": "GND", "~{CS}/SHDN": "ADC_CS",
              "Din": "SPI_MOSI", "Dout": "SPI_MISO", "CLK": "SPI_CLK", "Vref": "VREF", "Vdd": "+3V3A"}, "B",
             group="adc"))
    add(capacitor("C39", "100n", "+3V3A", "GND", "adc"))

    # Gate input, the normalling of the gate jack, the LED level shifter.
    add(resistor("R44", "100k", "JACK_GATE", "GATE_B", "gate"))
    add(resistor("R45", "100k", "GATE_B", "GND", "gate"))
    add(Part("Q1", "Transistor_BJT:MMBT3904", "Package_TO_SOT_SMD:SOT-23", "MMBT3904",
             {"1": "GATE_B", "2": "GND", "3": "GATE_IN"}, "B", group="gate", lcsc="C20526"))
    add(resistor("R46", "10k", "+3V3", "GATE_IN", "gate"))
    add(resistor("R47", "100k", "+12V", "GATE_NORM", "gate", note="ohne Kabel ist das Gate an"))
    add(Part("U10", "74xGxx:74AHCT1G125", "Package_TO_SOT_SMD:SOT-23-5", "74AHCT1G125",
             {"1": "GND", "2": "LED_3V3", "3": "GND", "4": "LED_5V", "5": "+5V"}, "B", group="gate"))
    add(capacitor("C40", "100n", "+5V", "GND", "gate"))
    add(resistor("R48", "330", "LED_5V", "LED_DIN", "gate"))
    # The chips as JLCPCB's BOM tool matched them (2026-10-11).
    chips = {"U3": "C38423", "U6": "C5157710", "U7": "C7378", "U8": "C7378", "U9": "C16939", "U10": "C52140417"}
    for part in p:
        part.lcsc = chips.get(part.ref, part.lcsc)
    return p


# The through-hole variant: nothing to solder in SMD, boards ordered bare. The ESP32-S3-DevKitC-1 and a PCM5102A module
# (GY-PCM5102) plug into sockets on the main board's back, everything else is DIP, TO-92, axial or radial. Same panel,
# same board outlines, same two board-to-board headers: either control board fits either main board.
R_THT = "Resistor_THT:R_Axial_DIN0207_L6.3mm_D2.5mm_P10.16mm_Horizontal"
C_THT = "Capacitor_THT:C_Disc_D3.0mm_W2.0mm_P2.50mm"   # Vielschicht-Kerkos mit RM 2,54
CP_THT = "Capacitor_THT:CP_Radial_D5.0mm_P2.00mm"
DO41 = "Diode_THT:D_DO-41_SOD81_P10.16mm_Horizontal"
TO92 = "Package_TO_SOT_THT:TO-92_Inline"


def r_tht(ref, value, a, b, group, note=""):
    # Standing upright on the board plan (pads one above the other): five fit side by side in a 20 mm strip.
    return Part(ref, "Device:R", R_THT, value, {"1": a, "2": b}, "B", group=group, note=note, rot=90.0)


def c_tht(ref, value, a, b, group):
    if value.startswith("10u"):   # electrolytic, pin 1 is +
        return Part(ref, "Device:C_Polarized", CP_THT, "10u 25V", {"1": a, "2": b}, "B", group=group,
                    note="Elko, + an " + a)
    return Part(ref, "Device:C", C_THT, value, {"1": a, "2": b}, "B", group=group)


def control_board_tht():
    parts = control_board()
    for p in parts:
        if p.ref in ("C1", "C2"):
            p.footprint, p.lcsc, p.at = C_THT, "", (p.at[0], 80.5)
    return parts


def main_board_tht():
    p = connectors(socket=True)
    add = p.append

    # Power: Eurorack header, reverse-polarity diodes, 12 V -> 5 V converter. 3.3 V comes from the DevKit's own
    # regulator; the reference is an LDO at 3.0 V from 5 V, as no 3.0 V reference comes in TO-92.
    idc = {"1": "-12V_IN", "2": "-12V_IN", "9": "+12V_IN", "10": "+12V_IN"}
    idc.update({str(i): "GND" for i in range(3, 9)})
    add(Part("J1", "Connector_Generic:Conn_02x05_Odd_Even", "Connector_IDC:IDC-Header_2x05_P2.54mm_Vertical",
             "Eurorack", idc, "B", at=(57.0, 112.6), rot=90, note="Pin 1 = -12 V (roter Streifen)"))
    add(Part("D1", "Device:D_Schottky", DO41, "1N5817", {"1": "+12V", "2": "+12V_IN"}, "B", group="powerlow",
             note="Ring zur Kathode"))
    add(Part("D2", "Device:D_Schottky", DO41, "1N5817", {"1": "-12V_IN", "2": "-12V"}, "B", group="powerlow",
             note="Ring zur Kathode"))
    add(c_tht("C1", "10u", "+12V", "GND", "power"))
    add(c_tht("C2", "10u", "GND", "-12V", "power"))
    add(Part("U1", "Regulator_Switching:R-78E5.0-0.5", "Converter_DCDC:Converter_DCDC_RECOM_R-78E-0.5_THT",
             "R-78E5.0-0.5", {"1": "+12V", "2": "GND", "3": "+5V_REG"}, "B", at=(39.6, 111.6)))
    add(Part("D3", "Device:D_Schottky", DO41, "1N5817", {"1": "+5V", "2": "+5V_REG"}, "B", group="powerlow",
             note="trennt den Regler vom USB des DevKits"))
    add(c_tht("C3", "10u", "+5V", "GND", "power"))
    add(r_tht("R3", "10", "+3V3", "+3V3A", "ref", note="Filter für die Analog-Chips"))
    add(c_tht("C5", "10u", "+3V3A", "GND", "power"))
    add(Part("U3", "Regulator_Linear:MCP1700x-300xxTO", TO92, "MCP1700-3002E/TO",
             {"GND": "GND", "VI": "+5V", "VO": "VREF"}, "B", group="ref", note="3,0 V; Pin 1 GND, 2 IN, 3 OUT"))
    add(c_tht("C4", "1u", "+5V", "GND", "ref"))
    add(c_tht("C6", "1u", "VREF", "GND", "ref"))

    # The DevKit: pots on ADC1 (GPIO1..10), I2S, SPI, gate, buttons, LED; USB, reset and boot are its own.
    esp = {"G": "GND", "3V3": "+3V3", "5V": "+5V",
           "IO11": "I2S_BCK", "IO12": "I2S_LRCK", "IO13": "I2S_DIN",
           "IO14": "SPI_CLK", "IO15": "SPI_MOSI", "IO16": "SPI_MISO", "IO17": "ADC_CS", "IO18": "GATE_IN",
           "IO21": "BTN_WARP", "IO47": "BTN_FILTER", "IO40": "LED_3V3"}
    for i, (_, net, *_rest) in enumerate(POTS):
        esp["IO%d" % (i + 1)] = net
    add(Part("U4", "tonwerk:ESP32-S3-DevKitC-1", "tonwerk:ESP32-S3-DevKitC-1_Socket", "ESP32-S3-DevKitC-1-N16R8",
             esp, "B", at=(29.6, 60.2), note="auf 2 Buchsenleisten 1x22, USB zur Unterkante"))
    for i, (_, net, *_rest) in enumerate(POTS):
        add(c_tht("C%d" % (10 + i), "100n", net, "GND", "potcaps"))

    # DAC module and output stages. The module's output (its jack's or its L/G/R pads) comes over three wires to J13;
    # R7/R8 hold the op-amp's inputs at ground while no wire is there.
    add(Part("U5", "tonwerk:GY-PCM5102", "tonwerk:GY-PCM5102_Socket", "GY-PCM5102",
             {"VIN": "+5V", "GND": "GND", "LCK": "I2S_LRCK", "DIN": "I2S_DIN", "BCK": "I2S_BCK", "SCK": "GND"},
             "B", at=(34.5, 88.0), note="auf Buchsenleiste 1x6; SCK an GND"))
    add(Part("J13", "Connector_Generic:Conn_01x03", "Connector_PinHeader_2.54mm:PinHeader_1x03_P2.54mm_Vertical",
             "DAC L/G/R", {"1": "DAC_L", "2": "GND", "3": "DAC_R"}, "B", at=(60.0, 83.2), rot=90,
             note="drei Drähte vom Ausgang des Moduls"))
    add(r_tht("R7", "100k", "DAC_L", "GND", "out"))
    add(r_tht("R8", "100k", "DAC_R", "GND", "out"))
    add(Part("U6", "Amplifier_Operational:TL072", "Package_DIP:DIP-8_W7.62mm", "TL072",
             {"1": "OA_OUT_L", "2": "OA_FB_L", "3": "DAC_L", "4": "-12V", "5": "DAC_R", "6": "OA_FB_R",
              "7": "OA_OUT_R", "8": "+12V"}, "B", group="out", note="Fassung DIP-8"))
    add(r_tht("R9", "10k", "OA_FB_L", "GND", "out"))
    add(r_tht("R10", "6.8k", "OA_FB_L", "OA_OUT_L", "out"))
    add(r_tht("R11", "1k", "OA_OUT_L", "OUT_OSC", "out"))
    add(r_tht("R12", "10k", "OA_FB_R", "GND", "out"))
    add(r_tht("R13", "6.8k", "OA_FB_R", "OA_OUT_R", "out"))
    add(r_tht("R14", "1k", "OA_OUT_R", "OUT_VOICE", "out"))
    add(c_tht("C27", "100n", "+12V", "GND", "out"))
    add(c_tht("C28", "100n", "GND", "-12V", "out"))

    # CV inputs as on the SMD board. 1 % resistors do for V/OCT too: the firmware calibrates it at 1 V and 3 V.
    stages = [
        ("JACK_VOCT", "ADC_PITCH", "37.4k", "BIAS_PITCH", ("U7", "1", "2", "3")),
        ("JACK_TABLE", "ADC_TABLE", "30k", "BIAS_CV", ("U7", "7", "6", "5")),
        ("JACK_POS", "ADC_POS", "30k", "BIAS_CV", ("U7", "8", "9", "10")),
        ("JACK_WARP", "ADC_WARP", "30k", "BIAS_CV", ("U7", "14", "13", "12")),
        ("JACK_CUTOFF", "ADC_CUTOFF", "30k", "BIAS_CV", ("U8", "1", "2", "3")),
    ]
    amps = {"U7": {"4": "+3V3A", "11": "GND"}, "U8": {"4": "+3V3A", "11": "GND"}}
    r, c = 20, 30
    for jack, adc, rf, bias, (u, out, inv, non) in stages:
        amp_out, node = "AMP_" + adc[4:], "INV_" + adc[4:]
        amps[u].update({out: amp_out, inv: node, non: bias})
        add(r_tht("R%d" % r, "100k", jack, node, "cv"))
        add(r_tht("R%d" % (r + 1), rf, node, amp_out, "cv"))
        add(r_tht("R%d" % (r + 2), "1k", amp_out, adc, "cv"))
        add(c_tht("C%d" % c, "10n", adc, "GND", "cv"))
        r, c = r + 3, c + 1
    for out, inv, non, name in (("7", "6", "5", "U8B_OUT"), ("8", "9", "10", "U8C_OUT"), ("14", "13", "12", "U8D_OUT")):
        amps["U8"].update({out: name, inv: name, non: "BIAS_CV"})
    for u in ("U7", "U8"):
        add(Part(u, "Amplifier_Operational:MCP6004", "Package_DIP:DIP-14_W7.62mm", "MCP6004-I/P", amps[u], "B",
                 group="cv", note="Fassung DIP-14"))
    add(c_tht("C35", "100n", "+3V3A", "GND", "cv"))
    add(c_tht("C36", "100n", "+3V3A", "GND", "cv"))
    add(r_tht("R40", "10k", "VREF", "BIAS_PITCH", "cv"))
    add(r_tht("R41", "17.4k", "BIAS_PITCH", "GND", "cv"))
    add(c_tht("C37", "100n", "BIAS_PITCH", "GND", "cv"))
    add(r_tht("R42", "16k", "VREF", "BIAS_CV", "cv"))
    add(r_tht("R43", "10k", "BIAS_CV", "GND", "cv"))
    add(c_tht("C38", "100n", "BIAS_CV", "GND", "cv"))
    add(Part("U9", "Analog_ADC:MCP3208", "Package_DIP:DIP-16_W7.62mm", "MCP3208-CI/P",
             {"CH0": "ADC_PITCH", "CH1": "ADC_TABLE", "CH2": "ADC_POS", "CH3": "ADC_WARP", "CH4": "ADC_CUTOFF",
              "CH5": "GND", "CH6": "GND", "CH7": "GND", "DGND": "GND", "AGND": "GND", "~{CS}/SHDN": "ADC_CS",
              "Din": "SPI_MOSI", "Dout": "SPI_MISO", "CLK": "SPI_CLK", "Vref": "VREF", "Vdd": "+3V3A"}, "B",
             group="adc", note="Fassung DIP-16"))
    add(c_tht("C39", "100n", "+3V3A", "GND", "adc"))

    # Gate input, the gate jack's normalling, the LED level shifter (one gate of four; the others off, inputs low).
    # A 74HCT32 fits the same holes: pin 1 is then an OR input tied low, so 1Y follows pin 2; the other three
    # gates see one input high and drive their unconnected outputs high.
    add(r_tht("R44", "100k", "JACK_GATE", "GATE_B", "gate"))
    add(r_tht("R45", "100k", "GATE_B", "GND", "gate"))
    add(Part("Q1", "Transistor_BJT:BC547", TO92, "BC547", {"C": "GATE_IN", "B": "GATE_B", "E": "GND"}, "B",
             group="gate", note="C B E, flache Seite vorn"))
    add(r_tht("R46", "10k", "+3V3", "GATE_IN", "gate"))
    add(r_tht("R47", "100k", "+12V", "GATE_NORM", "gate", note="ohne Kabel ist das Gate an"))
    add(Part("U10", "74xx:74LS125", "Package_DIP:DIP-14_W7.62mm", "74HCT125",
             {"1": "GND", "2": "LED_3V3", "3": "LED_5V", "4": "+5V", "5": "GND", "10": "+5V", "9": "GND",
              "13": "+5V", "12": "GND", "14": "+5V", "7": "GND"}, "B", group="gate",
             note="74HCT125 oder 74AHCT125, nicht 74HC125, oder 74HCT32 (passt ohne Änderung); Fassung DIP-14"))
    add(c_tht("C40", "100n", "+5V", "GND", "gate"))
    add(r_tht("R48", "330", "LED_5V", "LED_DIN", "gate"))
    return p


BOARDS = {
    "tonwerk-bedien": control_board,
    "tonwerk-haupt": main_board,
    "tonwerk-bedien-tht": control_board_tht,
    "tonwerk-haupt-tht": main_board_tht,
}
