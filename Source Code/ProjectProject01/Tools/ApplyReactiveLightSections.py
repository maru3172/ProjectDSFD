"""Add partial diffuser discharge to the existing connected lamp materials; preserve their authored graph."""
import os
import shutil
import unreal

edit = unreal.MaterialEditingLibrary
project = os.path.abspath(unreal.Paths.project_dir())
backup = os.path.join(project, "Saved", "ReactiveLightQA", "Backups")
os.makedirs(backup, exist_ok=True)

for path in ["/Game/MyProject/Lighting/M_ReactiveBulb", "/Game/MyProject/Testing/ReactiveLighting/M_TestLamp"]:
    material = unreal.load_asset(path)
    if not material:
        raise RuntimeError("Missing connected bulb material: " + path)
    if any(isinstance(n, unreal.MaterialExpressionVectorParameter) and str(n.get_editor_property("parameter_name")) == "LampSections"
           for n in edit.get_material_expressions(material)):
        unreal.log("[LampSections] Preserved existing section graph: " + path)
        continue
    relative = path.removeprefix("/Game/") + ".uasset"
    destination = os.path.join(backup, os.path.basename(relative).replace(".uasset", "_BeforeSections.uasset"))
    if not os.path.exists(destination):
        shutil.copy2(os.path.join(project, "Content", *relative.split("/")), destination)
    previous = edit.get_material_property_input_node(material, unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    previous_output = edit.get_material_property_input_node_output_name(material, unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    if not previous:
        raise RuntimeError("Refusing to replace an unconnected emissive graph: " + path)
    levels = edit.create_material_expression(material, unreal.MaterialExpressionVectorParameter)
    levels.set_editor_property("parameter_name", "LampSections")
    levels.set_editor_property("default_value", unreal.LinearColor(1, 1, 1, 1))
    position = edit.create_material_expression(material, unreal.MaterialExpressionLocalPosition)
    mask = edit.create_material_expression(material, unreal.MaterialExpressionCustom)
    mask.set_editor_property("description", "Reactive diffuser partial discharge")
    mask.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT1)
    inputs = []
    for name in ["Position", "Levels"]:
        item = unreal.CustomInput()
        item.set_editor_property("input_name", name)
        inputs.append(item)
    mask.set_editor_property("inputs", inputs)
    # The existing diffuser is a scaled 100cm engine cube: local X remains stable on all faces.
    # No Time/noise textures: the actor supplies frozen values while offscreen, with no shader animation clock.
    mask.set_editor_property("code", "float x=saturate((Position.x+50.0)/100.0);\nreturn lerp(lerp(Levels.r,Levels.g,smoothstep(0.32,0.35,x)),Levels.b,smoothstep(0.64,0.67,x));")
    result = edit.create_material_expression(material, unreal.MaterialExpressionMultiply)
    for source, output, target, pin in [(position, "", mask, "Position"), (levels, "", mask, "Levels"),
                                        (previous, previous_output, result, "A"), (mask, "", result, "B")]:
        if not edit.connect_material_expressions(source, output, target, pin):
            raise RuntimeError("Cannot connect diffuser section graph: " + pin)
    if not edit.connect_material_property(result, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR):
        raise RuntimeError("Cannot connect section emission")
    edit.layout_material_expressions(material)
    edit.recompile_material(material)
    if not unreal.EditorAssetLibrary.save_loaded_asset(material, only_if_is_dirty=False):
        raise RuntimeError("Cannot save " + path)
    unreal.log("[LampSections] Added section discharge without replacing existing color/emissive graph: " + path)
