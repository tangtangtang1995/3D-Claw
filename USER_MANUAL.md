# 3D Claw User Manual

**3D Claw: An AI-assisted 3D geometry processing workspace built on Easy3D.**

3D Claw is an interactive desktop application for point-cloud and surface-mesh
processing. It extends the Easy3D viewer with AI-assisted parameter selection,
AI chat, AI-based result evaluation, AI 3D generation, and visual feedback for
long-running geometry algorithms.

This manual is written for public GitHub users. It focuses on what each visible
part of the application does, how the major algorithms should be used, and what
risks to watch for when processing real geometry data.

## 1. Core Ideas

### 1.1 Workspace

The workspace contains the loaded models and all derived outputs produced by
algorithms. A model can be a point cloud, a surface mesh, or an algorithm result
such as extracted planes, regions, hulls, sampled points, or generated meshes.

The current model is the model selected in the Model List. Most menu commands
and algorithm panels operate on the current model.

### 1.2 Model And Drawable

A model may contain several drawable parts:

| Drawable | Meaning |
| --- | --- |
| Points | Point vertices or point-cloud samples. |
| Lines | Line segments, wireframes, boundaries, or helper geometry. |
| Triangles | Surface faces of a mesh. |

Each drawable can be shown, hidden, selected, and styled independently.

### 1.3 Destructive And Derived Operations

Some operations modify the selected model in place, such as recentering,
applying a transformation, deleting a selection, or changing normals. Other
operations create derived models, such as Poisson reconstruction, sampling,
primitive extraction, or AI-generated meshes.

Before running a destructive command on valuable data, save the model or keep a
copy in the workspace.

### 1.4 CGAL-Enabled Builds

Some algorithms require CGAL. In a build without CGAL support, CGAL-dependent
commands are shown as unavailable or marked as requiring CGAL. The application
can still be used for viewing, editing, AI chat, generation, Easy3D algorithms,
and non-CGAL analysis.

## 2. First Launch

### 2.1 Welcome Dialog

On startup, 3D Claw may show a welcome dialog for AI setup.

| Element | Purpose |
| --- | --- |
| API Key | Password-style field for the AI chat key. |
| AI Reply Language | Selects the language used by AI replies. |
| Activate AI Chat | Applies the key and enables AI chat. |
| Skip | Opens the application without activating chat. |

If a saved configuration is found, the dialog indicates that the API key and
language preference were loaded from the local configuration file.

### 2.2 Credentials

AI Chat and AI 3D Generation use separate credentials:

| Feature | Credential |
| --- | --- |
| AI Chat | API key in the AI Chat panel or welcome dialog. |
| AI 3D Generation | SecretId and SecretKey in the AI 3D Generation panel. |

Keep credentials private. Do not commit personal keys into a public repository.

## 3. Main Window Layout

The default layout contains four main areas:

| Area | Default Position | Purpose |
| --- | --- | --- |
| Model List | Left | Workspace hierarchy, visibility, and selection. |
| Viewport | Center | 3D rendering and interaction. |
| Properties / Log / Health Report / History | Bottom left | Model details, messages, analysis, and operation history. |
| AI Chat / AI 3D Generation | Right | AI assistant and text/image-to-3D generation. |

Panels are dockable. Use the Window menu to show hidden panels or reset the
layout.

## 4. Viewport

The Viewport is the central 3D view.

### 4.1 Navigation

| Action | Effect |
| --- | --- |
| Left mouse drag | Rotate the camera. |
| Right mouse drag | Pan the camera. |
| Middle mouse drag or mouse wheel | Zoom. |
| `F` | Fit the current model to the screen. |
| `Esc` | Return to View/Navigate mode. |
| `Ctrl` + mouse wheel | Change UI scale. |
| `Ctrl` + `+` / `Ctrl` + `-` / `Ctrl` + `0` | Increase, decrease, or reset UI scale. |

### 4.2 Selection Modes

Selection behavior depends on the active mode selected from the Select menu.
Point-picking modes select points or vertices. Face-picking modes select mesh
faces. Rectangle modes select multiple primitives by dragging a screen-space
rectangle.

In selection modes, left click selects and right click removes from the current
selection.

### 4.3 Display Helpers

The View menu controls viewport helpers:

| Helper | Purpose |
| --- | --- |
| Backend Logo | Shows the rendering backend logo. |
| Frame Rate | Shows the current FPS. |
| Axes Gizmo | Shows the orientation gizmo. |
| Selection BBox | Shows the bounding box of selected elements. |
| Primitive ID Under Mouse | Displays the primitive index under the cursor. |
| Coordinates Under Mouse | Displays 3D coordinates under the cursor. |

### 4.4 Lighting And Background

Use `View > Display Settings...` to adjust:

| Setting | Purpose |
| --- | --- |
| Background | Viewport background color or gradient style. |
| Enable Surface Lighting | Enables lighting for meshes. |
| Light Direction | Direction of the main light. |
| Ambient | Base illumination. |
| Specular | Reflection highlight strength. |
| Shininess | Size and sharpness of specular highlights. |
| Normalize Light | Keeps light direction normalized. |
| Reset Lighting | Restores default lighting. |

The default lighting is intended to reveal shape while avoiding strong shiny
material highlights.

## 5. Model List

The Model List shows all loaded and generated objects.

### 5.1 Top Controls

| Control | Purpose |
| --- | --- |
| Show All | Makes all models and drawables visible. |
| Hide All | Hides all models and drawables. |

### 5.2 Model Rows

| Element | Purpose |
| --- | --- |
| Expand arrow | Expands or collapses child drawables and result groups. |
| Visibility checkbox | Shows or hides the model. |
| Model name | Selects the current model. |
| Context menu | Right-click for commands such as Ask AI or Delete. |
| Hover `?` | Opens an AI question about the hovered model or node. |

### 5.3 Drawable Rows

Each model may contain `Points`, `Lines`, and `Triangles` rows. These rows let
you show, hide, and select a specific drawable type for editing in the
Properties panel.

### 5.4 Algorithm Result Groups

Some algorithms create structured result groups. For example, region-growing
and RANSAC outputs may be grouped under result containers instead of being
listed as many independent top-level models.

Common result controls include:

