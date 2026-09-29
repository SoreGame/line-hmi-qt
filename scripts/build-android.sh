#!/usr/bin/env bash
# Сборка line-hmi-qt только под Android (arm64-v8a APK).
# Если Qt/SDK/NDK нет — ставит их в $HOME/Qt и $HOME/Android/Sdk.
#
# Переменные:
#   QT_ANDROID QT_HOST_PATH ANDROID_SDK_ROOT ANDROID_NDK_ROOT JAVA_HOME
#   CMAKE_BUILD_TYPE  — Release (по умолчанию) или Debug
#   SKIP_BOOTSTRAP=1  — не ставить Qt/SDK, только собирать
#   --install         — подписать debug-ключом и установить APK через adb
#   --setup-udev      — установить udev-правила для adb (sudo, один раз на ПК)
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DO_INSTALL=0
DO_SETUP_UDEV=0
for arg in "$@"; do
    case "$arg" in
        --install) DO_INSTALL=1 ;;
        --setup-udev) DO_SETUP_UDEV=1 ;;
    esac
done
ABI="${ANDROID_ABI:-arm64-v8a}"
PLATFORM="${ANDROID_PLATFORM:-android-26}"
BUILD_TYPE="${CMAKE_BUILD_TYPE:-Release}"
BUILD_DIR="${ROOT}/build-android"
QT_VERSION="${QT_VERSION:-6.8.3}"
QT_ROOT="${QT_ROOT:-$HOME/Qt}"
AQT_VENV="${AQT_VENV:-$HOME/.cache/line-hmi-qt/aqt-venv}"
NDK_VERSION="${NDK_VERSION:-26.1.10909125}"

die() { echo "error: $*" >&2; exit 1; }

UDEV_RULES_SRC="${ROOT}/scripts/51-android-udev.rules"

setup_android_udev() {
    [[ -f "$UDEV_RULES_SRC" ]] || die "нет $UDEV_RULES_SRC"
    echo "Ставлю udev-правила для adb → /etc/udev/rules.d/51-android-udev.rules"
    sudo cp "$UDEV_RULES_SRC" /etc/udev/rules.d/51-android-udev.rules
    sudo udevadm control --reload-rules
    sudo udevadm trigger
    echo "Готово. Переподключите USB или выполните:"
    echo "  adb kill-server && adb start-server"
}

adb_device_hint() {
    local devices="$1"
    if printf '%s\n' "$devices" | grep -q 'no permissions'; then
        echo "error: adb видит телефон, но нет прав USB (udev)." >&2
        echo "  Один раз на этом ПК:" >&2
        echo "    $0 --setup-udev" >&2
        echo "  или вручную:" >&2
        echo "    sudo cp scripts/51-android-udev.rules /etc/udev/rules.d/" >&2
        echo "    sudo udevadm control --reload-rules && sudo udevadm trigger" >&2
        echo "  затем: adb kill-server && adb start-server, переподключите кабель." >&2
        return
    fi
    if printf '%s\n' "$devices" | grep -qE '^[^[:space:]]+[[:space:]]+unauthorized'; then
        echo "error: телефон unauthorized — разблокируйте экран и подтвердите «Разрешить отладку по USB»." >&2
        return
    fi
    echo "error: телефон не в состоянии device (USB debugging + разрешение на ПК)." >&2
}

sign_apk_debug() {
    local src="$1"
    local dest="${src%.apk}-signed.apk"
    local ks="${ANDROID_DEBUG_KEYSTORE:-$HOME/.android/debug.keystore}"
    local apksigner="$ANDROID_SDK_ROOT/build-tools/34.0.0/apksigner"
    local keytool="${JAVA_HOME:-}/bin/keytool"
    [[ -x "$apksigner" ]] || die "нет apksigner ($apksigner)"
    [[ -x "$keytool" ]] || die "нет keytool ($keytool)"
    mkdir -p "$(dirname "$ks")"
    if [[ ! -f "$ks" ]]; then
        echo "Создаю debug keystore $ks" >&2
        "$keytool" -genkeypair -keystore "$ks" -alias androiddebugkey \
            -keyalg RSA -keysize 2048 -validity 10000 \
            -storepass android -keypass android \
            -dname "CN=Android Debug,O=Android,C=US"
    fi
    echo "Подписываю $src → $dest" >&2
    "$apksigner" sign --ks "$ks" --ks-key-alias androiddebugkey \
        --ks-pass pass:android --key-pass pass:android \
        --min-sdk-version 26 --out "$dest" "$src"
    echo "$dest"
}

