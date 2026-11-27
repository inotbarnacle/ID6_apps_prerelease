"""PowerDeck graph from ordinary RHMI images; no unverified bitmap API."""
from io import BytesIO
import xml.etree.ElementTree as ET
from PIL import Image, ImageDraw
import shift_art
from content_layout import WIDTH

BASE, EMPTY, BACKGROUND = 61000, 61387, 61388
HEIGHT, POINTS, LEFT = 64, 17, 140
STEP = (WIDTH - LEFT - 4) // (POINTS - 1)
ROWS = (20, 130, 240)
COLORS = ("#7bd2ff", "#ffab55", "#77d7a4")
SAVED_BASE, SAVED_EMPTY, SAVED_BACKGROUND_BASE = 63000, 64202, 64300
SAVED_HEIGHT, SAVED_POINTS, SAVED_LEFT, SAVED_TOP = 300, 25, 70, 30
SAVED_STEP = (WIDTH - SAVED_LEFT - 154) // (SAVED_POINTS - 1)
SAVED_RIGHT = SAVED_LEFT + (SAVED_POINTS - 1) * SAVED_STEP
SAVED_IMAGE_WIDTH = 64  # Padding prevents RHMI stretching the visible line segment.
SAVED_LEVELS = (400,)
METRIC_PS_KW = 0.73549875
SAVED_COLORS = ("#7bd2ff", "#ff4548")


def saved_power_ticks(kw_max):
    """Physical positions of 50-kW ticks on the shared power axis."""
    if kw_max not in SAVED_LEVELS:
        raise ValueError("Saved power axis is fixed at 400 kW")
    count = kw_max // 50
    return [(i * 50, SAVED_TOP + int(SAVED_HEIGHT * (kw_max - i * 50) / kw_max + 0.5))
            for i in range(count + 1)]


def saved_power_labels(kw_max):
    return [(value, int(value / METRIC_PS_KW + 0.5), y) for value, y in saved_power_ticks(kw_max)
            if value % 100 == 0]


def saved_background(kw_max):
    image = Image.new("RGBA", (WIDTH, 370))
    draw = ImageDraw.Draw(image)
    right = SAVED_LEFT + (SAVED_POINTS - 1) * SAVED_STEP
    bottom = SAVED_TOP + SAVED_HEIGHT
    draw.text((SAVED_LEFT, 0), "Torque (Nm)", font=shift_art.font(25), fill=SAVED_COLORS[0])
    draw.text((right, 0), "Power", font=shift_art.font(22), fill=SAVED_COLORS[1], anchor="rt")
    draw.text((SAVED_RIGHT + 50, 0), "kW", font=shift_art.font(20), fill=SAVED_COLORS[1], anchor="rt")
    draw.text((SAVED_RIGHT + 62, 0), "/", font=shift_art.font(20), fill="#a9b9c9", anchor="mt")
    draw.text((SAVED_RIGHT + 74, 0), "PS", font=shift_art.font(20), fill=SAVED_COLORS[0])
    for torque in range(0, 1001, 100):
        y = SAVED_TOP + SAVED_HEIGHT * (1000 - torque) // 1000
        draw.line((SAVED_LEFT, y, right, y), fill="#344455")
        draw.line((SAVED_LEFT - 7, y, SAVED_LEFT, y), fill=SAVED_COLORS[0])
        draw.text((60, y), str(torque), font=shift_art.font(22), fill=SAVED_COLORS[0], anchor="rm")
    for rpm in range(0, 6001, 1000):
        x = SAVED_LEFT + (right - SAVED_LEFT) * rpm // 6000
        draw.line((x, SAVED_TOP, x, bottom), fill="#344455")
        draw.line((x, bottom, x, bottom + 6), fill="#9eb0c2", width=2)
        draw.text((x, 340), str(rpm), font=shift_art.font(23), fill="#b8c6d3", anchor="mt")
    draw.text((WIDTH - 14, 350), "RPM", font=shift_art.font(23), fill="#b8c6d3", anchor="rm")
    for kw, y in saved_power_ticks(kw_max):
        length = 10 if kw % 100 == 0 else 5
        draw.line((right, y, right + length, y), fill=SAVED_COLORS[1])
    # Each row represents one power value in both units at the same height.
    for kw, ps, y in saved_power_labels(kw_max):
        draw.text((SAVED_RIGHT + 50, y), str(kw), font=shift_art.font(18), fill=SAVED_COLORS[1], anchor="rm")
        draw.text((SAVED_RIGHT + 62, y), "/", font=shift_art.font(18), fill="#a9b9c9", anchor="mm")
        draw.text((SAVED_RIGHT + 74, y), str(ps), font=shift_art.font(18), fill=SAVED_COLORS[0], anchor="lm")
    return image


