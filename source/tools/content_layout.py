"""Wide-screen RHMI content geometry for the N57 PowerDeck page."""
import xml.etree.ElementTree as ET

WIDTH, HEIGHT = 1236, 420


def flow_widths(app):
    for state in app.findall("hmiStates/hmiState"):
        for section in ("components", "optionComponents"):
            for node in state.findall(section + "/*"):
                if node.tag not in ("label", "button", "checkbox", "checkBox", "radiobutton"):
                    continue
                if node.find("properties/property[@id='20']") is not None:
                    continue
                props = node.find("properties")
                if props is None:
                    props = ET.SubElement(node, "properties")
                width = props.find("property[@id='9']")
                if width is None:
                    width = ET.SubElement(props, "property", id="9")
                width.set("value", str(WIDTH))