| Label | Meaning |
| --- | --- |
| Planes / Regions / Primitives | Main extracted entities. |
| CH | Convex hull display for extracted entities. |
| AS | Alpha-shape display for extracted entities. |

## 6. Properties Panel

The Properties panel edits the selected model or selected drawable.

### 6.1 General Section

The General section reports basic model information:

| Field | Meaning |
| --- | --- |
| Name | Current model name. |
| Type | Point cloud, surface mesh, or other supported object type. |
| Vertices / Edges / Faces | Geometry counts when available. |
| BBox center | Bounding-box center. |
| BBox size | Bounding-box dimensions. |
| BBox diag | Bounding-box diagonal length. |

### 6.2 Display: Points

| Control | Purpose |
| --- | --- |
| Visible | Shows or hides point rendering. |
| Point Size | Adjusts screen-space point size. |
| Impostor | Selects point rendering style such as plain points, spheres, or surfels. |
| Coloring Method | Chooses how point colors are computed. |

### 6.3 Display: Lines

| Control | Purpose |
| --- | --- |
| Visible | Shows or hides line rendering. |
| Line Width | Adjusts rendered line width. |
| Coloring Method | Chooses uniform color or attribute-based color. |

### 6.4 Display: Triangles

| Control | Purpose |
| --- | --- |
| Visible | Shows or hides mesh faces. |
| Opacity | Controls face transparency. |
| Smooth Shading | Uses interpolated normals for smoother visual shading. |
| Coloring Method | Chooses uniform color, scalar field, color property, or texture. |

### 6.5 Coloring Methods

| Method | Meaning |
| --- | --- |
| Uniform | Uses one manually selected color. |
| Scalar Field | Maps a scalar attribute to colors. |
| Color Property | Uses stored vertex or face colors. |
| Textured | Uses the model texture and UV coordinates when available. |

If a textured model is switched to uniform color and later switched back to
Textured, the application attempts to rebind the texture and UV coordinates.

### 6.6 Attributes / Scalar Fields

This section lists available geometry attributes and scalar fields.

| Control | Purpose |
| --- | --- |
| Attribute table | Shows attribute name, location, count, and range. |
| Show Attribute | Displays the selected attribute when possible. |
| Ask AI About This | Sends the selected attribute context to AI Chat. |
| Scalar Filter range | Selects a numeric range from the active scalar field. |
| Select Range | Selects elements inside the chosen scalar range. |
| Extract Selection | Creates a model from the selected elements. |
| Delete Selection | Deletes selected elements from the current model. |
| Clear previous selection | Clears existing selection before applying a new scalar range. |

### 6.7 Texture Controls

Texture-related controls show the texture source, loaded texture dimensions,
UV status, and active coloring mode. When available:

| Control | Purpose |
| --- | --- |
| Apply Texture | Applies or reapplies texture rendering. |
| Reload from Disk | Reloads the texture file from storage. |
| Open Folder | Opens the texture source folder. |

## 7. Log, History, And Health Report

### 7.1 Log

The Log panel shows application messages, warnings, and errors.

| Control | Purpose |
| --- | --- |
| Auto-scroll | Keeps the latest message visible. |
| Clear | Clears the visible log. |

Warnings and errors are colored to improve visibility.

### 7.2 History

The History panel records operations performed during the session.

| Control | Purpose |
| --- | --- |
| Auto-scroll | Keeps the latest operation visible. |
| Clear All | Clears the session history list. |
| Copy All | Copies all history entries. |
| Hover `?` | Asks AI to explain the hovered operation. |

Each entry may include operation status, elapsed time, source model, output
model, and details.

### 7.3 Health Report

The Health Report panel analyzes the current model.

| Field Or Control | Purpose |
| --- | --- |
| Current model/type | Shows what is being analyzed. |
| Re-analyze | Runs the health analysis again. |
| Boundary edges / loops | Indicates open mesh boundaries. |
| Degenerate faces | Reports faces with invalid or near-zero area. |
| Isolated vertices | Reports unused vertices. |
| Non-manifold vertices | Reports topology that may break many algorithms. |
| Connected components | Reports disconnected parts. |
| Attribute flags | Shows normals, colors, UVs, and other data availability. |
| Suggested Actions | Opens relevant repair, sampling, or reconstruction tools. |
| Ask AI | Sends the health report to AI Chat. |
| Hover `?` | Asks AI about a specific finding. |

Use the Health Report before running topology-sensitive algorithms such as
Poisson reconstruction, skeletonization, parameterization, and remeshing.

## 8. AI Chat Panel

AI Chat is a built-in assistant for interpreting the current scene, explaining
panels, recommending algorithms, and reviewing results.

AI Chat is context-aware when invoked from supported UI locations. Depending on
where the request starts, the prompt can include the active panel, selected
model, model statistics, health-report signals, current algorithm parameters,
runtime summaries, generated output metadata, and result metrics. This makes the
assistant different from a detached chat window: it can reason from the current
geometry workflow rather than only from manually typed text.

### 8.1 Header Controls

| Control | Purpose |
| --- | --- |
| Model label | Shows the active chat model. |
| Clear Chat | Clears the current conversation. |
| API key field | Sets the chat API key. |
| Apply Key | Applies the typed API key. |
| `-` / `+` | Decreases or increases chat font size. |
| Percentage | Shows current chat font scale. |
| Language combo | Chooses AI reply language. |
| Status | Shows Ready, Thinking, or error state. |

### 8.2 Context Controls

| Control | Purpose |
| --- | --- |
| Use Current Model Context | Includes current model statistics and state in prompts. |
| Explain Current Panel | Asks AI to explain the active panel or UI context. |
| What should I do next? | Asks AI for the next recommended action. |

### 8.3 Chat Area

AI messages support Markdown rendering. User messages are displayed as
selectable text. Message-level copy controls may appear for convenient reuse.

### 8.4 Input Area

| Control | Purpose |
| --- | --- |
| Text box | Type a question or instruction. |
| Send | Sends the message. |
| `Ctrl` + `Enter` | Keyboard shortcut for sending. |

### 8.5 Dynamic AI Buttons

3D Claw includes contextual AI entry points across the UI:

