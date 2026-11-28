"""Render a labeled synthetic preview from the production Library's captured state."""
import argparse
import ctypes
from pathlib import Path
import struct

from PIL import Image, ImageDraw, ImageFont


def render(renderer, models, output, panel='1280x480'):
    if panel not in ('800x480','960x480','1280x480','1440x540','1920x720'):
        raise ValueError('unsupported display panel')
    width,height=map(int,panel.split('x'))
    native=ctypes.CDLL(str(renderer.resolve()))
    native.overlay_state_size.restype=ctypes.c_uint
    native.overlay_state_magic.restype=ctypes.c_uint
    raw=models.read_bytes()
    if len(raw)!=native.overlay_state_size() or struct.unpack_from('<II',raw)!=(native.overlay_state_magic(),7):
        raise ValueError('expected a matching full-screen WarmDeck state')
    native.overlay_render.argtypes=(ctypes.c_void_p,ctypes.c_void_p,ctypes.c_void_p,ctypes.c_int,ctypes.c_int)
    native.overlay_render.restype=None
    packet=ctypes.create_string_buffer(raw)
    rgba=ctypes.create_string_buffer(width*height*4)
    rgb=ctypes.create_string_buffer(width*height*3)
    native.overlay_render(packet,rgba,rgb,width,height)
    preview=Image.new('RGB',(width,height+28),'#0b1016')
    preview.paste(Image.frombytes('RGB',(width,height),rgb.raw),(0,28))
    draw=ImageDraw.Draw(preview)
    draw.text((10,5),'Host renderer / synthetic data / not an iDrive capture',
              font=ImageFont.load_default(size=16),fill='#a8b3c2')
    preview.save(output)


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--renderer',type=Path,required=True)
    parser.add_argument('--models',type=Path,required=True)
    parser.add_argument('--out',type=Path,required=True)
    parser.add_argument('--panel',default='1280x480')
    args=parser.parse_args()
    render(args.renderer,args.models,args.out,args.panel)