dump_apk_and_maybe_install() {
    local apk=""
    local cand
    for cand in \
        "$BUILD_DIR/android-build/line-hmi-qt.apk" \
        "$BUILD_DIR/android-build/build/outputs/apk/release/android-build-release-unsigned.apk" \
        "$BUILD_DIR/android-build/build/outputs/apk/debug/android-build-debug.apk" \
        "$BUILD_DIR/android-build/line-hmi-qt-signed.apk"; do
        [[ -f "$cand" ]] && apk="$cand" && break
    done
    [[ -n "$apk" ]] || apk="$(find "$BUILD_DIR" -name '*.apk' -print -quit 2>/dev/null || true)"

    if [[ -z "$apk" || ! -f "$apk" ]]; then
        echo "error: APK не найден в $BUILD_DIR" >&2
        return 1
    fi

    local aapt="$ANDROID_SDK_ROOT/build-tools/34.0.0/aapt"
    local apksigner="$ANDROID_SDK_ROOT/build-tools/34.0.0/apksigner"
    local signed="unknown" badging="" abi="" minsdk="" pkg=""
    if [[ -x "$apksigner" ]]; then
        if "$apksigner" verify --min-sdk-version 26 "$apk" >/dev/null 2>&1; then
            signed="yes"
        else
            signed="no"
        fi
    fi
    if [[ "$signed" != "yes" ]]; then
        apk="$(sign_apk_debug "$apk")"
        if "$apksigner" verify --min-sdk-version 26 "$apk" >/dev/null 2>&1; then
            signed="yes"
        else
            signed="no"
        fi
    fi
    if [[ -x "$aapt" ]]; then
        badging="$("$aapt" dump badging "$apk" 2>/dev/null || true)"
        pkg="$(printf '%s\n' "$badging" | sed -n "s/^package: name='\\([^']*\\).*/\\1/p" | head -1)"
        minsdk="$(printf '%s\n' "$badging" | sed -n "s/^sdkVersion:'\\([^']*\\).*/\\1/p" | head -1)"
        abi="$(printf '%s\n' "$badging" | sed -n "s/^native-code: '\\(.*\\)'/\\1/p" | head -1)"
    fi
    echo "APK: $apk"
    echo "signed=$signed abi=$abi minSdk=$minsdk package=$pkg"

    local adb="$ANDROID_SDK_ROOT/platform-tools/adb"
    if [[ ! -x "$adb" ]]; then
        return 0
    fi
    local devices
    devices="$("$adb" devices -l 2>/dev/null || true)"
    echo "$devices"

    if [[ "$DO_INSTALL" != "1" ]]; then
        return 0
    fi
    if ! printf '%s\n' "$devices" | grep -qE '^[^[:space:]]+[[:space:]]+device[[:space:]]'; then
        adb_device_hint "$devices"
        return 1
    fi
    local pkg_name="${pkg:-org.scara.linehmi}"
    echo "Ставлю на основного пользователя (user 0), не во Второе пространство MIUI."
    "$adb" uninstall --user 10 "$pkg_name" >/dev/null 2>&1 || true
    "$adb" uninstall --user 0 "$pkg_name" >/dev/null 2>&1 || true
    "$adb" uninstall "$pkg_name" >/dev/null 2>&1 || true
    local out rc=0
    set +e
    out="$("$adb" install --user 0 -r -d -t "$apk" 2>&1)"
    rc=$?
    set -e
    echo "$out"
    if [[ "$rc" -eq 0 ]]; then
        echo "Запускаю $pkg_name …"
        "$adb" shell am start --user 0 -n "$pkg_name/org.qtproject.qt.android.bindings.QtActivity" || true
    fi
    return "$rc"
}

# dl.google.com часто не резолвится системным DNS — берём A-запись через DoH.
google_curl() {
    local ip
    ip="$(curl -fsSL --max-time 10 -H 'accept: application/dns-json' \
        'https://cloudflare-dns.com/dns-query?name=dl.google.com&type=A' \
        | python3 -c 'import json,sys; print(json.load(sys.stdin)["Answer"][0]["data"])')"
    [[ -n "$ip" ]] || die "не удалось узнать IP dl.google.com"
    curl --resolve "dl.google.com:443:$ip" "$@"
}

