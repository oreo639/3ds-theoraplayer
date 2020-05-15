# 3ds-Theora example
POC example of theora decoding for the Nintendo 3DS family of systems.

You can create a compatible video file using the following command:
`ffmpeg -i 'input.ext' -s 400x240 -vcodec theora -ar 32000 -acodec libvorbis -strict -2 "output.ogv"`
