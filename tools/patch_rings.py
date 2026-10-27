"""Applies our improvements to Rings' Part (MIT licence) into build/patched/.

1. Full modal resolution in polyphony: Rings divides its 64 modes between
   voices (60, 28 or 12 per voice). With rings::full_resolution set, every
   voice keeps 60 modes.
"""
import os, sys
root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
src = os.path.join(os.environ.get('MI') or os.path.join(root, 'vendor', 'eurorack'), 'rings', 'dsp', 'part.cc')
dst_dir = os.path.join(root, 'build', 'patched')
os.makedirs(dst_dir, exist_ok=True)
s = open(src).read()
old = 'int32_t resolution = 64 / polyphony_ - 4;'
if old not in s:
    sys.exit('patch_rings: unexpected part.cc, nothing changed')
s = s.replace(old, 'int32_t resolution = full_resolution ? 60 : 64 / polyphony_ - 4;')
s = s.replace('namespace rings {', 'namespace rings {\n\nextern bool full_resolution;  // set by the instrument', 1)
open(os.path.join(dst_dir, 'part.cc'), 'w').write(s)
print('patched part.cc written')