install_google_zip() {
    local url="$1"
    local dest="$2"
    if [[ -d "$dest" ]]; then
        return 0
    fi
    local tmp
    tmp="$(mktemp -d)"
    echo "  $url → $dest"
    google_curl -fL --retry 3 --retry-delay 2 -o "$tmp/pkg.zip" "$url"
    unzip -q "$tmp/pkg.zip" -d "$tmp/out"
    mkdir -p "$(dirname "$dest")"
    shopt -s nullglob
    local -a top
    top=("$tmp/out"/*)
    shopt -u nullglob
    if [[ ${#top[@]} -eq 1 && -d "${top[0]}" ]]; then
        mv "${top[0]}" "$dest"
    else
        mkdir -p "$dest"
        mv "$tmp/out"/* "$dest/"
    fi
    rm -rf "$tmp"
}

# Java игнорирует системный DNS для dl.google.com — подсовываем hosts через jdk.net.hosts.file.
ensure_java_dns() {
    local hosts_file="$HOME/.cache/line-hmi-qt/jdk-hosts"
    mkdir -p "$(dirname "$hosts_file")"
    python3 - "$hosts_file" <<'PY'
import json, pathlib, sys, urllib.request
out = pathlib.Path(sys.argv[1])
names = [
    "localhost",
    "dl.google.com",
    "maven.google.com",
    "repo.maven.apache.org",
    "repo1.maven.org",
    "plugins.gradle.org",
    "services.gradle.org",
    "gradle.org",
    "github.com",
    "objects.githubusercontent.com",
]
lines = ["127.0.0.1 localhost", "::1 localhost"]
etc = pathlib.Path("/etc/hosts")
if etc.exists():
    lines.extend(etc.read_text(errors="ignore").splitlines())
for name in names:
    if name == "localhost":
        continue
    url = f"https://cloudflare-dns.com/dns-query?name={name}&type=A"
    req = urllib.request.Request(url, headers={"accept": "application/dns-json"})
    try:
        with urllib.request.urlopen(req, timeout=10) as r:
            data = json.load(r)
        for a in data.get("Answer") or []:
            if a.get("type") == 1:
                lines.append(f"{a['data']} {name}")
                break
    except Exception as e:
        print(f"warning: DNS {name}: {e}", file=sys.stderr)
out.write_text("\n".join(lines) + "\n")
print(out)
PY
    export JDK_HOSTS_FILE="$hosts_file"
    export JAVA_TOOL_OPTIONS="${JAVA_TOOL_OPTIONS:-} -Djdk.net.hosts.file=$hosts_file"
    export GRADLE_OPTS="${GRADLE_OPTS:-} -Djdk.net.hosts.file=$hosts_file"
    pkill -f GradleDaemon >/dev/null 2>&1 || true
}

newest_dir() {
    local found="" d
    for d in "$@"; do
        [[ -d "$d" ]] || continue
        found="$d"
    done
    [[ -n "$found" ]] && printf '%s\n' "$found"
}

find_qt_android() {
    newest_dir \
        ${QT_ANDROID:+"$QT_ANDROID"} \
        "$QT_ROOT/$QT_VERSION/android_arm64_v8a" \
        "$HOME"/Qt/*/android_arm64_v8a \
        /opt/Qt/*/android_arm64_v8a || true
}

find_qt_host() {
    local android_dir="${1:-}"
    local parent=""
    [[ -n "$android_dir" ]] && parent="$(dirname "$android_dir")"
    newest_dir \
        ${QT_HOST_PATH:+"$QT_HOST_PATH"} \
        ${parent:+"$parent/linux_gcc_64"} \
        ${parent:+"$parent/gcc_64"} \
        "$QT_ROOT/$QT_VERSION/linux_gcc_64" \
        "$QT_ROOT/$QT_VERSION/gcc_64" \
        "$HOME"/Qt/*/linux_gcc_64 \
        "$HOME"/Qt/*/gcc_64 \
        /opt/Qt/*/gcc_64 || true
}

find_sdk() {
    newest_dir \
        ${ANDROID_SDK_ROOT:+"$ANDROID_SDK_ROOT"} \
        ${ANDROID_HOME:+"$ANDROID_HOME"} \
        "$HOME/Android/Sdk" \
        /opt/android-sdk \
        /usr/lib/android-sdk || true
}

find_ndk() {
    local sdk="${1:-}"
    newest_dir \
        ${ANDROID_NDK_ROOT:+"$ANDROID_NDK_ROOT"} \
        ${ANDROID_NDK:+"$ANDROID_NDK"} \
        ${sdk:+"$sdk"/ndk/*} \
        ${sdk:+"$sdk/ndk-bundle"} \
        /opt/android-ndk* || true
}

ensure_java() {
    if [[ -z "${JAVA_HOME:-}" ]]; then
        JAVA_HOME="$(newest_dir \
            "$HOME/.local/jdk-17" \
            /usr/lib/jvm/java-17-openjdk-amd64 \
            /usr/lib/jvm/java-17-openjdk \
            /usr/lib/jvm/java-21-openjdk-amd64 \
            /usr/lib/jvm/java-21-openjdk || true)"
        [[ -n "${JAVA_HOME:-}" ]] && export JAVA_HOME
    fi
    if [[ -z "${JAVA_HOME:-}" ]] || [[ ! -x "${JAVA_HOME}/bin/java" ]]; then
        echo "JDK не найден — качаю Temurin 17 в $HOME/.local/jdk-17 …"
        local tmp tarball extracted
        tmp="$(mktemp -d)"
        tarball="$tmp/jdk.tar.gz"
        curl -fsSL -o "$tarball" \
            "https://api.adoptium.net/v3/binary/latest/17/ga/linux/x64/jdk/hotspot/normal/eclipse?project=jdk"
        tar -xzf "$tarball" -C "$tmp"
        extracted="$(find "$tmp" -maxdepth 1 -type d -name 'jdk-17*' | head -1)"
        [[ -n "$extracted" ]] || die "не удалось распаковать JDK"
        mkdir -p "$HOME/.local"
        rm -rf "$HOME/.local/jdk-17"
        mv "$extracted" "$HOME/.local/jdk-17"
        rm -rf "$tmp"
        JAVA_HOME="$HOME/.local/jdk-17"
        export JAVA_HOME
    fi
    [[ -n "${JAVA_HOME:-}" && -x "${JAVA_HOME}/bin/java" ]] \
        || die "нужен JDK 17+. Задайте JAVA_HOME или поставьте:
  sudo apt-get install -y openjdk-17-jdk-headless"
}

ensure_aqt() {
    if [[ ! -x "$AQT_VENV/bin/aqt" ]]; then
        python3 -m venv "$AQT_VENV"
        "$AQT_VENV/bin/pip" install -q -U pip aqtinstall
    fi
}

ensure_qt() {
    local android_dir host_dir
    android_dir="$(find_qt_android)"
    if [[ -n "$android_dir" && -f "$android_dir/lib/cmake/Qt6/qt.toolchain.cmake" ]]; then
        QT_ANDROID="$android_dir"
        QT_HOST_PATH="$(find_qt_host "$android_dir")"
        [[ -n "$QT_HOST_PATH" ]] && return 0
    fi

    echo "Qt for Android не найден — ставлю $QT_VERSION в $QT_ROOT …"
    ensure_aqt
    mkdir -p "$QT_ROOT"
    "$AQT_VENV/bin/aqt" install-qt linux desktop "$QT_VERSION" linux_gcc_64 -O "$QT_ROOT"
    "$AQT_VENV/bin/aqt" install-qt all_os android "$QT_VERSION" android_arm64_v8a -O "$QT_ROOT"

    QT_ANDROID="$(find_qt_android)"
    QT_HOST_PATH="$(find_qt_host "${QT_ANDROID:-}")"
    [[ -n "${QT_ANDROID:-}" && -f "$QT_ANDROID/lib/cmake/Qt6/qt.toolchain.cmake" ]] \
        || die "не удалось поставить Qt for Android в $QT_ROOT"
    [[ -n "${QT_HOST_PATH:-}" && -d "$QT_HOST_PATH" ]] \
        || die "не удалось поставить host Qt (linux_gcc_64) в $QT_ROOT"
}

ensure_sdk_ndk() {
    ANDROID_SDK_ROOT="$(find_sdk)"
    if [[ -z "${ANDROID_SDK_ROOT:-}" ]]; then
        ANDROID_SDK_ROOT="$HOME/Android/Sdk"
    fi
    mkdir -p "$ANDROID_SDK_ROOT"

    local repo="https://dl.google.com/android/repository"
    echo "Android SDK → $ANDROID_SDK_ROOT"
    install_google_zip "$repo/platform-34-ext7_r03.zip" "$ANDROID_SDK_ROOT/platforms/android-34"
    install_google_zip "$repo/build-tools_r34-linux.zip" "$ANDROID_SDK_ROOT/build-tools/34.0.0"
    install_google_zip "$repo/platform-tools_r37.0.1-linux.zip" "$ANDROID_SDK_ROOT/platform-tools"
    install_google_zip "$repo/android-ndk-r26b-linux.zip" "$ANDROID_SDK_ROOT/ndk/$NDK_VERSION"

    ANDROID_NDK_ROOT="$(find_ndk "$ANDROID_SDK_ROOT")"
    [[ -d "$ANDROID_SDK_ROOT/platforms/android-34" ]] || die "нет platforms/android-34"
    [[ -d "$ANDROID_SDK_ROOT/build-tools/34.0.0" ]] || die "нет build-tools/34.0.0"
    [[ -n "${ANDROID_NDK_ROOT:-}" && -f "$ANDROID_NDK_ROOT/build/cmake/android.toolchain.cmake" ]] \
        || die "нет Android NDK. Задайте ANDROID_NDK_ROOT."
}

if [[ "$DO_SETUP_UDEV" == "1" ]]; then
    setup_android_udev
    exit 0
fi

if [[ "$DO_INSTALL" == "1" ]]; then
    ensure_java
    ANDROID_SDK_ROOT="$(find_sdk)"
    [[ -n "${ANDROID_SDK_ROOT:-}" && -d "$ANDROID_SDK_ROOT" ]] \
        || die "не найден Android SDK"
    export JAVA_HOME
    export PATH="$JAVA_HOME/bin:$PATH"
    export ANDROID_SDK_ROOT
    dump_apk_and_maybe_install
    exit $?
fi

if [[ "${SKIP_BOOTSTRAP:-0}" != "1" ]]; then
    ensure_java
    ensure_qt
    ensure_sdk_ndk
else
    ensure_java
    QT_ANDROID="$(find_qt_android)"
    QT_HOST_PATH="$(find_qt_host "${QT_ANDROID:-}")"
    ANDROID_SDK_ROOT="$(find_sdk)"
    ANDROID_NDK_ROOT="$(find_ndk "${ANDROID_SDK_ROOT:-}")"
    [[ -n "${QT_ANDROID:-}" && -f "$QT_ANDROID/lib/cmake/Qt6/qt.toolchain.cmake" ]] \
        || die "не найден Qt for Android. Запустите без SKIP_BOOTSTRAP=1"
    [[ -n "${QT_HOST_PATH:-}" && -d "$QT_HOST_PATH" ]] \
        || die "не найден host Qt (linux_gcc_64 / gcc_64)"
    [[ -n "${ANDROID_SDK_ROOT:-}" && -d "$ANDROID_SDK_ROOT" ]] \
        || die "не найден Android SDK"
    [[ -n "${ANDROID_NDK_ROOT:-}" && -f "$ANDROID_NDK_ROOT/build/cmake/android.toolchain.cmake" ]] \
        || die "не найден Android NDK"
fi

QT_TOOLCHAIN="$QT_ANDROID/lib/cmake/Qt6/qt.toolchain.cmake"
NDK_TOOLCHAIN="$ANDROID_NDK_ROOT/build/cmake/android.toolchain.cmake"
JOBS="$(nproc 2>/dev/null || echo 4)"

export JAVA_HOME
export PATH="$JAVA_HOME/bin:$PATH"
export ANDROID_SDK_ROOT ANDROID_NDK_ROOT
export ANDROID_HOME="$ANDROID_SDK_ROOT"
ensure_java_dns

echo "Qt Android : $QT_ANDROID"
echo "Qt host    : $QT_HOST_PATH"
echo "SDK        : $ANDROID_SDK_ROOT"
echo "NDK        : $ANDROID_NDK_ROOT"
echo "Java       : ${JAVA_HOME:-}"
echo "ABI        : $ABI  ($PLATFORM, $BUILD_TYPE)"
echo "Build dir  : $BUILD_DIR"
echo

mkdir -p "$ROOT/android/assets"
cp -f "$ROOT/config.json" "$ROOT/android/assets/config.json"

cmake -S "$ROOT" -B "$BUILD_DIR" \
    -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
    -DQT_HOST_PATH="$QT_HOST_PATH" \
    -DCMAKE_TOOLCHAIN_FILE="$QT_TOOLCHAIN" \
    -DQT_CHAINLOAD_TOOLCHAIN_FILE="$NDK_TOOLCHAIN" \
    -DANDROID_ABI="$ABI" \
    -DANDROID_PLATFORM="$PLATFORM" \
    -DANDROID_SDK_ROOT="$ANDROID_SDK_ROOT" \
    -DANDROID_NDK="$ANDROID_NDK_ROOT"

cmake --build "$BUILD_DIR" --target apk -j"$JOBS"

echo
echo "APK:"
find "$BUILD_DIR" -name '*.apk' -print
dump_apk_and_maybe_install || true
