"""Generate the LaunchDeck acceleration timers RHMI resources.

Writes native/manifests/LaunchDeck.manifest, manifests/LaunchDeck.xml, manifests/LaunchDeck_ALL_Images.zip,
src/plugins/launch_assets.h and a preview PNG. Layout targets the 1280 x 480 panel content area
used by ShiftDeck (x 52..1156, y 10..430).
"""
from io import BytesIO
from pathlib import Path
from xml.etree import ElementTree as ET
from zipfile import ZIP_DEFLATED, ZipFile, ZipInfo
import sys

from PIL import Image, ImageDraw, ImageFilter, ImageFont

NATIVE = Path(__file__).resolve().parents[1] / "native"
PLUGIN = NATIVE / "manifests"
NAME, TITLE, CONNECTOR, WEIGHT = "LaunchDeck", "LaunchDeck", 120, 219

# Panel geometry (content area).
LEFT, TOP, WIDTH, HEIGHT = 52, 10, 1104, 420

# Image IDs: one private block that no other packaged app uses.
BACKGROUND = 57800
BIG_BASE, BIG_GOLD_BASE, SMALL_BASE = 57810, 57830, 57850   # 0-9, '.', blank
STATUS_BASE = 57870
BAR_BASE, BAR_STEPS = 57880, 31                               # 0..31 inclusive
STATUSES = ("idle", "ready", "running", "done", "best", "aborted", "nodata", "waiting")

# Palette.
INK = "#f3f5f8"
MUTED = "#8a96a8"
ACCENT = "#ff3b30"
GOLD = "#ffc53d"
GREEN = "#34d27b"
PANEL = (16, 20, 27)
CARD = (25, 31, 41)
CARD_EDGE = (52, 63, 80)

# Component geometry.
BIG_W, BIG_H, BIG_CHARS = 112, 168, 5
BIG_X = LEFT + 150
BIG_Y = TOP + 74
SECONDS_X = BIG_X + BIG_W * BIG_CHARS + 18
STATUS_RECT = (LEFT + 24, TOP + 14, WIDTH - 48, 46)
BAR_RECT = (LEFT + 48, TOP + 262, WIDTH - 96, 22)
SMALL_W, SMALL_H = 34, 50
CARD_Y, CARD_H, CARD_W, CARD_GAP = TOP + 300, 104, 258, 12
CARDS = ("LIVE SPEED", "PREVIOUS 1", "PREVIOUS 2", "PREVIOUS 3")
MODES = (("0–60", "MPH"), ("60–130", "MPH"), ("0–100", "KM/H"), ("100–200", "KM/H"))
CARD_CHARS = (3, 5, 5, 5)


def font(size, bold=True, condensed=False):
    names = []
    if condensed:
        names += ["DejaVuSansCondensed-Bold.ttf" if bold else "DejaVuSansCondensed.ttf"]
    names += ["DejaVuSans-Bold.ttf" if bold else "DejaVuSans.ttf",
              "/System/Library/Fonts/Supplemental/Arial Bold.ttf",
              "/System/Library/Fonts/Supplemental/Arial.ttf"]
    for name in names:
        for folder in ("", "/usr/share/fonts/truetype/dejavu/"):
            try:
                return ImageFont.truetype(folder + name, size)
            except OSError:
                continue
    sys.exit("No usable font found. Install fonts-dejavu-core.")


def card_x(index):
    total = len(CARDS) * CARD_W + (len(CARDS) - 1) * CARD_GAP
    return LEFT + (WIDTH - total) // 2 + index * (CARD_W + CARD_GAP)


def small_x(index, char):
    return card_x(index) + (CARD_W - CARD_CHARS[index] * SMALL_W) // 2 + char * SMALL_W


