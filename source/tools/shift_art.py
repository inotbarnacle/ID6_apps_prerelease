"""Bitmap assets for ShiftDeck's native iDrive 6 image components."""
from functools import lru_cache
from io import BytesIO
from pathlib import Path
from zipfile import ZIP_DEFLATED, ZipFile, ZipInfo

from PIL import Image, ImageChops, ImageDraw, ImageFont

BAR_BASE, BAR_WARN = 55800, 55833
LIGHT_BASE, TARGET_BASE = 55866, 55880
DIGIT_BASE, DIGIT_WARN = 55890, 55902
SHIFT_OFF, SHIFT_ON = 55914, 55915
SPEED_LEGEND, RPM_LEGEND, SCALE, LOWER = range(55916, 55920)
MARK_BASE = 55920
TARGETS = (5800, 6000, 6250, 6500, 6700, 3500, 4000, 4500)
TARGET_ACTIONS = (41, 42, 43, 44, 45, 48, 49, 50)
DEFAULT_INDEX = TARGETS.index(6500)
N57_DEFAULT_INDEX = TARGETS.index(4000)
WHITE, DIM, BLUE, RED = "#f0f4fb", "#90a4ba", "#7bd2ff", "#ff4548"
YELLOW = "#ffc34d"
LINE = "#42556a"
BAR_WIDTH = 1104
BAR_START, BAR_PITCH, BAR_SLANT = 4, (BAR_WIDTH - 28) / 16, 20
N57_BAR_BASE, N57_BAR_WARN, N57_SCALE, N57_MARK_BASE = 58800, 58833, 58866, 58867
N57_MAX_RPM = 6000
# Photo-matched scale decoration, not an ECU rev limiter or shift recommendation.
N57_RED_TICK_RPM = 5500
N57_AXIS_LEFT, N57_AXIS_RIGHT = 1, BAR_WIDTH - 2
N57_LABEL_INSET = 8
N57_SWEEP_WARNING, N57_BAR_WARNING, N57_DIGIT_WARNING = 59000, 59129, 59162
N57_LIGHTS_WARNING, N57_SHIFT_WARNING = 59174, 59175
N57_LOWER_MARKER, N57_UPPER_MARKER, N57_WINDOW_LEGEND = 59300, 59301, 59302
N57_SWEEP_NORMAL = 59400


@lru_cache
def font(size):
    local = Path("/System/Library/Fonts/Supplemental/DIN Condensed Bold.ttf")
    return ImageFont.truetype(str(local) if local.is_file() else "DejaVuSansCondensed-Bold.ttf", size)


def art(size):
    image = Image.new("RGBA", size)
    return image, ImageDraw.Draw(image)


def text(draw, xy, value, size, color=WHITE, anchor="mt"):
    draw.text(xy, value, font=font(size), fill=color, anchor=anchor)


def n57_axis_x(rpm):
    rpm = min(max(rpm, 0), N57_MAX_RPM)
    return round(N57_AXIS_LEFT + (N57_AXIS_RIGHT - N57_AXIS_LEFT) * rpm / N57_MAX_RPM)


