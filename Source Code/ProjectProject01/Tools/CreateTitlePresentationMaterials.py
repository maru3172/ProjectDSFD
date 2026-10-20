"""Create title-only materials; preserve authored graphs and repair required usage only."""
import unreal

tools = unreal.AssetToolsHelpers.get_asset_tools()
edit = unreal.MaterialEditingLibrary
root = "/Game/MyProject/Title"
unreal.EditorAssetLibrary.make_directory(root)


def material(name, translucent=False):
    path = root + "/" + name
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        if name == "M_TitleSurface":
            existing = unreal.EditorAssetLibrary.load_asset(path)
            if not existing.get_editor_property("used_with_skeletal_mesh"):
                existing.set_editor_property("used_with_skeletal_mesh", True)
                edit.recompile_material(existing)
                unreal.EditorAssetLibrary.save_loaded_asset(existing)
                unreal.log("[TitleMaterials] Repaired skeletal-mesh usage without altering the material graph")
        unreal.log("[TitleMaterials] Preserved " + path)
        return None
    m = tools.create_asset(name, root, unreal.Material, unreal.MaterialFactoryNew())
    if translucent:
        m.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
        m.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
        m.set_editor_property("two_sided", True)
    return m


def node(m, cls, **properties):
    n = edit.create_material_expression(m, cls)
    for key, value in properties.items():
        n.set_editor_property(key, value)
    return n


def link(a, b, pin):
    if not edit.connect_material_expressions(a, "", b, pin):
        raise RuntimeError("Material connection failed: " + pin)


def save(m):
    edit.layout_material_expressions(m)
    edit.recompile_material(m)
    unreal.EditorAssetLibrary.save_loaded_asset(m)
    unreal.log("[TitleMaterials] Created " + m.get_path_name())


m = material("M_TitleSurface")
if m:
    m.set_editor_property("used_with_skeletal_mesh", True)
    color = node(m, unreal.MaterialExpressionVectorParameter, parameter_name="Color",
                 default_value=unreal.LinearColor(0.15, 0.16, 0.17, 1))
    rough = node(m, unreal.MaterialExpressionScalarParameter, parameter_name="Roughness", default_value=0.4)
    metal = node(m, unreal.MaterialExpressionScalarParameter, parameter_name="Metallic", default_value=0.0)
    edit.connect_material_property(color, "", unreal.MaterialProperty.MP_BASE_COLOR)
    edit.connect_material_property(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)
    edit.connect_material_property(metal, "", unreal.MaterialProperty.MP_METALLIC)
    save(m)

for name, mist in [("M_TitleMist", True), ("M_TitleDust", False)]:
    m = material(name, True)
    if not m:
        continue
    color = node(m, unreal.MaterialExpressionVectorParameter, parameter_name="Color",
                 default_value=unreal.LinearColor(0.16, 0.19, 0.22, 1))
    opacity = node(m, unreal.MaterialExpressionScalarParameter, parameter_name="Opacity", default_value=0.1)
    uv = node(m, unreal.MaterialExpressionTextureCoordinate)
    center = node(m, unreal.MaterialExpressionConstant2Vector, r=0.5, g=0.5)
    mask = node(m, unreal.MaterialExpressionSphereMask, attenuation_radius=0.49, hardness_percent=0.0)
    link(uv, mask, "A")
    link(center, mask, "B")
    shape = mask
    if mist:
        pan = node(m, unreal.MaterialExpressionPanner, speed_x=0.018, speed_y=-0.006)
        link(uv, pan, "Coordinate")
        z = node(m, unreal.MaterialExpressionConstant, r=0.0)
        xyz = node(m, unreal.MaterialExpressionAppendVector)
        link(pan, xyz, "A")
        link(z, xyz, "B")
        noise = node(m, unreal.MaterialExpressionNoise, scale=4.2, levels=2, quality=1,
                     noise_function=unreal.NoiseFunction.NOISEFUNCTION_GRADIENT_ALU,
                     output_min=0.0, output_max=1.0)
        noise_inputs = edit.get_material_expression_input_names(noise)
        link(xyz, noise, str(noise_inputs[0]))
        shape = node(m, unreal.MaterialExpressionMultiply)
        link(mask, shape, "A")
        link(noise, shape, "B")
    alpha = node(m, unreal.MaterialExpressionMultiply)
    link(shape, alpha, "A")
    link(opacity, alpha, "B")
    edit.connect_material_property(color, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    edit.connect_material_property(alpha, "", unreal.MaterialProperty.MP_OPACITY)
    save(m)

unreal.log("[TitleMaterials] Finished without editing gameplay/VFX materials or maps.")
