# Toolset proof

Every Runtime Toolsets toolset exercised in a running game: **32 toolsets, 447/448 tool calls passed**, each on a fresh scene with the default look. Regenerate with the scripts in `Scripts/proof` (see `publish_proof.py`).

| Toolset | Calls | Proof | Notes |
|---|---|---|---|
| session | 6/7 | [responses](toolsets/session.json) | session.call_tool: call_tool cannot call itself |
| build | 28/28 | [build](toolsets/build.jpg) |  |
| actors | 17/17 | [actors](toolsets/actors.jpg) |  |
| scene | 22/22 | [scene](toolsets/scene.jpg) |  |
| layout | 33/33 | [layout](toolsets/layout.jpg) |  |
| lighting | 21/21 | [lighting](toolsets/lighting.jpg), [lighting_morning](toolsets/lighting_morning.jpg), [lighting_night](toolsets/lighting_night.jpg), [lighting_sunset](toolsets/lighting_sunset.jpg) |  |
| camera | 16/16 | [camera](toolsets/camera.jpg), [camera_wireframe](toolsets/camera_wireframe.jpg) |  |
| capture | 9/9 | [capture](toolsets/capture.jpg), [capture_scene_capture](toolsets/capture_scene_capture.jpg) |  |
| materials | 16/16 | [materials](toolsets/materials.jpg) |  |
| style | 17/17 | [style_lowpoly](toolsets/style_lowpoly.jpg), [style_noir](toolsets/style_noir.jpg), [style_retro](toolsets/style_retro.jpg), [style_stylized](toolsets/style_stylized.jpg), [style_toon](toolsets/style_toon.jpg) |  |
| vfx | 15/15 | [vfx](toolsets/vfx.jpg) |  |
| ui | 13/13 | [ui](toolsets/ui.jpg) |  |
| spline | 16/16 | [spline](toolsets/spline.jpg) |  |
| pcg | 11/11 | [pcg](toolsets/pcg.jpg) |  |
| physics | 25/25 | [physics](toolsets/physics.jpg) |  |
| character | 11/11 | [character](toolsets/character.jpg) |  |
| game | 7/7 | [responses](toolsets/game.json) |  |
| input | 4/4 | [responses](toolsets/input.json) |  |
| console | 7/7 | [responses](toolsets/console.json) |  |
| log | 3/3 | [responses](toolsets/log.json) |  |
| profiling | 6/6 | [responses](toolsets/profiling.json) |  |
| reflection | 18/18 | [responses](toolsets/reflection.json) |  |
| history | 20/20 | [responses](toolsets/history.json) |  |
| tags | 13/13 | [responses](toolsets/tags.json) |  |
| level | 7/7 | [responses](toolsets/level.json) |  |
| skills | 5/5 | [responses](toolsets/skills.json) |  |
| ops | 5/5 | [responses](toolsets/ops.json) |  |
| gis | 15/15 | [gis](toolsets/gis.jpg) |  |
| ai | 19/19 | [ai](toolsets/ai.jpg) |  |
| gas | 19/19 | [responses](toolsets/gas.json) |  |
| statetree | 1/1 | [responses](toolsets/statetree.json) |  |
| logic | 22/22 | [logic](toolsets/logic.jpg) |  |

The one call that does not pass is expected: `session.call_tool` refuses to call itself, and the Python kit already reaches the game through it.

### actors

<img src="toolsets/actors.jpg" width="300" alt="actors">

### ai

<img src="toolsets/ai.jpg" width="300" alt="ai">

### build

<img src="toolsets/build.jpg" width="300" alt="build">

### camera

<img src="toolsets/camera.jpg" width="300" alt="camera"> <img src="toolsets/camera_wireframe.jpg" width="300" alt="camera_wireframe">

### capture

<img src="toolsets/capture.jpg" width="300" alt="capture"> <img src="toolsets/capture_scene_capture.jpg" width="300" alt="capture_scene_capture">

### character

<img src="toolsets/character.jpg" width="300" alt="character">

### gis

<img src="toolsets/gis.jpg" width="300" alt="gis">

### layout

<img src="toolsets/layout.jpg" width="300" alt="layout">

### lighting

<img src="toolsets/lighting.jpg" width="300" alt="lighting"> <img src="toolsets/lighting_morning.jpg" width="300" alt="lighting_morning"> <img src="toolsets/lighting_night.jpg" width="300" alt="lighting_night"> <img src="toolsets/lighting_sunset.jpg" width="300" alt="lighting_sunset">

### logic

<img src="toolsets/logic.jpg" width="300" alt="logic">

### materials

<img src="toolsets/materials.jpg" width="300" alt="materials">

### pcg

<img src="toolsets/pcg.jpg" width="300" alt="pcg">

### physics

<img src="toolsets/physics.jpg" width="300" alt="physics">

### scene

<img src="toolsets/scene.jpg" width="300" alt="scene">

### spline

<img src="toolsets/spline.jpg" width="300" alt="spline">

### style

