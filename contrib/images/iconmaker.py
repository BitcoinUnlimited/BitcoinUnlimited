#!/usr/bin/python3
from PIL import Image
import subprocess
import sys

def main(fname,outfile):
    im = Image.open(fname)
    icnFiles = []
    for sz in [512,256,128,64,32,16]:
        im2 = im.resize((sz,sz), Image.LANCZOS)
        outname = outfile + ("%d.png" % sz)
        im2.save(outname)
        if sz != 64:
            icnFiles.append(outname)
        subprocess.run(["convert", outname, outfile + ("%d.xpm" % sz)])
    im.save(outfile + ".ico")
    print(icnFiles)
    subprocess.run(["png2icns", outfile + ".icns"] + icnFiles)

def Test():
    main("icon1024.png", "bitcoin")

if __name__ == "__main__":
    argv = sys.argv
    if len(argv) == 1:
        main("icon1024.png", "bitcoin")
    elif len(argv) != 3 or argv[1] == "help":
        print("iconmaker.py converts an input file into icons of various sizes.\n")
        print("To run: ./iconmaker.py <input png file 1024px> <output file prefix, eg. bitcoin>.")
        print("If run with no args, runs: './iconmaker.py icon1024.png bitcoin'\n")
    else:
        main(argv[1],argv[2])