def bitmaps():
    for series, color in enumerate(COLORS):
        for delta in range(-HEIGHT, HEIGHT + 1):
            image = Image.new("RGBA", (STEP + 1, abs(delta) + 3))
            draw = ImageDraw.Draw(image)
            first = 1 if delta >= 0 else 1 - delta
            second = 1 + delta if delta >= 0 else 1
            draw.line((0, first, STEP, second), fill=color, width=2)
            yield BASE + series * 129 + delta + HEIGHT, image
    yield EMPTY, Image.new("RGBA", (1, 1))
    image = Image.new("RGBA", (WIDTH, 340))
    draw = ImageDraw.Draw(image)
    for series, (name, scale) in enumerate((("RPM", "0 .. 6000"), ("Nm", "-300 .. 1000"), ("kW calc.", "-200 .. 700"))):
        y = ROWS[series]
        draw.text((4, y + 3), name, font=shift_art.font(28), fill=COLORS[series])
        draw.text((4, y + 39), scale, font=shift_art.font(19), fill="#a9b9c9")
        for row in (0, HEIGHT // 2, HEIGHT):
            draw.line((LEFT, y + row, LEFT + (POINTS - 1) * STEP, y + row), fill="#344455")
        for column in range(0, POINTS, 8):
            x = LEFT + column * STEP
            draw.line((x, y, x, y + HEIGHT), fill="#263644")
    yield BACKGROUND, image
    for series, color in enumerate(SAVED_COLORS):
        for delta in range(-SAVED_HEIGHT, SAVED_HEIGHT + 1):
            image = Image.new("RGBA", (SAVED_IMAGE_WIDTH, abs(delta) + 3))
            draw = ImageDraw.Draw(image)
            first = 1 if delta >= 0 else 1 - delta
            second = 1 + delta if delta >= 0 else 1
            draw.line((0, first, SAVED_STEP, second), fill=color, width=2)
            yield SAVED_BASE + series * (2 * SAVED_HEIGHT + 1) + delta + SAVED_HEIGHT, image
    yield SAVED_EMPTY, Image.new("RGBA", (1, 1))
    for level, kw_max in enumerate(SAVED_LEVELS):
        yield SAVED_BACKGROUND_BASE + level, saved_background(kw_max)


def position(component, x, y, width, height):
    props = ET.SubElement(component, "properties")
    for key, value in ((20, x), (21, y), (9, width), (10, height), (2, "false"), (3, "true")):
        ET.SubElement(props, "property", id=str(key), value=str(value))


def augment(app):
    actions, models, states = (app.find(name) for name in ("actions", "models", "hmiStates"))
    for action in (160, 161):
        ET.SubElement(actions, "raAction", id=str(action))
    for action, target in ((162, 110), (163, 111)):
        ET.SubElement(actions, "hmiAction", id=str(action), target=str(target))
    options = states.find("hmiState[@id='100']/optionComponents")
    if options is None:
        options = ET.SubElement(states.find("hmiState[@id='100']"), "optionComponents")
    for identifier, action, text in ((460, 160, "Start measurement"), (461, 161, "Stop and save"),
                                     (462, 162, "Live graph"), (463, 163, "Saved measurement")):
        ET.SubElement(options, "button", id=str(identifier), action=str(action), text=text)
    ET.SubElement(models, "imageIdModel", id="2299", imageId=str(BACKGROUND))
    ET.SubElement(models, "raDataModel", id="2300", modelType="Richtext")
    for line in range(3 * (POINTS - 1)):
        ET.SubElement(models, "imageIdModel", id=str(2000 + line), imageId=str(EMPTY))
    for state, title, component_base in ((110, "PowerDeck - Live graph", 3000),):
        page = ET.SubElement(states, "hmiState", id=str(state), parentId="100", text=title)
        options = ET.SubElement(page, "optionComponents")
        for offset, action, caption in (((0, 160, "Start measurement"), (1, 161, "Stop and save")) if state==110 else ()):
            ET.SubElement(options, "button", id=str(component_base + 200 + offset), action=str(action), text=caption)
        components = ET.SubElement(page, "components")
        background = ET.SubElement(components, "image", id=str(component_base + 180), model="2299")
        position(background, 0, 0, WIDTH, 340)
        for series in range(3):
            for line in range(POINTS - 1):
                i = series * (POINTS - 1) + line
                part = ET.SubElement(components, "image", id=str(component_base + i), model=str(2000 + i))
                position(part, LEFT + line * STEP, ROWS[series], STEP + 1, 3)
        label = ET.SubElement(components, "label", id=str(component_base + 181), model="2300")
        position(label, 0, 350, WIDTH, 50)

    ET.SubElement(models, "imageIdModel", id="2499", imageId=str(SAVED_BACKGROUND_BASE))
    ET.SubElement(models, "raDataModel", id="2500", modelType="Richtext")
    ET.SubElement(models, "raDataModel", id="2510", modelType="Richtext")
    ET.SubElement(models, "raDataModel", id="2520", modelType="Richtext")
    for line in range(2 * (SAVED_POINTS - 1)):
        ET.SubElement(models, "imageIdModel", id=str(2400 + line), imageId=str(SAVED_EMPTY))
    page = ET.SubElement(states, "hmiState", id="111", parentId="100", text="PowerDeck - RPM curves")
    components = ET.SubElement(page, "components")
    background = ET.SubElement(components, "image", id="4180", model="2499")
    position(background, 0, 0, WIDTH, 370)
    for series in range(2):
        for line in range(SAVED_POINTS - 1):
            i = series * (SAVED_POINTS - 1) + line
            part = ET.SubElement(components, "image", id=str(4000 + i), model=str(2400 + i))
            position(part, SAVED_LEFT + line * SAVED_STEP, SAVED_TOP, SAVED_IMAGE_WIDTH, 3)
    label = ET.SubElement(components, "label", id="4181", model="2500")
    position(label, 0, 370, WIDTH, 50)
    for component, model, width, height in ((4190, 2510, 150, 34), (4191, 2520, 280, 26)):
        peak = ET.SubElement(components, "label", id=str(component), model=str(model))
        position(peak, SAVED_LEFT, SAVED_TOP, width, height)
        peak.find("properties/property[@id='3']").set("value", "false")