<img src="toolsets/style_lowpoly.jpg" width="300" alt="style_lowpoly"> <img src="toolsets/style_noir.jpg" width="300" alt="style_noir"> <img src="toolsets/style_retro.jpg" width="300" alt="style_retro"> <img src="toolsets/style_stylized.jpg" width="300" alt="style_stylized"> <img src="toolsets/style_toon.jpg" width="300" alt="style_toon">

### ui

<img src="toolsets/ui.jpg" width="300" alt="ui">

### vfx

<img src="toolsets/vfx.jpg" width="300" alt="vfx">

## Building types

Every building type of `unreal_kit.modern` (27/27 built), one call each: `modern.place(kind)`.

| Type | Parts | Size (m) | Image |
|---|---|---|---|
| apartment | 100 | 34 x 50 x 36 | [apartment](buildings/apartment.jpg) |
| villa | 45 | 26 x 34 x 5 | [villa](buildings/villa.jpg) |
| townhouse | 65 | 10 x 17 x 14 | [townhouse](buildings/townhouse.jpg) |
| shophouse | 91 | 10 x 15 x 20 | [shophouse](buildings/shophouse.jpg) |
| office | 269 | 37 x 52 x 52 | [office](buildings/office.jpg) |
| loft | 816 | 35 x 50 x 28 | [loft](buildings/loft.jpg) |
| office_tower | 231 | 38 x 54 x 89 | [office_tower](buildings/office_tower.jpg) |
| school | 284 | 66 x 102 x 10 | [school](buildings/school.jpg) |
| kindergarten | 90 | 37 x 48 x 6 | [kindergarten](buildings/kindergarten.jpg) |
| university | 338 | 87 x 87 x 28 | [university](buildings/university.jpg) |
| hospital | 548 | 115 x 83 x 45 | [hospital](buildings/hospital.jpg) |
| clinic | 116 | 32 x 53 x 10 | [clinic](buildings/clinic.jpg) |
| business_park | 429 | 72 x 84 x 22 | [business_park](buildings/business_park.jpg) |
| bank | 125 | 24 x 41 x 15 | [bank](buildings/bank.jpg) |
| hotel | 197 | 45 x 53 x 38 | [hotel](buildings/hotel.jpg) |
| mall | 377 | 111 x 89 x 13 | [mall](buildings/mall.jpg) |
| supermarket | 91 | 52 x 57 x 8 | [supermarket](buildings/supermarket.jpg) |
| government | 306 | 82 x 86 x 24 | [government](buildings/government.jpg) |
| museum | 291 | 70 x 76 x 18 | [museum](buildings/museum.jpg) |
| police_station | 308 | 37 x 63 x 17 | [police_station](buildings/police_station.jpg) |
| fire_station | 90 | 40 x 61 x 18 | [fire_station](buildings/fire_station.jpg) |
| mosque | 103 | 37 x 48 x 21 | [mosque](buildings/mosque.jpg) |
| church | 36 | 32 x 69 x 22 | [church](buildings/church.jpg) |
| warehouse | 330 | 90 x 76 x 13 | [warehouse](buildings/warehouse.jpg) |
| factory | 352 | 78 x 55 x 35 | [factory](buildings/factory.jpg) |
| parking_garage | 230 | 44 x 38 x 12 | [parking_garage](buildings/parking_garage.jpg) |
| petrol_station | 22 | 45 x 31 x 7 | [petrol_station](buildings/petrol_station.jpg) |

<img src="buildings/apartment.jpg" width="300" alt="apartment">
<img src="buildings/villa.jpg" width="300" alt="villa">
<img src="buildings/townhouse.jpg" width="300" alt="townhouse">
<img src="buildings/shophouse.jpg" width="300" alt="shophouse">
<img src="buildings/office.jpg" width="300" alt="office">
<img src="buildings/loft.jpg" width="300" alt="loft">
<img src="buildings/office_tower.jpg" width="300" alt="office_tower">
<img src="buildings/school.jpg" width="300" alt="school">
<img src="buildings/kindergarten.jpg" width="300" alt="kindergarten">
<img src="buildings/university.jpg" width="300" alt="university">
<img src="buildings/hospital.jpg" width="300" alt="hospital">
<img src="buildings/clinic.jpg" width="300" alt="clinic">
<img src="buildings/business_park.jpg" width="300" alt="business_park">
<img src="buildings/bank.jpg" width="300" alt="bank">
<img src="buildings/hotel.jpg" width="300" alt="hotel">
<img src="buildings/mall.jpg" width="300" alt="mall">
<img src="buildings/supermarket.jpg" width="300" alt="supermarket">
<img src="buildings/government.jpg" width="300" alt="government">
<img src="buildings/museum.jpg" width="300" alt="museum">
<img src="buildings/police_station.jpg" width="300" alt="police_station">
<img src="buildings/fire_station.jpg" width="300" alt="fire_station">
<img src="buildings/mosque.jpg" width="300" alt="mosque">
<img src="buildings/church.jpg" width="300" alt="church">
<img src="buildings/warehouse.jpg" width="300" alt="warehouse">
<img src="buildings/factory.jpg" width="300" alt="factory">
<img src="buildings/parking_garage.jpg" width="300" alt="parking_garage">
<img src="buildings/petrol_station.jpg" width="300" alt="petrol_station">
