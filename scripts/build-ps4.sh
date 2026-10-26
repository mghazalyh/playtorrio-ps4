#!/usr/bin/env bash
set -euo pipefail
: "${OO_PS4_TOOLCHAIN:?Set OO_PS4_TOOLCHAIN to official OpenOrbis SDK root}"
sdk="$(cd "$OO_PS4_TOOLCHAIN" && pwd)"
toolbin="$sdk/bin/linux"
work="$PWD/build/ps4"
mkdir -p "$work/obj" "$work/stage/sce_sys/about" "$work/stage/sce_module" "$work/stage/assets/fonts" "$PWD/dist"
for needed in "$sdk/link.x" "$sdk/lib/crt1.o" "$sdk/samples/_common/graphics.cpp" \
  "$toolbin/create-fself" "$toolbin/create-gp4" "$toolbin/PkgTool.Core"; do
  test -e "$needed" || { echo "Missing OpenOrbis SDK component: $needed" >&2; exit 2; }
done
flags=(-std=c++17 -D_GNU_SOURCE -D_POSIX_C_SOURCE=200809L -D_XOPEN_SOURCE=700 -DGRAPHICS_USES_FONT --target=x86_64-pc-freebsd12-elf -fPIC -funwind-tables
       -isysroot "$sdk" -isystem "$sdk/include" -isystem "$sdk/include/c++/v1"
       -I "$sdk/samples/_common" -I vendor)
clang++ "${flags[@]}" -c ps4/main.cpp -o "$work/obj/main.o"
clang++ "${flags[@]}" -c src/core.cpp -o "$work/obj/core.o"
clang++ "${flags[@]}" -c src/config_loader.cpp -o "$work/obj/config_loader.o"
clang++ "${flags[@]}" -c ps4/native_net.cpp -o "$work/obj/native_net.o"
clang++ "${flags[@]}" -c ps4/diagnostics.cpp -o "$work/obj/diagnostics.o"
clang++ "${flags[@]}" -c ps4/video_player.cpp -o "$work/obj/video_player.o"
clang++ "${flags[@]}" -c ps4/source_resolver.cpp -o "$work/obj/source_resolver.o"
python3 scripts/patch-graphics.py "$sdk/samples/_common/graphics.cpp" "$work/graphics-fixed.cpp"
clang++ "${flags[@]}" -c "$work/graphics-fixed.cpp" -o "$work/obj/graphics.o"
ld.lld -m elf_x86_64 -pie --script "$sdk/link.x" --eh-frame-hdr \
  "$work/obj/main.o" "$work/obj/core.o" "$work/obj/config_loader.o" "$work/obj/graphics.o" "$work/obj/native_net.o" "$work/obj/diagnostics.o" "$work/obj/video_player.o" "$work/obj/source_resolver.o" \
  -L"$sdk/lib" -lc -lkernel -lc++ -lSceVideoOut -lScePad -lSceUserService -lSceSysmodule -lSceFreeType -lSceNet -lSceSsl -lSceHttp -lSceAvPlayer -lSceAudioOut \
  "$sdk/lib/crt1.o" -o "$work/app.elf"
"$toolbin/create-fself" -in="$work/app.elf" -out="$work/app.oelf" \
  --eboot "$work/stage/eboot.bin" --paid 0x3800000000000011
sample="$sdk/samples/hello_world"
for source in "$sample/sce_sys/icon0.png" "$sample/sce_sys/about/right.sprx"; do
  test -f "$source" || { echo "Missing PS4 packaging asset: $source" >&2; exit 2; }
