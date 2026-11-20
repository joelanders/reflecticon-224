#!/bin/sh
# Remove macOS's download quarantine from this plugin's bundles, so that
# Gatekeeper lets a host load them. The bundles are ad-hoc signed and not
# notarized: a browser or unzip tool marks them "quarantined", and hosts then
# refuse them ("damaged" / "cannot be opened") or never list them.
#
# Double-click this file in Finder, or run it in Terminal:
#     sh macsetup_remove_quarantine.command
#
# It clears extended attributes (xattr -cr) only on bundles whose names start
# with the product name, in:
#   - the folder this script is in (and below), e.g. the unzipped download;
#   - ~/Library/Audio/Plug-Ins/Components   (AU)
#   - ~/Library/Audio/Plug-Ins/VST3         (VST3)
#   - /Library/Audio/Plug-Ins/Components and /Library/Audio/Plug-Ins/VST3
#     (only if you installed there; may need sudo)
#   - /Applications and ~/Applications      (the standalone app)
# Nothing else is touched, and nothing is copied or installed.

# The product name: keep in step with PLUGIN_PRODUCT_NAME in CMakeLists.txt
# (scripts/run_gates.sh checks that they agree).
PRODUCT="Reflecticon224"

# This script itself may be quarantined too.
xattr -d com.apple.quarantine "$0" >/dev/null 2>&1 || true

here=$(cd "$(dirname "$0")" && pwd)
echo "Clearing quarantine for $PRODUCT bundles"
echo

found=0
for dir in "$here" \
           "$HOME/Library/Audio/Plug-Ins/Components" \
           "$HOME/Library/Audio/Plug-Ins/VST3" \
           "/Library/Audio/Plug-Ins/Components" \
           "/Library/Audio/Plug-Ins/VST3" \
           "/Applications" \
           "$HOME/Applications"; do
    if [ ! -d "$dir" ]; then
        continue
    fi
    depth=1
    if [ "$dir" = "$here" ]; then
        depth=3
    fi
    # Bundles only (.component, .vst3, .app), matched by product name.
    find "$dir" -maxdepth "$depth" -type d \
        \( -name "$PRODUCT*.component" -o -name "$PRODUCT*.vst3" -o -name "$PRODUCT*.app" \) \
        -prune -print 2>/dev/null > "${TMPDIR:-/tmp}/macsetup_$$.txt"
    while IFS= read -r bundle; do
        found=$((found + 1))
        if xattr -cr "$bundle" 2>/dev/null; then
            echo "  cleared: $bundle"
        else
            echo "  could not clear (try: sudo xattr -cr \"$bundle\"): $bundle"
        fi
    done < "${TMPDIR:-/tmp}/macsetup_$$.txt"
    rm -f "${TMPDIR:-/tmp}/macsetup_$$.txt"
done

echo
if [ "$found" -eq 0 ]; then
    echo "No $PRODUCT bundles found. Put them in ~/Library/Audio/Plug-Ins/Components (AU), ~/Library/Audio/Plug-Ins/VST3 (VST3) or /Applications (the app), then run this again."
else
    echo "Done ($found bundle(s)). Restart your host, or rescan its plug-ins."
fi
