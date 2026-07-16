{
  lib,
  stdenv,
  meson,
  ninja,
  pkg-config,
  sdl3,
  libGL ? null,
  imguiSource,
  implotSource,
}:

stdenv.mkDerivation {
  pname = "plotcore-light";
  version = "0.1.0";

  src = lib.cleanSource ../.;

  strictDeps = true;

  nativeBuildInputs = [
    meson
    ninja
    pkg-config
  ];

  buildInputs = [ sdl3 ] ++ lib.optional (!stdenv.hostPlatform.isWindows) libGL;

  postPatch = ''
    mkdir -p external
    ln -s ${imguiSource} external/imgui
    ln -s ${implotSource} external/implot
  '';

  mesonFlags = [
    "-Dimgui_source_dir=external/imgui"
    "-Dimplot_source_dir=external/implot"
  ];

  mesonAutoFeatures = "disabled";
  mesonWrapMode = "nofallback";

  doCheck = stdenv.buildPlatform.canExecute stdenv.hostPlatform;

  postInstall = ''
    install -Dm644 ${imguiSource}/LICENSE.txt \
      "$out/share/licenses/plotcore/imgui-LICENSE.txt"
    install -Dm644 ${implotSource}/LICENSE \
      "$out/share/licenses/plotcore/implot-LICENSE"
  '';

  meta = {
    description = "Minimal SDL3, Dear ImGui, ImPlot, and OpenGL GUI smoke target";
    mainProgram = "plotcore-light";
    platforms = [
      "x86_64-linux"
      "x86_64-windows"
    ];
  };
}
