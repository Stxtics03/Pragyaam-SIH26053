"""Generate the two materials the viewer needs. Run ONCE, in the editor.

    "C:\\Program Files\\Epic Games\\UE_5.8\\Engine\\Binaries\\Win64\\UnrealEditor-Cmd.exe" ^
        "<repo>\\VRgridViewer\\VRgridViewer.uproject" ^
        -run=pythonscript -script="<repo>\\VRgridViewer\\Content\\Python\\setup_assets.py"

or, from an open editor: Tools -> Execute Python Script.

WHY THIS IS A SCRIPT AND NOT C++. Per-instance colour on an instanced static
mesh comes from `PerInstanceCustomData`, which a material graph has to read.
A material graph is an asset, not code, and it cannot be compiled at runtime in
a packaged game -- so it has to be authored once, and authoring it by hand in
the editor is a dozen mouse actions nobody should have to repeat. This is those
actions, written down.

Until it has run, the viewer still works: `AVrgSceneActor::MakeLayer` falls back
to `BasicShapeMaterial`, so every layer renders in flat grey. Geometry, cell
sizes, foveation, the ghost toggle and playback are all correct in that state --
only the colour is missing.

UNLIT, on purpose. This is a data view, not a scene. Under a lit material the
height ramp would be multiplied by whatever the sky light happens to be doing,
and two cells with the same elevation would read as different colours because
one faces away from the sun. Emissive-unlit means the colour on screen IS the
colour the exporter wrote, which is the only way the Unreal window and the
Rerun window can be compared by eye.
"""

import pathlib

import unreal

PACKAGE_PATH = "/Game/VRgrid"
OPAQUE_NAME = "M_VrgInstanced"
LIT_NAME = "M_VrgInstancedLit"
PED_NAME = "M_VrgPed"
SCAN_NAME = "M_VrgScan"
BAND_NAME = "M_VrgBand"
TRANSLUCENT_NAME = "M_VrgInstancedTranslucent"

_mel = unreal.MaterialEditingLibrary
_assets = unreal.AssetToolsHelpers.get_asset_tools()


def _custom_data(material, index, x, y):
    """One PerInstanceCustomData float. Index 0-2 are RGB, 3 is alpha --
    the order `FVrgFrameReader::ColorToCustomData` writes them in."""
    node = _mel.create_material_expression(
        material, unreal.MaterialExpressionPerInstanceCustomData, x, y)
    node.set_editor_property("data_index", index)
    # A cell that somehow arrives without custom data shows as mid grey rather
    # than black, so "the material is not getting data" is visibly different
    # from "this cell is dark".
    node.set_editor_property("const_default_value", 0.5)
    return node


def _build_lit(name):
    """A LIT instanced material whose base colour is per-instance custom data.

    The map viewer wants unlit -- the colour on screen must be the colour the
    exporter wrote. The city sim wants the opposite: asphalt, kerbs and facades
    have to take the streetlights and the car's headlights, or the street reads
    as flat coloured paper. Same custom-data plumbing, different output pin.
    """
    path = f"{PACKAGE_PATH}/{name}"
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        unreal.EditorAssetLibrary.delete_asset(path)

    material = _assets.create_asset(name, PACKAGE_PATH, unreal.Material,
                                    unreal.MaterialFactoryNew())
    material.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_DEFAULT_LIT)
    material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_OPAQUE)
    for flag in ("used_with_instanced_static_meshes", "used_with_static_lighting"):
        try:
            material.set_editor_property(flag, True)
        except Exception as exc:  # noqa: BLE001
            unreal.log_warning(f"VRgrid: could not set {flag}: {exc}")

    r = _custom_data(material, 0, -700, -100)
    g = _custom_data(material, 1, -700, 0)
    b = _custom_data(material, 2, -700, 100)
    rg = _mel.create_material_expression(
        material, unreal.MaterialExpressionAppendVector, -450, -50)
    _mel.connect_material_expressions(r, "", rg, "A")
    _mel.connect_material_expressions(g, "", rg, "B")
    rgb = _mel.create_material_expression(
        material, unreal.MaterialExpressionAppendVector, -250, 0)
    _mel.connect_material_expressions(rg, "", rgb, "A")
    _mel.connect_material_expressions(b, "", rgb, "B")
    _mel.connect_material_property(rgb, "", unreal.MaterialProperty.MP_BASE_COLOR)

    # Custom data 3 carries ROUGHNESS here rather than alpha: wet asphalt and a
    # concrete kerb are the same grey and read completely differently, and it
    # costs nothing to send it per instance.
    rough = _custom_data(material, 3, -700, 220)
    _mel.connect_material_property(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)

    _mel.recompile_material(material)
    unreal.EditorAssetLibrary.save_asset(path)
    unreal.log(f"VRgrid: wrote {path}")
    return material