| Location | Behavior |
| --- | --- |
| Menu group AI items | Ask which tool in that menu is appropriate. |
| Menu command hover `?` | Ask AI about one command. |
| Model List hover `?` | Ask AI about a model or result node. |
| Health Report hover `?` | Ask AI about a specific issue. |
| History hover `?` | Ask AI to explain an operation. |
| Algorithm `AI Parameter Advice` | Ask AI for parameter recommendations. |
| Algorithm `AI Evaluate Result` | Ask AI to assess the completed result. |

Dynamic `?` buttons only appear when the pointer is over a supported UI item.

### 8.6 AI Advice And Evaluation Context

AI requests are built from the best available local context. Exact fields vary
by panel and algorithm, but common context includes:

| Context | Examples |
| --- | --- |
| Model identity | Model name, type, selected drawable, and workspace location. |
| Geometry scale | Vertex/edge/face counts, point count, bounding-box size, and diagonal. |
| Data quality | Normals, UVs, texture state, health-report findings, topology warnings. |
| Algorithm parameters | Current dialog values, presets, preview settings, thresholds, iteration counts. |
| Runtime result | Output counts, elapsed time, convergence flags, displacement, area/volume ratios, extracted region counts, or simplification ratios when available. |
| Next-step affordances | Available repair, smoothing, simplification, remeshing, sampling, or inspection actions. |

Use `AI Parameter Advice` before running an unfamiliar algorithm. Use
`AI Evaluate Result` after completion when the panel exposes result statistics.
For AI 3D Generation, enabling auto-evaluation sends the generated model's
metadata and inspection signals to AI Chat after the model is imported.

## 9. AI 3D Generation Panel

The AI 3D Generation panel creates a 3D model from an image or text prompt.

### 9.1 Input Mode

| Control | Purpose |
| --- | --- |
| Image | Uses an image file as generation input. |
| Text | Uses a text prompt as generation input. |
| Image Path | Path to the image file. |
| Browse... | Selects an image file. |
| Prompt | Text prompt used in Text mode. |

### 9.2 Options

| Option | Meaning |
| --- | --- |
| Generate Type | Chooses generation style, such as normal, low-poly, or geometry-focused output. |
| Enable PBR | Requests PBR material output when supported. This may cost more credits. |
| Face Count | Target face count, clamped to the supported service range. |

### 9.3 API Credentials

| Field | Purpose |
| --- | --- |
| SecretId | Service credential ID. |
| SecretKey | Service credential key. |

### 9.4 Execution Controls

| Control | Purpose |
| --- | --- |
| Auto-evaluate with AI on completion | Sends output statistics to AI Chat after generation. |
| Generate | Starts generation. |
| Cancel | Cancels an active generation job when available. |
| Close | Closes the panel. |

### 9.5 Output Controls

After a successful generation, the panel shows:

| Field Or Control | Purpose |
| --- | --- |
| Job ID | Remote generation job identifier. |
| Output Folder | Local folder containing generated files. |
| Model File | Main generated model file. |
| Open Folder | Opens the output folder. |
| Copy Model Path | Copies the main model path. |
| Copy Folder | Copies the output folder path. |

Generated models are saved under a `generated_models` folder next to the
application executable when that location is writable. If it is not writable,
the application falls back to a user data location. Generated model names include
their format extension.

## 10. Menu Bar Reference

### 10.1 File

| Command | Purpose |
| --- | --- |
| Open | Loads point-cloud or mesh files. |
| Save | Saves the current model. |
| Exit | Closes the application. |

Common supported model formats include PLY, OBJ, STL, OFF, GLB, GLTF, XYZ, and
BIN, depending on the data type and available loaders.

### 10.2 Edit

| Command | Purpose |
| --- | --- |
| Ask AI: which edit action is safe? | Sends edit context to AI Chat. |
| Translational Recenter | Recenters loaded models by the first model bounding box center. |
| Add Gaussian Noise... | Adds controlled random noise to geometry. |
| Apply Manipulated Transformation | Bakes the current manipulator transform into geometry. |
| Give Up Manipulated Transformation | Resets manipulator transforms without changing geometry. |
| Align | Opens alignment tools. |
| Crop / Clip | Opens spatial crop and clipping tools. |
| Extract Boundary | Extracts mesh boundaries into a new surface representation. |

Risk note: recentering and applied transforms modify coordinates. Save important
models before applying them.

### 10.3 Edit > Align

| Command | Purpose |
| --- | --- |
| Transform... | Applies manual translation, rotation, and scale. |
| Point-Pair Alignment... | Aligns two models from corresponding point pairs. |
| ICP (Point-to-Point)... | Runs iterative closest point alignment. |

ICP is useful when models are already roughly aligned. It can converge to the
wrong local minimum if the initial pose is poor or the overlap is small.

### 10.4 Edit > Crop / Clip

| Command | Purpose |
| --- | --- |
| Box Crop... | Crops geometry by an axis-aligned box. |
| Plane Clip... | Clips geometry by a plane. |

Use crop and clip tools to isolate regions before expensive reconstruction,
remeshing, or analysis.

### 10.5 Select

| Command | Purpose |
| --- | --- |
| Ask AI | Asks which selection action is appropriate. |
| View/Navigate | Disables primitive selection and returns to navigation. |
| Pick Mesh Vertex | Selects mesh vertices. |
| Pick Mesh Face | Selects mesh faces. |
| Pick Point Cloud Point | Selects point-cloud points. |
| Rectangle Mesh Faces | Selects mesh faces in a screen-space rectangle. |
| Rectangle Point Cloud Points | Selects point-cloud points in a screen-space rectangle. |
| Select All | Selects all selectable primitives in the current mode. |
| Invert Selection | Inverts the current selection. |
| Clear Selection | Clears the current selection. |
| Extract Selection... | Creates a new model from selected elements. |
| Delete Selection... | Deletes selected elements after confirmation. |
| Show Selection Overlay | Toggles the visual overlay for selected primitives. |

Risk note: deletion is destructive. Confirm that the active model and selection
mode are correct before deleting.

### 10.6 View

