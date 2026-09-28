"""Report what the generated materials actually contain.

An unlit material with nothing wired to Emissive Color renders PURE BLACK, and
looks identical from the outside to a misaimed camera or an empty scene. This
prints which it is.
"""

import unreal

PATHS = [
    "/Game/VRgrid/M_VrgInstanced",
    "/Game/VRgrid/M_VrgInstancedTranslucent",
]


def describe(path):
    mat = unreal.EditorAssetLibrary.load_asset(path)
    if mat is None:
        unreal.log_error(f"VERIFY {path}: MISSING")
        return

    unreal.log(f"VERIFY {path}:")
    unreal.log(f"   shading_model = {mat.get_editor_property('shading_model')}")
    unreal.log(f"   blend_mode    = {mat.get_editor_property('blend_mode')}")
    unreal.log(f"   two_sided     = {mat.get_editor_property('two_sided')}")
    # The flag that decides whether this material can render on an ISM at all.
    try:
        ism = mat.get_editor_property("used_with_instanced_static_meshes")
    except Exception as exc:  # noqa: BLE001
        ism = f"<{exc}>"
    unreal.log(f"   used_with_ISM = {ism}")

    for prop in (unreal.MaterialProperty.MP_EMISSIVE_COLOR,
                 unreal.MaterialProperty.MP_BASE_COLOR,
                 unreal.MaterialProperty.MP_OPACITY):
        try:
            node = unreal.MaterialEditingLibrary.get_material_property_input_node(mat, prop)
        except Exception as exc:                      # noqa: BLE001
            node = f"<query failed: {exc}>"
        unreal.log(f"   {prop} <- {node}")

    try:
        exprs = unreal.MaterialEditingLibrary.get_num_material_expressions(mat)
        unreal.log(f"   expressions   = {exprs}")
    except Exception as exc:                          # noqa: BLE001
        unreal.log(f"   expressions   = <{exc}>")


for p in PATHS:
    describe(p)

unreal.log("VERIFY: done")
