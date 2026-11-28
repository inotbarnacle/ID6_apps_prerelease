"""Render a labeled host BoostDeck preview from captured production updates."""
import argparse
from pathlib import Path
from preview_full_warm import render

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--renderer',type=Path,required=True)
    parser.add_argument('--models',type=Path,required=True)
    parser.add_argument('--out',type=Path,required=True)
    parser.add_argument('--panel',default='1280x480')
    args=parser.parse_args()
    render(args.renderer,args.models,args.out,args.panel,expected_page=0)
