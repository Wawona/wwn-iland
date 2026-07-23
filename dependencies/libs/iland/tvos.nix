# tvOS: non-GL / non-IOKit stub (platform-targets matrix).
# Not pulled when weston enableIlandDrm=false; present so accidental builds
# never import the iOS ANGLE recipe.
{ lib, pkgs, ... }:

pkgs.runCommand "iland-userland-tvos-stub-0.1.0" {
  nativeBuildInputs = [ pkgs.binutils ];
} ''
  mkdir -p "$out/lib" "$out/include" "$out/nix-support"
  # Empty static archive — no ANGLE, no IOKit symbols.
  ar rcs "$out/lib/libiland_userland.a"
  echo stub > "$out/nix-support/link-kind"
  cat > "$out/include/iland_stub.h" <<'EOF'
/* tvOS iland stub — no GPU userland. */
EOF
''
