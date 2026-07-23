# watchOS: non-GL / non-IOKit stub (platform-targets matrix).
# Not pulled when weston enableIlandDrm=false; present so accidental builds
# never import the iOS ANGLE recipe.
{ lib, pkgs, ... }:

pkgs.runCommand "iland-userland-watchos-stub-0.1.0" {
  nativeBuildInputs = [ pkgs.binutils ];
} ''
  mkdir -p "$out/lib" "$out/include" "$out/nix-support"
  ar rcs "$out/lib/libiland_userland.a"
  echo stub > "$out/nix-support/link-kind"
  cat > "$out/include/iland_stub.h" <<'EOF'
/* watchOS iland stub — no GPU userland. */
EOF
''