def _build_ped():
    """A plain lit material with a Tint parameter, for characters.

    The engine ships no clothed human -- only the UE mannequin -- so the best
    available read at street distance is per-person colour: a skin tone on the
    head and hands slot, a clothing tone on the body. Overriding the mesh's own
    materials also sidesteps Manny's, which reference MI_Manny_01 while the
    engine only ships MI_Manny_01_New.
    """
    path = f"{PACKAGE_PATH}/{PED_NAME}"
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        unreal.EditorAssetLibrary.delete_asset(path)

    material = _assets.create_asset(PED_NAME, PACKAGE_PATH, unreal.Material,
                                    unreal.MaterialFactoryNew())
    material.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_DEFAULT_LIT)
    material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_OPAQUE)
    try:
        material.set_editor_property("used_with_skeletal_mesh", True)
    except Exception as exc:  # noqa: BLE001
        unreal.log_warning(f"VRgrid: could not set used_with_skeletal_mesh: {exc}")

    tint = _mel.create_material_expression(
        material, unreal.MaterialExpressionVectorParameter, -420, 0)
    tint.set_editor_property("parameter_name", "Tint")
    tint.set_editor_property("default_value", unreal.LinearColor(0.35, 0.35, 0.38, 1.0))
    _mel.connect_material_property(tint, "", unreal.MaterialProperty.MP_BASE_COLOR)

    rough = _mel.create_material_expression(
        material, unreal.MaterialExpressionScalarParameter, -420, 200)
    rough.set_editor_property("parameter_name", "Rough")
    rough.set_editor_property("default_value", 0.72)
    _mel.connect_material_property(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)

    _mel.recompile_material(material)
    unreal.EditorAssetLibrary.save_asset(path)
    unreal.log(f"VRgrid: wrote {path}")
    return material


def _build_scan():
    """The scan shell -- built by `setup_scan_material.py`, NOT here.

    ⚑ THIS FUNCTION USED TO WRITE ITS OWN M_VrgScan, and that is a trap.
      The real material is a per-pixel sweep: it takes EgoXY, SweepR,
      SweepWidth, RestTint and PeakOpacity/RestOpacity so the red edge travels
      ACROSS a car's bodywork instead of the whole object blinking. The version
      that lived here had only Tint and Opacity, with Opacity defaulting to
      zero -- so regenerating the materials to add an unrelated one silently
      replaced the sweep with a fully transparent stub and the scan highlight
      vanished from the demo. Same asset path, same name, no error.

      Delegating means there is one definition of M_VrgScan and
      `demo.ps1 materials` cannot undo the sweep again.
    """
    import importlib.util
    here = pathlib.Path(__file__).parent / "setup_scan_material.py"
    spec = importlib.util.spec_from_file_location("vrg_setup_scan", here)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module.build()


