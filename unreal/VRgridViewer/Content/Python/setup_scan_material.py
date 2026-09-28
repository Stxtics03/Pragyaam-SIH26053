"""The scan material: a sweep line that travels ACROSS an object's body.

The first version drove one colour for the whole mesh, so an object blinked red
as the sweep reached it. That reads as a state change, not as a measurement.

This one evaluates PER PIXEL. Each point on the car or the person works out how
far it is from the vehicle, compares that to where the sweep edge is right now,
and lights only if the edge is passing through it. The red band therefore moves
over the bodywork at the same speed as the rectangle on the road, because both
are reading the same number -- the front bumper lights before the rear one.

Distance is CHEBYSHEV, max(|dx|,|dy|), because the sweep edge and the accuracy
bands are squares. Measuring radially lights the sides of the scene early.

Parameters driven from C++ each frame:

    EgoXY        vehicle position, world centimetres (R,G used)
    SweepR       sweep edge half-width right now, centimetres
    SweepWidth   thickness of the lit band, centimetres
    Tint         colour at the sweep edge
    RestTint     colour away from it (the object's accuracy band)
    PeakOpacity  opacity at the edge
    RestOpacity  opacity away from it
"""

import unreal

PACKAGE_PATH = "/Game/VRgrid"
NAME = "M_VrgScan"

_mel = unreal.MaterialEditingLibrary
_assets = unreal.AssetToolsHelpers.get_asset_tools()


def _expr(mat, cls, x, y):
    return _mel.create_material_expression(mat, cls, x, y)


def _mask(mat, src, x, y, r=False, g=False, b=False, a=False):
    m = _expr(mat, unreal.MaterialExpressionComponentMask, x, y)
    m.set_editor_property("r", r)
    m.set_editor_property("g", g)
    m.set_editor_property("b", b)
    m.set_editor_property("a", a)
    _mel.connect_material_expressions(src, "", m, "")
    return m


def build():
    path = f"{PACKAGE_PATH}/{NAME}"
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        unreal.EditorAssetLibrary.delete_asset(path)

    mat = _assets.create_asset(NAME, PACKAGE_PATH, unreal.Material,
                               unreal.MaterialFactoryNew())
    mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    mat.set_editor_property("two_sided", True)
    for flag in ("used_with_skeletal_mesh", "used_with_instanced_static_meshes"):
        try:
            mat.set_editor_property(flag, True)
        except Exception as exc:  # noqa: BLE001
            unreal.log_warning(f"VRgrid: could not set {flag}: {exc}")

    # --- where am I, relative to the car ---------------------------------
    wp = _expr(mat, unreal.MaterialExpressionWorldPosition, -1500, 0)
    wp_xy = _mask(mat, wp, -1320, 0, r=True, g=True)

    ego = _expr(mat, unreal.MaterialExpressionVectorParameter, -1500, 200)
    ego.set_editor_property("parameter_name", "EgoXY")
    ego_xy = _mask(mat, ego, -1320, 200, r=True, g=True)

    delta = _expr(mat, unreal.MaterialExpressionSubtract, -1140, 100)
    _mel.connect_material_expressions(wp_xy, "", delta, "A")
    _mel.connect_material_expressions(ego_xy, "", delta, "B")

    dabs = _expr(mat, unreal.MaterialExpressionAbs, -980, 100)
    _mel.connect_material_expressions(delta, "", dabs, "")

    dx = _mask(mat, dabs, -830, 40, r=True)
    dy = _mask(mat, dabs, -830, 160, g=True)

    # Chebyshev: the square's half-width that just touches this point.
    cheb = _expr(mat, unreal.MaterialExpressionMax, -670, 100)
    _mel.connect_material_expressions(dx, "", cheb, "A")
    _mel.connect_material_expressions(dy, "", cheb, "B")

    # --- how far is that from the sweep edge ------------------------------
    sweep_r = _expr(mat, unreal.MaterialExpressionScalarParameter, -670, 260)
    sweep_r.set_editor_property("parameter_name", "SweepR")
    sweep_r.set_editor_property("default_value", 0.0)

    diff = _expr(mat, unreal.MaterialExpressionSubtract, -510, 140)
    _mel.connect_material_expressions(cheb, "", diff, "A")
    _mel.connect_material_expressions(sweep_r, "", diff, "B")

    diff_abs = _expr(mat, unreal.MaterialExpressionAbs, -370, 140)
    _mel.connect_material_expressions(diff, "", diff_abs, "")

    width = _expr(mat, unreal.MaterialExpressionScalarParameter, -370, 300)
    width.set_editor_property("parameter_name", "SweepWidth")
    width.set_editor_property("default_value", 250.0)

    norm = _expr(mat, unreal.MaterialExpressionDivide, -230, 200)
    _mel.connect_material_expressions(diff_abs, "", norm, "A")
    _mel.connect_material_expressions(width, "", norm, "B")

    clamped = _expr(mat, unreal.MaterialExpressionClamp, -110, 200)
    _mel.connect_material_expressions(norm, "", clamped, "")

    # 1 at the edge, falling to 0 away from it.
    hit = _expr(mat, unreal.MaterialExpressionOneMinus, 20, 200)
    _mel.connect_material_expressions(clamped, "", hit, "")

    # --- colour and opacity ------------------------------------------------
    tint = _expr(mat, unreal.MaterialExpressionVectorParameter, -230, 420)
    tint.set_editor_property("parameter_name", "Tint")
    tint.set_editor_property("default_value", unreal.LinearColor(1.0, 0.1, 0.1, 1.0))

    rest_tint = _expr(mat, unreal.MaterialExpressionVectorParameter, -230, 560)
    rest_tint.set_editor_property("parameter_name", "RestTint")
    rest_tint.set_editor_property("default_value", unreal.LinearColor(0.0, 0.62, 0.45, 1.0))

    col = _expr(mat, unreal.MaterialExpressionLinearInterpolate, 180, 480)
    _mel.connect_material_expressions(rest_tint, "", col, "A")
    _mel.connect_material_expressions(tint, "", col, "B")
    _mel.connect_material_expressions(hit, "", col, "Alpha")
    _mel.connect_material_property(col, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)

    peak_o = _expr(mat, unreal.MaterialExpressionScalarParameter, -230, 700)
    peak_o.set_editor_property("parameter_name", "PeakOpacity")
    peak_o.set_editor_property("default_value", 0.9)

    rest_o = _expr(mat, unreal.MaterialExpressionScalarParameter, -230, 820)
    rest_o.set_editor_property("parameter_name", "RestOpacity")
    rest_o.set_editor_property("default_value", 0.16)

    opac = _expr(mat, unreal.MaterialExpressionLinearInterpolate, 180, 760)
    _mel.connect_material_expressions(rest_o, "", opac, "A")
    _mel.connect_material_expressions(peak_o, "", opac, "B")
    _mel.connect_material_expressions(hit, "", opac, "Alpha")
    _mel.connect_material_property(opac, "", unreal.MaterialProperty.MP_OPACITY)

    _mel.recompile_material(mat)
    unreal.EditorAssetLibrary.save_asset(path)
    unreal.log(f"VRgrid: wrote {path} (per-pixel sweep)")


build()