| Command | Purpose |
| --- | --- |
| Fit Screen | Fits the current model in the viewport. |
| Snapshot... | Saves an image of the viewport. |
| Display Settings... | Opens background, lighting, and UI-scale settings. |
| Show Backend Logo | Toggles the rendering backend logo. |
| Show Frame Rate | Toggles FPS display. |
| Show Axes Gizmo | Toggles the axes gizmo. |
| Show Selection BBox | Toggles selected-object bounding box display. |
| Show Primitive ID Under Mouse | Shows the primitive index under the cursor. |
| Show Coordinates Under Mouse | Shows 3D coordinates under the cursor. |
| Camera | Opens camera copy, save, restore, path, and animation tools. |

### 10.7 View > Camera

| Command | Purpose |
| --- | --- |
| Copy Camera | Copies the current camera state. |
| Paste Camera | Restores a copied camera state. |
| Save Camera State... | Saves the camera state to a file. |
| Restore Camera State... | Loads a saved camera state. |
| Import Camera Path... | Imports a camera path. |
| Export Camera Path... | Exports a camera path. |
| Animation / Walk Through... | Opens camera animation and walkthrough tools. |

### 10.8 Window

| Command | Purpose |
| --- | --- |
| Model List | Shows or hides the Model List panel. |
| Properties | Shows or hides the Properties panel. |
| Log | Shows or hides the Log panel. |
| Health Report | Shows or hides the Health Report panel. |
| History | Shows or hides the History panel. |
| AI Chat | Shows or hides AI Chat. |
| AI 3D Generation | Shows or hides AI 3D Generation. |
| Reset Layout | Restores the default dock layout. |

### 10.9 Point Cloud

| Command | Purpose |
| --- | --- |
| Ask AI: which point-cloud tool should I use? | Sends point-cloud context to AI Chat. |
| Down Sampling... | Reduces point count using spatial sampling. |
| Estimate Normals... | Computes point normals. |
| Reorient Normals | Attempts to make normal directions consistent. |
| Normalize Normals | Normalizes normal vectors. |
| Poisson Surface Reconstruction... | Reconstructs a watertight mesh from oriented points. |
| RANSAC Primitive Extraction... | Detects primitive shapes, commonly planes. |
| Region Growing... | Segments smooth or planar point-cloud regions. |
| Delaunay Triangulation 2D | Builds a 2D triangulation from point positions. |
| Delaunay Triangulation 3D | Builds a 3D triangulation structure from points. |

Normal estimation and orientation are often required before reconstruction,
RANSAC, and region growing.

### 10.10 Surface Mesh

| Command Group | Commands |
| --- | --- |
| Topology | Extract Connected Components, Dual, Planar Partition, Polygonization, Triangulation, Tetrahedralization. |
| Repair | Stitch with Reorientation, Stitch without Reorientation, Reverse Orientation, Remove Isolated Vertices, Orient and Stitch Polygon Soup. |
| Sampling | Samples points from a mesh surface. |
| Subdivision | Catmull-Clark, Loop, Sqrt3. |
| Simplify | CGAL Quality, Easy3D Fast Decimate. |
| Smooth / Fair | CGAL Angle/Area/MCF, Easy3D Laplacian, Fairing. |
| Hole Filling | Fills boundary loops. |
| Remesh | ACVD, Easy3D Isotropic, VSA Approximation, Planar Patch Remeshing. |
| Parameterize / UV | CGAL LSCM UV view, Easy3D Basic. |
| Geodesic / Distance | Distance Field, Easy3D Quick. |
| Deformation / Advanced | MCF Skeletonization, ARAP Deformation, Alpha Wrapping 3D. |

### 10.11 Analyze

| Command | Purpose |
| --- | --- |
| Ask AI: which analysis tool should I use? | Sends analysis context to AI Chat. |
| Manipulate Properties... | Edits model properties. |
| Compute Height Field | Computes height-related attributes. |
| Compute Surface Mesh Curvatures... | Computes curvature attributes. |
| Report Topology Statistics | Reports topology metrics. |
| Measurement | Opens distance, polyline, angle, bounding box, area, and volume tools. |
| Point Cloud <-> Mesh Distance... | Computes distances between a point cloud and a mesh. |
| Point Cloud <-> Point Cloud Distance... | Computes distances between two point clouds. |

### 10.12 AI

| Command | Purpose |
| --- | --- |
| AI Chat | Opens the AI Chat panel. |
| 3D Generation... | Opens the AI 3D Generation panel. |

### 10.13 Help

| Command | Purpose |
| --- | --- |
| About | Shows application information. |
| Manual | Points users to project documentation. |

## 11. Algorithm Panel Conventions

Most algorithm panels share a common interaction model:

| Control | Meaning |
| --- | --- |
| AI Parameter Advice | Sends current model statistics and panel parameters to AI Chat. |
| Apply / Run / Detect / Generate | Starts the operation. |
| Cancel | Requests cancellation of a running job. |
| Live Preview | Shows intermediate algorithm state when supported. |
| Speed / Throttle / Snapshot Interval | Controls visual update frequency, not necessarily algorithm accuracy. |
| Status text | Reports queued, running, completed, failed, or cancelled state. |
| AI Evaluate Result | Sends result statistics and context to AI Chat after completion. |
| Close | Closes the panel. |

Cancellation is cooperative. Some algorithms check the cancel flag only between
major internal steps, so cancellation may not be immediate on large data.

## 12. Point-Cloud Algorithms

### 12.1 Down Sampling

| Parameter | Meaning |
| --- | --- |
| Cell Size | Spatial grid size used to merge or keep representative points. |
| Query | Shows or estimates sampling information for the current setting. |
| Apply | Creates or updates the downsampled result. |
| Cancel | Cancels the running task. |

Use case: reduce dense scans before normal estimation, reconstruction, or
segmentation.

Risk: a large cell size removes details and can erase thin structures.

### 12.2 Estimate Normals

| Parameter | Meaning |
| --- | --- |
| K Neighbors | Number of nearby points used to estimate each normal. |
| Reorient Normals | Attempts to make normal directions globally consistent. |
| Normalize | Converts normals to unit length. |
| Apply | Computes normals. |
| Cancel | Cancels the running task. |

Use case: prepare point clouds for Poisson reconstruction, RANSAC, and region
growing.

Risk: small neighborhoods are noisy; large neighborhoods oversmooth sharp edges
and small details.

