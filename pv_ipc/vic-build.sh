#!/bin/bash

# for x86_64

git clone https://github.com/kercre123/vic-toolchain --depth=1

set -e

#$HOME/projects/vic-toolchain/arm-linux-gnueabihf/bin/arm-linux-gnueabihf-gcc -static -I$(pwd)/include -L$(pwd)/lib/pi -lpv_porcupine -lpthread -o build/pv_server server/pv_server.c
mkdir -p build

$(pwd)/vic-toolchain/arm-linux-gnueabihf/bin/arm-linux-gnueabihf-gcc -static -I$(pwd)/include -L$(pwd)/../EXTERNALS/deps/picovoice/vicos/lib -o build/pv_server server/pv_server.c -lpv_porcupine -lpthread -lm

#$HOME/.anki/vicos-sdk/dist/1.1.0-r04/prebuilt/bin/arm-oe-linux-gnueabi-clang -static -I$(pwd)/include -L$(pwd)/lib/pi -lpv_porcupine -lpthread -o build/pv_server server/pv_server.c

$(pwd)/../anki-deps/vicos-sdk/dist/4.0.0-r05/prebuilt/bin/arm-oe-linux-gnueabi-clang -c -fPIC -I$(pwd)/include -o build/pv_porcupine.o interface/pv_interface.c
$(pwd)/../anki-deps/vicos-sdk/dist/4.0.0-r05/prebuilt/bin/arm-oe-linux-gnueabi-ar rcs build/libpv_porcupine_interface.a build/pv_porcupine.o

mv build/libpv_porcupine_interface.a ../EXTERNALS/deps/picovoice/vicos/lib/
mv build/pv_server ../EXTERNALS/deps/picovoice/vicos/bin/
chmod +rwx ../EXTERNALS/deps/picovoice/vicos/bin/pv_server
rm -rf build


echo "successful"
