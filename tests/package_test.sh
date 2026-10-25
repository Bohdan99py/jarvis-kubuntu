#!/bin/bash
# Install, upgrade, downgrade (rollback) and remove the .deb in a throw-away root — never the
# real system: dpkg --root under fakeroot, maintainer scripts outside a chroot with the system
# commands they call replaced by stubs that only log their arguments.
#
#   tests/package_test.sh NEW.deb [OLD.deb]
#
# Checks: the new package installs on its own; over the old one it upgrades (version, the widgets'
# and the visual module's files are the new ones, nothing of the old one is left that the new one
# does not ship); the maintainer scripts only enable the user unit globally and refresh the menu and
# icon caches (no layout, no plasmashell, no sudo); going back to the old one works; remove leaves
# no Jarvis file. It does not touch user data or the desktop layout: the package never does.
set -euo pipefail
NEW=$(realpath "$1"); OLD=${2:+$(realpath "$2")}
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
R=$T/root; LOG=$T/calls.log
mkdir -p "$R/var/lib/dpkg/info" "$R/var/lib/dpkg/updates" "$T/bin"
: > "$R/var/lib/dpkg/status"; : > "$R/var/lib/dpkg/available"; : > "$LOG"
for c in systemctl update-desktop-database gtk-update-icon-cache; do
    printf '#!/bin/sh\necho "%s $*" >> "%s"\n' "$c" "$LOG" > "$T/bin/$c"; chmod +x "$T/bin/$c"
done
# (The empty root has none of the dependencies: dpkg's complaints about them are expected and hidden.)
dpkg_() {
    PATH="$T/bin:$PATH" fakeroot dpkg --root="$R" --force-not-root --force-script-chrootless --force-depends --log=/dev/null "$@" \
        >"$T/dpkg.out" 2>&1 || { cat "$T/dpkg.out"; fail "dpkg $*"; }
}
version() { dpkg-query --admindir="$R/var/lib/dpkg" -W -f='${Version}' jarvis; }
files() { dpkg-query --admindir="$R/var/lib/dpkg" -L jarvis | sort; }
fail() { echo "FAIL: $*"; exit 1; }
new_version=$(dpkg-deb -f "$NEW" Version)

check_new() {
    [ "$(version)" = "$new_version" ] || fail "installed $(version), expected $new_version"
    for f in usr/bin/jarvis usr/bin/jarvisd usr/share/plasma/plasmoids/org.jarvis.dock/contents/ui/PanelGlass.qml \
             usr/share/plasma/plasmoids/org.jarvis.dock/metadata.json usr/share/systemd/user/jarvis.service; do
        [ -e "$R/$f" ] || fail "missing /$f"
    done
    grep -q "\"Version\": \"$new_version\"" "$R/usr/share/plasma/plasmoids/org.jarvis.dock/metadata.json" || fail "the dock's metadata is not $new_version"
    # Exactly the new package's files on disk (nothing left from the old one).
    diff <(dpkg-deb -c "$NEW" | awk '{print $6}' | sed 's|^\./|/|; s|/$||' | grep -v '^/$' | sort) <(files | grep -v '^/\.$') >/dev/null \
        || fail "the installed files differ from the package's"
    grep -q "systemctl --global enable jarvis.service" "$LOG" || fail "postinst did not enable the user unit"
    if grep -v -e '^systemctl --global enable jarvis.service$' -e '^systemctl --global disable jarvis.service$' \
               -e '^update-desktop-database ' -e '^gtk-update-icon-cache ' "$LOG"; then
        fail "a maintainer script called something else (above)"
    fi
}

dpkg_ -i "$NEW"; check_new
echo "ok: $new_version installs on its own"
dpkg_ -r jarvis
[ -z "$(find "$R/usr" -path '*jarvis*' -not -type d 2>/dev/null)" ] || fail "remove left files: $(find "$R/usr" -path '*jarvis*' -not -type d | head)"
grep -q "systemctl --global disable jarvis.service" "$LOG" || fail "prerm did not disable the user unit"
echo "ok: remove leaves no Jarvis file and disables the user unit"
if [ -n "$OLD" ]; then
    old_version=$(dpkg-deb -f "$OLD" Version)
    : > "$LOG"
    dpkg_ -i "$OLD"; [ "$(version)" = "$old_version" ] || fail "the old package did not install"
    dpkg_ -i "$NEW"; check_new
    echo "ok: upgrade $old_version → $new_version: the new files only, scripts as expected"
    dpkg_ -i "$OLD"; [ "$(version)" = "$old_version" ] || fail "going back to $old_version failed"
    grep -q "\"Version\": \"$old_version\"" "$R/usr/share/plasma/plasmoids/org.jarvis.dock/metadata.json" || fail "the old dock did not come back"
    # Exactly the old package's files: nothing of the new one stays.
    diff <(dpkg-deb -c "$OLD" | awk '{print $6}' | sed 's|^\./|/|; s|/$||' | grep -v '^/$' | sort) <(files | grep -v '^/\.$') >/dev/null \
        || fail "a file of $new_version stayed after going back"
    echo "ok: rollback $new_version → $old_version: the old files are back"
fi
echo "PASS: the package installs, upgrades, rolls back and removes cleanly (in $R, not the system)"