### 12.3 Poisson Surface Reconstruction

| Parameter | Meaning |
| --- | --- |
| Depth | Octree depth. Higher values preserve more detail and cost more memory. |
| Samples Per Node | Smoothness and sampling density control. |
| ISO Divider | Iso-surface extraction control. |
| CG Depth | Conjugate-gradient solver depth. |
| Scale | Reconstruction domain expansion factor. |
| AI Parameter Advice | Requests parameter guidance. |
| Apply | Starts reconstruction. |
| Cancel | Cancels the job. |
| AI Evaluate Result | Reviews the generated mesh after completion. |

Use case: reconstruct a watertight surface from a dense oriented point cloud.

Risks:

- Incorrect normal orientation can create inverted, bloated, or missing surfaces.
- Excessive depth can cause very high memory usage.
- Poisson tends to close holes; this is useful for scans but wrong for intended
  open surfaces.

### 12.4 RANSAC Primitive Extraction

| Parameter | Meaning |
| --- | --- |
| Epsilon | Distance tolerance for assigning points to primitives. |
| Normal Threshold | Normal-angle tolerance. |
| Cluster Epsilon | Spatial clustering tolerance. |
| Min Points | Minimum support points required for a primitive. |
| Live Preview | Shows detection progress. |
| Throttle | Limits preview update frequency. |
| Detect Primitives | Starts extraction. |
| Resume / Step / Pause | Controls staged or live execution when supported. |
| Cancel | Cancels extraction. |
| AI Evaluate Result | Reviews extracted primitives. |

Use case: detect planes and other simple primitives in scanned scenes,
architectural data, and CAD-like point clouds.

Risks:

- The correct tolerance depends on model scale and noise.
- Too small a tolerance fragments primitives.
- Too large a tolerance merges unrelated surfaces.
- Poor normals reduce primitive quality.

### 12.5 Region Growing

| Parameter | Meaning |
| --- | --- |
| K Neighbors | Neighborhood size for local region decisions. |
| Max Distance | Maximum distance from region model or local fit. |
| Max Angle | Maximum normal-angle deviation in degrees. |
| Min Region Size | Minimum number of points required for a valid region. |
| Live Preview | Shows growing progress. |
| Detect Regions | Starts segmentation. |
| Resume / Step / Pause | Controls staged execution when supported. |
| Cancel | Cancels segmentation. |
| AI Evaluate Result | Reviews the resulting regions. |

Use case: segment contiguous planes or smooth regions in point clouds.

Risks:

- Sensitive to normals, point density, and scale.
- Low thresholds over-segment.
- High thresholds merge adjacent surfaces.
- Many tiny regions usually indicate noisy normals or an overly strict distance
  threshold.

### 12.6 Delaunay Triangulation 2D

Use case: create a terrain-like triangulation from points that are meaningful in
the XY plane.

Risk: not appropriate for arbitrary 3D point clouds with overlapping projection
or vertical surfaces.

### 12.7 Delaunay Triangulation 3D

Use case: create a 3D triangulation structure for volumetric point distributions.

Risk: output complexity can be high, and the result is not the same as a clean
surface reconstruction.

## 13. Surface-Mesh Algorithms

### 13.1 Topology And Repair

| Command | Use Case | Risk |
| --- | --- | --- |
| Extract Connected Components | Split or inspect disconnected parts. | Small fragments may be valid details. |
| Dual | Build the dual mesh. | Changes mesh structure substantially. |
| Planar Partition | Partition mesh by planar structure. | Works best on CAD-like meshes. |
| Polygonization | Convert suitable data into polygonal representation. | Depends strongly on input quality. |
| Triangulation | Convert polygonal faces to triangles. | May alter face layout. |
| Tetrahedralization | Build volumetric tetrahedra. | Requires suitable closed input. |
| Stitch with Reorientation | Merge compatible boundaries and fix orientation. | Can alter topology. |
| Stitch without Reorientation | Stitch boundaries while preserving orientation assumptions. | Wrong orientation may remain. |
| Reverse Orientation | Flips face orientation. | Incorrect use can invert normals. |
| Remove Isolated Vertices | Deletes vertices not used by faces. | Usually safe, but still modifies the model. |
| Orient and Stitch Polygon Soup | Builds coherent mesh from polygon soup. | May fail on inconsistent input. |

### 13.2 Surface Mesh Sampling

| Parameter | Meaning |
| --- | --- |
| Number of Points | Target number of sampled surface points. |
| Apply | Starts sampling. |
| Cancel | Cancels sampling. |

Use case: create a point-cloud representation of a mesh for reconstruction,
distance analysis, or point-cloud algorithms.

Risk: very low sample counts miss small features; very high counts increase
cost for later point-cloud processing.

### 13.3 Easy3D Fast Decimate

| Parameter | Meaning |
| --- | --- |
| Target Vertex Count | Desired output vertex count. |
| Apply | Starts simplification. |
| Cancel | Cancels simplification. |

Use case: quickly reduce mesh size for visualization or lightweight processing.

Risk: faster decimation may preserve features less carefully than quality-based
CGAL simplification.

### 13.4 CGAL Quality Simplification

| Parameter | Meaning |
| --- | --- |
| Strategy | Error metric and simplification policy. |
| Stop Mode | Stops by ratio, count, or supported criterion. |
| Target Ratio | Fraction of original complexity to keep. |
| Target Count | Desired final count. |
| Live Preview | Shows simplification progress. |
| Preview Speed | Controls preview update rate. |
| Snapshot Interval | Controls how often preview states are captured. |
| Bounded Normal Change | Prevents excessive normal deviation when supported. |
| Polyhedral Envelope | Constrains simplified surface within an envelope. |
| Envelope (% BBox) | Envelope size relative to the bounding box. |
| Run | Starts simplification. |
| Cancel | Cancels simplification. |
| AI Evaluate Result | Reviews result quality. |

Use case: high-quality mesh reduction while preserving shape.

Risks:

- Very aggressive ratios can destroy small parts or sharp features.
- Strict envelopes can slow the operation or prevent strong simplification.
- Non-manifold or degenerate meshes should be repaired first.

### 13.5 Easy3D Laplacian Smoothing