done
cp "$sample/sce_sys/icon0.png" "$work/stage/sce_sys/icon0.png"
cp "$sample/sce_sys/about/right.sprx" "$work/stage/sce_sys/about/right.sprx"
font="$sdk/samples/font/assets/fonts/Gontserrat-Regular.ttf"
license="$sdk/samples/font/assets/fonts/OFL.txt"
test -s "$font" && test -s "$license" || { echo "Missing licensed font from OpenOrbis sample" >&2; exit 2; }
cp "$font" "$work/stage/assets/fonts/Gontserrat-Regular.ttf"
cp "$license" "$work/stage/assets/fonts/OFL.txt"
if [[ -d "$sample/sce_module" ]]; then cp -a "$sample/sce_module/." "$work/stage/sce_module/"; fi
cd "$work/stage"
titleid=BREW20346
contentid=IV0000-BREW20346_00-PLAYTORRIOPS4APP
pkgtool="$toolbin/PkgTool.Core"
"$pkgtool" sfo_new sce_sys/param.sfo
"$pkgtool" sfo_setentry sce_sys/param.sfo APP_TYPE --type Integer --maxsize 4 --value 1
"$pkgtool" sfo_setentry sce_sys/param.sfo APP_VER --type Utf8 --maxsize 8 --value 01.09
"$pkgtool" sfo_setentry sce_sys/param.sfo ATTRIBUTE --type Integer --maxsize 4 --value 0
"$pkgtool" sfo_setentry sce_sys/param.sfo CATEGORY --type Utf8 --maxsize 4 --value gd
"$pkgtool" sfo_setentry sce_sys/param.sfo CONTENT_ID --type Utf8 --maxsize 48 --value "$contentid"
"$pkgtool" sfo_setentry sce_sys/param.sfo DOWNLOAD_DATA_SIZE --type Integer --maxsize 4 --value 0
"$pkgtool" sfo_setentry sce_sys/param.sfo SYSTEM_VER --type Integer --maxsize 4 --value 0
"$pkgtool" sfo_setentry sce_sys/param.sfo TITLE --type Utf8 --maxsize 128 --value "PlayTorrio PS4 UI Test"
"$pkgtool" sfo_setentry sce_sys/param.sfo TITLE_ID --type Utf8 --maxsize 12 --value "$titleid"
"$pkgtool" sfo_setentry sce_sys/param.sfo VERSION --type Utf8 --maxsize 8 --value 01.09
files=("eboot.bin" "sce_sys/about/right.sprx" "sce_sys/param.sfo" "sce_sys/icon0.png" "assets/fonts/Gontserrat-Regular.ttf" "assets/fonts/OFL.txt")
for f in sce_module/*; do if [[ -f "$f" ]]; then files+=("$f"); fi; done
"$toolbin/create-gp4" -out pkg.gp4 --content-id="$contentid" --files "${files[*]}"
"$pkgtool" pkg_build pkg.gp4 .
pkg="$(find . -maxdepth 2 -type f -name '*.pkg' -print -quit)"
test -n "$pkg" && test -s "$pkg" || { echo "OpenOrbis did not produce a PKG" >&2; exit 3; }
cp "$pkg" "$OLDPWD/dist/PlayTorrio-PS4-GoldHEN-10.01-EXPERIMENTAL.pkg"
file "$OLDPWD/dist/PlayTorrio-PS4-GoldHEN-10.01-EXPERIMENTAL.pkg"

# shadPS4 QtLauncher v397 no longer has Install Packages. Supply the
# exact UNPACKED homebrew files, preserving /app0 relative paths.
python3 - "$work/stage" "$OLDPWD/dist/PlayTorrio-PS4-shadPS4-extracted.zip" <<'PY'
import pathlib, sys, zipfile
stage = pathlib.Path(sys.argv[1])
dest = pathlib.Path(sys.argv[2])
members = ["eboot.bin", "sce_sys/param.sfo", "sce_sys/icon0.png"]
for member in members:
    if not (stage / member).is_file():
        raise SystemExit(f"Missing extracted game file: {member}")
with zipfile.ZipFile(dest, "w", zipfile.ZIP_DEFLATED) as z:
    for base in ("eboot.bin", "sce_sys", "sce_module", "assets"):
        path = stage / base
        paths = [path] if path.is_file() else sorted(path.rglob("*"))
        for f in paths:
            if f.is_file():
                z.write(f, "BREW20346/" + f.relative_to(stage).as_posix())
with zipfile.ZipFile(dest) as z:
    for required in ("BREW20346/eboot.bin", "BREW20346/sce_sys/param.sfo",
                     "BREW20346/assets/fonts/Gontserrat-Regular.ttf"):
        if required not in z.namelist() or not z.getinfo(required).file_size:
            raise SystemExit(f"Invalid emulator ZIP: {required} missing")
print(f"Emulator-ready unpacked PS4 game folder: {dest} ({dest.stat().st_size} bytes)")
PY

