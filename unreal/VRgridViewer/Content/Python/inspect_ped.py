"""What material slots does the mannequin expose?

Decides whether pedestrians can have skin AND clothing as separate colours, or
whether each person is one tint.
"""

import unreal

for path in ("/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple",
             "/Game/Characters/Mannequins/Meshes/SKM_Quinn_Simple"):
    mesh = unreal.EditorAssetLibrary.load_asset(path)
    if mesh is None:
        unreal.log_error(f"PEDSLOT {path}: MISSING")
        continue
    mats = mesh.get_editor_property("materials")
    unreal.log(f"PEDSLOT {path}: {len(mats)} slots")
    for i, m in enumerate(mats):
        try:
            name = m.get_editor_property("material_slot_name")
            mi = m.get_editor_property("material_interface")
            unreal.log(f"PEDSLOT   [{i}] slot='{name}' material='{mi.get_name() if mi else None}'")
        except Exception as exc:  # noqa: BLE001
            unreal.log(f"PEDSLOT   [{i}] <{exc}>")

unreal.log("PEDSLOT: done")
