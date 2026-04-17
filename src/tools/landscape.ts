import { z } from "zod";
import { categoryTool, bp, type ToolDef } from "../types.js";

export const landscapeTool: ToolDef = categoryTool(
  "landscape",
  "Landscape terrain: info, layers, sculpting, painting, materials, heightmap import.",
  {
    get_info:          bp("Get landscape setup", "get_landscape_info"),
    list_layers:       bp("List paint layers", "list_landscape_layers"),
    sample:            bp("Sample height/layers. Params: x, y", "sample_landscape"),
    list_splines:      bp("Read landscape splines", "list_landscape_splines"),
    get_component:     bp("Inspect component. Params: componentIndex", "get_landscape_component"),
    sculpt:            bp("Sculpt heightmap. Params: x, y, radius, strength, falloff?", "sculpt_landscape"),
    paint_layer:       bp("Paint weight layer. Params: layerName, x, y, radius, strength?", "paint_landscape_layer"),
    set_material:      bp("Set landscape material. Params: materialPath", "set_landscape_material"),
    add_layer_info:    bp("Register paint layer. Params: layerName", "add_landscape_layer_info"),
    import_heightmap:  bp("Import heightmap file. Params: filePath", "import_landscape_heightmap"),
    get_material_usage_summary: bp("Per-proxy summary: landscape/hole material paths + component/grass/nanite counts (#150)", "get_landscape_material_usage_summary"),
  },
  undefined,
  {
    x: z.number().optional(), y: z.number().optional(),
    radius: z.number().optional(), strength: z.number().optional(),
    falloff: z.number().optional(),
    layerName: z.string().optional(),
    materialPath: z.string().optional(),
    filePath: z.string().optional(),
    componentIndex: z.number().optional(),
  },
);
