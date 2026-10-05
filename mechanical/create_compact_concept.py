"""Import the actual R1 PCB and show a 23.8x41.1x14.8 mm enclosure envelope.
Connector and IC models are package proxies. Not an enclosure production file.
"""
from pathlib import Path
import bpy,math,json
from mathutils import Vector
ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'mechanical'
GLB=ROOT/'hardware/sticks3-piezo-hat/preview/routed-board.glb'
bpy.ops.object.select_all(action='SELECT');bpy.ops.object.delete(use_global=False)
bpy.ops.import_scene.gltf(filepath=str(GLB))
imported=list(bpy.context.scene.objects)
points=[o.matrix_world@Vector(v) for o in imported if o.type=='MESH' for v in o.bound_box]
lo=[min(v[i] for v in points)*1000 for i in range(3)];hi=[max(v[i] for v in points)*1000 for i in range(3)]
(OUT/'compact-envelope-measurements.json').write_text(json.dumps({'model_bounds_mm':{'min':lo,'max':hi,'size':[hi[i]-lo[i] for i in range(3)]},'board_mm':[21,38,1],'case_body_mm':[23.8,41.1,14.8],'including_model_contacts_mm':[23.8,hi[1]-lo[1]+1.3,14.8],'including_datasheet_contacts_nominal_mm':[23.8,33.1+6.9+6.0+1.3,14.8],'connector_tolerance_length_mm':.4,'length_budget_max_mm':33.1+6.9+6+.4+1.3+.2,'case_positive_tolerance_budget_mm':.2,'status':'package model calculation, mating and cable strain relief not validated'},indent=2))
parent=bpy.data.objects.new('Actual R1 PCB assembly',None);bpy.context.collection.objects.link(parent)
for o in imported:
 if o.parent is None:
  world=o.matrix_world.copy();o.parent=parent;o.matrix_world=world
parent.rotation_euler=(math.pi/2,0,0);parent.location=(-.0505,.0033,.0768)
def material(name,color,metallic=0):
 m=bpy.data.materials.new(name);m.diffuse_color=(*color,1);m.use_nodes=True
 p=m.node_tree.nodes.get('Principled BSDF');p.inputs['Base Color'].default_value=(*color,1);p.inputs['Metallic'].default_value=metallic;p.inputs['Roughness'].default_value=.4
 return m
case=material('Enclosure envelope',(.17,.24,.32));ground=material('Studio',(.80,.84,.88))
def box(name,dims,loc,mat,bevel=.3):
 bpy.ops.mesh.primitive_cube_add(size=1,location=tuple(v/1000 for v in loc));o=bpy.context.object;o.name=name;o.dimensions=tuple(v/1000 for v in dims);bpy.ops.object.transform_apply(location=False,rotation=False,scale=True);o.data.materials.append(mat)
 if bevel:
  mod=o.modifiers.new('Edge radius','BEVEL');mod.width=bevel/1000;mod.segments=4;o.modifiers.new('Normals','WEIGHTED_NORMAL')
 return o
box('Back wall',(23.8,1.1,41.1),(0,6.85,27.55),case)
box('Left wall',(1.1,13.7,41.1),(-11.35,-.55,27.55),case)
box('Right wall',(1.1,13.7,41.1),(11.35,-.55,27.55),case)
box('Top wall - cable opening pending',(21.6,13.7,1.1),(0,-.55,47.55),case)
box('Exploded cover - fasteners pending',(23.8,1.1,41.1),(34,-6.85,27.55),case)
# Edge guides grip only the outer 0.3 mm of PCB, within the part-free border.
# 1.2 mm slot for a nominal 1.0 mm board; retention/print tolerances are provisional.
for sx in [-1,1]:
 box('PCB rear guide '+str(sx),(.8,1.05,28),(sx*10.6,4.975,29.8),case,.12)
 box('PCB front guide '+str(sx),(.8,.45,28),(sx*10.6,3.025,29.8),case,.12)
box('PCB upper end stop',(19,1.6,.6),(0,3.8,47.1),case,.1)
stopmat=material('Removable lower stop',(.55,.30,.08))
box('PCB lower receiver - removable fixing pending',(19,2.2,.8),(0,5.2,8.3),stopmat,.12)

box('Studio floor',(140,120,1),(12,0,-1),ground)
scene=bpy.context.scene;scene.unit_settings.system='METRIC';scene.unit_settings.length_unit='MILLIMETERS'
scene['status']='Edge guides, upper stop and removable lower receiver concept; lower-stop fastening, Hat2 mating, cover retention and cable exit must be finalized'
scene.render.engine='CYCLES';scene.cycles.samples=32;scene.cycles.use_denoising=True
scene.render.resolution_x=1300;scene.render.resolution_y=1100;scene.render.resolution_percentage=100
scene.world.color=(.25,.25,.25)
scene.view_settings.exposure=-3.5
for loc,energy,size in [((-.06,-.08,.10),2,.10),((.09,.02,.09),1.8,.08)]:
 bpy.ops.object.light_add(type='AREA',location=loc);o=bpy.context.object;o.data.energy=energy;o.data.size=size;o.rotation_euler=(Vector((.012,0,.025))-o.location).to_track_quat('-Z','Y').to_euler()
bpy.ops.object.camera_add(location=(.095,-.15,.10));cam=bpy.context.object;cam.rotation_euler=(Vector((.012,0,.025))-cam.location).to_track_quat('-Z','Y').to_euler();cam.data.type='ORTHO';cam.data.ortho_scale=.085;scene.camera=cam
scene.render.image_settings.file_format='PNG';scene.render.filepath='//compact-hat-concept.png'
bpy.ops.wm.save_as_mainfile(filepath=str(OUT/'compact-hat-concept.blend'));bpy.ops.render.render(write_still=True)
print('MODEL_ENVELOPE',hi[1]-lo[1]+1.3)