| Parameter | Meaning |
| --- | --- |
| Scheme | Explicit or implicit smoothing strategy. |
| Iterations | Number of smoothing iterations. |
| Uniform Laplace | Uses uniform weighting when enabled. |
| Apply | Starts smoothing. |
| Cancel | Cancels smoothing. |

Use case: reduce noise on a mesh.

Risk: Laplacian smoothing can shrink the model and soften sharp features.

### 13.6 CGAL Smoothing

| Parameter | Meaning |
| --- | --- |
| Mode | Smoothing method, such as angle, area, or mean-curvature flow. |
| Iterations | Number of passes. |
| Time Step | Step size for flow-based smoothing modes. |
| Preserve Boundary | Tries to keep boundary vertices fixed. |
| Preserve Sharp Edges | Protects feature edges. |
| Sharp Angle | Angle threshold for detecting sharp features. |
| Relax Constraints | Loosens preservation constraints. |
| Safety Constraints | Enables additional checks when supported. |
| Project to Original Surface | Projects smoothed result back toward original shape. |
| Rescale After Smoothing | Compensates for scale drift when supported. |
| Live Preview | Shows intermediate states. |
| Run | Starts smoothing. |
| Cancel | Requests cancellation. |
| AI Evaluate Result | Reviews the smoothed result. |

Use case: denoise meshes with more control than basic Laplacian smoothing.

Risks:

- Strong smoothing can remove geometric features.
- Some smoothing kernels have long internal steps; cancellation may be delayed.
- Boundary and feature preservation settings should match the model purpose.

### 13.7 Fairing

| Parameter | Meaning |
| --- | --- |
| Criterion | Chooses the energy minimized, such as area or curvature. |
| Apply | Starts fairing. |
| Cancel | Cancels fairing. |

Use case: improve local surface smoothness.

Risk: fairing can distort intentionally sharp or mechanical surfaces.

### 13.8 Hole Filling

| Control | Meaning |
| --- | --- |
| Apply | Fills detected boundary loops. |
| Cancel | Cancels filling. |

Use case: repair unintended holes before reconstruction, remeshing,
skeletonization, or volume computation.

Risk: the generated cap may be geometrically wrong for intentional openings or
large missing areas.

### 13.9 ACVD Remeshing

| Parameter | Meaning |
| --- | --- |
| Mode | Remeshing mode. |
| Target Vertices | Desired number of output vertices. |
| Presets | Quick target settings based on current mesh size. |
| Gradation | Controls variation in element density. |
| Vertex Count Ratio | Advanced target-size control. |
| Random Seed | Reproducibility control. |
| Settle Hold | Time to keep final preview stable before committing. |
| Live Preview | Shows clustering/remeshing progress. |
| Run | Starts remeshing. |
| Cancel | Cancels remeshing. |
| AI Evaluate Result | Reviews output quality. |

Use case: produce a more uniform mesh for downstream processing or display.

Risks:

- Output may not preserve fine features.
- Very low target counts can change the shape.
- Random seed can affect the final layout.

### 13.10 Easy3D Isotropic Remeshing

| Parameter | Meaning |
| --- | --- |
| Scheme | Uniform or adaptive remeshing. |
| Edge Length | Target edge length. |
| Use Features | Attempts to preserve feature edges. |
| Feature Angle | Angle threshold for feature detection. |
| Apply | Starts remeshing. |
| Cancel | Cancels remeshing. |

Use case: regularize triangle quality and edge length.

Risk: poor feature settings can either smooth away sharp detail or over-protect
noisy edges.

### 13.11 VSA Approximation

| Parameter | Meaning |
| --- | --- |
| Metric | Error metric for proxy fitting. |
| Seeding | Initial proxy placement strategy. |
| Target Proxies | Number of surface proxies. |
| Iterations | Optimization iterations. |
| Relaxations per Seed | Advanced stabilization setting. |
| Random Seed | Reproducibility control. |
| Extract Approximated Mesh | Outputs a simplified proxy mesh. |
| Keep Segmentation | Keeps segmentation information when supported. |
| Live Preview | Shows proxy evolution. |
| Run | Starts approximation. |
| Cancel | Cancels approximation. |
| AI Evaluate Result | Reviews segmentation and proxy quality. |

Use case: approximate a mesh with a small number of planar or smooth patches.

Risk: low proxy counts oversimplify; high counts reduce the benefit of
approximation.

### 13.12 Planar Patch Remeshing

| Parameter | Meaning |
| --- | --- |
| Mode | Patch extraction/remeshing mode. |
| Cos Angle | Angular tolerance for planar grouping. |
| Distance Ratio | Distance tolerance relative to model scale. |
| Postprocess Regions | Cleans or refines detected regions. |
| Live Preview | Shows progress. |
| Run | Starts remeshing. |
| Cancel | Cancels remeshing. |
| AI Evaluate Result | Reviews planar-patch quality. |

Use case: CAD-like, architectural, or piecewise-planar meshes.

Risk: organic or highly curved geometry is usually a poor fit for planar patch
assumptions.

### 13.13 Parameterization / UV

| Control | Meaning |
| --- | --- |
| Method | Selects LSCM, ARAP, or another available parameterization method. |
| Pick Seam Start / Pick Seam End | Defines seam endpoints on closed meshes. |
| Add Seam Path | Adds a seam path between selected endpoints. |
| Clear Seams | Removes current seam definitions. |
| Show Seams | Displays seam overlays. |
| Iterations | Iteration count for iterative methods. |
| Tolerance | Convergence tolerance. |
| Lambda | Weighting parameter where supported. |
| Show process | Shows intermediate UV process when available. |
| Run | Starts parameterization. |
| Cancel | Cancels parameterization. |
| UV View: Fit / Reset | Controls the UV viewport. |
| Heatmap | Shows distortion heatmap. |
| Edges / Boundary | Toggles UV edge and boundary display. |
| 3D Heatmap | Shows UV distortion back on the 3D model. |
| Correspondence Pick 3D Point | Links 3D and UV locations. |
| AI Evaluate Result | Reviews UV quality and distortion. |

Use case: generate UV coordinates for texturing or distortion analysis.

Risks:

- Closed meshes usually require seams.
- Bad seams can create high distortion.
- Non-manifold topology can make parameterization fail.