def _build(name, translucent):
    path = f"{PACKAGE_PATH}/{name}"
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        unreal.EditorAssetLibrary.delete_asset(path)

    material = _assets.create_asset(name, PACKAGE_PATH, unreal.Material,
                                    unreal.MaterialFactoryNew())

    material.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    material.set_editor_property(
        "blend_mode",
        unreal.BlendMode.BLEND_TRANSLUCENT if translucent else unreal.BlendMode.BLEND_OPAQUE)
    # Two-sided: a cell is a thin box and the camera does get inside one.
    material.set_editor_property("two_sided", True)

    # ⚑ THE USAGE FLAG. Without it a material cannot compile for the instanced
    #   static-mesh vertex factory, and at runtime -- where nothing can be
    #   recompiled -- UE silently substitutes the DEFAULT LIT material. On a
    #   map with no lights that draws pure black, so every layer vanishes while
    #   the instance counts, the camera and the view target all read healthy.
    #   The editor sets this automatically the first time a material is dropped
    #   on an ISM by hand, which is exactly why it is easy to miss in a script.
    for flag in ("used_with_instanced_static_meshes", "used_with_static_lighting"):
        try:
            material.set_editor_property(flag, True)
        except Exception as exc:  # noqa: BLE001
            unreal.log_warning(f"VRgrid: could not set {flag}: {exc}")

    r = _custom_data(material, 0, -700, -100)
    g = _custom_data(material, 1, -700, 0)
    b = _custom_data(material, 2, -700, 100)

    rg = _mel.create_material_expression(
        material, unreal.MaterialExpressionAppendVector, -450, -50)
    _mel.connect_material_expressions(r, "", rg, "A")
    _mel.connect_material_expressions(g, "", rg, "B")

    rgb = _mel.create_material_expression(
        material, unreal.MaterialExpressionAppendVector, -250, 0)
    _mel.connect_material_expressions(rg, "", rgb, "A")
    _mel.connect_material_expressions(b, "", rgb, "B")

    _mel.connect_material_property(rgb, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)

    if translucent:
        a = _custom_data(material, 3, -700, 220)
        _mel.connect_material_property(a, "", unreal.MaterialProperty.MP_OPACITY)

    _mel.recompile_material(material)
    unreal.EditorAssetLibrary.save_asset(path)
    unreal.log(f"VRgrid: wrote {path}")
    return material


def _build_band():
    """The accuracy bands, as ONE translucent surface driven by vertex colour.

    The bands used to be a row of instanced boxes, and every box took its
    height from the terrain under its own centre. Neighbours therefore sat at
    slightly different heights, and the road showed through the step between
    them -- the "rectangle slabs" inside the scan. A single lattice whose
    vertices are shared cannot step, so the surface is continuous and the
    overlap that used to double the alpha is gone too.

    Colour and opacity come from VERTEX COLOUR rather than per-instance custom
    data, because a procedural mesh has no instances. Unlit, for the same
    reason the map layers are: the band colour IS the accuracy reading, and the
    sun must not multiply it.
    """
    path = f"{PACKAGE_PATH}/{BAND_NAME}"
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        unreal.EditorAssetLibrary.delete_asset(path)

    material = _assets.create_asset(BAND_NAME, PACKAGE_PATH, unreal.Material,
                                    unreal.MaterialFactoryNew())
    material.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    material.set_editor_property("two_sided", True)

    vc = _mel.create_material_expression(
        material, unreal.MaterialExpressionVertexColor, -420, 0)
    # The VertexColor node's pins are "", R, G, B, A -- there is no "RGB", and
    # naming one that does not exist connects NOTHING and only logs a warning,
    # which is how this shipped invisible the first time.
    if not _mel.connect_material_property(vc, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR):
        raise RuntimeError("VRgrid: vertex colour -> emissive did not connect")
    if not _mel.connect_material_property(vc, "A", unreal.MaterialProperty.MP_OPACITY):
        raise RuntimeError("VRgrid: vertex alpha -> opacity did not connect")

    _mel.recompile_material(material)
    unreal.EditorAssetLibrary.save_asset(path)
    unreal.log(f"VRgrid: wrote {path}")
    return material


def main():
    if not unreal.EditorAssetLibrary.does_directory_exist(PACKAGE_PATH):
        unreal.EditorAssetLibrary.make_directory(PACKAGE_PATH)

    _build(OPAQUE_NAME, translucent=False)
    _build(TRANSLUCENT_NAME, translucent=True)
    _build_lit(LIT_NAME)
    _build_ped()
    _build_scan()
    _build_band()

    unreal.log("VRgrid: materials ready. The viewer picks them up by path -- "
               "no level edit needed.")


if __name__ == "__main__":
    main()
