"""Creates the custom game fire material (Docs/CustomGamePlayground.md 12.1).

Run with: UnrealEditor-Cmd.exe ProjectDFSD.uproject -run=pythonscript -script=<this file>

Unlit and masked (it draws in the opaque pass, which works everywhere, including scene captures; additive
translucency did not draw there). One vector parameter "Color" drives the emissive output (HDR allowed). The tip of
each flame is cut back on the mesh's V coordinate (parameter TipFade), so flames end above the cone point instead of in a
hard cone point.
"""
import unreal

PATH = "/Game/MyProject/Playground"
NAME = "M_PlaygroundFire"
full = f"{PATH}/{NAME}"

if unreal.EditorAssetLibrary.does_asset_exist(full):
    unreal.EditorAssetLibrary.delete_asset(full)

tools = unreal.AssetToolsHelpers.get_asset_tools()
mat = tools.create_asset(NAME, PATH, unreal.Material, unreal.MaterialFactoryNew())
mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_MASKED)
mat.set_editor_property("two_sided", True)
mat.set_editor_property("opacity_mask_clip_value", 0.5)

lib = unreal.MaterialEditingLibrary
color = lib.create_material_expression(mat, unreal.MaterialExpressionVectorParameter, -700, 0)
color.set_editor_property("parameter_name", "Color")
color.set_editor_property("default_value", unreal.LinearColor(6.0, 2.0, 0.4, 1.0))
lib.connect_material_property(color, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)

# Opacity: 1 - V^2 * TipFade (solid at the base, cut towards the tip at the 0.5 clip).
uv = lib.create_material_expression(mat, unreal.MaterialExpressionTextureCoordinate, -1100, 250)
mask = lib.create_material_expression(mat, unreal.MaterialExpressionComponentMask, -900, 250)
mask.set_editor_property("r", False)
mask.set_editor_property("g", True)
lib.connect_material_expressions(uv, "", mask, "")
sq = lib.create_material_expression(mat, unreal.MaterialExpressionMultiply, -700, 250)
lib.connect_material_expressions(mask, "", sq, "A")
lib.connect_material_expressions(mask, "", sq, "B")
# TipFade (0..1): 1 on flames, 0 on the round ember bed so the floor glow is never cut.
fade = lib.create_material_expression(mat, unreal.MaterialExpressionScalarParameter, -900, 450)
fade.set_editor_property("parameter_name", "TipFade")
fade.set_editor_property("default_value", 1.0)
scaled = lib.create_material_expression(mat, unreal.MaterialExpressionMultiply, -620, 320)
lib.connect_material_expressions(sq, "", scaled, "A")
lib.connect_material_expressions(fade, "", scaled, "B")
inv = lib.create_material_expression(mat, unreal.MaterialExpressionOneMinus, -450, 250)
lib.connect_material_expressions(scaled, "", inv, "")
# Soft tip: a plain mask cut on (1 - V^2) (clip 0.5) keeps the lower part of each cone.

lib.connect_material_property(inv, "", unreal.MaterialProperty.MP_OPACITY_MASK)

lib.recompile_material(mat)
unreal.EditorAssetLibrary.save_asset(full, only_if_is_dirty=False)
probe = f"{PATH}/M_PlaygroundFireOpaqueProbe"
if unreal.EditorAssetLibrary.does_asset_exist(probe):
    unreal.EditorAssetLibrary.delete_asset(probe)
unreal.log(f"Created {full}")