### 13.14 Geodesic / Distance Field

| Control | Meaning |
| --- | --- |
| Method | Selects the distance algorithm. |
| Pick Source | Picks source vertex or point. |
| Add Source | Adds typed source ID. |
| Clear Sources | Clears all sources. |
| Pick Target | Picks a target for path comparison. |
| Set Target | Sets typed target ID. |
| Clear Target | Clears target. |
| Use Virtual Edges | Allows virtual-edge behavior when supported. |
| Live Preview | Shows distance propagation. |
| Compare Exact Path | Compares with exact path when available. |
| Heat Method Variant | Chooses heat-method variant when supported. |
| Run | Computes distance field. |
| Cancel | Cancels computation. |
| AI Evaluate Result | Reviews the result. |

Use case: measure intrinsic distances on a mesh and visualize distance fields.

Risk: invalid topology, disconnected components, or wrong source IDs can produce
misleading results.

### 13.15 MCF Skeletonization

| Control | Meaning |
| --- | --- |
| Max Iterations | Maximum contraction iterations. |
| Area Variation Factor | Controls contraction stopping behavior. |
| Quality / Speed | Trades accuracy for performance. |
| Medially Centered | Weight for medial centering. |
| Medial Smoothness | Smoothness weight. |
| Max Triangle Angle | Quality threshold. |
| Auto Min Edge Length | Automatically estimates minimum edge length. |
| Min Edge Length | Manual lower edge-length threshold. |
| Show Original Ghost | Displays original mesh as reference. |
| Show Skeleton | Displays extracted skeleton. |
| Show Meso Mesh | Displays intermediate contracted mesh. |
| Show Correspondence | Shows mesh-to-skeleton correspondence. |
| Show SDF Heatmap | Displays scalar field visualization. |
| Live Preview | Shows contraction progress. |
| Run | Starts skeletonization. |
| Cancel | Cancels skeletonization. |
| AI Evaluate Result | Reviews the skeleton result. |

Use case: extract curve skeletons from closed organic shapes.

Risks:

- Works best on closed, pure-triangle, single-component meshes.
- Open boundaries and non-manifold topology can produce unstable skeletons.
- Very noisy meshes should be smoothed or remeshed first.

### 13.16 ARAP Deformation

| Control | Meaning |
| --- | --- |
| Pick ROI Seed | Picks the seed for the region of interest. |
| Pick Control | Picks a control vertex. |
| Erase Picked | Removes picked entries. |
| K-Ring | Expands ROI by ring distance. |
| Grow ROI | Adds neighboring vertices to the ROI. |
| Clear ROI | Clears the region of interest. |
| Group | Selects or edits a control group. |
| Add Control | Adds the selected control. |
| Clear Controls | Clears all control handles. |
| Save Selection / Load Selection | Stores or restores ARAP selections. |
| Mode | Chooses deformation mode. |
| Iterations | Solver iteration count. |
| Tolerance | Solver convergence tolerance. |
| Translate / Rotate | Control transformation. |
| Set Sample Translate | Sets a sample translation. |
| Reset Transform | Resets the current transform. |
| Drag Active Group | Enables interactive dragging. |
| Cancel Drag | Stops active dragging. |
| Reset Preview | Restores preview state. |
| Live Preview | Shows deformation preview. |
| Run | Applies deformation. |
| Cancel | Cancels the operation. |
| AI Evaluate Result | Reviews deformation quality. |

Use case: local shape editing while preserving local rigidity.

Risks:

- Requires a meaningful ROI and stable control handles.
- Too small an ROI causes sharp distortion.
- Too large an ROI may move more of the model than intended.

### 13.17 Alpha Wrapping 3D

| Parameter | Meaning |
| --- | --- |
| Alpha | Detail scale of the wrap. |
| Offset | Distance offset around the input. |
| Speed / Balanced / Quality presets | Quick parameter presets. |
| Live Preview | Shows wrapping progress. |
| Show live surface | Displays intermediate surface when supported. |
| Surface opacity | Controls preview transparency. |
| Smooth shading | Toggles shaded preview. |
| Run AW3 | Starts alpha wrapping. |
| Cancel | Cancels wrapping. |
| AI Evaluate Result | Reviews wrap quality. |

Use case: create a watertight 2-manifold surface around point clouds, triangle
soups, or imperfect meshes.

Risks:

- Small alpha/offset may preserve noise.
- Large alpha/offset may oversmooth and bridge nearby parts.
- The output is a wrap around the input, not necessarily a faithful repair of
the original topology.

## 14. Analysis And Measurement

### 14.1 Surface Mesh Curvatures

| Parameter | Meaning |
| --- | --- |
| Post-smoothing Iterations | Smooths curvature values after computation. |
| Use 2-Ring Neighborhood | Uses a larger neighborhood for estimation. |
| Apply | Computes curvature attributes. |
| Cancel | Cancels computation. |

Use case: inspect sharpness, ridges, valleys, and smoothness.

Risk: curvature is sensitive to mesh quality and noise.

### 14.2 Measurement Dialog

| Control | Purpose |
| --- | --- |
| Type | Chooses Distance, Polyline, Angle, Bounding Box, Surface Area, or Volume. |
| Snap to vertex | Snaps picked points to vertices. |
| Start Picking / Stop Picking | Starts or stops interactive measurement. |
| Finish Polyline | Completes a polyline measurement. |
| Copy Last | Copies the latest result. |
| Copy All | Copies all results. |
| Clear All | Clears measurement records. |
| Undo Last Point | Removes the last picked point. |

Risk: surface area and volume require suitable mesh topology. Open or
self-intersecting meshes can give misleading values.

### 14.3 Point Cloud <-> Mesh Distance

Use case: compare a scan against a reconstructed or reference mesh.

Common controls include model selectors, Compute, Min Display, Max Display,
Reset Range, metrics display, Compute Again, Close, and Cancel.

Risk: distance interpretation depends on scale, alignment, and whether outliers
should be included.

### 14.4 Point Cloud <-> Point Cloud Distance

Use case: compare two scans, downsampled variants, or generated point sets.

Risk: nearest-neighbor distances do not prove semantic correspondence. Align
models before measuring.

## 15. Common Workflows