def n57_bitmaps():
    # The legacy worker build has 32 steps; both profiles use the same axis.
    clip = Image.new("L", (BAR_WIDTH, 76))
    ImageDraw.Draw(clip).rounded_rectangle((1, 4, BAR_WIDTH - 2, 71), radius=8, fill=255)
    for alert, base in ((False, N57_BAR_BASE), (True, N57_BAR_WARN)):
        for step in range(33):
            image, d = art((BAR_WIDTH, 76))
            d.rounded_rectangle((1, 4, BAR_WIDTH - 2, 71), radius=8, fill="#222d3b")
            if step:
                fill, painter = art(image.size)
                right = n57_axis_x(N57_MAX_RPM * step / 32)
                painter.rectangle((1, 4, right, 71), fill=RED if alert else "#409af6")
                fill.putalpha(ImageChops.multiply(fill.getchannel("A"), clip))
                image.alpha_composite(fill)
            yield base + step, image

    image, d = art((BAR_WIDTH, 49))
    for rpm in range(0, N57_MAX_RPM + 1, 500):
        x = n57_axis_x(rpm)
        major = rpm % 1000 == 0
        color = RED if rpm >= N57_RED_TICK_RPM else DIM if major else LINE
        d.line((x, 3, x, 17 if major else 11), fill=color, width=2)
        if major:
            anchor = "lt" if rpm == 0 else "rt" if rpm == N57_MAX_RPM else "mt"
            label_x = N57_LABEL_INSET if rpm == 0 else BAR_WIDTH - N57_LABEL_INSET if rpm == N57_MAX_RPM else x
            text(d, (label_x, 17), str(rpm // 1000), 26, color, anchor)
    yield N57_SCALE, image
    for index, rpm in enumerate(TARGETS):
        image, d = art((BAR_WIDTH, 9))
        x = n57_axis_x(rpm)
        d.line((x, 0, x, 8), fill="#ffc34d", width=3)
        yield N57_MARK_BASE + index, image
    yield from n57_window_bitmaps()


def n57_window_bitmaps():
    # Private N57 banks keep normal blue until its configurable warning window.
    for resolution, base, color in ((128, N57_SWEEP_NORMAL, "#409af6"),
                                     (128, N57_SWEEP_WARNING, YELLOW),
                                     (32, N57_BAR_WARNING, YELLOW)):
        for step in range(resolution + 1):
            image, d = art((BAR_WIDTH, 76))
            d.rounded_rectangle((1, 4, BAR_WIDTH - 2, 71), radius=8, fill="#242b36")
            if step:
                right = n57_axis_x(N57_MAX_RPM * step / resolution)
                d.rounded_rectangle((1, 4, right, 71), radius=8, fill=color)
            yield base + step, image
    for index, glyph in enumerate("0123456789- "):
        image, d = art((58, 92))
        text(d, (29, 4), glyph, 91, YELLOW)
        yield N57_DIGIT_WARNING + index, image
    image, d = art((BAR_WIDTH, 34))
    for index in range(12):
        x = 103 + index * 76
        d.rounded_rectangle((x, 6, x + 59, 29), radius=6, fill=YELLOW)
    yield N57_LIGHTS_WARNING, image
    image, d = art((350, 84))
    d.rounded_rectangle((3, 4, 346, 79), radius=8, fill="#352a17", outline=YELLOW, width=2)
    text(d, (175, 18), "S H I F T", 52, YELLOW)
    yield N57_SHIFT_WARNING, image
    for identifier, color, width in ((N57_LOWER_MARKER, YELLOW, 3), (N57_UPPER_MARKER, RED, 5)):
        # RHMI stretches textures narrower than 64px; transparent padding retains
        # a thin marker while placing its ink centre at the native component X+3.
        image, d = art((64, 9))
        d.line((3, 0, 3, 8), fill=color, width=width)
        yield identifier, image
    image, d = art((300, 135))
    text(d, (150, 4), "SHIFT WINDOW", 27, DIM)
    text(d, (150, 106), "RPM", 24, DIM)
    yield N57_WINDOW_LEGEND, image


def bitmaps():
    for alert in (False, True):
        for step in range(33):
            image, d = art((BAR_WIDTH, 80))
            x0, pitch, slant = BAR_START, BAR_PITCH, BAR_SLANT
            left, right = x0, x0 + 16*pitch
            def band(a, b, color):
                d.polygon([(a+slant,4),(b+slant,4),(b,75),(a,75)], fill=color)
            ramp = ("#2373e7", "#3287f0", "#409af6", "#55acfd", "#70c4ff",
                    "#ffc050", "#ff944d", "#ff7054")
            for seg in range(16):
                a = left + seg*pitch
                b = a + pitch - 3
                band(a, b, "#40191e" if seg >= 14 else "#222d3b")
                fill_to = min(left + step*pitch/2, b)
                if fill_to > a:
                    band(a, fill_to, RED if alert else ramp[seg//2])
            if step:
                x = min(right-2, left + step*pitch/2)
                d.line([(x+slant,2),(x,77)], fill=WHITE, width=4)
            yield (BAR_WARN if alert else BAR_BASE)+step, image

    for step in range(14):
        image, d = art((1104,76))
        d.line([(0,43),(209,43),(253,73),(851,73),(895,43),(1103,43)], fill=LINE, width=2)
        d.line([(254,73),(850,73)], fill="#7093b5" if step!=13 else RED, width=3)
        for i in range(12):
            x = 257+i*50
            bright = "#36d790" if i<5 else "#ffc34d" if i<9 else RED
            dark = "#102c27" if i<5 else "#352a17" if i<9 else "#391a23"
            d.rounded_rectangle((x,4,x+40,31),radius=6,
                                fill=RED if step==13 else bright if i<step else dark)
        yield LIGHT_BASE+step,image

    for i,rpm in enumerate(TARGETS):
        image,d=art((180,135))
        text(d,(173,4),"SHIFT AT",25,DIM,"rt")
        text(d,(173,32),str(rpm),68,BLUE,"rt")
        text(d,(173,101),"RPM",22,DIM,"rt")
        d.line((5,133,173,133),fill=LINE,width=2)
        yield TARGET_BASE+i,image
        image,d=art((BAR_WIDTH,9))
        x=BAR_START+BAR_SLANT+round(16*BAR_PITCH*rpm/8000)
        d.line((x,1,x,8),fill="#ffc34d",width=3)
        yield MARK_BASE+i,image

    for color,base in ((WHITE,DIGIT_BASE),(RED,DIGIT_WARN)):
        for i,c in enumerate("0123456789- "):
            image,d=art((58,92))
            text(d,(29,4),c,91,color)
            yield base+i,image

    for identifier,phrase in ((SHIFT_OFF,""),(SHIFT_ON,"S H I F T")):
        image,d=art((350,84))
        if phrase:
            d.rounded_rectangle((3,4,346,79),radius=8,fill="#3c1420",outline=RED,width=2)
            text(d,(175,18),phrase,52,RED)
        yield identifier,image

    image,d=art((200,179))
    text(d,(2,3),"SPEED",25,DIM,"lt")
    text(d,(2,143),"MPH",22,DIM,"lt")
    d.line((2,177,143,177),fill=LINE,width=2)
    yield SPEED_LEGEND,image

    image,d=art((68,35))
    text(d,(2,3),"RPM",25,DIM,"lt")
    yield RPM_LEGEND,image

    image,d=art((BAR_WIDTH,51))
    for i in range(17):
        x=BAR_START+round(i*BAR_PITCH)
        color=RED if i>=14 else WHITE if i%2==0 else LINE
        d.line((x,3,x,17 if i%2==0 else 11),fill=color,width=2)
        if i%2==0:
            text(d,(max(9,x),22),str(i//2),26,color)
    yield SCALE,image

    image,d=art((1104,33))
    d.line([(0,29),(209,29),(253,3),(851,3),(895,29),(1103,29)],fill=LINE,width=2)
    yield LOWER,image
    yield from n57_bitmaps()


def save_assets(out, constants):
    with ZipFile(out,"w",ZIP_DEFLATED) as archive:
        for identifier,image in bitmaps():
            data=BytesIO()
            image.save(data,format="PNG",optimize=True)
            archive.writestr(ZipInfo(f"{identifier}.png"),data.getvalue(),ZIP_DEFLATED)
    constants.write_text(constants_text())


def constants_text():
    return (
        "// Generated by tools/make_drive_apps.py.\n"
        + "".join(f"static const unsigned {key} = {value}u;\n" for key,value in (
            ("kBarBase",BAR_BASE),("kBarWarn",BAR_WARN),("kLightBase",LIGHT_BASE),
            ("kTargetBase",TARGET_BASE),("kDigitBase",DIGIT_BASE),("kDigitWarn",DIGIT_WARN),
            ("kMarkBase",MARK_BASE),("kScale",SCALE),
            ("kN57BarBase",N57_BAR_BASE),("kN57BarWarn",N57_BAR_WARN),
            ("kN57Scale",N57_SCALE),("kN57MarkBase",N57_MARK_BASE),
            ("kN57SweepNormal",N57_SWEEP_NORMAL),
            ("kN57SweepWarning",N57_SWEEP_WARNING),("kN57BarWarning",N57_BAR_WARNING),
            ("kN57DigitWarning",N57_DIGIT_WARNING),("kN57LightsWarning",N57_LIGHTS_WARNING),
            ("kN57ShiftWarning",N57_SHIFT_WARNING),("kN57LowerMarker",N57_LOWER_MARKER),
            ("kN57UpperMarker",N57_UPPER_MARKER),("kN57WindowLegend",N57_WINDOW_LEGEND),
            ("kN57AxisLeft",52+N57_AXIS_LEFT),("kN57AxisSpan",N57_AXIS_RIGHT-N57_AXIS_LEFT),
            ("kShiftOff",SHIFT_OFF),("kShiftOn",SHIFT_ON),("kDefaultTarget",DEFAULT_INDEX),
            ("kDefaultN57Target",N57_DEFAULT_INDEX)))
        + "static const unsigned kShiftTargets[] = {"
        + ", ".join(map(str,TARGETS)) + "};\n"
    )
