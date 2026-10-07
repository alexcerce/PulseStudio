#!/bin/sh
# Builds Pulse's effects as VST3 plugins, one at a time, into dist/VST3.
#   tools/build-plugins.sh            all of them
#   tools/build-plugins.sh PulseGate  just the ones named
# Each plugin is a whole build of the engine (about a minute and a half, and a quarter of a gigabyte of
# intermediate files), so what a plugin leaves behind in build-dev is removed as soon as it is copied out.
cd "$(dirname "$0")/.." || exit 1
targets="$*"
[ -z "$targets" ] && targets=$(grep -o '^pulse_fx_plugin([A-Za-z0-9]*' CMakeLists.txt | sed 's/.*(//')
mkdir -p dist/VST3
failed=""
for t in $targets; do
    touch .autopilot.lock
    if cmake --build build-dev --config Release --target "${t}_VST3" > "build-dev/${t}.plugin.log" 2>&1; then
        for b in build-dev/${t}_artefacts/Release/VST3/*.vst3; do
            rm -rf "dist/VST3/$(basename "$b")"
            cp -r "$b" dist/VST3/
            echo "built: $(basename "$b")"
        done
        rm -f "build-dev/${t}.plugin.log"
    else
        failed="$failed $t"
        echo "FAILED: $t (see build-dev/${t}.plugin.log)"
    fi
    rm -rf "build-dev/${t}_artefacts/Release" "build-dev/${t}.dir" "build-dev/${t}_VST3.dir" "build-dev/${t}_rc_lib.dir"
done
[ -z "$failed" ] || exit 1