def glyph(char, width, height, size, colour, glow=None):
    image = Image.new("RGBA", (width, height))
    if char == " ":
        return image
    text_font = font(size, condensed=True)
    layer = Image.new("RGBA", (width, height))
    ImageDraw.Draw(layer).text((width / 2, height / 2), char, font=text_font, anchor="mm", fill=colour)
    if glow:
        halo = Image.new("RGBA", (width, height))
        ImageDraw.Draw(halo).text((width / 2, height / 2), char, font=text_font, anchor="mm", fill=glow)
        image.alpha_composite(halo.filter(ImageFilter.GaussianBlur(7)))
    image.alpha_composite(layer)
    return image


def digit_set(width, height, size, colour, glow=None):
    return [glyph(c, width, height, size, colour, glow) for c in "0123456789. "]


def background(mode):
    image = Image.new("RGBA", (WIDTH, HEIGHT), PANEL + (255,))
    draw = ImageDraw.Draw(image)
    for y in range(HEIGHT):  # subtle vertical sheen
        shade = int(10 * (1 - abs(y - HEIGHT * 0.35) / HEIGHT))
        draw.line((0, y, WIDTH, y), fill=(PANEL[0] + shade, PANEL[1] + shade, PANEL[2] + shade + 2, 255))
    draw.rounded_rectangle((0, 0, WIDTH - 1, HEIGHT - 1), radius=18, outline=CARD_EDGE, width=2)
    label = font(25, condensed=True)
    draw.text((BIG_X - LEFT - 22, BIG_Y - TOP + 46), MODES[mode][0], font=label, anchor="rm", fill=MUTED)
    draw.text((BIG_X - LEFT - 22, BIG_Y - TOP + 84), MODES[mode][1], font=label, anchor="rm", fill=ACCENT)
    draw.text((SECONDS_X - LEFT, BIG_Y - TOP + BIG_H - 40), "SEC", font=font(34, condensed=True),
              anchor="lm", fill=MUTED)
    for index, title in enumerate(CARDS):
        x = card_x(index) - LEFT
        y = CARD_Y - TOP
        draw.rounded_rectangle((x, y, x + CARD_W, y + CARD_H), radius=14, fill=CARD + (255,),
                               outline=CARD_EDGE, width=2)
        draw.text((x + CARD_W / 2, y + 20), title, font=font(20, condensed=True), anchor="mm", fill=MUTED)
    return image


def status_banner(kind):
    x, y, w, h = STATUS_RECT
    image = Image.new("RGBA", (w, h))
    draw = ImageDraw.Draw(image)
    text, colour = {
        "idle": ("STOP THE CAR TO ARM", MUTED),
        "ready": ("READY", GREEN),
        "running": ("MEASURING", ACCENT),
        "done": ("RUN COMPLETE", INK),
        "best": ("NEW PERSONAL BEST", GOLD),
        "aborted": ("RUN ABORTED", ACCENT),
        "waiting": ("SLOW BELOW START SPEED", MUTED),
        "nodata": ("WAITING FOR SPEED DATA", MUTED),
    }[kind]
    text_font = font(28, condensed=True)
    dot = 9
    width = draw.textlength(text, font=text_font) + dot * 2 + 16
    start = (w - width) / 2
    if kind == "ready":
        draw.line((start, h/2, start+6, h/2+6, start+17, h/2-7), fill=colour, width=4)
    else:
        draw.ellipse((start, h / 2 - dot, start + dot * 2, h / 2 + dot), fill=colour)
    draw.text((start + dot * 2 + 16, h / 2), text, font=text_font, anchor="lm", fill=colour)
    return image


