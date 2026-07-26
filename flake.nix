{
  description = "Native and Windows cross-build environment for rtktrace";

  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixpkgs-unstable";

  outputs =
    { nixpkgs, ... }:
    let
      system = "x86_64-linux";
      pkgs = import nixpkgs { inherit system; };
      windowsPkgs = pkgs.pkgsCross.mingwW64;

      imguiSource = pkgs.fetchFromGitHub {
        owner = "ocornut";
        repo = "imgui";
        tag = "v1.91.4";
        hash = "sha256-6j4keBOAzbBDsV0+R4zTNlsltxz2dJDGI43UIrHXDNM=";
      };

      implotSource = pkgs.fetchFromGitHub {
        owner = "epezent";
        repo = "implot";
        tag = "v0.17";
        hash = "sha256-HNzNRHPLr352EDkAci4nx5qQnPI308rGH8yHkF+n5OY=";
      };

      mkRtktrace = targetPkgs:
        targetPkgs.callPackage ./nix/package.nix {
          inherit imguiSource implotSource;
        };

      linuxPackage = mkRtktrace pkgs;
      windowsPackage = mkRtktrace windowsPkgs;

      windowsPkgConfig = pkgs.writeShellScript "rtktrace-windows-pkg-config" ''
        export PKG_CONFIG_LIBDIR="${pkgs.lib.getDev windowsPkgs.sdl3}/lib/pkgconfig"
        export PKG_CONFIG_PATH=
        exec ${pkgs.pkg-config}/bin/pkg-config "$@"
      '';

      targetPrefix = windowsPkgs.stdenv.cc.targetPrefix;
      windowsCrossFile = pkgs.writeText "rtktrace-windows-cross.ini" ''
        [binaries]
        c = '${windowsPkgs.stdenv.cc}/bin/${targetPrefix}gcc'
        cpp = '${windowsPkgs.stdenv.cc}/bin/${targetPrefix}g++'
        ar = '${windowsPkgs.stdenv.cc.bintools}/bin/${targetPrefix}ar'
        strip = '${windowsPkgs.stdenv.cc.bintools}/bin/${targetPrefix}strip'
        windres = '${windowsPkgs.stdenv.cc.bintools}/bin/${targetPrefix}windres'
        pkg-config = '${windowsPkgConfig}'

        [host_machine]
        system = 'windows'
        cpu_family = 'x86_64'
        cpu = 'x86_64'
        endian = 'little'

        [properties]
        needs_exe_wrapper = true
      '';
    in
    {
      packages.${system} = {
        default = linuxPackage;
        rtktrace-light-linux = linuxPackage;
        rtktrace-light-windows = windowsPackage;
        rtktrace-full-linux = linuxPackage;
        rtktrace-full-windows = windowsPackage;
      };

      devShells.${system} = {
        default = pkgs.mkShell {
          inputsFrom = [ linuxPackage ];
          packages = [ pkgs.file ];
          IMGUI_SOURCE_DIR = toString imguiSource;
          IMPLOT_SOURCE_DIR = toString implotSource;
        };

        linux = pkgs.mkShell {
          inputsFrom = [ linuxPackage ];
          packages = [ pkgs.file ];
          IMGUI_SOURCE_DIR = toString imguiSource;
          IMPLOT_SOURCE_DIR = toString implotSource;
        };

        windows = pkgs.mkShell {
          inputsFrom = [ windowsPackage ];
          packages = [
            pkgs.file
            windowsPkgs.stdenv.cc
            windowsPkgs.stdenv.cc.bintools
          ];
          IMGUI_SOURCE_DIR = toString imguiSource;
          IMPLOT_SOURCE_DIR = toString implotSource;
          RTKTRACE_MESON_CROSS_FILE = toString windowsCrossFile;
        };
      };

      checks.${system} = {
        linux-build = linuxPackage;
        linux-test = linuxPackage;
        windows-build = windowsPackage;
      };
    };
}
