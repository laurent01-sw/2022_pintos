#!/bin/bash
cd build
pintos-mkdisk filesys.dsk --filesys-size=2
pintos -p ../../examples/echo -a echo  --qemu -- -f -q run 'echo x'
cd ..