### 15.1 Point Cloud To Mesh

1. Open the point cloud.
2. Inspect it in the Health Report.
3. Downsample if the cloud is too dense.
4. Estimate normals.
5. Reorient and normalize normals.
6. Run Poisson Surface Reconstruction or Alpha Wrapping 3D.
7. Review the result with AI Evaluate Result.
8. Save the reconstructed mesh.

### 15.2 Scan Segmentation

1. Open the point cloud.
2. Estimate and orient normals.
3. Use Region Growing for contiguous surface regions or RANSAC for primitive
   extraction.
4. Inspect the grouped result in the Model List.
5. Toggle Planes/Regions, CH, and AS outputs as needed.
6. Ask AI to evaluate whether thresholds caused over-segmentation or merging.

### 15.3 Mesh Cleanup And Remeshing

1. Open the mesh.
2. Run Health Report.
3. Remove isolated vertices and repair orientation if needed.
4. Fill unintended holes.
5. Smooth or remesh using the method appropriate for the model.
6. Use simplification if the mesh is too heavy.
7. Save the cleaned result.

### 15.4 AI-Assisted Parameter Selection

1. Open a model and select it.
2. Open an algorithm panel.
3. Click AI Parameter Advice.
4. Review the recommendation and adjust parameters.
5. Run the algorithm.
6. Click AI Evaluate Result after completion.

### 15.5 AI 3D Generation

1. Open AI 3D Generation.
2. Choose Image or Text mode.
3. Provide an image path or prompt.
4. Open Options and choose generate type, PBR, and face count.
5. Enter SecretId and SecretKey if not already configured.
6. Enable Auto-evaluate if you want an AI review after completion.
7. Click Generate.
8. Use Output Folder or Model File controls to locate the saved result.

## 16. Practical Guidance

### 16.1 Use The Health Report Early

Many geometry algorithms assume clean topology, consistent normals, or a single
connected component. The Health Report is the fastest way to catch common input
problems before spending time tuning parameters.

### 16.2 Match Algorithm To Data

| Data Type | Good Starting Tools |
| --- | --- |
| Dense oriented point cloud | Poisson Surface Reconstruction. |
| Noisy point cloud | Down Sampling, Estimate Normals, Region Growing. |
| Architectural scan | RANSAC Primitive Extraction, Region Growing, Planar Patch Remeshing. |
| Heavy triangle mesh | CGAL Quality Simplification or Easy3D Fast Decimate. |
| Noisy mesh | CGAL Smoothing or Easy3D Laplacian Smoothing. |
| Open or damaged mesh | Hole Filling, Stitching, Alpha Wrapping 3D. |
| Organic closed mesh | MCF Skeletonization, ACVD Remeshing. |
| Texture workflow | Parameterization / UV, Properties texture controls. |

### 16.3 Prefer Conservative First Runs

For unfamiliar data, start with moderate parameters and preview-enabled
algorithms. Extreme settings can be useful, but they make it harder to diagnose
whether a bad result came from the data, the parameter scale, or the algorithm
choice.

### 16.4 Understand AI Output

AI recommendations are advisory. They are most useful when the current model
context is enabled and the model has meaningful statistics. Always verify the
result visually and with the Health Report when topology matters.

## 17. Troubleshooting

### 17.1 AI Chat Returns HTTP 401

The chat service rejected the API key. Re-enter the key in AI Chat and click
Apply Key. Confirm that the key is active for the selected provider.

### 17.2 AI 3D Generation Does Not Start

Check SecretId and SecretKey in the AI 3D Generation panel. Also verify that the
selected mode has valid input: an image path for Image mode or a prompt for Text
mode.

### 17.3 Generated Model Is Visible But Hard To Find

Open the AI 3D Generation panel after completion and use Output Folder, Model
File, Open Folder, Copy Model Path, or Copy Folder. By default, successful
outputs are written under `generated_models` next to the application executable
when possible.

### 17.4 Texture Cannot Be Displayed

Check that the model has both texture image data and valid UV coordinates. In
Properties, switch Triangles coloring method to Textured and use texture reload
or apply controls if available.

### 17.5 Algorithm Appears To Be Running Without Visible Change

Check the active algorithm panel, status text, Log, and History. Some algorithms
run in background jobs and may only update the viewport at preview intervals.
Cancellation may also be delayed until the algorithm reaches a safe checkpoint.

### 17.6 CGAL Command Is Disabled

The application was likely built without CGAL support, or the required CGAL
module is unavailable. Use a CGAL-enabled build to access CGAL algorithms.

### 17.7 Reconstruction Looks Inflated, Inverted, Or Broken

For point-cloud reconstruction, inspect normals first. Run normal estimation,
reorientation, and normalization before Poisson reconstruction or region-based
segmentation.

### 17.8 Distance Or Volume Results Look Wrong

Check model scale, alignment, and topology. Volume usually requires a closed,
well-oriented mesh. Distance tools require the compared models to be in the same
coordinate system.

## 18. Keyboard Shortcuts

| Shortcut | Action |
| --- | --- |
| `Ctrl` + `O` | Open model. |
| `Ctrl` + `S` | Save current model. |
| `Alt` + `F4` | Exit. |
| `Esc` | Return to View/Navigate mode. |
| `F` | Fit current model to screen. |
| `Ctrl` + `A` | Select all in the active selection mode. |
| `Ctrl` + `Enter` | Send AI Chat message. |
| `Ctrl` + `+` | Increase UI scale. |
| `Ctrl` + `-` | Decrease UI scale. |
| `Ctrl` + `0` | Reset UI scale. |

## 19. Glossary

| Term | Meaning |
| --- | --- |
| Drawable | A renderable part of a model, such as points, lines, or triangles. |
| Current model | The model selected in the Model List. |
| Scalar field | A numeric attribute attached to vertices, faces, or points. |
| UV | 2D texture coordinates assigned to a surface mesh. |
| CH | Convex hull display for extracted primitives or regions. |
| AS | Alpha-shape display for extracted primitives or regions. |
| Live Preview | Intermediate visualization while an algorithm is running. |
| AI Parameter Advice | AI prompt that asks for parameter recommendations. |
| AI Evaluate Result | AI prompt that asks for result assessment after an algorithm finishes. |