def bar(step):
    _, _, w, h = BAR_RECT
    image = Image.new("RGBA", (w, h))
    draw = ImageDraw.Draw(image)
    draw.rounded_rectangle((0, 0, w - 1, h - 1), radius=h // 2, fill=(34, 41, 53, 255))
    filled = (w - 1) * step // BAR_STEPS
    if filled >= h:
        gradient = Image.new("RGBA", (filled, h))
        for x in range(filled):
            t = x / max(1, w - 1)
            colour = (int(255 * min(1, 0.2 + t)), int(197 * (1 - t) + 59 * t), int(61 * (1 - t) + 48 * t), 255)
            ImageDraw.Draw(gradient).line((x, 0, x, h), fill=colour)
        mask = Image.new("L", (filled, h))
        ImageDraw.Draw(mask).rounded_rectangle((0, 0, filled - 1, h - 1), radius=h // 2, fill=255)
        image.paste(gradient, (0, 0), mask)
    return image


def assets():
    images = {BACKGROUND+i: background(i) for i in range(len(MODES))}
    for base, glyphs in ((BIG_BASE, digit_set(BIG_W, BIG_H, 168, INK)),
                         (BIG_GOLD_BASE, digit_set(BIG_W, BIG_H, 168, GOLD, glow=(255, 197, 61, 150))),
                         (SMALL_BASE, digit_set(SMALL_W, SMALL_H, 46, INK))):
        for offset, image in enumerate(glyphs):
            images[base + offset] = image
    for offset, kind in enumerate(STATUSES):
        images[STATUS_BASE + offset] = status_banner(kind)
    for step in range(BAR_STEPS + 1):
        images[BAR_BASE + step] = bar(step)
    return images


def save_xml(doc, name):
    ET.indent(doc, space="  ")
    ET.ElementTree(doc).write(PLUGIN / name, encoding="utf-8", xml_declaration=True)


def manifest():
    doc = ET.parse(PLUGIN / "ShiftDeck.manifest").getroot()
    doc.find("name").text = NAME
    doc.find("file").text = "{pluginDir}/" + NAME
    doc.find("config/dependencies/pluginAppConnector/id").text = str(CONNECTOR)
    params = doc.find("config/parameters")
    params.find("DescFile").text = f"{{pluginDir}}/{NAME}.xml"
    params.find("ImageDBFile").text = f"{{pluginDir}}/{NAME}_ALL_Images.zip"
    save_xml(doc, f"{NAME}.manifest")


def place(parent, element_id, model, x, y, w, h):
    image = ET.SubElement(parent, "image", id=str(element_id), model=str(model))
    props = ET.SubElement(image, "properties")
    for prop, value in ((20, x), (21, y), (9, w), (10, h)):
        ET.SubElement(props, "property", id=str(prop), value=str(value))
    ET.SubElement(props, "property", id="2", value="false")
    ET.SubElement(props, "property", id="3", value="true")


def layout():
    """Return [(model, initial image, x, y, w, h)] in drawing order."""
    items = [(50, BACKGROUND, LEFT, TOP, WIDTH, HEIGHT),
             (51, STATUS_BASE + STATUSES.index("nodata"), *STATUS_RECT),
             (52, BAR_BASE, *BAR_RECT)]
    for i in range(BIG_CHARS):
        items.append((60 + i, BIG_BASE + 11, BIG_X + i * BIG_W, BIG_Y, BIG_W, BIG_H))
    card_models = (70, 75, 80, 85)
    for card, first_model in enumerate(card_models):
        for i in range(CARD_CHARS[card]):
            items.append((first_model + i, SMALL_BASE + 11, small_x(card, i), CARD_Y + 40, SMALL_W, SMALL_H))
    return items


def description():
    doc = ET.Element("pluginApps")
    app = ET.SubElement(doc, "pluginApp", applicationType="VehicleInformation",
                        applicationWeight=str(WEIGHT), text=TITLE)
    entry = ET.SubElement(app, "entryButton", id="1", action="10")
    ET.SubElement(ET.SubElement(entry, "properties"), "property", id="23", value="2")
    actions = ET.SubElement(app, "actions")
    ET.SubElement(actions, "hmiAction", id="10", target="100")
    for i in range(len(MODES)):
        ET.SubElement(actions, "raAction", id=str(41+i))
    models = ET.SubElement(app, "models")
    states = ET.SubElement(app, "hmiStates")
    page = ET.SubElement(states, "hmiState", id="100", text=TITLE)
    options = ET.SubElement(page, "optionComponents")
    for i, (limits, units) in enumerate(MODES):
        ET.SubElement(options, "button", id=str(30+i), action=str(41+i), text=f"{limits} {units.lower()}")
    content = ET.SubElement(page, "components")
    for index, (model, image, x, y, w, h) in enumerate(layout()):
        ET.SubElement(models, "imageIdModel", id=str(model), imageId=str(image))
        place(content, 200 + index, model, x, y, w, h)
    save_xml(doc, f"{NAME}.xml")


def header():
    lines = ["// Generated by tools/launch_art.py.",
             f"static const unsigned kLaunchBackground = {BACKGROUND}u;",
             f"static const unsigned kLaunchBigBase = {BIG_BASE}u;",
             f"static const unsigned kLaunchBigGoldBase = {BIG_GOLD_BASE}u;",
             f"static const unsigned kLaunchSmallBase = {SMALL_BASE}u;",
             f"static const unsigned kLaunchStatusBase = {STATUS_BASE}u;",
             f"static const unsigned kLaunchBarBase = {BAR_BASE}u;",
             f"static const unsigned kLaunchBarSteps = {BAR_STEPS}u;",
             f"static const unsigned kLaunchBigChars = {BIG_CHARS}u;",
             "enum LaunchStatus { " + ", ".join(f"kStatus{s.capitalize()}" for s in STATUSES) + " };",
             "enum LaunchModel { kModelStatus = 51u, kModelBar = 52u, kModelBig = 60u,",
             "                   kModelSpeed = 70u, kModelLast = 75u, kModelPrevious = 80u, kModelThird = 85u };",
             f"static const unsigned kLaunchSpeedChars = {CARD_CHARS[0]}u;",
             f"static const unsigned kLaunchTimeChars = {CARD_CHARS[1]}u;", ""]
    (NATIVE / "src" / "plugins" / "launch_assets.h").write_text("\n".join(lines))


def archive(images):
    with ZipFile(PLUGIN / f"{NAME}_ALL_Images.zip", "w", ZIP_DEFLATED) as zip_file:
        for identifier in sorted(images):
            data = BytesIO()
            images[identifier].save(data, format="PNG", optimize=True)
            zip_file.writestr(ZipInfo(f"{identifier}.png"), data.getvalue(), ZIP_DEFLATED)


def preview(images, path, state):
    """Compose one screen state for review: state = (status, big text, gold, bar, cards)."""
    status, big, gold, step, cards = state
    screen = Image.new("RGBA", (1280, 480), (0, 0, 0, 255))
    def put(identifier, x, y):
        screen.alpha_composite(images[identifier], (x, y))
    put(BACKGROUND, LEFT, TOP)
    put(STATUS_BASE + STATUSES.index(status), STATUS_RECT[0], STATUS_RECT[1])
    put(BAR_BASE + step, BAR_RECT[0], BAR_RECT[1])
    base = BIG_GOLD_BASE if gold else BIG_BASE
    for i, c in enumerate(big.rjust(BIG_CHARS)):
        put(base + "0123456789. ".index(c), BIG_X + i * BIG_W, BIG_Y)
    for card, text in enumerate(cards):
        for i, c in enumerate(text.rjust(CARD_CHARS[card])):
            put(SMALL_BASE + "0123456789. ".index(c), small_x(card, i), CARD_Y + 40)
    screen.convert("RGB").save(path)


if __name__ == "__main__":
    images = assets()
    manifest()
    description()
    header()
    archive(images)
    out = NATIVE / "build"
    out.mkdir(exist_ok=True)
    preview(images, out / "launch_preview_best.png", ("best", "4.87", True, BAR_STEPS, (" 61", "2.31", "5.12", "4.87")))
    preview(images, out / "launch_preview_running.png", ("running", "2.94", False, 18, (" 37", "2.31", "5.12", "4.87")))
    preview(images, out / "launch_preview_ready.png", ("ready", "0.00", False, 0, ("0", "4.87", "5.12", "5.25")))
    print(f"{len(images)} images written")
