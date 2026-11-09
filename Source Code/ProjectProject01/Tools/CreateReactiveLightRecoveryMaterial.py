"""Create the spotlight projection used during the last incomplete restart."""
import unreal

path = "/Game/MyProject/Lighting/M_ReactiveRecoveryProjection"
if not unreal.EditorAssetLibrary.does_asset_exist(path):
    material = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        "M_ReactiveRecoveryProjection", "/Game/MyProject/Lighting", unreal.Material, unreal.MaterialFactoryNew())
    material.set_editor_property("material_domain", unreal.MaterialDomain.MD_LIGHT_FUNCTION)
    edit = unreal.MaterialEditingLibrary
    uv = edit.create_material_expression(material, unreal.MaterialExpressionTextureCoordinate)
    recovery = edit.create_material_expression(material, unreal.MaterialExpressionVectorParameter)
    recovery.set_editor_property("parameter_name", "Recovery")
    recovery.set_editor_property("default_value", unreal.LinearColor(1, 0, 1, 0))
    mask = edit.create_material_expression(material, unreal.MaterialExpressionCustom)
    mask.set_editor_property("description", "One-sided light spreads across the existing cone")
    mask.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT1)
    inputs = []
    for name in ["UV", "DirectionAndSpread", "Weight"]:
        entry = unreal.CustomInput()
        entry.set_editor_property("input_name", name)
        inputs.append(entry)
    mask.set_editor_property("inputs", inputs)
    mask.set_editor_property("code", """
float side = dot(UV - 0.5, DirectionAndSpread.xy);
float edge = lerp(0.12, -0.65, DirectionAndSpread.z);
return lerp(1.0, lerp(0.025, 1.0, smoothstep(edge - 0.14, edge + 0.08, side)), Weight);
""")
    for source, output, pin in [(uv, "", "UV"), (recovery, "RGB", "DirectionAndSpread"), (recovery, "A", "Weight")]:
        if not edit.connect_material_expressions(source, output, mask, pin):
            raise RuntimeError("Cannot connect recovery projection: " + pin)
    if not edit.connect_material_property(mask, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR):
        raise RuntimeError("Cannot connect recovery projection output")
    edit.layout_material_expressions(material)
    edit.recompile_material(material)
    if not unreal.EditorAssetLibrary.save_loaded_asset(material, only_if_is_dirty=False):
        raise RuntimeError("Cannot save recovery projection")
unreal.log("[ReactiveRecovery] Projection material ready: " + path)
